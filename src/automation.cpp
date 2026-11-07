#include "systems_internal.h"

using namespace systems_detail;

namespace {
int carriedCount(World& w, ObjectId u, int item) {
    const Inventory* inv = w.get<Inventory>(u);
    return inv ? carriedCount(*inv, item) : 0;
}

bool inOwnClaim(World& w, const std::vector<Claim>& claims, ObjectId prop, int faction) {
    const Transform* t = w.get<Transform>(prop);
    return t && claimAt(claims, t->x, t->z, faction) != nullptr;
}

bool fullFor(World& w, ObjectId u, int item) {
    const Inventory* inv = w.get<Inventory>(u);
    if (!inv || slotsRoomFor(inv->slots.data(), INVENTORY_SLOTS, item) <= 0) return true;
    const ItemDef* def = findItem(item);
    return encumbranceFor(carriedWeightOf(w, u) + (def ? def->weight : 0.0f), statsOf(w, u)) >= ENCUMBERED_SHARE;
}

BehaviorWait haulWait(World& w, const std::vector<Claim>& claims, ObjectId u, const BehaviorRule& r, int item = 0) {
    const Prop* p = w.get<Prop>(r.prop);
    const Storage* st = w.get<Storage>(r.prop);
    const Inventory* inv = w.get<Inventory>(u);
    if (!p || !st || !inv) return BehaviorWait::PropGone;
    if (!inOwnClaim(w, claims, r.prop, factionOf(w, u))) return BehaviorWait::OutsideClaim;
    if (lockShutFor(w, claims, r.prop, u)) return BehaviorWait::Locked;
    bool carries = false;
    for (const ItemStack& s : inv->slots) {
        if (s.item <= 0 || s.count <= 0 || (item != 0 && s.item != item) || !propAcceptsItem(p->kind, s.item)) continue;
        carries = true;
        if (slotsRoomFor(st->slots.data(), STORAGE_SLOTS, s.item) > 0) return BehaviorWait::None;
    }
    return carries ? BehaviorWait::PropFull : BehaviorWait::NothingToHaul;
}
bool haulable(World& w, const std::vector<Claim>& claims, ObjectId u, const BehaviorRule& r, int item = 0) {
    return haulWait(w, claims, u, r, item) == BehaviorWait::None;
}

bool canHaulItem(World& w, const std::vector<Claim>& claims, ObjectId u, const Behaviors& b, int item) {
    for (const BehaviorRule& r : b.rules) {
        if (r.on && r.kind == BehaviorKind::AutoHaul && haulable(w, claims, u, r, item)) return true;
    }
    return false;
}

bool harvestLoaded(World& w, const std::vector<Claim>& claims, ObjectId u, const Behaviors& b, int item) {
    if (fullFor(w, u, item)) return true;
    return carriedCount(w, u, item) >= AUTO_LOAD && canHaulItem(w, claims, u, b, item);
}

ObjectId harvestTarget(World& w, const std::vector<Claim>& claims, ObjectId u, int ruleKind) {
    const Transform* ut = w.get<Transform>(u);
    if (!ut || ruleKind < 0 || ruleKind >= propKindCount()) return NO_OBJECT;
    const PropKind kind = (PropKind)ruleKind;
    if (!yieldsItems(kind)) return NO_OBJECT;
    const int faction = factionOf(w, u);
    const std::vector<ObjectId> worked = workedProps(w);
    return nearestProp(w, *ut, [&](ObjectId p) {
        return w.get<Prop>(p)->kind == kind && inOwnClaim(w, claims, p, faction) && !lockShutFor(w, claims, p, u) &&
               countIn(worked, p) < propInfo(kind).workers;
    });
}

bool onOtherOrder(World& w, ObjectId u, const Behaviors& b) {
    if (const PropOrder* po = w.get<PropOrder>(u); po && !po->automatic) return true;
    if (const Working* wk = w.get<Working>(u); wk && (wk->unlocking || !b.busy || wk->prop != b.stepProp)) return true;
    if (w.has<Occupying>(u) || w.has<CarryOrder>(u) || w.has<Carrying>(u) || hasPlayerOrder(w, u)) return true;
    if (const SoloMove* so = w.get<SoloMove>(u); so && so->next < so->waypoints.size() && !w.has<PropOrder>(u) && !b.seeking &&
        !(b.busy && isWalkBehavior(b.step.kind))) {
        return true;
    }
    if (const ActiveSquadMember* sm = w.get<ActiveSquadMember>(u)) {
        if (const ActiveSquad* sq = w.get<ActiveSquad>(sm->squad); sq && !sq->arrived) return true;
    }
    return false;
}

void autoGo(World& w, Terrain& t, ObjectId u, ObjectId prop, PropAction action) {
    const Transform* pt = w.get<Transform>(prop);
    if (!pt) return;
    autoWalk(w, t, u, Vec2{ pt->x, pt->z });
    w.add(u, PropOrder{ prop, action, true });
}

BehaviorRule* stepRule(Behaviors& b) {
    for (BehaviorRule& r : b.rules) {
        if (sameBehavior(r, b.step)) return &r;
    }
    return nullptr;
}

BehaviorWait patrolSpot(World& w, const Terrain& t, const std::vector<Claim>& claims, ObjectId u, Rng& rng, Vec2& to) {
    const Transform* ut = w.get<Transform>(u);
    const Claim* c = ut ? claimAt(claims, ut->x, ut->z, factionOf(w, u)) : nullptr;
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

BehaviorWait roamSpot(World& w, const Terrain& t, const std::vector<Claim>& claims, ObjectId u, Rng& rng, Vec2& to) {
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

ObjectId restTarget(World& w, const std::vector<Claim>& claims, ObjectId u) {
    const Transform* ut = w.get<Transform>(u);
    if (!ut) return NO_OBJECT;
    std::vector<ObjectId> wanted;
    for (ObjectId o : w.view<PropOrder>()) {
        if (o != u) wanted.push_back(w.get<PropOrder>(o)->prop);
    }
    std::sort(wanted.begin(), wanted.end());
    const std::vector<ObjectId> occupied = occupiedProps(w);
    const Occupying* lyingIn = w.get<Occupying>(u);
    const int faction = factionOf(w, u);
    ObjectId best = NO_OBJECT;
    float bestD2 = 0.0f;
    bool bestOwned = false;
    for (ObjectId p : w.view<Prop, Transform>()) {
        const PropKind kind = w.get<Prop>(p)->kind;
        if (restedGainPerHour(kind) <= 0.0) continue;
        const bool ownBed = lyingIn && lyingIn->prop == p;
        if ((countIn(occupied, p) > 0 && !ownBed) || lockShutFor(w, claims, p, u)) continue;
        if (std::binary_search(wanted.begin(), wanted.end(), p)) continue;
        const Transform* pt = w.get<Transform>(p);
        const float d2 = (pt->x - ut->x) * (pt->x - ut->x) + (pt->z - ut->z) * (pt->z - ut->z);
        if (d2 > REST_SEARCH_RANGE * REST_SEARCH_RANGE) continue;
        const Owner* own = w.get<Owner>(p);
        const bool owned = own && own->faction == faction;
        if (best == NO_OBJECT || (owned && !bestOwned) || (owned == bestOwned && d2 < bestD2)) {
            best = p;
            bestD2 = d2;
            bestOwned = owned;
        }
    }
    return best;
}

ObjectId rescueTarget(World& w, ObjectId u) {
    const Transform* ut = w.get<Transform>(u);
    if (!ut) return NO_OBJECT;
    const int mine = factionOf(w, u);
    std::vector<ObjectId> fetched;
    for (ObjectId o : w.view<CarryOrder>()) {
        if (o != u) fetched.push_back(w.get<CarryOrder>(o)->target);
    }
    ObjectId best = NO_OBJECT;
    float bestD2 = RESCUE_RANGE * RESCUE_RANGE;
    for (ObjectId v : w.view<Unconscious, Transform>()) {
        if (v == u || w.has<Corpse>(v) || w.has<Carried>(v) || w.has<Mob>(v) || factionOf(w, v) != mine) continue;
        const ObjectId bed = occupiedProp(w, v);
        if (bed != NO_OBJECT && w.has<Prop>(bed) && restedGainPerHour(w.get<Prop>(bed)->kind) > 0.0) continue;
        if (std::find(fetched.begin(), fetched.end(), v) != fetched.end()) continue;
        const Transform* vt = w.get<Transform>(v);
        const float d2 = distSq(*vt, *ut);
        if (d2 <= bestD2) {
            best = v;
            bestD2 = d2;
        }
    }
    return best;
}

BehaviorWait spawnSpot(World& w, const Terrain& t, ObjectId u, Rng& rng, Vec2& to) {
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

bool startStep(World& w, Terrain& t, const std::vector<Claim>& claims, ObjectId u, Behaviors& b, BehaviorRule& r, Rng& rng) {
    if (r.kind == BehaviorKind::Rescue) {
        r.why = BehaviorWait::None;
        if (w.has<Mob>(u) || w.has<Carrying>(u)) return false;
        const ObjectId target = rescueTarget(w, u);
        if (target == NO_OBJECT) return false;
        if (restTarget(w, claims, u) == NO_OBJECT) {
            r.why = BehaviorWait::NoBed;
            return false;
        }
        const Transform* tt = w.get<Transform>(target);
        b.busy = true;
        b.step = r;
        b.stepProp = NO_OBJECT;
        autoWalk(w, t, u, Vec2{ tt->x, tt->z });
        w.add(u, CarryOrder{ target });
        return true;
    }
    if (r.kind == BehaviorKind::Rest) {
        const Needs* n = w.get<Needs>(u);
        r.why = BehaviorWait::None;
        if (!n || w.has<Mob>(u) || percentOf(n->rested, n->maxRested) >= r.percent) return false;
        if (w.has<OccupyBan>(u)) return false;
        const ObjectId bed = restTarget(w, claims, u);
        if (bed == NO_OBJECT) {
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
        b.stepProp = NO_OBJECT;
        autoWalk(w, t, u, to);
        return true;
    }
    ObjectId prop = NO_OBJECT;
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
        else if ((prop = harvestTarget(w, claims, u, r.propKind)) == NO_OBJECT) r.why = BehaviorWait::NoProp;
        if (r.why != BehaviorWait::None) return false;
    }
    r.why = BehaviorWait::None;
    b.busy = true;
    b.step = r;
    b.stepProp = prop;
    autoGo(w, t, u, prop, action);
    return true;
}
}

namespace {
bool tryStep(World& w, Terrain& t, const std::vector<Claim>& claims, ObjectId u, Behaviors& b, BehaviorRule& r, Rng& rng) {
    if (!isAutoStep(r.kind) || !r.on || r.cooldown > 0.0f) return false;
    if (startStep(w, t, claims, u, b, r, rng)) return true;
    r.cooldown = r.why == BehaviorWait::NoBed ? NO_BED_RETRY_SECONDS : BEHAVIOR_RETRY_SECONDS;
    return false;
}

BehaviorRule* rescueRule(Behaviors& b) {
    for (BehaviorRule& r : b.rules) {
        if (r.kind == BehaviorKind::Rescue) return &r;
    }
    return nullptr;
}
}

void systems_detail::autoWalk(World& w, Terrain& t, ObjectId u, Vec2 to) {
    clearOrdersButFight(w, u);
    w.remove<IdleSpot>(u);
    walkSolo(w, t, u, to);
}

void systems_detail::stopAutoStep(World& w, ObjectId u, Behaviors& b) {
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

bool systems_detail::openTile(const Terrain& t, float x, float z) {
    return x >= 0.0f && z >= 0.0f && x < t.size() && z < t.size() && t.walkable((int)std::floor(x), (int)std::floor(z));
}

void systems_detail::stepOffProp(World& w, Terrain& t, ObjectId u, ObjectId prop) {
    Transform* ut = w.get<Transform>(u);
    const Transform* pt = w.get<Transform>(prop);
    const Prop* pp = w.get<Prop>(prop);
    if (!ut || !pt || !pp) return;
    const Footprint& pf = propInfo(pp->kind).foot;
    const int cx = (int)std::floor(pt->x), cz = (int)std::floor(pt->z);
    auto onOtherProp = [&](float x, float z) {
        for (ObjectId o : w.view<Prop, Transform>()) {
            if (o == prop) continue;
            const PropInfo& oi = propInfo(w.get<Prop>(o)->kind);
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
            if (insideFootprint(pf, x - pt->x, z - pt->z, pp->yaw)) continue;
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

void leaveOpenLocksSystem(World& w, Terrain& t) {
    for (ObjectId u : w.view<Occupying>()) {
        if (w.has<Commandable>(u) || outCold(w, u) || w.has<Carried>(u)) continue;
        const ObjectId prop = w.get<Occupying>(u)->prop;
        const Lock* lock = w.get<Lock>(prop);
        if (!lock || lock->locked) continue;
        w.remove<Occupying>(u);
        stepOffProp(w, t, u, prop);
        queueLog(w, langf("MISC_BREAKS_FREE", { nameOf(w, u) }), LogKind::Other, npcOnly(w, u));
    }
}

void automationSystem(World& w, Terrain& t, float dt, Rng& rng) {
    const std::vector<Claim> claims = claimsOf(w);
    for (ObjectId u : w.view<Behaviors, Transform>()) {
        Behaviors* b = w.get<Behaviors>(u);
        bool any = false;
        for (BehaviorRule& r : b->rules) {
            if (!isAutoStep(r.kind)) continue;
            r.cooldown = std::max(0.0f, r.cooldown - dt);
            if (!r.on) r.why = BehaviorWait::None;
            any = any || r.on;
        }
        if (b->busy) {
            const BehaviorRule* current = stepRule(*b);
            if (!current || !current->on) stopAutoStep(w, u, *b);
        }
        if (!any && b->seek == NO_OBJECT && !b->seeking) continue;
        const bool yours = w.has<Commandable>(u);
        const float resume = yours ? AUTO_RESUME_SECONDS : 0.0f;
        const Needs* n = w.get<Needs>(u);
        if (outOfAction(w, u) || (n && n->hp <= 0)) {
            b->busy = false;
            b->seeking = false;
            b->recovered = 0.0f;
            continue;
        }
        {
            const ObjectId bed = occupiedProp(w, u);
            const Prop* bp = bed != NO_OBJECT ? w.get<Prop>(bed) : nullptr;
            bool leaveOn = false;
            for (const BehaviorRule& r : b->rules) leaveOn = leaveOn || (r.on && r.kind == BehaviorKind::LeaveBed);
            if (leaveOn && bp && restedGainPerHour(bp->kind) > 0.0 && n && n->hp >= n->maxHp && n->rested >= n->maxRested) {
                b->recovered += dt;
                if (b->recovered >= LEAVE_BED_SECONDS) {
                    w.remove<Occupying>(u);
                    stepOffProp(w, t, u, bed);
                    b->recovered = 0.0f;
                    if (b->busy && b->step.kind == BehaviorKind::Rest) b->busy = false;
                }
            } else {
                b->recovered = 0.0f;
            }
        }
        const Combat* c = w.get<Combat>(u);
        if ((c && c->target != NO_OBJECT) || w.has<AttackOrder>(u)) {
            if (b->busy) stopAutoStep(w, u, *b);
            if (b->seeking) w.remove<SoloMove>(u);
            b->seeking = false;
            b->wait = resume;
            continue;
        }
        if (b->seeking) {
            const SoloMove* so = w.get<SoloMove>(u);
            const bool walking = so && so->next < so->waypoints.size();
            if (b->seek == NO_OBJECT || !walking || flatGap(w, u, b->seek) <= SHARE_RANGE) {
                if (walking) w.remove<SoloMove>(u);
                b->seeking = false;
            } else if (onOtherOrder(w, u, *b)) {
                b->seeking = false;
                b->wait = resume;
            }
            continue;
        }
        if (b->seek != NO_OBJECT && w.has<Transform>(b->seek) && flatGap(w, u, b->seek) > SHARE_RANGE &&
            !onOtherOrder(w, u, *b) && (b->busy || b->wait <= 0.0f)) {
            if (b->busy) stopAutoStep(w, u, *b);
            const Transform* gt = w.get<Transform>(b->seek);
            const Transform* ut = w.get<Transform>(u);
            const float dx = gt->x - ut->x, dz = gt->z - ut->z, d = std::max(1e-3f, std::hypot(dx, dz));
            const float stop = std::min(SHARE_WALK_STOP, d);
            leaveSquad(w, u);
            w.remove<IdleSpot>(u);
            walkSolo(w, t, u, Vec2{ gt->x - dx / d * stop, gt->z - dz / d * stop });
            b->seeking = true;
            continue;
        }
        if (!any) {
            b->wait = onOtherOrder(w, u, *b) ? resume : std::max(0.0f, b->wait - dt);
            continue;
        }
        const PropOrder* po = w.get<PropOrder>(u);
        const SoloMove* walk = w.get<SoloMove>(u);
        const bool walkingStep = b->busy && isWalkBehavior(b->step.kind) && walk && walk->next < walk->waypoints.size();
        const bool going = (po && po->automatic) || walkingStep;
        const Working* wk = w.get<Working>(u);
        const bool working = b->busy && wk && !wk->unlocking && wk->prop == b->stepProp;

        if (b->busy && b->step.kind == BehaviorKind::Rescue) {
            const PropOrder* rpo = w.get<PropOrder>(u);
            const bool yoursNow = w.has<ActiveSquadMember>(u) || (rpo && !rpo->automatic) || hasPlayerOrder(w, u);
            if (yoursNow) {
                b->busy = false;
                b->wait = resume;
                continue;
            }
            if (w.has<CarryOrder>(u)) continue;
            bool noBed = false;
            if (w.has<Carrying>(u)) {
                if (rpo && rpo->automatic) continue;
                const ObjectId bed = restTarget(w, claims, u);
                if (bed != NO_OBJECT && bed != b->stepProp) {
                    b->stepProp = bed;
                    autoGo(w, t, u, bed, PropAction::Place);
                    continue;
                }
                noBed = bed == NO_OBJECT;
                putDown(w, u);
            }
            b->busy = false;
            w.remove<SoloMove>(u);
            if (BehaviorRule* r = stepRule(*b)) {
                r->cooldown = noBed ? NO_BED_RETRY_SECONDS : BEHAVIOR_RETRY_SECONDS;
                if (noBed) r->why = BehaviorWait::NoBed;
            }
            continue;
        }
        // A unit out cold that it can rescue comes before lying down
        if (going && b->busy && b->step.kind == BehaviorKind::Rest) {
            BehaviorRule* rescue = rescueRule(*b);
            if (rescue && tryStep(w, t, claims, u, *b, *rescue, rng)) continue;
        }
        if (b->busy && b->step.kind == BehaviorKind::Rest) {
            const Occupying* oc = w.get<Occupying>(u);
            if (oc && oc->prop == b->stepProp) {
                const ObjectId bed = oc->prop;
                BehaviorRule* rescue = rescueRule(*b);
                if (rescue && tryStep(w, t, claims, u, *b, *rescue, rng)) {
                    stepOffProp(w, t, u, bed);
                    const CarryOrder* fetch = w.get<CarryOrder>(u);
                    const Transform* to = fetch ? w.get<Transform>(fetch->target) : nullptr;
                    if (to) planSolo(w, t, u, Vec2{ to->x, to->z });
                    continue;
                }
                if (n && n->rested < n->maxRested) continue;
                w.remove<Occupying>(u);
                b->busy = false;
                if (BehaviorRule* r = stepRule(*b)) r->cooldown = BEHAVIOR_RETRY_SECONDS;
                continue;
            }
        }
        if (b->busy) {
            if (going) continue;
            if (working) {
                if (!harvestLoaded(w, claims, u, *b, propInfo((PropKind)b->step.propKind).item)) continue;
                w.remove<Working>(u);
                b->busy = false;
            } else if (onOtherOrder(w, u, *b)) {
                b->busy = false;
                b->wait = resume;
                continue;
            } else {
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
                b->wait = resume;
                continue;
            }
            if (b->wait > 0.0f) {
                b->wait -= dt;
                continue;
            }
        }

        // Rescue always comes before Rest, wherever the list has them
        for (BehaviorRule& r : b->rules) {
            if (r.kind == BehaviorKind::Rest) {
                BehaviorRule* rescue = rescueRule(*b);
                if (rescue && tryStep(w, t, claims, u, *b, *rescue, rng)) break;
            }
            if (tryStep(w, t, claims, u, *b, r, rng)) break;
        }
    }
}

