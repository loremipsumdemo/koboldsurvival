#include "systems_internal.h"

using namespace systems_detail;

namespace {
Vec2 middleOf(World& w, const std::vector<ObjectId>& units) {
    float cx = 0.0f, cz = 0.0f;
    for (ObjectId u : units) {
        const Transform* tr = w.get<Transform>(u);
        cx += tr->x;
        cz += tr->z;
    }
    return Vec2{ cx / (float)units.size(), cz / (float)units.size() };
}

// Sets up the squad and hands out the slots. With no walk, it forms up around point without a route.
void formSquad(World& w, Terrain& t, const std::vector<ObjectId>& units, Vec2 point, bool walk, const FormationSpec& spec) {
    const Vec2 from = middleOf(w, units);
    ActiveSquad s;
    s.cx = from.x;
    s.cz = from.z;
    s.size = (int)units.size();
    s.spacing = formationSpacing(spec.choice);
    s.offsets = formationOffsets(spec.choice.shape, s.size, s.spacing, spec.width);
    const ObjectId sq = w.create();
    w.add(sq, std::move(s));
    for (ObjectId u : units) clearOrders(w, u);

    ActiveSquad* squad = w.get<ActiveSquad>(sq);
    if (walk) {
        planActiveSquad(w, t, sq, point);
    } else {
        squad->arrived = true;
    }
    Vec2 dest = walk ? from : point;
    float endX = spec.headX, endZ = spec.headZ;
    if (!squad->waypoints.empty()) {
        dest = squad->waypoints.back();
        const Vec2 before = squad->waypoints.size() > 1 ? squad->waypoints[squad->waypoints.size() - 2] : from;
        const float len = std::hypot(dest.x - before.x, dest.z - before.z);
        if (!spec.facing && len > 1e-4f) {
            endX = (dest.x - before.x) / len;
            endZ = (dest.z - before.z) / len;
        }
        const Vec2 first = squad->waypoints.front();
        const float firstLen = std::hypot(first.x - from.x, first.z - from.z);
        if (firstLen > 1e-4f) {
            squad->headX = (first.x - from.x) / firstLen;
            squad->headZ = (first.z - from.z) / firstLen;
        }
    } else {
        squad->headX = endX;
        squad->headZ = endZ;
    }
    squad->endX = endX;
    squad->endZ = endZ;
    squad->end = dest;
    squad->finalSpots = fitFormation(t, dest, endX, endZ, squad->offsets, squad->spacing);

    std::vector<Vec2> places;
    places.reserve(units.size());
    for (ObjectId u : units) {
        const Transform* tr = w.get<Transform>(u);
        places.push_back(Vec2{ tr->x, tr->z });
    }
    const std::vector<int> slots = assignFormationSlots(places, squad->finalSpots, dest.x - from.x, dest.z - from.z);
    for (size_t i = 0; i < units.size(); ++i) {
        ActiveSquadMember member;
        member.squad = sq;
        member.slot = slots[i];
        w.add(units[i], member);
    }
}

std::vector<ObjectId> ableToMove(World& w, const std::vector<ObjectId>& given) {
    std::vector<ObjectId> units;
    for (ObjectId u : given) {
        if (!cannotTakeOrder(w, u)) units.push_back(u);
    }
    return units;
}
}

void issueFormationOrder(World& w, Terrain& t, const std::vector<ObjectId>& given, Vec2 point, const FormationSpec& spec) {
    const std::vector<ObjectId> units = ableToMove(w, given);
    if (units.empty()) return;

    if (units.size() == 1) {
        ObjectId u = units[0];
        clearOrders(w, u);
        walkSolo(w, t, u, point);
        return;
    }
    formSquad(w, t, units, point, true, spec);
}

void issueMoveOrder(World& w, Terrain& t, const std::vector<ObjectId>& given, Vec2 clickPoint) {
    FormationSpec spec;
    spec.choice = w.resource<FormationChoice>();
    issueFormationOrder(w, t, given, clickPoint, spec);
}

void reformUnits(World& w, Terrain& t, const std::vector<ObjectId>& given, const FormationChoice& choice) {
    const std::vector<ObjectId> units = ableToMove(w, given);
    if (units.size() < 2) return;
    FormationSpec spec;
    spec.choice = choice;
    spec.facing = true;
    Vec2 middle = middleOf(w, units);
    // The new shape keeps the old one's middle and the way it faces, only its shape or spacing changes
    for (ObjectId u : units) {
        const ActiveSquadMember* sm = w.get<ActiveSquadMember>(u);
        const ActiveSquad* sq = sm ? w.get<ActiveSquad>(sm->squad) : nullptr;
        if (!sq) continue;
        spec.headX = sq->endX;
        spec.headZ = sq->endZ;
        if (!sq->arrived && !sq->waypoints.empty()) {
            formSquad(w, t, units, sq->waypoints.back(), true, spec);
            return;
        }
        middle = sq->end;
        break;
    }
    formSquad(w, t, units, middle, false, spec);
}

FormationSpec formationFromDrag(World& w, const std::vector<ObjectId>& units, Vec2 a, Vec2 b, const FormationChoice& choice, Vec2& point) {
    FormationSpec spec;
    spec.choice = choice;
    point = a;
    const float dx = b.x - a.x, dz = b.z - a.z, len = std::hypot(dx, dz);
    if (len < FORMATION_DRAG_LEAST || units.empty()) return spec;
    // Where the drag started is the middle; the drag reaches out to one edge, so a circle's radius or half a line
    spec.width = len * 2.0f;
    spec.facing = true;
    spec.headX = dz / len;
    spec.headZ = -dx / len;
    const Vec2 from = middleOf(w, units);
    if ((point.x - from.x) * spec.headX + (point.z - from.z) * spec.headZ < 0.0f) {
        spec.headX = -spec.headX;
        spec.headZ = -spec.headZ;
    }
    return spec;
}

std::vector<Vec2> previewFormation(World& w, const Terrain& t, const std::vector<ObjectId>& units, Vec2 point, const FormationSpec& spec) {
    if (units.empty()) return {};
    const float spacing = formationSpacing(spec.choice);
    float headX = spec.headX, headZ = spec.headZ;
    if (!spec.facing) {
        const Vec2 from = middleOf(w, units);
        const float len = std::hypot(point.x - from.x, point.z - from.z);
        if (len > 1e-4f) {
            headX = (point.x - from.x) / len;
            headZ = (point.z - from.z) / len;
        }
    }
    return fitFormation(t, point, headX, headZ, formationOffsets(spec.choice.shape, (int)units.size(), spacing, spec.width), spacing);
}

void issuePickupOrder(World& w, Terrain& t, const std::vector<ObjectId>& units, ObjectId item) {
    const Transform* it = w.get<Transform>(item);
    if (!it || !w.has<GroundItem>(item)) return;
    const Vec2 target{ it->x, it->z };

    for (ObjectId u : units) {
        if (!w.has<Inventory>(u) || !w.get<Transform>(u)) continue;
        if (cannotTakeOrder(w, u) || !canHoldItem(w, u, w.get<GroundItem>(item)->item)) continue;
        clearOrders(w, u);
        walkSolo(w, t, u, target, storeyOf(w, item));
        w.add(u, PickupOrder{ item });
    }
}

void issuePickupChain(World& w, Terrain& t, const std::vector<ObjectId>& units, const std::vector<ObjectId>& items) {
    PickupJob job;
    for (ObjectId it : items) {
        if (w.has<GroundItem>(it) && w.get<Transform>(it)) job.items.push_back(it);
    }
    if (job.items.empty()) return;

    std::vector<ObjectId> crew;
    for (ObjectId u : units) {
        if (!w.has<Inventory>(u) || !w.get<Transform>(u) || cannotTakeOrder(w, u)) continue;
        clearOrders(w, u);
        crew.push_back(u);
    }
    if (crew.empty()) return;

    const ObjectId jobE = w.create();
    w.add(jobE, std::move(job));
    for (ObjectId u : crew) w.add(u, PickupChain{ jobE, {}, 0 });
    pickupChainSystem(w, t);
}

void pickupChainSystem(World& w, Terrain& t) {
    std::map<ObjectId, std::vector<ObjectId>> crews;
    for (ObjectId u : w.view<PickupChain>()) {
        const ObjectId jobE = w.get<PickupChain>(u)->job;
        if (!w.has<PickupJob>(jobE) || !w.get<Transform>(u) || !w.has<Inventory>(u)) {
            cancelPickup(w, u);
            continue;
        }
        crews[jobE].push_back(u);
    }
    for (ObjectId jobE : w.view<PickupJob>()) {
        PickupJob* job = w.get<PickupJob>(jobE);
        job->items.erase(std::remove_if(job->items.begin(), job->items.end(),
                                        [&](ObjectId it) { return !w.has<GroundItem>(it) || !w.get<Transform>(it); }),
                         job->items.end());
        auto found = crews.find(jobE);
        if (found == crews.end() || job->items.empty()) {
            if (found != crews.end()) {
                for (ObjectId u : found->second) cancelPickup(w, u);
            }
            w.destroy(jobE);
            continue;
        }
        std::vector<ObjectId>& crew = found->second;

        std::vector<ObjectId> claimed;
        std::vector<ObjectId> idle;
        for (ObjectId u : crew) {
            if (const PickupOrder* po = w.get<PickupOrder>(u)) claimed.push_back(po->item);
            else idle.push_back(u);
        }

        auto freeFor = [&](ObjectId u, ObjectId it) {
            if (std::find(claimed.begin(), claimed.end(), it) != claimed.end()) return false;
            const PickupChain* ch = w.get<PickupChain>(u);
            if (std::find(ch->skipped.begin(), ch->skipped.end(), it) != ch->skipped.end()) return false;
            return canHoldItem(w, u, w.get<GroundItem>(it)->item);
        };
        while (!idle.empty()) {
            float bestD2 = -1.0f;
            size_t bu = 0;
            ObjectId bi = NO_OBJECT;
            for (size_t i = 0; i < idle.size(); ++i) {
                const Transform* ut = w.get<Transform>(idle[i]);
                for (ObjectId it : job->items) {
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
            if (bi == NO_OBJECT) break;
            const ObjectId u = idle[bu];
            const Transform* itr = w.get<Transform>(bi);
            walkSolo(w, t, u, Vec2{ itr->x, itr->z }, storeyOf(w, bi));
            w.add(u, PickupOrder{ bi });
            w.get<PickupChain>(u)->replans = 0;
            claimed.push_back(bi);
            idle.erase(idle.begin() + bu);
        }

        for (ObjectId u : idle) {
            const PickupChain* ch = w.get<PickupChain>(u);
            bool any = false;
            for (ObjectId it : job->items) {
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

void steerUnits(World& w, const std::vector<ObjectId>& units, Vec2 target) {
    for (ObjectId u : units) {
        if (!w.has<Commandable>(u) || !w.get<Transform>(u) || cannotTakeOrder(w, u)) continue;
        clearOrders(w, u);
        w.add(u, SoloMove{});
        SoloMove* so = w.get<SoloMove>(u);
        so->waypoints.assign(1, target);
        so->next = 0;
    }
}

void issueAttackOrder(World& w, const std::vector<ObjectId>& units, ObjectId target) {
    if (!w.has<Entity>(target) || !w.get<Transform>(target) || inPrisonerCage(w, target)) return;
    for (ObjectId u : units) {
        if (!w.has<Commandable>(u) || !w.get<Transform>(u)) continue;
        if (cannotTakeOrder(w, u)) continue;
        clearOrders(w, u);
        w.add(u, AttackOrder{ target });
        ensureCombat(w, u);
        w.get<Combat>(u)->target = target;
    }
}

void issueTalkOrder(World& w, const std::vector<ObjectId>& units, ObjectId npc, bool trade, bool shop) {
    if (!w.has<Entity>(npc) || !w.get<Transform>(npc) || w.has<Mob>(npc)) return;
    for (ObjectId u : units) {
        if (!w.has<Commandable>(u) || !w.get<Transform>(u) || cannotTakeOrder(w, u)) continue;
        if (w.has<Mob>(u)) continue;
        clearOrders(w, u);
        w.add(u, TalkOrder{ npc, trade, shop });
    }
}

void clearTalkOrders(World& w) {
    for (ObjectId u : w.view<TalkOrder>()) w.remove<TalkOrder>(u);
}

int pickupSystem(World& w, Terrain& t) {
    int taken = 0;
    for (ObjectId u : w.view<PickupOrder, Transform, Inventory>()) {
        const ObjectId item = w.get<PickupOrder>(u)->item;
        const Transform* ut = w.get<Transform>(u);
        const Transform* it = w.get<Transform>(item);
        GroundItem* gi = w.get<GroundItem>(item);
        if (!it || !gi) {
            endPickup(w, u);
            continue;
        }

        const float dx = it->x - ut->x, dz = it->z - ut->z;
        if (dx * dx + dz * dz <= PICKUP_REACH * PICKUP_REACH) {
            if (!canHoldItem(w, u, gi->item)) {
                if (PickupChain* ch = w.get<PickupChain>(u)) ch->skipped.push_back(item);
                endPickup(w, u);
                continue;
            }
            const int count = gi->count;
            const std::string itemName = itemNameOf(gi->item);
            const int left = addToInventory(*w.get<Inventory>(u), gi->item, gi->count);
            if (left < count) {
                queueLog(w, langf("MISC_PICKS_UP", { nameOf(w, u), std::to_string(count - left), itemName }), LogKind::Pickup, npcOnly(w, u));
                queueUnitLabel(w, u, itemGainLabel(count - left, itemName));
                queueSoundFrom(w, Sfx::Pickup, u);
            }
            if (left == 0) w.destroy(item);
            else gi->count = left;
            if (w.has<Commandable>(u) && left < count) ++taken;
            endPickup(w, u);
            continue;
        }

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
    pickupChainSystem(w, t);
    return taken;
}

void issueFollowOrder(World& w, const std::vector<ObjectId>& units, ObjectId target) {
    if (!w.has<Entity>(target) || !w.get<Transform>(target)) return;
    for (ObjectId u : units) {
        if (u == target || !w.has<Commandable>(u) || !w.get<Transform>(u) || cannotTakeOrder(w, u)) continue;
        clearOrders(w, u);
        w.add(u, FollowOrder{ target });
    }
}

void issueDefendOrder(World& w, const std::vector<ObjectId>& units, ObjectId ward) {
    if (!w.has<Entity>(ward) || !w.get<Transform>(ward)) return;
    for (ObjectId u : units) {
        if (u == ward || !w.has<Commandable>(u) || !w.get<Transform>(u) || cannotTakeOrder(w, u)) continue;
        clearOrders(w, u);
        w.add(u, DefendOrder{ ward, NO_OBJECT });
    }
}

void defendSystem(World& w) {
    std::vector<ObjectId> fighters;
    bool fightersListed = false;
    for (ObjectId d : w.view<DefendOrder>()) {
        DefendOrder* o = w.get<DefendOrder>(d);
        if (!w.has<Entity>(o->ward) || !w.get<Transform>(o->ward)) {
            w.remove<AttackOrder>(d);
            w.remove<DefendOrder>(d);
            continue;
        }
        auto attackingWard = [&](ObjectId a) {
            const Combat* c = w.get<Combat>(a);
            const Needs* n = w.get<Needs>(a);
            return a != d && !w.has<Commandable>(a) && w.has<Entity>(a) && w.get<Transform>(a) && c &&
                   c->target == o->ward && (!n || n->hp > 0);
        };
        if (o->engaged != NO_OBJECT && attackingWard(o->engaged) && w.has<AttackOrder>(d)) continue;
        if (o->engaged != NO_OBJECT) {
            w.remove<AttackOrder>(d);
            if (Combat* c = w.get<Combat>(d)) c->target = NO_OBJECT;
            o->engaged = NO_OBJECT;
        }
        const Transform* dt = w.get<Transform>(d);
        ObjectId best = NO_OBJECT;
        float bestD2 = 0.0f;
        if (!fightersListed) {
            fighters = w.view<Entity, Transform, Combat>();
            fightersListed = true;
        }
        for (ObjectId a : fighters) {
            if (!attackingWard(a)) continue;
            const Transform* at = w.get<Transform>(a);
            const float d2 = distSq(*at, *dt);
            if (best == NO_OBJECT || d2 < bestD2) {
                best = a;
                bestD2 = d2;
            }
        }
        if (best != NO_OBJECT) {
            o->engaged = best;
            w.add(d, AttackOrder{ best });
        }
    }
}
