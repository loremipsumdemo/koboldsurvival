#include "patrons.h"
#include "faction_rules.h"
#include <algorithm>
#include <cmath>
#include "claims.h"
#include "commerce.h"
#include "inventory.h"
#include "items.h"
#include "npc_camps.h"
#include "systems_internal.h"
#include "useitem.h"
#include "world_labels.h"

using namespace systems_detail;

namespace {
bool rulesDoSomething(const FactionRules& r) {
    return r.downedBody != DownedBodyRule::Leave || r.downedTake != DownedTakeRule::Nothing;
}

// The nearest Prisoner Cage of this faction's with room, that no one else is carrying a prisoner to
ObjectId freeCage(World& w, const std::vector<Claim>& claims, ObjectId u, int faction) {
    const Transform* ut = w.get<Transform>(u);
    std::vector<ObjectId> promised;
    for (ObjectId o : w.view<DealJob>()) {
        if (o != u) promised.push_back(w.get<DealJob>(o)->cage);
    }
    ObjectId best = NO_OBJECT;
    float bestD2 = 0.0f;
    for (ObjectId c : w.view<Prop, Transform>()) {
        const PropInfo& info = propInfo(w.get<Prop>(c)->kind);
        if (!info.cage || info.taming || w.has<Blueprint>(c) || !propOfFaction(w, claims, c, faction)) continue;
        const int coming = (int)std::count(promised.begin(), promised.end(), c);
        if (occupantCount(w, c) + coming >= occupyCapacity(w, c)) continue;
        const Transform* ct = w.get<Transform>(c);
        const float d2 = distSq(*ct, *ut);
        if (best == NO_OBJECT || d2 < bestD2) {
            best = c;
            bestD2 = d2;
        }
    }
    return best;
}

// Where a unit lying at (x, z) in this claim is put down: straight out from the claim's middle, past its edge
Vec2 pastClaimEdge(const Terrain& t, const Claim& c, float x, float z) {
    float dx = x - c.x, dz = z - c.z;
    const float len = std::sqrt(dx * dx + dz * dz);
    if (len < 0.01f) {
        dx = 1.0f;
        dz = 0.0f;
    } else {
        dx /= len;
        dz /= len;
    }
    const float reach = c.radius + CARRY_OUT_MARGIN;
    const float most = (float)t.size() - 1.0f;
    const Vec2 to{ std::clamp(c.x + dx * reach, 1.0f, most), std::clamp(c.z + dz * reach, 1.0f, most) };
    return t.snap(to);
}

// The nearest knocked-out unit this unit's faction would deal with, that no one else is on their way to
ObjectId downedTarget(World& w, ObjectId u) {
    const Transform* ut = w.get<Transform>(u);
    if (!ut) return NO_OBJECT;
    const int faction = factionOf(w, u);
    std::vector<ObjectId> taken;
    for (ObjectId o : w.view<DealJob>()) {
        if (o != u) taken.push_back(w.get<DealJob>(o)->target);
    }
    ObjectId best = NO_OBJECT;
    float bestD2 = DEAL_RANGE * DEAL_RANGE;
    for (ObjectId v : w.view<Unconscious, Transform>()) {
        if (v == u || !dealableBy(w, faction, v)) continue;
        if (std::find(taken.begin(), taken.end(), v) != taken.end()) continue;
        const Transform* vt = w.get<Transform>(v);
        const float d2 = distSq(*vt, *ut);
        if (d2 <= bestD2) {
            best = v;
            bestD2 = d2;
        }
    }
    return best;
}

// Reputation lost with the victim's faction when yours takes from or cages one of theirs
void reputationHit(World& w, int faction, ObjectId victim) {
    const int theirs = factionOf(w, victim);
    if (faction != FACTION_PLAYER || theirs == FACTION_PLAYER || theirs == FACTION_FACTIONLESS || isMobFaction(theirs)) return;
    const int now = changePlayerReputation(w, theirs, -REP_KO_PENALTY);
    queueLog(w, langf("MISC_REPUTATION_DROPS", { factionName(theirs), std::to_string(REP_KO_PENALTY), std::to_string(now) }), LogKind::Other);
}

// Takes from target what the rule says into u's pack; what u can't hold is dropped beside target. How many stacks moved.
int takeFromDowned(World& w, ObjectId u, ObjectId target, DownedTakeRule rule) {
    if (rule == DownedTakeRule::Nothing) return 0;
    std::vector<ItemStack> taken;
    if (rule == DownedTakeRule::Weapons || rule == DownedTakeRule::Everything) {
        if (Equipment* worn = w.get<Equipment>(target)) {
            for (int slot : { (int)GearSlot::MainHand, (int)GearSlot::OffHand }) {
                if (worn->items[slot] == 0) continue;
                taken.push_back(ItemStack{ worn->items[slot], 1 });
                worn->items[slot] = 0;
            }
        }
    }
    if (Inventory* from = w.get<Inventory>(target)) {
        for (ItemStack& s : from->slots) {
            if (s.item == 0 || s.count <= 0) continue;
            const ItemDef* d = findItem(s.item);
            const bool food = d && restoreAmount(*d, UseEffect::Stat::Hunger) > 0;
            if (rule != DownedTakeRule::Everything && !food) continue;
            taken.push_back(s);
            s = ItemStack{};
        }
    }
    if (taken.empty()) return 0;
    Inventory* into = w.has<Mob>(u) ? nullptr : w.get<Inventory>(u);
    std::vector<ItemStack> dropped;
    for (const ItemStack& s : taken) {
        const int item = reclaimStolen(w, u, s.item);
        const int left = into ? addToInventory(*into, item, s.count) : s.count;
        if (left < s.count) queueItemMove(w, target, u, s.item, s.count - left);
        if (left > 0) dropped.push_back(ItemStack{ item, left });
    }
    if (!dropped.empty()) dropStacks(w, *w.get<Transform>(target), dropped, storeyOf(w, target));
    queueSound(w, Sfx::Take);
    queueLog(w, langf(dropped.empty() ? "MISC_TAKES_FROM_DOWNED" : "MISC_TAKES_FROM_DOWNED_DROPS", { nameOf(w, u), nameOf(w, target) }),
             LogKind::Other, npcOnly(w, u, target));
    return (int)taken.size();
}

void pickUp(World& w, ObjectId u, ObjectId target) {
    clearOrders(w, target);
    w.remove<IdleSpot>(target);
    if (Combat* c = w.get<Combat>(target)) c->target = NO_OBJECT;
    w.add(target, Carried{ u });
    w.add(u, Carrying{ target });
    queueLog(w, langf("MISC_PICKS_UP_UNIT", { nameOf(w, u), nameOf(w, target) }), LogKind::Other, npcOnly(w, u, target));
    queueSoundFrom(w, Sfx::CarryUp, u);
}

// Puts the carried unit into the cage and locks it, as placing one in a cage by hand does
bool lockInCage(World& w, ObjectId u, ObjectId cage) {
    const Carrying* cr = w.get<Carrying>(u);
    const Transform* pt = w.get<Transform>(cage);
    const Prop* p = w.get<Prop>(cage);
    if (!cr || !pt || !p || occupantCount(w, cage) >= occupyCapacity(w, cage)) return false;
    const ObjectId carried = cr->unit;
    Transform* ct = w.get<Transform>(carried);
    if (!ct) return false;
    w.remove<Carrying>(u);
    w.remove<Carried>(carried);
    w.remove<PlayingDead>(carried);
    *ct = *pt;
    w.remove<IdleSpot>(carried);
    w.add(carried, Occupying{ cage });
    const PropInfo& info = propInfo(p->kind);
    queueSoundFrom(w, w.has<Lock>(cage) ? Sfx::LockedIn : Sfx::Occupy, cage);
    queueLog(w, langf("MISC_PLACES_IN", { nameOf(w, u), nameOf(w, carried), lang(info.nameKey) }), LogKind::Other, npcOnly(w, u, carried));
    if (Lock* lk = w.get<Lock>(cage)) {
        lk->locked = true;
        lk->faction = factionOf(w, u);
        queueLog(w, langf("MISC_LOCKED_IN", { nameOf(w, carried), lang(info.nameKey) }), LogKind::Other, npcOnly(w, carried));
    }
    patronEvent(w, PatronEvent::Caged, factionOf(w, u), factionOf(w, carried));
    return true;
}

void finishOff(World& w, ObjectId u, ObjectId target) {
    if (Unconscious* un = w.get<Unconscious>(target)) un->koFaction = factionOf(w, u);
    if (Needs* n = w.get<Needs>(target)) {
        n->hp = 0;
        n->changed |= NEEDS_HP_CHANGED;
    }
    queueLog(w, langf("MISC_FINISHES_OFF", { nameOf(w, u), nameOf(w, target) }), LogKind::Other, npcOnly(w, u, target));
    finishDying(w, target);
    queueSoundFrom(w, Sfx::Dies, target);
}

bool startDeal(World& w, Terrain& t, ObjectId u, ObjectId target, bool ordered) {
    DealJob job;
    job.target = target;
    job.ordered = ordered;
    const Transform* tt = w.get<Transform>(target);
    if (!tt) return false;
    if (ordered) clearOrders(w, u);
    else clearOrdersButFight(w, u);
    w.remove<IdleSpot>(u);
    if (flatGap(w, u, target) > CARRY_REACH) walkSoloTo(w, t, u, Vec2{ tt->x, tt->z }, storeyOf(w, target));
    w.add(u, job);
    return true;
}
}

bool dealableBy(World& w, int faction, ObjectId v) {
    if (!w.has<Unconscious>(v) || w.has<Corpse>(v) || w.has<Carried>(v) || w.has<Mob>(v) || inPrisonerCage(w, v)) return false;
    if (const Dealt* d = w.get<Dealt>(v); d && d->faction == faction) return false;
    const int theirs = factionOf(w, v);
    return theirs != faction && factionsHostile(w, faction, theirs);
}

bool startDealStep(World& w, Terrain& t, ObjectId u, Behaviors& b, BehaviorRule& r) {
    r.why = BehaviorWait::None;
    if (w.has<Mob>(u) || !w.has<Transform>(u) || w.has<Carrying>(u) || w.has<DealJob>(u)) return false;
    if (!rulesDoSomething(factionRules(w, factionOf(w, u)))) {
        r.why = BehaviorWait::NoDowned;
        return false;
    }
    const ObjectId target = downedTarget(w, u);
    if (target == NO_OBJECT) {
        r.why = BehaviorWait::NoDowned;
        return false;
    }
    b.busy = true;
    b.step = r;
    b.stepProp = NO_OBJECT;
    return startDeal(w, t, u, target, false);
}

bool issueDealOrder(World& w, Terrain& t, const std::vector<ObjectId>& units, ObjectId target, std::string& why) {
    if (!rulesDoSomething(factionRules(w, FACTION_PLAYER))) {
        why = lang("MISC_DEAL_RULES_LEAVE");
        return false;
    }
    if (!dealableBy(w, FACTION_PLAYER, target)) {
        why = lang("MISC_DEAL_NOT_DOWNED_ENEMY");
        return false;
    }
    for (ObjectId u : units) {
        if (u == target || !w.has<Commandable>(u) || w.has<Mob>(u) || !w.has<Transform>(u) || w.has<Carrying>(u) || cannotTakeOrder(w, u)) continue;
        w.remove<DealJob>(u);
        return startDeal(w, t, u, target, true);
    }
    why = lang("MISC_ALREADY_CARRYING");
    return false;
}

void dealJobSystem(World& w, Terrain& t, float dt) {
    std::vector<Claim> claims;
    bool claimsRead = false;
    auto claimList = [&]() -> const std::vector<Claim>& {
        if (!claimsRead) claims = claimsOf(w);
        claimsRead = true;
        return claims;
    };
    for (ObjectId u : w.view<DealJob, Transform>()) {
        DealJob& job = *w.get<DealJob>(u);
        const ObjectId target = job.target;
        const int faction = factionOf(w, u);
        Behaviors* b = w.get<Behaviors>(u);
        auto end = [&](float cooldown) {
            const bool ordered = job.ordered;
            w.remove<DealJob>(u);
            w.remove<SoloMove>(u);
            w.remove<Route>(u);
            if (const Carrying* cr = w.get<Carrying>(u); cr && cr->unit == target) putDown(w, u);
            if (!b || ordered || !b->busy || b->step.kind != BehaviorKind::DealWithDowned) return;
            b->busy = false;
            if (BehaviorRule* r = ruleOf(*b, BehaviorKind::DealWithDowned)) r->cooldown = std::max(r->cooldown, cooldown);
        };
        auto dealt = [&]() {
            if (w.has<Entity>(target)) w.add(target, Dealt{ faction });
        };
        // After carrying one out, a unit of the faction's own (not one you ordered) goes back in: to a random clear tile (no
        // foundation, no prop) from halfway to the claim's centre to the centre
        auto backSpot = [&](Vec2& out) {
            if (job.ordered || !job.hasHome) return false;
            return claimInnerSpot(w, t, u, job.homeX, job.homeZ, out);
        };
        if (outCold(w, u) || w.has<Carried>(u) || !w.has<Transform>(target) || !outCold(w, target) || w.has<Corpse>(target)) {
            end(BEHAVIOR_RETRY_SECONDS);
            continue;
        }
        const FactionRules& rules = factionRules(w, faction);
        if (job.phase == DealJob::Phase::Reach) {
            if (w.has<Carried>(target) || inPrisonerCage(w, target)) {
                end(BEHAVIOR_RETRY_SECONDS);
                continue;
            }
            if (flatGap(w, u, target) > CARRY_REACH) {
                if (!walkingNow(w, u)) {
                    job.stuck += dt;
                    const Transform* tt = w.get<Transform>(target);
                    if (job.stuck > 12.0f) end(BEHAVIOR_RETRY_SECONDS * 3.0f);
                    else if (job.stuck > 1.0f && job.stuck - dt <= 1.0f) walkSoloTo(w, t, u, Vec2{ tt->x, tt->z }, storeyOf(w, target));
                }
                continue;
            }
            if (w.has<Climbing>(u)) continue;
            job.stuck = 0.0f;
            w.remove<SoloMove>(u);
            w.remove<Route>(u);
            if (takeFromDowned(w, u, target, rules.downedTake) > 0) reputationHit(w, faction, target);
            DownedBodyRule body = rules.downedBody;
            if (body == DownedBodyRule::FinishOff) {
                finishOff(w, u, target);
                end(0.0f);
                continue;
            }
            if (body == DownedBodyRule::Cage) {
                const ObjectId cage = freeCage(w, claimList(), u, faction);
                if (cage != NO_OBJECT) {
                    pickUp(w, u, target);
                    job.cage = cage;
                    job.phase = DealJob::Phase::Carry;
                    const Transform* ct = w.get<Transform>(cage);
                    walkSoloTo(w, t, u, Vec2{ ct->x, ct->z }, storeyOf(w, cage));
                    continue;
                }
                body = DownedBodyRule::CarryOut;   // no cage free: out of the claim instead
            }
            if (body == DownedBodyRule::CarryOut) {
                const Transform* tt = w.get<Transform>(target);
                const Claim* c = claimAt(claimList(), tt->x, tt->z, faction);
                if (c) {
                    const Vec2 to = pastClaimEdge(t, *c, tt->x, tt->z);
                    pickUp(w, u, target);
                    job.toX = to.x;
                    job.toZ = to.z;
                    job.homeX = c->x;
                    job.homeZ = c->z;
                    job.hasHome = true;
                    job.phase = DealJob::Phase::Carry;
                    walkSoloTo(w, t, u, to, 0);
                    continue;
                }
            }
            // Left lying (or already outside the claim)
            dealt();
            end(0.0f);
            continue;
        }
        // Carrying it
        const Carrying* cr = w.get<Carrying>(u);
        if (!cr || cr->unit != target) {
            end(BEHAVIOR_RETRY_SECONDS);
            continue;
        }
        float gap = 0.0f;
        if (job.cage != NO_OBJECT) {
            if (!w.has<Transform>(job.cage)) {
                end(BEHAVIOR_RETRY_SECONDS);
                continue;
            }
            gap = flatGap(w, u, job.cage) - PROP_REACH;
        } else {
            const Transform* ut = w.get<Transform>(u);
            gap = std::hypot(ut->x - job.toX, ut->z - job.toZ) - 1.5f;
        }
        if (gap > 0.0f) {
            if (!walkingNow(w, u)) {
                job.stuck += dt;
                // Out as far as it could get: put down there
                if (job.cage == NO_OBJECT && job.stuck > 3.0f) {
                    putDown(w, u);
                    dealt();
                    Vec2 back;
                    const bool goBack = backSpot(back);
                    end(0.0f);
                    if (goBack) walkSoloTo(w, t, u, back, 0);
                } else if (job.stuck > 12.0f) {
                    end(BEHAVIOR_RETRY_SECONDS * 3.0f);
                } else if (job.stuck > 1.0f && job.stuck - dt <= 1.0f) {
                    if (job.cage != NO_OBJECT) {
                        const Transform* ct = w.get<Transform>(job.cage);
                        walkSoloTo(w, t, u, Vec2{ ct->x, ct->z }, storeyOf(w, job.cage));
                    } else {
                        walkSoloTo(w, t, u, Vec2{ job.toX, job.toZ }, 0);
                    }
                }
            }
            continue;
        }
        if (w.has<Climbing>(u)) continue;
        w.remove<SoloMove>(u);
        w.remove<Route>(u);
        if (job.cage != NO_OBJECT) {
            if (lockInCage(w, u, job.cage)) reputationHit(w, faction, target);
            end(0.0f);
            continue;
        }
        putDown(w, u);
        queueLog(w, langf("MISC_CARRIES_OUT", { nameOf(w, u), nameOf(w, target) }), LogKind::Other, npcOnly(w, u, target));
        dealt();
        Vec2 back;
        const bool goBack = backSpot(back);
        end(0.0f);
        if (goBack) walkSoloTo(w, t, u, back, 0);
    }
}

void joinFaction(World& w, ObjectId u, int faction) {
    if (!w.has<Entity>(u) || factionOf(w, u) == faction || !findFaction(faction)) return;
    leaveSquad(w, u);
    if (w.has<Carrying>(u)) putDown(w, u);
    w.remove<Commandable>(u);
    w.remove<Selected>(u);
    w.remove<SquadMember>(u);
    w.remove<Citizen>(u);
    w.remove<SpawnHome>(u);
    w.remove<Provoked>(u);
    w.remove<Raider>(u);
    w.remove<Shopper>(u);
    w.remove<Trespassing>(u);
    w.remove<Recruiting>(u);
    if (Faction* fa = w.get<Faction>(u)) fa->id = faction;
    else w.add(u, Faction{ faction });
    if (Combat* c = w.get<Combat>(u)) c->target = NO_OBJECT;
    if (factionPlayerControlled(faction)) {
        w.remove<Hostile>(u);
        w.remove<Neutral>(u);
        if (Entity* en = w.get<Entity>(u)) en->speed = Entity{}.speed;
        makeCitizen(w, u);
    } else {
        Behaviors& b = w.has<Behaviors>(u) ? *w.get<Behaviors>(u) : w.add(u, Behaviors{});
        b.rules = faction == FACTION_FACTIONLESS ? factionlessBehaviors() : npcFactionBehaviors();
        b.busy = false;
        b.pending = true;
        if (Entity* en = w.get<Entity>(u)) en->speed = HOSTILE_WALK_SPEED;
        if (!w.has<Dialogue>(u)) w.add(u, defaultDialogue());
    }
    queueLog(w, langf("MISC_JOINS_FACTION", { nameOf(w, u), factionName(faction) }), LogKind::Other, LogWho(false, u));
}

void recruitOnWaking(World& w, ObjectId u) {
    if (w.has<Mob>(u) || w.has<Corpse>(u)) return;
    const ObjectId bed = occupiedProp(w, u);
    const Prop* p = bed != NO_OBJECT ? w.get<Prop>(bed) : nullptr;
    if (!p || !isBed(p->kind)) return;
    int owner = -1;
    if (const Owner* own = w.get<Owner>(bed)) {
        owner = own->faction;
    } else if (const Transform* bt = w.get<Transform>(bed)) {
        const std::vector<Claim> claims = claimsOf(w);
        if (const Claim* c = claimAt(claims, bt->x, bt->z)) owner = c->owner;
    }
    const int mine = factionOf(w, u);
    // Units of yours are never taken: they go home when they wake
    if (owner < 0 || owner == mine || factionPlayerControlled(mine) || isMobFaction(owner) || !factionCanClaim(owner)) return;
    const RecruitRule rule = factionRules(w, owner).recruit;
    if (rule != RecruitRule::Rescued && rule != RecruitRule::Both) return;
    joinFaction(w, u, owner);
    patronEvent(w, PatronEvent::Rescue, owner, mine);   // the one that took it in speaks to the one it came from
}

namespace {
// The rules are looked at twice a second, not every frame
struct RulesClock {
    float since = 0.0f;
};
}

void factionRulesSystem(World& w, float frameDt) {
    RulesClock& clock = w.resource<RulesClock>();
    clock.since += frameDt;
    if (clock.since < 0.5f) return;
    const float dt = clock.since;
    clock.since = 0.0f;
    const std::vector<Claim> claims = claimsOf(w);
    if (claims.empty()) {
        for (ObjectId u : w.view<Trespassing>()) w.remove<Trespassing>(u);
        for (ObjectId u : w.view<Recruiting>()) w.remove<Recruiting>(u);
        return;
    }
    const std::vector<ObjectId> units = w.view<Entity, Transform>();
    for (ObjectId u : units) {
        const Transform* ut = w.get<Transform>(u);
        const bool away = w.has<Mob>(u) || outCold(w, u) || w.has<Carried>(u) || lockedIn(w, u) || w.has<Corpse>(u);
        const Claim* c = away ? nullptr : claimAt(claims, ut->x, ut->z);
        const int mine = factionOf(w, u);
        const int owner = c ? c->owner : -1;
        // Recruiting: a Factionless unit lingering in the claim of a faction that takes them in
        RecruitRule recruit = RecruitRule::Never;
        if (owner >= 0 && mine == FACTION_FACTIONLESS && !factionsHostile(w, owner, mine)) recruit = factionRules(w, owner).recruit;
        if (recruit == RecruitRule::Factionless || recruit == RecruitRule::Both) {
            Recruiting& rc = w.has<Recruiting>(u) ? *w.get<Recruiting>(u) : w.add(u, Recruiting{ owner, 0.0f });
            if (rc.faction != owner) rc = Recruiting{ owner, 0.0f };
            rc.seconds += dt;
            if (rc.seconds >= RECRUIT_SECONDS) {
                joinFaction(w, u, owner);
                continue;
            }
        } else {
            w.remove<Recruiting>(u);
        }
        // Trespassing: an outsider, neither ally nor enemy, on business of its own (not shopping or talking)
        TrespassRule rule = TrespassRule::Ignore;
        if (owner >= 0 && owner != mine && !factionsAllied(w, owner, mine) && !factionsHostile(w, owner, mine) &&
            !w.has<Shopper>(u) && !w.has<InConversation>(u)) {
            rule = factionRules(w, owner).trespass;
        }
        if (rule == TrespassRule::Ignore) {
            w.remove<Trespassing>(u);
            continue;
        }
        Trespassing& tp = w.has<Trespassing>(u) ? *w.get<Trespassing>(u) : w.add(u, Trespassing{ owner });
        if (tp.faction != owner) tp = Trespassing{ owner };
        tp.seconds += dt;
        const bool yoursInvolved = factionPlayerControlled(owner) || factionPlayerControlled(mine);
        if (!tp.warned) {
            tp.warned = true;
            if (rule == TrespassRule::Warn) {
                queueLog(w, langf("MISC_TRESPASS_WARNED", { nameOf(w, u), factionName(owner) }), LogKind::Other, LogWho(!yoursInvolved, u),
                         yoursInvolved ? Tint::Red : Tint::Gray);
                patronEvent(w, PatronEvent::Trespass, owner, mine);
            }
        }
        if (tp.attacked || (rule == TrespassRule::Warn && tp.seconds < TRESPASS_WARN_SECONDS)) continue;
        tp.attacked = true;
        queueLog(w, langf("MISC_TRESPASS_ATTACKED", { nameOf(w, u), factionName(owner) }), LogKind::Other, LogWho(!yoursInvolved, u),
                 yoursInvolved ? Tint::Red : Tint::Gray);
        patronEvent(w, PatronEvent::Trespass, owner, mine);
        // The owner's units who see it turn on it; a trespasser on your land turns on yours, so they fight it
        if (factionPlayerControlled(owner)) {
            provoke(w, u, owner);
            continue;
        }
        for (ObjectId o : units) {
            if (o == u || factionOf(w, o) != owner || outCold(w, o) || inPrisonerCage(w, o)) continue;
            const Transform* ot = w.get<Transform>(o);
            if (distSq(*ot, *ut) > PROVOKED_SIGHT_RANGE * PROVOKED_SIGHT_RANGE) continue;
            provoke(w, o, mine);
            if (Combat* cb = w.get<Combat>(o); cb && cb->target == NO_OBJECT && !w.has<Commandable>(o)) cb->target = u;
        }
    }
}
