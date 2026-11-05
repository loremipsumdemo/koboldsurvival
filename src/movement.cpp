// Orders that move units (move, pickup, attack, talk, follow, defend), picking up ground items, active squads,
// steering and the defend system.
#include "systems_internal.h"

using namespace systems_detail;

void issueMoveOrder(World& w, Terrain& t, const std::vector<Entity>& given, Vec2 clickPoint) {
    // A unit locked in a cage stays where it is
    std::vector<Entity> units;
    for (Entity u : given) {
        if (!cannotTakeOrder(w, u)) units.push_back(u);
    }
    if (units.empty()) return;

    // A single unit walks its own route, with no formation. A move order replaces any pickup.
    if (units.size() == 1) {
        Entity u = units[0];
        clearOrders(w, u);
        walkSolo(w, t, u, clickPoint);
        return;
    }

    // The formation center starts at the average position of the selected units
    float cx = 0.0f, cz = 0.0f;
    for (Entity u : units) {
        Transform* tr = w.get<Transform>(u);
        cx += tr->x;
        cz += tr->z;
    }
    cx /= units.size();
    cz /= units.size();

    ActiveSquad s;
    s.cx = cx;
    s.cz = cz;
    s.size = (int)units.size();
    Entity sq = w.create();
    w.add(sq, s);

    // Each selected unit joins the new active squad. Overwriting ActiveSquadMember drops any old one.
    for (size_t i = 0; i < units.size(); ++i) {
        clearOrders(w, units[i]);
        w.add(units[i], ActiveSquadMember{ sq, (int)i });
    }

    planActiveSquad(w, t, sq, clickPoint);
}

void issuePickupOrder(World& w, Terrain& t, const std::vector<Entity>& units, Entity item) {
    const Transform* it = w.get<Transform>(item);
    if (!it || !w.has<GroundItem>(item)) return;
    const Vec2 target{ it->x, it->z };

    for (Entity u : units) {
        if (!w.has<Inventory>(u) || !w.get<Transform>(u)) continue;   // only units that carry items
        if (cannotTakeOrder(w, u) || !canHoldItem(w, u, w.get<GroundItem>(item)->item)) continue;
        clearOrders(w, u);
        walkSolo(w, t, u, target);
        w.add(u, PickupOrder{ item });
    }
}

void issuePickupChain(World& w, Terrain& t, const std::vector<Entity>& units, const std::vector<Entity>& items) {
    PickupJob job;
    for (Entity it : items) {
        if (w.has<GroundItem>(it) && w.get<Transform>(it)) job.items.push_back(it);
    }
    if (job.items.empty()) return;

    std::vector<Entity> crew;
    for (Entity u : units) {
        if (!w.has<Inventory>(u) || !w.get<Transform>(u) || cannotTakeOrder(w, u)) continue;   // only units that carry items
        clearOrders(w, u);
        crew.push_back(u);
    }
    if (crew.empty()) return;

    const Entity jobE = w.create();
    w.add(jobE, std::move(job));
    for (Entity u : crew) w.add(u, PickupChain{ jobE, {}, 0 });
    pickupChainSystem(w, t);   // hand out the first items at once
}

void pickupChainSystem(World& w, Terrain& t) {
    // The job's listed items that are still on the ground, and the units on each job
    std::map<Entity, std::vector<Entity>> crews;
    for (Entity u : w.view<PickupChain>()) {
        const Entity jobE = w.get<PickupChain>(u)->job;
        if (!w.has<PickupJob>(jobE) || !w.get<Transform>(u) || !w.has<Inventory>(u)) {
            cancelPickup(w, u);
            continue;
        }
        crews[jobE].push_back(u);
    }
    for (Entity jobE : w.view<PickupJob>()) {
        PickupJob* job = w.get<PickupJob>(jobE);
        // An item someone took (or that is gone some other way) drops off the list
        job->items.erase(std::remove_if(job->items.begin(), job->items.end(),
                                        [&](Entity it) { return !w.has<GroundItem>(it) || !w.get<Transform>(it); }),
                         job->items.end());
        auto found = crews.find(jobE);
        if (found == crews.end() || job->items.empty()) {
            if (found != crews.end()) {
                for (Entity u : found->second) cancelPickup(w, u);
            }
            w.destroy(jobE);
            continue;
        }
        std::vector<Entity>& crew = found->second;

        // Items already being walked to by a unit of this job are left to it
        std::vector<Entity> claimed;
        std::vector<Entity> idle;
        for (Entity u : crew) {
            if (const PickupOrder* po = w.get<PickupOrder>(u)) claimed.push_back(po->item);
            else idle.push_back(u);
        }

        // Idle units take free items, nearest pair first, so the closest unit goes for each and two units never
        // walk to the same item
        auto freeFor = [&](Entity u, Entity it) {
            if (std::find(claimed.begin(), claimed.end(), it) != claimed.end()) return false;
            const PickupChain* ch = w.get<PickupChain>(u);
            if (std::find(ch->skipped.begin(), ch->skipped.end(), it) != ch->skipped.end()) return false;
            return canHoldItem(w, u, w.get<GroundItem>(it)->item);
        };
        while (!idle.empty()) {
            float bestD2 = -1.0f;
            size_t bu = 0;
            Entity bi = NULL_ENTITY;
            for (size_t i = 0; i < idle.size(); ++i) {
                const Transform* ut = w.get<Transform>(idle[i]);
                for (Entity it : job->items) {
                    if (!freeFor(idle[i], it)) continue;
                    const Transform* itr = w.get<Transform>(it);
                    const float dx = itr->x - ut->x, dz = itr->z - ut->z;
                    const float d2 = dx * dx + dz * dz;
                    if (bestD2 < 0.0f || d2 < bestD2) {
                        bestD2 = d2;
                        bu = i;
                        bi = it;
                    }
                }
            }
            if (bi == NULL_ENTITY) break;   // every item left is taken by another unit, or out of these units' reach
            const Entity u = idle[bu];
            const Transform* itr = w.get<Transform>(bi);
            walkSolo(w, t, u, Vec2{ itr->x, itr->z });
            w.add(u, PickupOrder{ bi });
            w.get<PickupChain>(u)->replans = 0;
            claimed.push_back(bi);
            idle.erase(idle.begin() + bu);
        }

        // A unit left idle waits in case another unit's item frees up, but once nothing on the list is left that it
        // could take, its chain ends
        for (Entity u : idle) {
            const PickupChain* ch = w.get<PickupChain>(u);
            bool any = false;
            for (Entity it : job->items) {
                if (std::find(ch->skipped.begin(), ch->skipped.end(), it) == ch->skipped.end() &&
                    canHoldItem(w, u, w.get<GroundItem>(it)->item)) {
                    any = true;
                    break;
                }
            }
            if (!any) cancelPickup(w, u);
        }
    }
}

void steerUnits(World& w, const std::vector<Entity>& units, Vec2 target) {
    for (Entity u : units) {
        if (!w.has<Commandable>(u) || !w.get<Transform>(u) || cannotTakeOrder(w, u)) continue;
        clearOrders(w, u);
        w.add(u, SoloMove{});
        SoloMove* so = w.get<SoloMove>(u);
        so->waypoints.assign(1, target);   // one straight waypoint: no path is planned
        so->next = 0;
    }
}

void issueAttackOrder(World& w, const std::vector<Entity>& units, Entity target) {
    if (!w.has<Unit>(target) || !w.get<Transform>(target)) return;
    for (Entity u : units) {
        if (!w.has<Commandable>(u) || !w.get<Transform>(u)) continue;   // only your own units fight on order
        if (cannotTakeOrder(w, u)) continue;
        // A fight replaces any move, pickup or formation the unit was on
        clearOrders(w, u);
        w.add(u, AttackOrder{ target });
        ensureCombat(w, u);
        w.get<Combat>(u)->target = target;
    }
}

// Right-click on an NPC: each selected unit walks up to it. The first in talk range starts the talk.
void issueTalkOrder(World& w, const std::vector<Entity>& units, Entity npc, bool trade, bool shop) {
    if (!w.has<Unit>(npc) || !w.get<Transform>(npc) || w.has<Mob>(npc)) return;   // a mob has nothing to say
    for (Entity u : units) {
        if (!w.has<Commandable>(u) || !w.get<Transform>(u) || cannotTakeOrder(w, u)) continue;
        if (w.has<Mob>(u)) continue;   // nor does one of yours talk or trade for you (mobs.h)
        clearOrders(w, u);
        w.add(u, TalkOrder{ npc, trade, shop });
    }
}

void clearTalkOrders(World& w) {
    for (Entity u : w.view<TalkOrder>()) w.remove<TalkOrder>(u);
}

int pickupSystem(World& w, Terrain& t) {
    int taken = 0;
    // view() returns a copy, so ending a pickup inside the loop is safe
    for (Entity u : w.view<PickupOrder, Transform, Inventory>()) {
        const Entity item = w.get<PickupOrder>(u)->item;
        const Transform* ut = w.get<Transform>(u);
        const Transform* it = w.get<Transform>(item);
        GroundItem* gi = w.get<GroundItem>(item);
        if (!it || !gi) {                       // someone else took it, or it was removed
            endPickup(w, u);
            continue;
        }

        const float dx = it->x - ut->x, dz = it->z - ut->z;
        if (dx * dx + dz * dz <= PICKUP_REACH * PICKUP_REACH) {
            // Take what fits. Whatever is left stays on the ground. If nothing fits the order ends,
            // and the unit stays where it is. A mob takes nothing that is not its own (mobs.h).
            if (!canHoldItem(w, u, gi->item)) {
                if (PickupChain* ch = w.get<PickupChain>(u)) ch->skipped.push_back(item);   // others may still take it
                endPickup(w, u);
                continue;
            }
            const int count = gi->count;
            const std::string itemName = itemNameOf(gi->item);
            const int left = addToInventory(*w.get<Inventory>(u), gi->item, gi->count);
            if (left < count) {
                queueLog(langf("MISC_PICKS_UP", { nameOf(w, u), std::to_string(count - left), itemName }), LogKind::Pickup, npcOnly(w, u));
                queueUnitLabel(u, itemGainLabel(count - left, itemName));   // "+Iron Ore" over its head
                queueSoundFrom(w, Sfx::Pickup, u);
            }
            if (left == 0) w.destroy(item);
            else gi->count = left;
            if (w.has<Commandable>(u) && left < count) ++taken;
            endPickup(w, u);
            continue;
        }

        // Stopped short with no route left (the item can't be reached), so give up. On a pickup chain the unit may only
        // have been pushed off its way, so it plans a new route a few times before it leaves the item to the others.
        const SoloMove* so = w.get<SoloMove>(u);
        if (!so || so->next >= so->waypoints.size()) {
            PickupChain* ch = w.get<PickupChain>(u);
            if (ch && ch->replans < CHAIN_REPLANS) {
                ++ch->replans;
                walkSolo(w, t, u, Vec2{ it->x, it->z });
                if (!w.get<SoloMove>(u)->waypoints.empty()) continue;
            }
            if (ch) ch->skipped.push_back(item);
            endPickup(w, u);
        }
    }
    pickupChainSystem(w, t);   // units on a chain that just took (or lost) their item go for the next one
    return taken;
}

void squadSystem(World& w, float dt, bool running) {
    for (Entity e : w.view<ActiveSquad>()) {
        ActiveSquad* sq = w.get<ActiveSquad>(e);
        if (sq->arrived) continue;

        // The active squad keeps to the pace of its slowest member that can still move. Members that can't
        // move at all (downed, or carrying too much) are left behind, and don't hold the active squad back.
        // Walking, the active squad is also held to its own pace. Running, each member runs at its own speed,
        // so the center goes at the slowest member's run pace.
        float slowest = -1.0f;
        for (Entity m : w.view<ActiveSquadMember, Unit>()) {
            if (w.get<ActiveSquadMember>(m)->squad != e) continue;
            const float s = walkSpeedOf(w, m, running);
            if (s > 0.0f && (slowest < 0.0f || s < slowest)) slowest = s;
        }
        const float pace = slowest < 0.0f ? 0.0f : running ? slowest : std::min(sq->speed, slowest);

        Vec2 wp = sq->waypoints[sq->next];
        float tx = wp.x, tz = wp.z;
        float dx = tx - sq->cx, dz = tz - sq->cz;
        float d = std::sqrt(dx * dx + dz * dz);
        float step = pace * dt;

        if (d > 1e-4f) {
            sq->headX = dx / d;
            sq->headZ = dz / d;
        }

        if (d <= step) {
            sq->cx = tx;
            sq->cz = tz;
            if (++sq->next >= sq->waypoints.size()) sq->arrived = true;
        } else {
            sq->cx += dx / d * step;
            sq->cz += dz / d * step;
        }
    }
}

namespace {

// The unit a unit is following or defending (and not off fighting an attacker), or NULL_ENTITY
Entity leadOf(World& w, Entity e) {
    if (const FollowOrder* f = w.get<FollowOrder>(e)) return f->target;
    if (const DefendOrder* d = w.get<DefendOrder>(e); d && !w.has<AttackOrder>(e)) return d->ward;
    return NULL_ENTITY;
}

// Whether a unit following or defending lead is close enough to stop. It is when it is within FOLLOW_DISTANCE of the
// lead, or when it touches another unit with the same lead that is closer to the lead than it is and lies toward the
// lead from it (within 60 degrees). That other unit must always be the closer of the two, so two followers can never
// hold each other in place: when the lead walks off, the nearest one goes after it, and the rest follow in turn.
// A crowd around one unit settles into a cluster instead of pressing in on it forever.
// Stopping behind another counts only within a radius that grows with the crowd: about what that many units need
// packed around the lead. Further out, a unit keeps on, and the idle units in its way step aside (steeringSystem),
// so a crowd that came in single file gathers round the lead instead of trailing behind it in a long line.
bool closeEnoughToLead(World& w, const std::vector<Entity>& units, Entity e, Entity lead) {
    const Transform* tr = w.get<Transform>(e);
    const Transform* lt = w.get<Transform>(lead);
    if (!tr || !lt) return true;
    const float gx = lt->x - tr->x, gz = lt->z - tr->z;
    const float myGap = std::sqrt(gx * gx + gz * gz);
    if (myGap <= FOLLOW_DISTANCE) return true;
    const Unit* mu = w.get<Unit>(e);
    int crowd = 0;
    for (Entity o : units) crowd += leadOf(w, o) == lead ? 1 : 0;
    const float spacing = 2.0f * mu->radius + UNIT_CLEARANCE;
    if (myGap > FOLLOW_DISTANCE + 0.75f * spacing * std::sqrt((float)crowd)) return false;
    for (Entity o : units) {
        if (o == e || o == lead || leadOf(w, o) != lead) continue;
        const Transform* ot = w.get<Transform>(o);
        const Unit* ou = w.get<Unit>(o);
        const float ox = ot->x - tr->x, oz = ot->z - tr->z;
        const float touching = mu->radius + ou->radius + UNIT_CLEARANCE + 0.3f;
        const float gap = std::sqrt(ox * ox + oz * oz);
        if (gap > touching || gap < 1e-4f) continue;
        const float theirGap = std::hypot(lt->x - ot->x, lt->z - ot->z);
        if (theirGap >= myGap) continue;                       // it must be closer to the lead
        if (ox * gx + oz * gz < 0.5f * gap * myGap) continue;   // and toward the lead from here, within 60 degrees
        return true;
    }
    return false;
}

} // namespace

namespace {

constexpr float unitWidth = 2.0f * 0.5f + UNIT_CLEARANCE;   // one unit's room: its tile, and the gap

using Vec2f = std::pair<float, float>;

std::int64_t cellKey(int cx, int cz) { return (std::int64_t)(((std::uint64_t)(std::uint32_t)cx << 32) | (std::uint32_t)cz); }

// What one frame of steering works out for each unit that moves, by its place in units
struct SteerFrame {
    std::vector<Entity> units;
    std::vector<Vec2f> vel;
    std::vector<float> speeds;
    // Each unit's goal this frame, whether it is going somewhere of its own (else it is idle, holding its spot), and
    // whether it is pinned in place (lying on a bed)
    std::vector<Vec2f> goal;
    std::vector<char> going, pinned;
    // For a unit settled while following or defending, and not in a fight, the unit it follows; for a worker settled
    // at its prop, the prop (else NULL_ENTITY). Units that share one hold still among themselves: see steerVelocity.
    std::vector<Entity> calmLead;
    std::vector<Entity> attacked;   // the units something is attacking this frame, sorted
    // Positions as they stand before anyone moves, and the units sorted by the one-tile cell they are in, so each
    // unit looks only at the units in its own and the eight cells around it: only those can overlap its square
    std::vector<Vec2f> pos;
    std::vector<std::pair<std::int64_t, std::uint32_t>> byCell;
    bool underAttack(Entity e) const { return std::binary_search(attacked.begin(), attacked.end(), e); }
};

// A unit's goal: where its orders take it, or the spot it is idling at. Positions are read here but not changed, so the
// order of units doesn't matter.
void steerGoal(World& w, Terrain& t, SteerFrame& s, size_t i, bool running) {
    const std::vector<Entity>& units = s.units;
    auto underAttack = [&](Entity e) { return s.underAttack(e); };
    Entity e = units[i];
    Transform* tr = w.get<Transform>(e);

    float tx = tr->x, tz = tr->z;   // default: stay put
    float leadGap = -1.0f;          // how far a following or defending unit is from the unit it follows
    // A unit on a bed or bedroll lies still on it: it does not move, and nothing pushes it off
    if (w.has<Occupying>(e)) {
        s.speeds[i] = 0.0f;
        s.pinned[i] = 1;
        s.goal[i] = { tr->x, tr->z };
        return;
    }
    if (const Working* g = w.get<Working>(e); g && !w.has<SoloMove>(e)) {
        // A worker stays where it is while it is in reach. One pushed out of reach walks back toward the prop.
        if (const Transform* pt = w.get<Transform>(g->prop)) {
            const float gx = pt->x - tr->x, gz = pt->z - tr->z;
            if (gx * gx + gz * gz > PROP_REACH * PROP_REACH) {
                tx = pt->x;
                tz = pt->z;
            } else if (!underAttack(e)) {
                s.calmLead[i] = g->prop;   // settled at its work, among the others working the same prop
            }
        }
    } else if (const TalkOrder* talk = w.get<TalkOrder>(e)) {
        // A talk: walk at the NPC until a unit is in talk range, which starts the talk
        if (const Transform* nt = w.get<Transform>(talk->npc)) {
            tx = nt->x;
            tz = nt->z;
        }
    } else if (ActiveSquadMember* sm = w.get<ActiveSquadMember>(e)) {
        if (ActiveSquad* sq = w.get<ActiveSquad>(sm->squad)) {
            // Slots are centered on the active squad's center, so the group's middle sits on
            // the click point: columns across the heading, rows behind it.
            int cols = std::min(SQUAD_COLUMNS, sq->size);
            int rows = (sq->size + cols - 1) / cols;
            float colOff = sm->slot % cols - (cols - 1) * 0.5f;
            float rowOff = sm->slot / cols - (rows - 1) * 0.5f;
            float spacing = slotSpacing(sq->size);

            float rightX = -sq->headZ, rightZ = sq->headX;
            float backX = -sq->headX, backZ = -sq->headZ;
            tx = sq->cx + (rightX * colOff + backX * rowOff) * spacing;
            tz = sq->cz + (rightZ * colOff + backZ * rowOff) * spacing;

            // Don't ask units to stand inside walls
            Vec2 st = t.snap({ tx, tz });
            tx = st.x;
            tz = st.z;
        }
    } else if (SoloMove* so = w.get<SoloMove>(e)) {
        // Move past any waypoints the unit has already reached
        while (so->next < so->waypoints.size()) {
            Vec2 wp = so->waypoints[so->next];
            float wx = wp.x - tr->x, wz = wp.z - tr->z;
            // Corners can be passed loosely; the final point is where the unit should stop
            bool last = so->next + 1 == so->waypoints.size();
            float reach = last ? 0.05f : 0.2f;
            if (wx * wx + wz * wz > reach * reach) break;
            ++so->next;
        }
        if (so->next < so->waypoints.size()) {
            tx = so->waypoints[so->next].x;
            tz = so->waypoints[so->next].z;
        }
    } else if (const AttackOrder* ao = w.get<AttackOrder>(e)) {
        // An ordered fight: walk at the red unit, and stop once it is close enough to swing
        if (const Transform* tt = w.get<Transform>(ao->target)) {
            const float gx = tt->x - tr->x, gz = tt->z - tr->z;
            if (std::sqrt(gx * gx + gz * gz) > reachOf(w, e) * CHASE_STOP_SHARE) {
                tx = tt->x;
                tz = tt->z;
            }
        }
    } else if (w.has<FollowOrder>(e) || w.has<DefendOrder>(e)) {
        // Following or defending (with no attacker to fight): walk after the unit, and stop once close enough
        const Entity lead = leadOf(w, e);
        if (const Transform* lt = w.get<Transform>(lead)) {
            leadGap = std::hypot(lt->x - tr->x, lt->z - tr->z);
            // Once settled, a unit that is not in a fight stays put, without checking again, until the unit it
            // follows has moved off by at least a whole unit's room. Small moves of the lead, or of the others
            // around it, don't set it shuffling.
            const IdleSpot* rest = w.get<IdleSpot>(e);
            const bool calm = !underAttack(e) && !underAttack(lead);
            const bool stay = calm && rest && rest->leadGap >= 0.0f && leadGap <= rest->leadGap + unitWidth;
            if (stay) {
                s.calmLead[i] = lead;
            } else if (!closeEnoughToLead(w, units, e, lead)) {
                tx = lt->x;
                tz = lt->z;
            } else if (calm) {
                s.calmLead[i] = lead;
            }
        } else {
            w.remove<FollowOrder>(e);   // the unit it followed is gone
        }
    } else if (!w.has<Commandable>(e)) {
        // A unit that is not yours walks at its target while it has one, and stops once it is close enough to hit
        if (const Combat* c = w.get<Combat>(e); c && c->target != NULL_ENTITY) {
            if (const Transform* tt = w.get<Transform>(c->target)) {
                const float gx = tt->x - tr->x, gz = tt->z - tr->z;
                if (std::sqrt(gx * gx + gz * gz) > reachOf(w, e) * CHASE_STOP_SHARE) {
                    tx = tt->x;
                    tz = tt->z;
                }
            }
        }
    }

    // With nowhere to go, a unit holds the spot where it came to rest. With somewhere to go, the spot is dropped,
    // and a new one is taken where it next stops.
    const bool idle = tx == tr->x && tz == tr->z;
    // Anything that sets a unit walking ends its playing dead, so it never walks lying down
    if (!idle && w.has<PlayingDead>(e)) {
        w.remove<PlayingDead>(e);
        queueLog(langf("MISC_GETS_UP", { nameOf(w, e) }), LogKind::Other, npcOnly(w, e));
    }
    if (idle) {
        IdleSpot* spot = w.get<IdleSpot>(e);
        if (!spot) spot = &w.add(e, IdleSpot{ tr->x, tr->z, leadGap });
        else if (spot->leadGap < 0.0f) spot->leadGap = leadGap;
        tx = spot->x;
        tz = spot->z;
    } else if (w.has<IdleSpot>(e)) {
        w.remove<IdleSpot>(e);
    }
    s.goal[i] = { tx, tz };
    s.going[i] = idle ? 0 : 1;
    // A downed unit has speed 0. A player unit's speed depends on its load, Athletics, stamina and pace.
    s.speeds[i] = walkSpeedOf(w, e, running);
}

// The units in the nine cells around a unit
void unitsNearby(const SteerFrame& s, const Transform* tr, size_t i, std::vector<std::uint32_t>& nearby) {
    // The units in the nine cells around this one, in the order of units[] (so the pushes add up as before)
    nearby.clear();
    const int cx = (int)std::floor(tr->x / UNIT_BOX), cz = (int)std::floor(tr->z / UNIT_BOX);
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dz = -1; dz <= 1; ++dz) {
            const std::int64_t key = cellKey(cx + dx, cz + dz);
            auto it = std::lower_bound(s.byCell.begin(), s.byCell.end(), std::make_pair(key, (std::uint32_t)0));
            for (; it != s.byCell.end() && it->first == key; ++it) {
                if (it->second != i) nearby.push_back(it->second);
            }
        }
    }
    std::sort(nearby.begin(), nearby.end());
}

    // A unit's velocity: toward its goal, plus separation from the units too close to it.
        // An idle unit that a unit going somewhere is pushing gives way: it stops pulling back to its spot, and steps
    // aside, off the other unit's way. Once nothing pushes it, it walks back to its spot.
void steerVelocity(World& w, SteerFrame& s, size_t i, std::vector<std::uint32_t>& nearby) {
    const Entity e = s.units[i];
    const Transform* tr = w.get<Transform>(e);
    unitsNearby(s, tr, i, nearby);

    float vx = 0.0f, vz = 0.0f, sideX = 0.0f, sideZ = 0.0f;
    bool shoved = false;
    for (const std::uint32_t j : nearby) {
        const std::pair<float, float>& o = s.pos[j];
        float sx = tr->x - o.first, sz = tr->z - o.second;
        // Every unit and mob is the same one-tile square, lined up with the tiles: two overlap when they are less
        // than a tile apart on both axes, and are pushed apart along the axis they overlap least on
        const float overX = UNIT_BOX - std::fabs(sx), overZ = UNIT_BOX - std::fabs(sz);
        if (overX <= 0.0f || overZ <= 0.0f) continue;
        // Two settled units following the same unit leave each other be: no pushing back and forth between them
        if (s.calmLead[i] != NULL_ENTITY && s.calmLead[i] == s.calmLead[j]) continue;
        if (std::fabs(sx) < 1e-4f && std::fabs(sz) < 1e-4f) sx = (e & 1u) ? 1e-3f : -1e-3f;   // right on top: pick a side
        const bool alongX = overX < overZ;
        const float push = (alongX ? overX : overZ) * 8.0f;
        if (alongX) vx += (sx >= 0.0f ? 1.0f : -1.0f) * push;
        else vz += (sz >= 0.0f ? 1.0f : -1.0f) * push;
        if (s.going[i] && s.pinned[j]) {
            // Against a unit that can't give way (one lying on a bed): slide around it, on the side already nearer
            float hx = s.goal[i].first - tr->x, hz = s.goal[i].second - tr->z;
            const float hl = std::sqrt(hx * hx + hz * hz);
            if (hl > 1e-4f) {
                hx /= hl;
                hz /= hl;
                float side = -hz * sx + hx * sz;                          // which side of this unit's heading it is on
                if (std::fabs(side) < 1e-3f) side = (e & 1u) ? 1.0f : -1.0f;   // dead ahead: pick a side
                const float sgn = side > 0.0f ? 1.0f : -1.0f;
                vx += -hz * sgn * push * 1.5f;
                vz += hx * sgn * push * 1.5f;
            }
        }
        if (!s.going[i] && s.going[j]) {
            // Pushed by a unit on its way: step to the side of its path, the side this unit is already on
            shoved = true;
            float hx = s.goal[j].first - o.first, hz = s.goal[j].second - o.second;
            const float hl = std::sqrt(hx * hx + hz * hz);
            if (hl > 1e-4f) {
                hx /= hl;
                hz /= hl;
                float side = hx * sz - hz * sx;                      // which side of its heading this unit is on
                if (std::fabs(side) < 1e-3f) side = (e & 1u) ? 1.0f : -1.0f;   // dead ahead: pick a side
                const float s = side > 0.0f ? 1.0f : -1.0f;
                sideX += -hz * s * push;
                sideZ += hx * s * push;
            }
        }
    }
    if (shoved) {
        vx += sideX;
        vz += sideZ;
    } else {
        const float dx = s.goal[i].first - tr->x, dz = s.goal[i].second - tr->z;
        const float dist = std::sqrt(dx * dx + dz * dz);
        // A settled follower only walks back to its spot when it is at least a whole unit's room away from it
        const float deadZone = s.calmLead[i] != NULL_ENTITY ? unitWidth : 0.05f;
        if (dist > deadZone && s.speeds[i] > 0.0f) {
            const float sp = std::min(s.speeds[i], dist * 2.5f);   // slow down near the goal
            vx += dx / dist * sp;
            vz += dz / dist * sp;
        }
    }
    s.vel[i] = { vx, vz };
}

// Applies a unit's movement and slide along walls. Walking gives XP, and records the distance walked and
// the load for the combat step, which runs first next frame.
void applySteering(World& w, Terrain& t, Entity e, Vec2f velocity, float dt, bool running) {
    Transform* tr = w.get<Transform>(e);
    const bool player = w.has<Commandable>(e);
    const float load = encumbranceOf(w, e);
    const float x0 = tr->x, z0 = tr->z, y0 = tr->y;

    float nx = tr->x + velocity.first * dt;
    float nz = tr->z + velocity.second * dt;
    // Try each axis separately so units slide along walls instead of stopping
    if (t.isFree(nx, tr->z)) tr->x = nx;
    if (t.isFree(tr->x, nz)) tr->z = nz;
    tr->y = t.heightAt(tr->x, tr->z);

    const float moved = std::hypot(tr->x - x0, tr->z - z0);
    if (Combat* c = w.get<Combat>(e)) {
        c->moved = moved;
        c->encumbrance = load;
    }
    if (moved > 0.0f) {
        grantXp(w, e, StatId::Endurance, ENDURANCE_XP_PER_TILE * moved, lang("MISC_REASON_WALKING"));
        grantXp(w, e, StatId::Strength, strengthXpWhileWalking(load, dt), lang("MISC_REASON_CARRYING"), LogKind::XpCarrying);
        if (tr->y > y0) grantXp(w, e, StatId::Athletics, tr->y - y0, lang("MISC_REASON_CLIMBING"));
        // Moving at run pace marks the unit for a double hunger tick at the next hour. A nudge from a neighbor,
        // a hundredth of a tile or less in a frame, is not walking.
        if (player && running && moved >= 0.01f) {
            if (Needs* nd = w.get<Needs>(e)) nd->hurried = true;
        }
    }
}

} // namespace

void steeringSystem(World& w, Terrain& t, float dt, bool running) {
    SteerFrame s;
    // Every unit that moves: player units by their orders, hostiles by their chase. An Unconscious unit does not move
    // and is not pushed, nor does it push anyone: it lies where it fell, or goes with the unit carrying it.
    s.units = w.view<Unit, Transform>();
    std::vector<Entity>& units = s.units;
    units.erase(std::remove_if(units.begin(), units.end(), [&](Entity e) { return w.has<Unconscious>(e) || w.has<Carried>(e); }),
                units.end());
    s.vel.assign(units.size(), { 0.0f, 0.0f });
    s.speeds.assign(units.size(), 0.0f);
    s.goal.assign(units.size(), { 0.0f, 0.0f });
    s.going.assign(units.size(), 0);
    s.pinned.assign(units.size(), 0);
    s.calmLead.assign(units.size(), NULL_ENTITY);

    for (Entity e : units) {
        if (const Combat* c = w.get<Combat>(e); c && c->target != NULL_ENTITY) s.attacked.push_back(c->target);
    }
    std::sort(s.attacked.begin(), s.attacked.end());

    for (size_t i = 0; i < units.size(); ++i) steerGoal(w, t, s, i, running);

    s.pos.resize(units.size());
    s.byCell.resize(units.size());
    for (size_t i = 0; i < units.size(); ++i) {
        const Transform* tr = w.get<Transform>(units[i]);
        s.pos[i] = { tr->x, tr->z };
        s.byCell[i] = { cellKey((int)std::floor(tr->x / UNIT_BOX), (int)std::floor(tr->z / UNIT_BOX)), (std::uint32_t)i };
    }
    std::sort(s.byCell.begin(), s.byCell.end());

    std::vector<std::uint32_t> nearby;
    for (size_t i = 0; i < units.size(); ++i) {
        if (!s.pinned[i]) steerVelocity(w, s, i, nearby);
    }
    for (size_t i = 0; i < units.size(); ++i) applySteering(w, t, units[i], s.vel[i], dt, running);
}

void cleanupActiveSquads(World& w) {
    std::vector<Entity> used;
    for (Entity e : w.view<ActiveSquadMember>()) used.push_back(w.get<ActiveSquadMember>(e)->squad);

    for (Entity sq : w.view<ActiveSquad>()) {
        if (std::find(used.begin(), used.end(), sq) == used.end()) w.destroy(sq);
    }
}

void issueFollowOrder(World& w, const std::vector<Entity>& units, Entity target) {
    if (!w.has<Unit>(target) || !w.get<Transform>(target)) return;
    for (Entity u : units) {
        if (u == target || !w.has<Commandable>(u) || !w.get<Transform>(u) || cannotTakeOrder(w, u)) continue;
        clearOrders(w, u);
        w.add(u, FollowOrder{ target });
    }
}

void issueDefendOrder(World& w, const std::vector<Entity>& units, Entity ward) {
    if (!w.has<Unit>(ward) || !w.get<Transform>(ward)) return;
    for (Entity u : units) {
        if (u == ward || !w.has<Commandable>(u) || !w.get<Transform>(u) || cannotTakeOrder(w, u)) continue;
        clearOrders(w, u);
        w.add(u, DefendOrder{ ward, NULL_ENTITY });
    }
}

void defendSystem(World& w) {
    std::vector<Entity> fighters;   // the units that can attack, listed once for every defender that looks
    bool fightersListed = false;
    for (Entity d : w.view<DefendOrder>()) {
        DefendOrder* o = w.get<DefendOrder>(d);
        if (!w.has<Unit>(o->ward) || !w.get<Transform>(o->ward)) {   // the unit defended is gone
            w.remove<AttackOrder>(d);
            w.remove<DefendOrder>(d);
            continue;
        }
        // Still fighting an attacker that is standing and still after the ward: keep at it
        auto attackingWard = [&](Entity a) {
            const Combat* c = w.get<Combat>(a);
            const Needs* n = w.get<Needs>(a);
            // Your own units never fight each other, even one that attacks the unit defended
            return a != d && !w.has<Commandable>(a) && w.has<Unit>(a) && w.get<Transform>(a) && c &&
                   c->target == o->ward && (!n || n->hp > 0);
        };
        if (o->engaged != NULL_ENTITY && attackingWard(o->engaged) && w.has<AttackOrder>(d)) continue;
        if (o->engaged != NULL_ENTITY) {
            w.remove<AttackOrder>(d);
            if (Combat* c = w.get<Combat>(d)) c->target = NULL_ENTITY;
            o->engaged = NULL_ENTITY;
        }
        // The nearest unit now attacking the ward, if any, becomes the one to fight
        const Transform* dt = w.get<Transform>(d);
        Entity best = NULL_ENTITY;
        float bestD2 = 0.0f;
        if (!fightersListed) {
            fighters = w.view<Unit, Transform, Combat>();
            fightersListed = true;
        }
        for (Entity a : fighters) {
            if (!attackingWard(a)) continue;
            const Transform* at = w.get<Transform>(a);
            const float d2 = (at->x - dt->x) * (at->x - dt->x) + (at->z - dt->z) * (at->z - dt->z);
            if (best == NULL_ENTITY || d2 < bestD2) {
                best = a;
                bestD2 = d2;
            }
        }
        if (best != NULL_ENTITY) {
            o->engaged = best;
            w.add(d, AttackOrder{ best });
        }
    }
}
