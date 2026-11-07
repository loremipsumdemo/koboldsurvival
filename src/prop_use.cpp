#include "systems_internal.h"

using namespace systems_detail;

namespace {
void stopWorkersAt(World& w, ObjectId prop) {
    for (ObjectId u : w.view<Working>()) {
        if (w.get<Working>(u)->prop != prop) continue;
        queueLog(w, langf("MISC_CRAFT_STOPS", { nameOf(w, u) }), LogKind::Other, npcOnly(w, u));
        w.remove<Working>(u);
    }
    if (PropWork* pw = w.get<PropWork>(prop)) {
        pw->progress = 0.0;
        pw->workers = 0;
    }
}
}

CraftState craftStateOf(World& w, ObjectId prop) {
    const Prop* p = w.get<Prop>(prop);
    const Storage* st = w.get<Storage>(prop);
    if (!p || propInfo(p->kind).use != PropUse::Crafting) return CraftState::NoInput;
    const PropInfo& info = propInfo(p->kind);
    if (info.input == 0) return CraftState::Ready;
    if (!st) return CraftState::NoInput;
    for (const ItemStack& stack : st->slots) {
        if (stack.count > 0 && recipeOutput(p->kind, stack.item) > 0) return CraftState::Ready;
    }
    return CraftState::NoInput;
}

bool propInTheWay(World& w, ObjectId unit, ObjectId prop) {
    const Transform* a = w.get<Transform>(unit);
    const Transform* b = w.get<Transform>(prop);
    if (!a || !b) return false;
    const float abx = b->x - a->x, abz = b->z - a->z;
    const float len2 = abx * abx + abz * abz;
    for (ObjectId p : w.view<Prop, Transform>()) {
        if (p == prop) continue;
        const Transform* c = w.get<Transform>(p);
        float t = len2 > 1e-8f ? ((c->x - a->x) * abx + (c->z - a->z) * abz) / len2 : 0.0f;
        t = std::clamp(t, 0.0f, 1.0f);
        const float nx = a->x + abx * t - c->x, nz = a->z + abz * t - c->z;
        if (nx * nx + nz * nz < PROP_BLOCK_RADIUS * PROP_BLOCK_RADIUS) return true;
    }
    return false;
}

ObjectId propOccupant(World& w, ObjectId prop) {
    for (ObjectId u : w.view<Occupying>()) {
        if (w.get<Occupying>(u)->prop == prop) return u;
    }
    return NO_OBJECT;
}

void issuePropOrder(World& w, Terrain& t, const std::vector<ObjectId>& units, ObjectId prop, PropAction action) {
    const Transform* pt = w.get<Transform>(prop);
    if (!pt || !w.has<Prop>(prop)) return;
    for (ObjectId u : units) {
        if (!w.has<Commandable>(u) || !w.get<Transform>(u)) continue;
        if (w.has<PlayingDead>(u)) breakFree(w, u);
        if (outCold(w, u) || w.has<Carried>(u)) continue;
        if (lockedIn(w, u)) {
            if (action == PropAction::Unlock && occupiedProp(w, u) == prop) w.add(u, PropOrder{ prop, action });
            continue;
        }
        clearOrders(w, u);
        walkSolo(w, t, u, Vec2{ pt->x, pt->z });
        w.add(u, PropOrder{ prop, action });
    }
}

std::vector<PropArrival> propOrderSystem(World& w, Terrain& t) {
    std::vector<PropArrival> out;
    for (ObjectId u : w.view<PropOrder, Transform>()) {
        const ObjectId prop = w.get<PropOrder>(u)->prop;
        const Transform* pt = w.get<Transform>(prop);
        if (!pt || !w.has<Prop>(prop)) {
            w.remove<PropOrder>(u);
            w.remove<SoloMove>(u);
            continue;
        }
        if (flatGap(w, u, prop) <= PROP_REACH) {
            const PropAction action = w.get<PropOrder>(u)->action;
            const bool automatic = w.get<PropOrder>(u)->automatic;
            w.remove<PropOrder>(u);
            w.remove<SoloMove>(u);
            if (action == PropAction::Store) {
                if (!lockShutFor(w, prop, u)) storeCarried(w, u, prop, 0);
                continue;
            }
            if (action == PropAction::Destroy) {
                if (destroyedByHand(w.get<Prop>(prop)->kind)) {
                    queueSoundFrom(w, Sfx::Drop, prop);
                    destroyBuilding(w, prop);
                }
                continue;
            }
            PropArrival a;
            a.unit = u;
            a.prop = prop;
            a.action = action;
            a.automatic = automatic;
            const PropInfo& info = propInfo(w.get<Prop>(prop)->kind);
            const Lock* lock = w.get<Lock>(prop);
            if (action == PropAction::Evacuate) {
                const ObjectId inside = propOccupant(w, prop);
                if (lockShutFor(w, prop, u)) {
                    a.locked = true;
                } else if (inside != NO_OBJECT && inside != u) {
                    endPropTasks(w, inside);
                    stepOffProp(w, t, inside, prop);
                    w.add(inside, OccupyBan{ EVACUATE_OCCUPY_BAN_SECONDS });
                    a.started = true;
                    queueSoundFrom(w, Sfx::PutDown, prop);
                    queueLog(w, langf("MISC_EVACUATES", { nameOf(w, u), nameOf(w, inside), lang(info.nameKey) }), LogKind::Other, npcOnly(w, u, inside));
                }
                out.push_back(a);
                continue;
            }
            if (action == PropAction::Lock) {
                if (Lock* lk = w.get<Lock>(prop); lk && !lk->locked && !w.has<Mob>(u)) {
                    lk->locked = true;
                    if (lk->faction < 0) {
                        const Faction* f = w.get<Faction>(u);
                        lk->faction = f ? f->id : FACTION_PLAYER;
                    }
                    a.started = true;
                    queueSoundFrom(w, Sfx::LockedIn, prop);
                    queueLog(w, langf("MISC_LOCKS", { nameOf(w, u), lang(info.nameKey) }), LogKind::Other, npcOnly(w, u));
                    const ObjectId inside = propOccupant(w, prop);
                    if (inside != NO_OBJECT && lockedIn(w, inside)) {
                        queueLog(w, langf("MISC_LOCKED_IN", { nameOf(w, inside), lang(info.nameKey) }), LogKind::Other, npcOnly(w, inside));
                    }
                }
                out.push_back(a);
                continue;
            }
            if (action == PropAction::Unlock && lock && lock->locked && !lockShutFor(w, prop, u) && !w.has<Mob>(u)) {
                w.get<Lock>(prop)->locked = false;
                a.started = true;
                queueSoundFrom(w, Sfx::Unlocked, prop);
                queueLog(w, langf("MISC_UNLOCKS", { nameOf(w, u), lang(info.nameKey) }), LogKind::Other, npcOnly(w, u));
                out.push_back(a);
                continue;
            }
            if (action == PropAction::Unlock) {
                a.craft = tryStartUnlock(w, u, prop);
                a.started = a.craft == WorkStart::Started;
                out.push_back(a);
                continue;
            }
            if (lockShutFor(w, prop, u)) {
                a.locked = true;
                out.push_back(a);
                continue;
            }
            if (action == PropAction::Place && propInfo(w.get<Prop>(prop)->kind).burnsCorpses) {
                const Carrying* cr = w.get<Carrying>(u);
                if (cr && w.has<Corpse>(cr->unit)) {
                    const ObjectId corpse = cr->unit;
                    const Transform at = *pt;
                    queueLog(w, langf("MISC_PLACES_IN", { nameOf(w, u), nameOf(w, corpse), lang(info.nameKey) }), LogKind::Other, npcOnly(w, u, corpse));
                    queueSoundFrom(w, Sfx::Drop, prop);
                    const std::vector<ItemStack> belongings = unitBelongings(w, corpse);
                    w.remove<Carrying>(u);
                    w.destroy(corpse);
                    dropStacks(w, at, belongings);
                    a.placed = true;
                    a.started = true;
                }
                out.push_back(a);
                continue;
            }
            if (action == PropAction::Inventory) {
                if (hasStorage(info)) out.push_back(a);
                continue;
            }
            switch (info.use) {
            case PropUse::Storage:
                break;
            case PropUse::Occupy: {
                if (action == PropAction::Place) {
                    const Carrying* cr = w.get<Carrying>(u);
                    if (!cr) break;
                    const ObjectId carried = cr->unit;
                    const ObjectId occupant = propOccupant(w, prop);
                    if (w.has<Corpse>(carried)) {
                        a.corpse = true;
                    } else if (propInTheWay(w, u, prop)) {
                        a.unreachable = true;
                    } else if (occupant != NO_OBJECT) {
                        a.taken = true;
                    } else if (Transform* ct = w.get<Transform>(carried)) {
                        w.remove<Carrying>(u);
                        w.remove<Carried>(carried);
                        w.remove<PlayingDead>(carried);
                        ct->x = pt->x;
                        ct->y = pt->y;
                        ct->z = pt->z;
                        w.remove<IdleSpot>(carried);
                        w.add(carried, Occupying{ prop });
                        a.placed = true;
                        a.started = true;
                        queueSoundFrom(w, w.has<Lock>(prop) ? Sfx::LockedIn : Sfx::Occupy, prop);
                        queueLog(w, langf("MISC_PLACES_IN", { nameOf(w, u), nameOf(w, carried), lang(info.nameKey) }), LogKind::Other, npcOnly(w, u, carried));
                        if (Lock* lk = w.get<Lock>(prop)) {
                            lk->locked = true;
                            queueLog(w, langf("MISC_LOCKED_IN", { nameOf(w, carried), lang(info.nameKey) }), LogKind::Other, npcOnly(w, carried));
                        }
                    }
                    break;
                }
                if (w.has<OccupyBan>(u)) {
                    a.occupyBanned = true;
                    break;
                }
                const ObjectId occupant = propOccupant(w, prop);
                if (propInTheWay(w, u, prop)) {
                    a.unreachable = true;
                } else if (occupant != NO_OBJECT && occupant != u) {
                    a.taken = true;
                } else {
                    const bool newOccupant = occupant != u;
                    Transform* ut = w.get<Transform>(u);
                    ut->x = pt->x;
                    ut->y = pt->y;
                    ut->z = pt->z;
                    if (w.has<Carrying>(u)) putDown(w, u);
                    w.remove<IdleSpot>(u);
                    w.add(u, Occupying{ prop });
                    a.started = true;
                    queueSoundFrom(w, w.has<Lock>(prop) && newOccupant ? Sfx::LockedIn : Sfx::Occupy, prop);
                    if (Lock* lk = newOccupant ? w.get<Lock>(prop) : nullptr) {
                        lk->locked = true;
                        queueLog(w, langf("MISC_LOCKED_IN", { nameOf(w, u), lang(info.nameKey) }), LogKind::Other, npcOnly(w, u));
                    }
                }
                break;
            }
            case PropUse::Resource:
                if (w.has<Mob>(u)) break;
                if (workersAt(w, prop) >= info.workers) {
                    a.taken = true;
                } else {
                    w.add(u, Working{ prop });
                    a.started = true;
                }
                break;
            case PropUse::Crafting:
                a.craft = tryStartWork(w, u, prop);
                a.started = a.craft == WorkStart::Started;
                a.taken = a.craft == WorkStart::Full;
                break;
            case PropUse::PassiveCrafting:
            case PropUse::None:
                break;
            }
            if (info.use != PropUse::None && info.use != PropUse::Storage && info.use != PropUse::PassiveCrafting) out.push_back(a);
            continue;
        }
        const SoloMove* so = w.get<SoloMove>(u);
        if (!so || so->next >= so->waypoints.size()) {
            w.remove<PropOrder>(u);
            w.remove<SoloMove>(u);
        }
    }

    std::vector<ObjectId> lying = w.view<Occupying, Transform>();
    std::sort(lying.begin(), lying.end());
    std::vector<ObjectId> bedsTaken;
    for (ObjectId u : lying) {
        const ObjectId bed = w.get<Occupying>(u)->prop;
        const Transform* pt = w.get<Transform>(bed);
        const Prop* bp = w.get<Prop>(bed);
        if (!pt || !bp || propInfo(bp->kind).use != PropUse::Occupy || w.has<Carried>(u) ||
            std::find(bedsTaken.begin(), bedsTaken.end(), bed) != bedsTaken.end()) {
            w.remove<Occupying>(u);
            continue;
        }
        bedsTaken.push_back(bed);
        Transform* ut = w.get<Transform>(u);
        ut->x = pt->x;
        ut->y = pt->y;
        ut->z = pt->z;
    }
    return out;
}

std::vector<WorkGain> workSystem(World& w, double gameSeconds, Rng& rng) {
    std::vector<WorkGain> gains;
    std::map<ObjectId, std::vector<ObjectId>> workersOf;
    for (ObjectId u : w.view<Working, Transform>()) {
        const Working* wk = w.get<Working>(u);
        const ObjectId prop = wk->prop;
        const bool shut = lockShutFor(w, prop, u);
        const bool valid = wk->unlocking ? shut : (isWorkProp(useOf(w, prop)) && !shut);
        if (!valid) {
            w.remove<Working>(u);
            continue;
        }
        if (const Needs* n = w.get<Needs>(u); n && n->hp <= 0) {
            w.remove<Working>(u);
            continue;
        }
        if (flatGap(w, u, prop) > PROP_REACH + 0.5f) continue;
        workersOf[prop].push_back(u);
    }
    for (ObjectId e : w.view<PropWork>()) w.get<PropWork>(e)->workers = 0;

    for (auto& [prop, workers] : workersOf) {
        std::sort(workers.begin(), workers.end());
        const PropKind propKind = w.get<Prop>(prop)->kind;
        auto addGain = [&](WorkGain g) {
            g.propKind = idx(propKind);
            gains.push_back(g);
        };
        const PropInfo& info = propInfo(w.get<Prop>(prop)->kind);
        if (w.get<Working>(workers[0])->unlocking) {
            PropWork& pw = w.has<PropWork>(prop) ? *w.get<PropWork>(prop) : w.add(prop, PropWork{});
            pw.workers = (int)workers.size();
            double perHour = 0.0;
            for (ObjectId u : workers) {
                perHour += 1.0 / (LOCKPICK_SHARE * workHoursFor(levelOf(statsOf(w, u), StatId::Lockpicking)));
            }
            pw.progress += gameSeconds / 3600.0 * perHour;
            Lock* lock = w.get<Lock>(prop);
            while (pw.progress >= 1.0 && lock->locked) {
                pw.progress -= 1.0;
                ++pw.made;
                const ObjectId picker = workers[pw.turn % workers.size()];
                ++pw.turn;
                const int level = levelOf(statsOf(w, picker), StatId::Lockpicking);
                if ((double)rng.unit() < lockpickChance(level)) {
                    lock->locked = false;
                    addGain(WorkGain{ picker, 0, WorkGain::Kind::Unlocked });
                    queueLog(w, langf("MISC_UNLOCKS", { nameOf(w, picker), lang(info.nameKey) }), LogKind::Other, npcOnly(w, picker));
                    grantXp(w, picker, StatId::Lockpicking, LOCKPICK_XP_SUCCESS, "MISC_REASON_LOCKPICKING");
                } else {
                    addGain(WorkGain{ picker, 0, WorkGain::Kind::LockpickFailed });
                    grantXp(w, picker, StatId::Lockpicking, LOCKPICK_XP_FAIL, "MISC_REASON_LOCKPICKING");
                }
            }
            if (!lock->locked) {
                for (ObjectId u : workers) w.remove<Working>(u);
                pw.progress = 0.0;
                pw.workers = 0;
            }
            continue;
        }
        if (propInfo(propKind).debris) {
            PropWork& pw = w.has<PropWork>(prop) ? *w.get<PropWork>(prop) : w.add(prop, PropWork{});
            pw.workers = (int)workers.size();
            pw.progress += gameSeconds / 3600.0 * (double)workers.size() / DEBRIS_CLEAR_HOURS;
            if (pw.progress >= 1.0) {
                for (ObjectId u : workers) {
                    w.remove<Working>(u);
                    grantXp(w, u, StatId::Strength, DEBRIS_CLEAR_XP, w.has<Commandable>(u) ? "MISC_REASON_CLEARING" : nullptr);
                }
                if (w.has<Commandable>(workers[0])) queueLog(w, langf("MISC_CLEARS_DEBRIS", { nameOf(w, workers[0]) }), LogKind::Other);
                queueSoundFrom(w, Sfx::Drop, prop);
                breakBuilding(w, prop);
            }
            continue;
        }
        const bool crafting = info.use == PropUse::Crafting;
        auto stopAll = [&]() {
            stopWorkersAt(w, prop);
            workers.clear();
        };
        if (crafting && craftStateOf(w, prop) != CraftState::Ready) {
            stopAll();
            continue;
        }
        PropWork& pw = w.has<PropWork>(prop) ? *w.get<PropWork>(prop) : w.add(prop, PropWork{});
        pw.workers = (int)workers.size();

        double perHour = 0.0;
        for (ObjectId u : workers) perHour += propInfo(w.get<Prop>(prop)->kind).speed / workHoursFor(levelOf(statsOf(w, u), info.stat));
        pw.progress += gameSeconds / 3600.0 * perHour;

        while (pw.progress >= 1.0 && !workers.empty()) {
            if (crafting) {
                int inItem = 0, outItem = info.item;
                if (info.input != 0) {
                    outItem = 0;
                    if (const Storage* st = w.get<Storage>(prop)) {
                        for (const ItemStack& stack : st->slots) {
                            if (stack.count <= 0) continue;
                            if (const int made = recipeOutput(w.get<Prop>(prop)->kind, stack.item); made > 0) {
                                inItem = stack.item;
                                outItem = made;
                                break;
                            }
                        }
                    }
                }
                const std::string outName = itemNameOf(outItem);
                if (craftStateOf(w, prop) != CraftState::Ready) {
                    stopAll();
                    break;
                }
                ObjectId got = outItem <= 0 ? workers[pw.turn % workers.size()] : NO_OBJECT;
                for (size_t k = 0; k < workers.size() && got == NO_OBJECT; ++k) {
                    const ObjectId cand = workers[(pw.turn + k) % workers.size()];
                    const Inventory* inv = w.get<Inventory>(cand);
                    if (inv && slotsCanTake(inv->slots.data(), INVENTORY_SLOTS, outItem, 1)) got = cand;
                }
                if (got == NO_OBJECT) {
                    for (ObjectId u : workers) {
                        queueLog(w, langf("MISC_GATHER_NO_ROOM", { nameOf(w, u), outName }), LogKind::Other, npcOnly(w, u));
                        w.remove<Working>(u);
                    }
                    workers.clear();
                    pw.progress = 1.0;
                    break;
                }
                if (info.input != 0) {
                    Storage* st = w.get<Storage>(prop);
                    if (!st || inItem <= 0 || !takeOne(st->slots.data(), STORAGE_SLOTS, inItem)) {
                        stopAll();
                        break;
                    }
                }
                pw.progress -= 1.0;
                ++pw.made;
                ++pw.turn;
                if (outItem > 0) {
                    giveOrDrop(w, got, outItem, 1);
                    addGain(WorkGain{ got, outItem });
                    queueLog(w, langf("MISC_CRAFTS", { nameOf(w, got), outName }), LogKind::Pickup, npcOnly(w, got));
                    grantXp(w, got, info.stat, WORK_XP_PER_ITEM, "MISC_REASON_CRAFTING");
                } else {
                    addGain(WorkGain{ got, idx(info.stat), WorkGain::Kind::Trained, WORK_XP_PER_ITEM });
                    grantXp(w, got, info.stat, WORK_XP_PER_ITEM, "MISC_REASON_TRAINING");
                }
                if (craftStateOf(w, prop) != CraftState::Ready) {
                    stopAll();
                    break;
                }
                continue;
            }
            pw.progress -= 1.0;
            ++pw.made;
            const size_t pick = pw.turn % workers.size();
            ++pw.turn;
            const ObjectId got = workers[pick];
            int gives = info.item;
            if (info.altItem > 0 && info.altOneIn > 0 && rng.range(1, info.altOneIn) == 1) gives = info.altItem;
            const std::string itemName = itemNameOf(gives);
            Inventory* inv = w.get<Inventory>(got);
            if (inv && addToInventory(*inv, gives, 1) == 0) {
                const bool yours = w.has<Commandable>(got);
                if (yours) {
                    addGain(WorkGain{ got, gives });
                    queueLog(w, langf("MISC_GATHERS", { nameOf(w, got), itemName }), LogKind::Pickup, npcOnly(w, got));
                }
                grantXp(w, got, info.stat, WORK_XP_PER_ITEM, yours ? "MISC_REASON_GATHERING" : nullptr);
            } else {
                if (const Transform* ut = w.get<Transform>(got)) {
                    spawnGroundItem(w, Transform{ ut->x, ut->y, ut->z }, gives, 1);
                }
                queueLog(w, langf("MISC_GATHER_NO_ROOM", { nameOf(w, got), itemName }), LogKind::Drop, npcOnly(w, got));
                queueUnitLabel(w, got, itemLossLabel(1, itemName), true);
                queueSoundFrom(w, Sfx::Drop, got);
                w.remove<Working>(got);
                workers.erase(workers.begin() + (long)pick);
            }
        }
        pw.workers = (int)workers.size();
        if (workers.empty() && !crafting) pw.progress = std::min(pw.progress, 0.999);
    }
    return gains;
}

int storeCarried(World& w, ObjectId unit, ObjectId prop, int item) {
    Inventory* inv = w.get<Inventory>(unit);
    Storage* st = w.get<Storage>(prop);
    const Prop* p = w.get<Prop>(prop);
    if (!inv || !st || !p) return 0;
    int moved = 0;
    int movedItem = 0;
    for (ItemStack& s : inv->slots) {
        if (s.item <= 0 || s.count <= 0) continue;
        if (item != 0 && s.item != item) continue;
        if (!propAcceptsItem(p->kind, s.item)) continue;
        const int left = addToSlots(st->slots.data(), STORAGE_SLOTS, s.item, s.count);
        moved += s.count - left;
        if (s.count - left > 0) movedItem = s.item;
        s.count = left;
        if (left <= 0) s = ItemStack{};
    }
    if (moved > 0) {
        propStorageChanged(w, prop);
        if (w.has<Commandable>(unit)) {
            queueLog(w, langf("MISC_AUTO_HAUL_STORES", { nameOf(w, unit), std::to_string(moved), itemNameOf(movedItem),
                                                    propDisplayName(w, prop) }), LogKind::Pickup, npcOnly(w, unit));
        }
    }
    return moved;
}

WorkStart tryStartWork(World& w, ObjectId unit, ObjectId prop) {
    const Prop* p = w.get<Prop>(prop);
    if (!p || !isWorkProp(propInfo(p->kind).use) || !w.get<Transform>(unit)) return WorkStart::TooFar;
    if (w.has<Mob>(unit)) return WorkStart::TooFar;
    if (const Working* wk = w.get<Working>(unit); wk && wk->prop == prop) return WorkStart::Started;
    if (flatGap(w, unit, prop) > STORAGE_CLOSE_RANGE) return WorkStart::TooFar;
    if (const Needs* n = w.get<Needs>(unit); n && n->hp <= 0) return WorkStart::TooFar;
    if (lockShutFor(w, prop, unit)) return WorkStart::Locked;
    if (lockedIn(w, unit)) return WorkStart::Locked;
    if (workersAt(w, prop) >= propInfo(p->kind).workers) return WorkStart::Full;
    if (propInfo(p->kind).use == PropUse::Crafting) {
        const CraftState cs = craftStateOf(w, prop);
        if (cs == CraftState::NoInput) return WorkStart::NoInput;
    }
    clearOrders(w, unit);
    w.add(unit, Working{ prop });
    return WorkStart::Started;
}

WorkStart tryStartUnlock(World& w, ObjectId unit, ObjectId prop) {
    const Lock* lock = w.get<Lock>(prop);
    if (!lock || !w.has<Prop>(prop) || !w.get<Transform>(unit)) return WorkStart::NotLocked;
    if (w.has<Mob>(unit)) return WorkStart::SkillTooLow;
    if (!lockShutFor(w, prop, unit)) return WorkStart::NotLocked;
    if (const Working* wk = w.get<Working>(unit); wk && wk->prop == prop && wk->unlocking) return WorkStart::Started;
    if (flatGap(w, unit, prop) > STORAGE_CLOSE_RANGE) return WorkStart::TooFar;
    if (const Needs* n = w.get<Needs>(unit); n && n->hp <= 0) return WorkStart::TooFar;
    if (levelOf(statsOf(w, unit), StatId::Lockpicking) < lock->level) return WorkStart::SkillTooLow;
    if (workersAt(w, prop) >= 1) return WorkStart::Full;
    const bool inside = occupiedProp(w, unit) == prop;
    if (!inside && lockedIn(w, unit)) return WorkStart::Locked;
    leaveSquad(w, unit);
    w.remove<SoloMove>(unit);
    cancelPickup(w, unit);
    w.remove<AttackOrder>(unit);
    w.remove<TalkOrder>(unit);
    if (const Faction* f = w.get<Faction>(unit); (!f || f->id == FACTION_PLAYER) && lock->faction >= 0 &&
                                                 lock->faction != FACTION_PLAYER) {
        const int now = changePlayerReputation(w, lock->faction, -REP_LOCKPICK_PENALTY);
        queueLog(w, langf("MISC_REPUTATION_DROPS", { factionName(lock->faction), std::to_string(REP_LOCKPICK_PENALTY),
                                                  std::to_string(now) }), LogKind::Other);
    }
    if (inside) {
        w.remove<PropOrder>(unit);
        w.remove<Working>(unit);
        w.remove<FollowOrder>(unit);
        w.remove<DefendOrder>(unit);
    } else {
        endPropTasks(w, unit);
    }
    w.add(unit, Working{ prop, true });
    return WorkStart::Started;
}

void propStorageChanged(World& w, ObjectId prop) {
    const Prop* p = w.get<Prop>(prop);
    if (!p || propInfo(p->kind).use != PropUse::Crafting) return;
    if (craftStateOf(w, prop) != CraftState::Ready) stopWorkersAt(w, prop);
}

bool propAcceptsItem(PropKind kind, int item) {
    const PropInfo& info = propInfo(kind);
    if (item <= 0) return false;
    if (info.use == PropUse::PassiveCrafting && info.item == 0) return item == info.input;
    if (info.use == PropUse::Crafting || info.use == PropUse::PassiveCrafting) return recipeOutput(kind, item) > 0;
    const ItemDef* def = findItem(item);
    switch (info.accepts) {
    case ItemFilter::Any: return true;
    case ItemFilter::RawFood: return def && hasTag(*def, "raw_food") && def->typeId != "consumable";
    case ItemFilter::Food: return def && def->typeId == "consumable";
    case ItemFilter::Weapon: return def && def->typeId == "weapon";
    case ItemFilter::Armor: return def && def->typeId == "armor";
    case ItemFilter::Water: return def && hasTag(*def, "water");
    }
    return false;
}

int recipeOutput(PropKind kind, int input) {
    const PropInfo& info = propInfo(kind);
    if (input <= 0 || (info.use != PropUse::Crafting && info.use != PropUse::PassiveCrafting)) return 0;
    if (info.input == INPUT_COOKABLE) {
        const ItemDef* def = findItem(input);
        return def && def->cooksInto > 0 && findItem(def->cooksInto) ? def->cooksInto : 0;
    }
    return input == info.input ? info.item : 0;
}

void occupyBanSystem(World& w, float dt) {
    for (ObjectId u : w.view<OccupyBan>()) {
        OccupyBan* ban = w.get<OccupyBan>(u);
        ban->secondsLeft -= dt;
        if (ban->secondsLeft <= 0.0f) w.remove<OccupyBan>(u);
    }
}

ObjectId occupiedProp(World& w, ObjectId u) {
    const Occupying* oc = w.get<Occupying>(u);
    return oc ? oc->prop : NO_OBJECT;
}

bool lockedIn(World& w, ObjectId u) {
    const Occupying* oc = w.get<Occupying>(u);
    if (!oc) return false;
    if (lockShutFor(w, oc->prop, u)) return true;
    const Lock* lk = w.get<Lock>(oc->prop);
    return lk && lk->locked && w.has<Mob>(u);
}

static int nextPassiveInput(const Storage* st, PropKind kind) {
    if (!st) return 0;
    const PropInfo& info = propInfo(kind);
    for (const ItemStack& s : st->slots) {
        if (s.count <= 0 || s.item <= 0) continue;
        if (info.input == INPUT_COOKABLE ? recipeOutput(kind, s.item) > 0 : s.item == info.input) return s.item;
    }
    return 0;
}

static int nextPassiveOutput(const Storage* st, const Fuel& fuel, PropKind kind) {
    const PropInfo& info = propInfo(kind);
    if (info.input != INPUT_COOKABLE) return info.item;
    if (fuel.item > 0) {
        if (const int made = recipeOutput(kind, fuel.item); made > 0) return made;
    }
    const int next = nextPassiveInput(st, kind);
    return next > 0 ? recipeOutput(kind, next) : 0;
}

PassiveState passiveStateOf(World& w, ObjectId prop) {
    const Prop* p = w.get<Prop>(prop);
    const Output* out = w.get<Output>(prop);
    const Fuel* fuel = w.get<Fuel>(prop);
    if (!p || !out || !fuel) return PassiveState::NoFuel;
    const Storage* st = w.get<Storage>(prop);
    const int made = nextPassiveOutput(st, *fuel, p->kind);
    if (made > 0 && !slotsCanTake(out->slots.data(), STORAGE_SLOTS, made, 1)) return PassiveState::OutputFull;
    if (fuel->hours > 1e-9) return PassiveState::Burning;
    if (nextPassiveInput(st, p->kind) > 0) return PassiveState::Burning;
    return PassiveState::NoFuel;
}

void passiveCraftSystem(World& w, double gameSeconds) {
    for (ObjectId e : w.view<Prop, Output, Fuel>()) {
        const PropKind kind = w.get<Prop>(e)->kind;
        const PropInfo& info = propInfo(kind);
        if (info.use != PropUse::PassiveCrafting) continue;
        const bool cooks = info.input == INPUT_COOKABLE;
        Output& out = *w.get<Output>(e);
        Fuel& fuel = *w.get<Fuel>(e);
        Storage* st = w.get<Storage>(e);
        PropWork& pw = w.has<PropWork>(e) ? *w.get<PropWork>(e) : w.add(e, PropWork{});
        const double craftHours = PASSIVE_CRAFT_HOURS > 0.0 ? PASSIVE_CRAFT_HOURS : 1.0;
        const double perInput = info.fuelHours > 0.0 ? info.fuelHours : 1.0;
        double hours = std::max(0.0, gameSeconds / 3600.0);
        bool burning = false;
        for (int guard = 0; guard < 100000; ++guard) {
            if (pw.progress >= 1.0 - 1e-9) {
                const int made = cooks ? (fuel.item > 0 ? recipeOutput(kind, fuel.item) : 0) : info.item;
                if (made > 0) {
                    if (!slotsCanTake(out.slots.data(), STORAGE_SLOTS, made, 1)) break;
                    addToSlots(out.slots.data(), STORAGE_SLOTS, made, 1);
                    ++pw.made;
                    queueSoundFrom(w, cooks ? Sfx::DoneCooking : Sfx::DoneForge, e);
                }
                pw.progress = 0.0;
                if (cooks && fuel.hours <= 1e-9) fuel.item = 0;
            }
            const int next = nextPassiveOutput(st, fuel, kind);
            if (next > 0 && !slotsCanTake(out.slots.data(), STORAGE_SLOTS, next, 1)) break;
            if (hours <= 1e-12) {
                burning = fuel.hours > 1e-9 || nextPassiveInput(st, kind) > 0;
                break;
            }
            if (fuel.hours <= 1e-9) {
                fuel.hours = 0.0;
                const int in = nextPassiveInput(st, kind);
                if (in <= 0 || !takeOne(st->slots.data(), STORAGE_SLOTS, in)) break;
                fuel.hours = perInput;
                fuel.item = in;
            }
            const double step = std::min({ hours, fuel.hours, (1.0 - pw.progress) * craftHours });
            if (step <= 0.0) break;
            burning = true;
            hours -= step;
            fuel.hours -= step;
            pw.progress = std::min(1.0, pw.progress + step / craftHours);
        }
        pw.workers = burning ? 1 : 0;
    }
}
