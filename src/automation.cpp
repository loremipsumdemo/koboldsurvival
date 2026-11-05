// Behavior rules run on their own: hauling, harvesting, resting, rescuing, patrols and roaming, stepping off a
// prop. Patrol spawns and wild mobs are in wild_ai.cpp.
#include "systems_internal.h"

using namespace systems_detail;

namespace {

// How many of an item a unit carries
int carriedCount(World& w, Entity u, int item) {
    const Inventory* inv = w.get<Inventory>(u);
    return inv ? carriedCount(*inv, item) : 0;
}

// A unit's faction, the Player Faction for one with none
int unitFaction(World& w, Entity u) {
    const Faction* f = w.get<Faction>(u);
    return f ? f->id : FACTION_PLAYER;
}

// Whether a prop stands inside a claim of this faction, and so may be worked or hauled to by its automation
bool inOwnClaim(World& w, const std::vector<Claim>& claims, Entity prop, int faction) {
    const Transform* t = w.get<Transform>(prop);
    return t && claimAt(claims, t->x, t->z, faction) != nullptr;
}

// Whether a unit is full for an item: no room for another, or another would weigh it down to Encumbered (and soon to a
// stop)
bool fullFor(World& w, Entity u, int item) {
    const Inventory* inv = w.get<Inventory>(u);
    if (!inv || slotsRoomFor(inv->slots.data(), INVENTORY_SLOTS, item) <= 0) return true;
    const ItemDef* def = findItem(item);
    return encumbranceFor(carriedWeightOf(w, u) + (def ? def->weight : 0.0f), statsOf(w, u)) >= ENCUMBERED_SHARE;
}

// Why an Auto-Haul can't be done now, or BehaviorWait::None when it can: its prop is there, open to the unit, in a
// claim of the unit's faction, and has room for at least one item the unit carries. item 0: any item the prop takes;
// otherwise only that item.
BehaviorWait haulWait(World& w, const std::vector<Claim>& claims, Entity u, const BehaviorRule& r, int item = 0) {
    const Prop* p = w.get<Prop>(r.prop);
    const Storage* st = w.get<Storage>(r.prop);
    const Inventory* inv = w.get<Inventory>(u);
    if (!p || !st || !inv) return BehaviorWait::PropGone;
    if (!inOwnClaim(w, claims, r.prop, unitFaction(w, u))) return BehaviorWait::OutsideClaim;
    if (lockShutFor(w, r.prop, u)) return BehaviorWait::Locked;
    bool carries = false;
    for (const ItemStack& s : inv->slots) {
        if (s.item <= 0 || s.count <= 0 || (item != 0 && s.item != item) || !propAcceptsItem(p->kind, s.item)) continue;
        carries = true;
        if (slotsRoomFor(st->slots.data(), STORAGE_SLOTS, s.item) > 0) return BehaviorWait::None;
    }
    return carries ? BehaviorWait::PropFull : BehaviorWait::NothingToHaul;
}
bool haulable(World& w, const std::vector<Claim>& claims, Entity u, const BehaviorRule& r, int item = 0) {
    return haulWait(w, claims, u, r, item) == BehaviorWait::None;
}

// Whether one of the unit's Auto-Hauls that is on could take this item now
bool canHaulItem(World& w, const std::vector<Claim>& claims, Entity u, const Behaviors& b, int item) {
    for (const BehaviorRule& r : b.rules) {
        if (r.on && r.kind == BehaviorKind::AutoHaul && haulable(w, claims, u, r, item)) return true;
    }
    return false;
}

// Whether an Auto-Harvest has a load: the unit carries AUTO_LOAD of what it gathers and an Auto-Haul can take it, or
// it has no room for more
bool harvestLoaded(World& w, const std::vector<Claim>& claims, Entity u, const Behaviors& b, int item) {
    if (fullFor(w, u, item)) return true;
    return carriedCount(w, u, item) >= AUTO_LOAD && canHaulItem(w, claims, u, b, item);
}

// The nearest resource prop of the kind an Auto-Harvest works, in a claim of the unit's faction, open to it and with
// room for another worker. NULL_ENTITY when there is none. ruleKind is the rule's propKind, -1 for none.
Entity harvestTarget(World& w, const std::vector<Claim>& claims, Entity u, int ruleKind) {
    const Transform* ut = w.get<Transform>(u);
    if (!ut || ruleKind < 0 || ruleKind >= PROP_KIND_COUNT) return NULL_ENTITY;
    const PropKind kind = (PropKind)ruleKind;
    if (!yieldsItems(kind)) return NULL_ENTITY;
    const int faction = unitFaction(w, u);
    Entity best = NULL_ENTITY;
    float bestD2 = 0.0f;
    const std::vector<Entity> worked = workedProps(w);
    for (Entity p : w.view<Prop, Transform>()) {
        if (w.get<Prop>(p)->kind != kind || !inOwnClaim(w, claims, p, faction) || lockShutFor(w, p, u)) continue;
        if (countIn(worked, p) >= propInfo(kind).workers) continue;
        const Transform* pt = w.get<Transform>(p);
        const float d2 = (pt->x - ut->x) * (pt->x - ut->x) + (pt->z - ut->z) * (pt->z - ut->z);
        if (best == NULL_ENTITY || d2 < bestD2) {
            best = p;
            bestD2 = d2;
        }
    }
    return best;
}

// Whether a unit is busy with something that is not its automation: an order of yours, a seat or bed, carrying someone
bool onOtherOrder(World& w, Entity u, const Behaviors& b) {
    if (const PropOrder* po = w.get<PropOrder>(u); po && !po->automatic) return true;
    if (const Working* wk = w.get<Working>(u); wk && (wk->unlocking || !b.busy || wk->prop != b.stepProp)) return true;
    if (w.has<Occupying>(u) || w.has<FollowOrder>(u) || w.has<DefendOrder>(u) || w.has<PickupOrder>(u) || w.has<PickupChain>(u) ||
        w.has<LootOrder>(u) || w.has<CarryOrder>(u) || w.has<TalkOrder>(u) ||
        w.has<Carrying>(u) || w.has<AttackOrder>(u)) {
        return true;
    }
    if (const SoloMove* so = w.get<SoloMove>(u); so && so->next < so->waypoints.size() && !w.has<PropOrder>(u) && !b.seeking &&
        !(b.busy && isWalkBehavior(b.step.kind))) {
        return true;
    }
    if (const ActiveSquadMember* sm = w.get<ActiveSquadMember>(u)) {
        if (const ActiveSquad* sq = w.get<ActiveSquad>(sm->squad); sq && !sq->arrived) return true;
    }
    return false;
}

// Sends a unit on an automated step: it walks to the prop, and in reach does the action
void autoGo(World& w, Terrain& t, Entity u, Entity prop, PropAction action) {
    const Transform* pt = w.get<Transform>(prop);
    if (!pt) return;
    autoWalk(w, t, u, Vec2{ pt->x, pt->z });
    w.add(u, PropOrder{ prop, action, true });
}

// The behavior in the list that the unit's step is for, or nullptr if it was deleted
BehaviorRule* stepRule(Behaviors& b) {
    for (BehaviorRule& r : b.rules) {
        if (sameBehavior(r, b.step)) return &r;
    }
    return nullptr;
}

// Patrol: a walkable spot inside the claim the unit stands in, when that claim is its faction's
BehaviorWait patrolSpot(World& w, const Terrain& t, const std::vector<Claim>& claims, Entity u, Rng& rng, Vec2& to) {
    const Transform* ut = w.get<Transform>(u);
    const Claim* c = ut ? claimAt(claims, ut->x, ut->z, unitFaction(w, u)) : nullptr;
    if (!c) return BehaviorWait::NotInClaim;
    for (int attempt = 0; attempt < 12; ++attempt) {
        const float a = rng.unit() * TWO_PI, d = std::sqrt(rng.unit()) * (CLAIM_RADIUS - 2.0f);
        const float x = c->x + std::cos(a) * d, z = c->z + std::sin(a) * d;
        if (!openTile(t, x, z)) continue;
        to = Vec2{ std::floor(x) + 0.5f, std::floor(z) + 0.5f };
        return BehaviorWait::None;
    }
    return BehaviorWait::NotInClaim;
}

// Roam: a walkable spot a short way off, outside every claim
BehaviorWait roamSpot(World& w, const Terrain& t, const std::vector<Claim>& claims, Entity u, Rng& rng, Vec2& to) {
    const Transform* ut = w.get<Transform>(u);
    if (!ut) return BehaviorWait::NowhereToRoam;
    for (int attempt = 0; attempt < 12; ++attempt) {
        const float a = rng.unit() * TWO_PI, d = ROAM_STEP_MIN + rng.unit() * (ROAM_STEP_MAX - ROAM_STEP_MIN);
        const float x = ut->x + std::cos(a) * d, z = ut->z + std::sin(a) * d;
        if (!openTile(t, x, z) || claimAt(claims, x, z)) continue;
        to = Vec2{ std::floor(x) + 0.5f, std::floor(z) + 0.5f };
        return BehaviorWait::None;
    }
    return BehaviorWait::NowhereToRoam;
}

// Rest: the nearest bed or bedroll nobody lies in or is on the way to, that is not locked to the unit
Entity restTarget(World& w, Entity u) {
    const Transform* ut = w.get<Transform>(u);
    if (!ut) return NULL_ENTITY;
    std::vector<Entity> wanted;   // the beds others are already on their way to
    for (Entity o : w.view<PropOrder>()) {
        if (o != u) wanted.push_back(w.get<PropOrder>(o)->prop);
    }
    std::sort(wanted.begin(), wanted.end());
    const std::vector<Entity> occupied = occupiedProps(w);
    Entity best = NULL_ENTITY;
    float bestD2 = 0.0f;
    for (Entity p : w.view<Prop, Transform>()) {
        const PropKind kind = w.get<Prop>(p)->kind;
        if (restedGainPerHour(kind) <= 0.0) continue;
        if (countIn(occupied, p) > 0 || lockShutFor(w, p, u)) continue;
        if (std::binary_search(wanted.begin(), wanted.end(), p)) continue;
        const Transform* pt = w.get<Transform>(p);
        const float d2 = (pt->x - ut->x) * (pt->x - ut->x) + (pt->z - ut->z) * (pt->z - ut->z);
        if (best == NULL_ENTITY || d2 < bestD2) {
            best = p;
            bestD2 = d2;
        }
    }
    return best;
}

// Rescue: the nearest unit of its own faction lying out cold on the ground (not a corpse, not in a bed, carried by
// nobody and not being fetched by anyone else) within RESCUE_RANGE
Entity rescueTarget(World& w, Entity u) {
    const Transform* ut = w.get<Transform>(u);
    if (!ut) return NULL_ENTITY;
    const int mine = factionOf(w, u);
    std::vector<Entity> fetched;
    for (Entity o : w.view<CarryOrder>()) {
        if (o != u) fetched.push_back(w.get<CarryOrder>(o)->target);
    }
    Entity best = NULL_ENTITY;
    float bestD2 = RESCUE_RANGE * RESCUE_RANGE;
    for (Entity v : w.view<Unconscious, Transform>()) {
        if (v == u || w.has<Corpse>(v) || w.has<Carried>(v) || w.has<Mob>(v) || factionOf(w, v) != mine) continue;
        const Entity bed = occupiedProp(w, v);
        if (bed != NULL_ENTITY && w.has<Prop>(bed) && restedGainPerHour(w.get<Prop>(bed)->kind) > 0.0) continue;
        if (std::find(fetched.begin(), fetched.end(), v) != fetched.end()) continue;
        const Transform* vt = w.get<Transform>(v);
        const float d2 = (vt->x - ut->x) * (vt->x - ut->x) + (vt->z - ut->z) * (vt->z - ut->z);
        if (d2 <= bestD2) {
            best = v;
            bestD2 = d2;
        }
    }
    return best;
}

// Patrol Spawn: a walkable spot within SPAWN_PATROL_RADIUS of where the mob was placed
BehaviorWait spawnSpot(World& w, const Terrain& t, Entity u, Rng& rng, Vec2& to) {
    const SpawnHome* h = w.get<SpawnHome>(u);
    if (!h) return BehaviorWait::NowhereToRoam;
    for (int attempt = 0; attempt < 12; ++attempt) {
        const float a = rng.unit() * TWO_PI, d = std::sqrt(rng.unit()) * SPAWN_PATROL_RADIUS;
        const float x = h->x + std::cos(a) * d, z = h->z + std::sin(a) * d;
        if (!openTile(t, x, z)) continue;
        to = Vec2{ std::floor(x) + 0.5f, std::floor(z) + 0.5f };
        return BehaviorWait::None;
    }
    return BehaviorWait::NowhereToRoam;
}

// Tries to start an automated behavior's step. False, with nothing else changed, when it can't be done now; r.why
// then says why.
bool startStep(World& w, Terrain& t, const std::vector<Claim>& claims, Entity u, Behaviors& b, BehaviorRule& r, Rng& rng) {
    if (r.kind == BehaviorKind::Rescue) {
        // To a unit of its faction out cold, to pick it up, when there is a bed or bedroll to take it to
        r.why = BehaviorWait::None;
        if (w.has<Mob>(u) || w.has<Carrying>(u)) return false;
        const Entity target = rescueTarget(w, u);
        if (target == NULL_ENTITY) return false;
        if (restTarget(w, u) == NULL_ENTITY) {
            r.why = BehaviorWait::NoBed;
            return false;
        }
        const Transform* tt = w.get<Transform>(target);
        b.busy = true;
        b.step = r;
        b.stepProp = NULL_ENTITY;
        autoWalk(w, t, u, Vec2{ tt->x, tt->z });
        w.add(u, CarryOrder{ target });
        return true;
    }
    if (r.kind == BehaviorKind::Rest) {
        // Only below its percent of Rested: then to the nearest free bed or bedroll, to lie in it
        const Needs* n = w.get<Needs>(u);
        r.why = BehaviorWait::None;
        if (!n || w.has<Mob>(u) || percentOf(n->rested, n->maxRested) >= r.percent) return false;
        const Entity bed = restTarget(w, u);
        if (bed == NULL_ENTITY) {
            r.why = BehaviorWait::NoBed;
            return false;
        }
        b.busy = true;
        b.step = r;
        b.stepProp = bed;
        autoGo(w, t, u, bed, PropAction::Occupy);
        return true;
    }
    if (isWalkBehavior(r.kind)) {
        Vec2 to{ 0.0f, 0.0f };
        r.why = r.kind == BehaviorKind::Patrol ? patrolSpot(w, t, claims, u, rng, to)
              : r.kind == BehaviorKind::PatrolSpawn ? spawnSpot(w, t, u, rng, to) : roamSpot(w, t, claims, u, rng, to);
        if (r.why != BehaviorWait::None) return false;
        b.busy = true;
        b.step = r;
        b.stepProp = NULL_ENTITY;
        autoWalk(w, t, u, to);
        return true;
    }
    Entity prop = NULL_ENTITY;
    PropAction action = PropAction::Use;
    if (r.kind == BehaviorKind::AutoHaul) {
        r.why = haulWait(w, claims, u, r);
        if (r.why != BehaviorWait::None) return false;
        prop = r.prop;
        action = PropAction::Store;
    } else {
        const int item = propInfo((PropKind)r.propKind).item;
        if (fullFor(w, u, item)) r.why = BehaviorWait::CarryFull;
        else if (harvestLoaded(w, claims, u, b, item)) r.why = BehaviorWait::LoadWaiting;
        else if ((prop = harvestTarget(w, claims, u, r.propKind)) == NULL_ENTITY) r.why = BehaviorWait::NoProp;
        if (r.why != BehaviorWait::None) return false;
    }
    r.why = BehaviorWait::None;
    b.busy = true;
    b.step = r;
    b.stepProp = prop;
    autoGo(w, t, u, prop, action);
    return true;
}

}  // namespace

// Sends a unit on a walk step: it walks to the spot, and the step ends when it gets there
void systems_detail::autoWalk(World& w, Terrain& t, Entity u, Vec2 to) {
    clearOrdersButFight(w, u);
    w.remove<IdleSpot>(u);
    walkSolo(w, t, u, to);
}

// Ends the automation's own step, if the unit is on one
void systems_detail::stopAutoStep(World& w, Entity u, Behaviors& b) {
    if (const PropOrder* po = w.get<PropOrder>(u); po && po->automatic) {
        w.remove<PropOrder>(u);
        w.remove<SoloMove>(u);
    }
    if (b.busy && isWalkBehavior(b.step.kind)) w.remove<SoloMove>(u);
    if (const Occupying* oc = w.get<Occupying>(u); oc && b.busy && b.step.kind == BehaviorKind::Rest && oc->prop == b.stepProp) {
        w.remove<Occupying>(u);
    }
    if (const Working* wk = w.get<Working>(u); wk && !wk->unlocking && b.busy && wk->prop == b.stepProp) w.remove<Working>(u);
    b.busy = false;
}

// Whether a tile is walkable ground a walk may end on
bool systems_detail::openTile(const Terrain& t, float x, float z) {
    return x >= 0.0f && z >= 0.0f && x < t.size() && z < t.size() && t.walkable((int)std::floor(x), (int)std::floor(z));
}

// Leave Bed: the unit getting up from a bed, bedroll or other resting prop steps off it onto the open ground beside it.
// The tiles around the prop are tried ring by ring, nearest first (a big prop's own tiles are skipped): the first
// walkable one no other prop stands on is taken, or failing that the first walkable one at all. With no walkable
// tile near, it stays where it is.
void stepOffProp(World& w, Terrain& t, Entity u, Entity prop) {
    Transform* ut = w.get<Transform>(u);
    const Transform* pt = w.get<Transform>(prop);
    const Prop* pp = w.get<Prop>(prop);
    if (!ut || !pt || !pp) return;
    const Footprint& pf = propInfo(pp->kind).foot;
    const int cx = (int)std::floor(pt->x), cz = (int)std::floor(pt->z);
    // Whether a point lies on a prop other than the one being left. A foundation is a floor, and blocks nothing.
    auto onOtherProp = [&](float x, float z) {
        for (Entity o : w.view<Prop, Transform>()) {
            if (o == prop) continue;
            const PropInfo& oi = propInfo(w.get<Prop>(o)->kind);
            if (oi.foot.foundation) continue;
            const Transform* ot = w.get<Transform>(o);
            const float dx = x - ot->x, dz = z - ot->z;
            const float reach = footprintReach(oi.foot) + PROP_BLOCK_RADIUS;
            if (dx * dx + dz * dz > reach * reach) continue;
            if (insideFootprint(oi.foot, dx, dz, w.get<Prop>(o)->yaw)) return true;
            if (dx * dx + dz * dz < PROP_BLOCK_RADIUS * PROP_BLOCK_RADIUS) return true;
        }
        return false;
    };
    constexpr int MAX_RING = 3;
    bool haveFallback = false;
    float fx = 0.0f, fy = 0.0f, fz = 0.0f;
    for (int ring = 1; ring <= MAX_RING; ++ring) {
        // The ring's edge-sharing tiles come before its corners, so the unit steps straight off a side when it can
        std::vector<std::pair<int, int>> tiles;
        for (int dz = -ring; dz <= ring; ++dz) {
            for (int dx = -ring; dx <= ring; ++dx) {
                if (std::max(std::abs(dx), std::abs(dz)) == ring) tiles.push_back({ dx, dz });
            }
        }
        std::stable_sort(tiles.begin(), tiles.end(), [](const auto& a, const auto& b) {
            return a.first * a.first + a.second * a.second < b.first * b.first + b.second * b.second;
        });
        for (const auto& [dx, dz] : tiles) {
            const int tx = cx + dx, tz = cz + dz;
            const float x = tx + 0.5f, z = tz + 0.5f;
            if (insideFootprint(pf, x - pt->x, z - pt->z, pp->yaw)) continue;   // still on the prop it is leaving
            bool walk = false;
            float h = 0.0f;
            if (!t.tileGround(tx, tz, walk, h) || !walk) continue;
            if (!onOtherProp(x, z)) {
                ut->x = x;
                ut->y = h;
                ut->z = z;
                return;
            }
            if (!haveFallback) {
                haveFallback = true;
                fx = x;
                fy = h;
                fz = z;
            }
        }
    }
    if (haveFallback) {
        ut->x = fx;
        ut->y = fy;
        ut->z = fz;
    }
}

void automationSystem(World& w, Terrain& t, float dt, Rng& rng) {
    const std::vector<Claim> claims = claimsOf(w);
    for (Entity u : w.view<Behaviors, Transform>()) {
        Behaviors* b = w.get<Behaviors>(u);
        bool any = false;
        for (BehaviorRule& r : b->rules) {
            if (!isAutoStep(r.kind)) continue;
            r.cooldown = std::max(0.0f, r.cooldown - dt);
            if (!r.on) r.why = BehaviorWait::None;
            any = any || r.on;
        }
        // The step's behavior was switched off or deleted: the step stops
        if (b->busy) {
            const BehaviorRule* current = stepRule(*b);
            if (!current || !current->on) stopAutoStep(w, u, *b);
        }
        if (!any && b->seek == NULL_ENTITY && !b->seeking) continue;
        const bool yours = w.has<Commandable>(u);
        const float resume = yours ? AUTO_RESUME_SECONDS : 0.0f;
        // Out cold, carried, shut in or downed: nothing to do
        const Needs* n = w.get<Needs>(u);
        if (w.has<Unconscious>(u) || w.has<Carried>(u) || w.has<PlayingDead>(u) || lockedIn(w, u) || (n && n->hp <= 0)) {
            b->busy = false;
            b->seeking = false;
            b->recovered = 0.0f;
            continue;
        }
        // Leave Bed: lying in a bed or bedroll at full health and fully rested for a while, it gets up
        {
            const Entity bed = occupiedProp(w, u);
            const Prop* bp = bed != NULL_ENTITY ? w.get<Prop>(bed) : nullptr;
            bool leaveOn = false;
            for (const BehaviorRule& r : b->rules) leaveOn = leaveOn || (r.on && r.kind == BehaviorKind::LeaveBed);
            if (leaveOn && bp && restedGainPerHour(bp->kind) > 0.0 && n && n->hp >= n->maxHp && n->rested >= n->maxRested) {
                b->recovered += dt;
                if (b->recovered >= LEAVE_BED_SECONDS) {
                    w.remove<Occupying>(u);
                    stepOffProp(w, t, u, bed);   // off the bed, onto the open ground beside it
                    b->recovered = 0.0f;
                    if (b->busy && b->step.kind == BehaviorKind::Rest) b->busy = false;
                }
            } else {
                b->recovered = 0.0f;
            }
        }
        // A fight comes first: the step stops, and the automation waits for the fight to end
        const Combat* c = w.get<Combat>(u);
        if ((c && c->target != NULL_ENTITY) || w.has<AttackOrder>(u)) {
            if (b->busy) stopAutoStep(w, u, *b);
            if (b->seeking) w.remove<SoloMove>(u);
            b->seeking = false;
            b->wait = resume;
            continue;
        }
        // Share: a unit in need walks to the unit that can share with it. The walk ends when it is close enough (Eat or
        // Heal then takes the item), when the need is gone, or when you give it an order. It may cut an automated
        // step short, but never an order of yours.
        if (b->seeking) {
            const SoloMove* so = w.get<SoloMove>(u);
            const bool walking = so && so->next < so->waypoints.size();
            if (b->seek == NULL_ENTITY || !walking || flatGap(w, u, b->seek) <= SHARE_RANGE) {
                if (walking) w.remove<SoloMove>(u);
                b->seeking = false;
            } else if (onOtherOrder(w, u, *b)) {
                b->seeking = false;
                b->wait = resume;
            }
            continue;
        }
        if (b->seek != NULL_ENTITY && w.has<Transform>(b->seek) && flatGap(w, u, b->seek) > SHARE_RANGE &&
            !onOtherOrder(w, u, *b) && (b->busy || b->wait <= 0.0f)) {
            if (b->busy) stopAutoStep(w, u, *b);
            const Transform* gt = w.get<Transform>(b->seek);
            const Transform* ut = w.get<Transform>(u);
            // Stop just short of the giver, on the near side, so the two don't push each other about
            const float dx = gt->x - ut->x, dz = gt->z - ut->z, d = std::max(1e-3f, std::hypot(dx, dz));
            const float stop = std::min(SHARE_WALK_STOP, d);
            leaveSquad(w, u);
            w.remove<IdleSpot>(u);
            walkSolo(w, t, u, Vec2{ gt->x - dx / d * stop, gt->z - dz / d * stop });
            b->seeking = true;
            continue;
        }
        if (!any) {
            // No automated behavior: only the wait before a walk to a sharer is kept
            b->wait = onOtherOrder(w, u, *b) ? resume : std::max(0.0f, b->wait - dt);
            continue;
        }
        const PropOrder* po = w.get<PropOrder>(u);
        const SoloMove* walk = w.get<SoloMove>(u);
        const bool walkingStep = b->busy && isWalkBehavior(b->step.kind) && walk && walk->next < walk->waypoints.size();
        const bool going = (po && po->automatic) || walkingStep;
        const Working* wk = w.get<Working>(u);
        const bool working = b->busy && wk && !wk->unlocking && wk->prop == b->stepProp;

        // Rescue: on its way to pick the unit up, then carrying it to a bed or bedroll; done once it is put in one.
        // An order of yours takes over.
        if (b->busy && b->step.kind == BehaviorKind::Rescue) {
            const PropOrder* rpo = w.get<PropOrder>(u);
            const bool yoursNow = w.has<ActiveSquadMember>(u) || (rpo && !rpo->automatic) ||
                                  w.has<AttackOrder>(u) || w.has<FollowOrder>(u) || w.has<DefendOrder>(u) ||
                                  w.has<PickupOrder>(u) || w.has<PickupChain>(u) || w.has<LootOrder>(u) || w.has<TalkOrder>(u);
            if (yoursNow) {
                b->busy = false;
                b->wait = resume;
                continue;
            }
            if (w.has<CarryOrder>(u)) continue;   // on its way to pick it up
            if (w.has<Carrying>(u)) {
                if (rpo && rpo->automatic) continue;   // taking it to the bed
                // To the nearest free bed: a new one if the last could not be got into, else it is put down here
                const Entity bed = restTarget(w, u);
                if (bed != NULL_ENTITY && bed != b->stepProp) {
                    b->stepProp = bed;
                    autoGo(w, t, u, bed, PropAction::Place);
                    continue;
                }
                putDown(w, u);
            }
            b->busy = false;
            w.remove<SoloMove>(u);
            if (BehaviorRule* r = stepRule(*b)) r->cooldown = BEHAVIOR_RETRY_SECONDS;
            continue;
        }
        // Resting: it stays in the bed until fully rested, then gets up and the automation goes on
        if (b->busy && b->step.kind == BehaviorKind::Rest) {
            const Occupying* oc = w.get<Occupying>(u);
            if (oc && oc->prop == b->stepProp) {
                if (n && n->rested < n->maxRested) continue;
                w.remove<Occupying>(u);
                b->busy = false;
                if (BehaviorRule* r = stepRule(*b)) r->cooldown = BEHAVIOR_RETRY_SECONDS;
                continue;
            }
        }
        if (b->busy) {
            if (going) continue;   // on its way
            if (working) {
                // A harvest goes on until the unit has a load for its haul, or no room
                if (!harvestLoaded(w, claims, u, *b, propInfo((PropKind)b->step.propKind).item)) continue;
                w.remove<Working>(u);
                b->busy = false;
            } else if (onOtherOrder(w, u, *b)) {   // you gave it something else to do
                b->busy = false;
                b->wait = resume;
                continue;
            } else {
                // The step ended by itself: the haul was put in, the walk got there, or the harvest did not start (the
                // prop had all its workers, or the way was blocked). A harvest that did not start waits before it is
                // tried again; a Patrol or a Roam pauses where it got to before its next walk.
                b->busy = false;
                if (BehaviorRule* r = stepRule(*b)) {
                    if (b->step.kind == BehaviorKind::AutoHarvest) r->cooldown = BEHAVIOR_RETRY_SECONDS;
                    if (b->step.kind == BehaviorKind::Patrol || b->step.kind == BehaviorKind::PatrolSpawn) {
                        r->cooldown = PATROL_PAUSE_MIN + rng.unit() * (PATROL_PAUSE_MAX - PATROL_PAUSE_MIN);
                    }
                    if (b->step.kind == BehaviorKind::Roam) r->cooldown = ROAM_PAUSE_MIN + rng.unit() * (ROAM_PAUSE_MAX - ROAM_PAUSE_MIN);
                }
                w.remove<SoloMove>(u);
            }
        } else {
            if (onOtherOrder(w, u, *b) || going) {
                // Busy with something of yours: wait until it has been idle a while
                b->wait = resume;
                continue;
            }
            if (b->wait > 0.0f) {
                b->wait -= dt;
                continue;
            }
        }

        // The next step: the first automated behavior, from the top, that is on and can be done now. One that can't
        // waits BEHAVIOR_RETRY_SECONDS before it is tried again.
        for (BehaviorRule& r : b->rules) {
            if (!isAutoStep(r.kind) || !r.on || r.cooldown > 0.0f) continue;
            if (startStep(w, t, claims, u, *b, r, rng)) break;
            r.cooldown = BEHAVIOR_RETRY_SECONDS;
        }
    }
}

