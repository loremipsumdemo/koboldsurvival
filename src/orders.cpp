#include "systems_internal.h"

using namespace systems_detail;

void issueMoveOrder(World& w, Terrain& t, const std::vector<Entity>& given, Vec2 clickPoint) {
    std::vector<Entity> units;
    for (Entity u : given) {
        if (!cannotTakeOrder(w, u)) units.push_back(u);
    }
    if (units.empty()) return;

    if (units.size() == 1) {
        Entity u = units[0];
        clearOrders(w, u);
        walkSolo(w, t, u, clickPoint);
        return;
    }

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
        if (!w.has<Inventory>(u) || !w.get<Transform>(u)) continue;
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
        if (!w.has<Inventory>(u) || !w.get<Transform>(u) || cannotTakeOrder(w, u)) continue;
        clearOrders(w, u);
        crew.push_back(u);
    }
    if (crew.empty()) return;

    const Entity jobE = w.create();
    w.add(jobE, std::move(job));
    for (Entity u : crew) w.add(u, PickupChain{ jobE, {}, 0 });
    pickupChainSystem(w, t);
}

void pickupChainSystem(World& w, Terrain& t) {
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

        std::vector<Entity> claimed;
        std::vector<Entity> idle;
        for (Entity u : crew) {
            if (const PickupOrder* po = w.get<PickupOrder>(u)) claimed.push_back(po->item);
            else idle.push_back(u);
        }

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
            if (bi == NULL_ENTITY) break;
            const Entity u = idle[bu];
            const Transform* itr = w.get<Transform>(bi);
            walkSolo(w, t, u, Vec2{ itr->x, itr->z });
            w.add(u, PickupOrder{ bi });
            w.get<PickupChain>(u)->replans = 0;
            claimed.push_back(bi);
            idle.erase(idle.begin() + bu);
        }

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
        so->waypoints.assign(1, target);
        so->next = 0;
    }
}

void issueAttackOrder(World& w, const std::vector<Entity>& units, Entity target) {
    if (!w.has<Unit>(target) || !w.get<Transform>(target)) return;
    for (Entity u : units) {
        if (!w.has<Commandable>(u) || !w.get<Transform>(u)) continue;
        if (cannotTakeOrder(w, u)) continue;
        clearOrders(w, u);
        w.add(u, AttackOrder{ target });
        ensureCombat(w, u);
        w.get<Combat>(u)->target = target;
    }
}

void issueTalkOrder(World& w, const std::vector<Entity>& units, Entity npc, bool trade, bool shop) {
    if (!w.has<Unit>(npc) || !w.get<Transform>(npc) || w.has<Mob>(npc)) return;
    for (Entity u : units) {
        if (!w.has<Commandable>(u) || !w.get<Transform>(u) || cannotTakeOrder(w, u)) continue;
        if (w.has<Mob>(u)) continue;
        clearOrders(w, u);
        w.add(u, TalkOrder{ npc, trade, shop });
    }
}

void clearTalkOrders(World& w) {
    for (Entity u : w.view<TalkOrder>()) w.remove<TalkOrder>(u);
}

int pickupSystem(World& w, Terrain& t) {
    int taken = 0;
    for (Entity u : w.view<PickupOrder, Transform, Inventory>()) {
        const Entity item = w.get<PickupOrder>(u)->item;
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
                queueLog(langf("MISC_PICKS_UP", { nameOf(w, u), std::to_string(count - left), itemName }), LogKind::Pickup, npcOnly(w, u));
                queueUnitLabel(u, itemGainLabel(count - left, itemName));
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
    std::vector<Entity> fighters;
    bool fightersListed = false;
    for (Entity d : w.view<DefendOrder>()) {
        DefendOrder* o = w.get<DefendOrder>(d);
        if (!w.has<Unit>(o->ward) || !w.get<Transform>(o->ward)) {
            w.remove<AttackOrder>(d);
            w.remove<DefendOrder>(d);
            continue;
        }
        auto attackingWard = [&](Entity a) {
            const Combat* c = w.get<Combat>(a);
            const Needs* n = w.get<Needs>(a);
            return a != d && !w.has<Commandable>(a) && w.has<Unit>(a) && w.get<Transform>(a) && c &&
                   c->target == o->ward && (!n || n->hp > 0);
        };
        if (o->engaged != NULL_ENTITY && attackingWard(o->engaged) && w.has<AttackOrder>(d)) continue;
        if (o->engaged != NULL_ENTITY) {
            w.remove<AttackOrder>(d);
            if (Combat* c = w.get<Combat>(d)) c->target = NULL_ENTITY;
            o->engaged = NULL_ENTITY;
        }
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
            const float d2 = distSq(*at, *dt);
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
