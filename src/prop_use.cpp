// Using props: prop orders and arriving at them, work and crafting, storing items, starting work or an unlock,
// and passive crafting.
#include "systems_internal.h"

using namespace systems_detail;

namespace {

// Sends every unit working a prop off it, with a line in the log for each, and clears the prop's progress, so work
// that was cut short counts for nothing
void stopWorkersAt(World& w, Entity prop) {
    for (Entity u : w.view<Working>()) {
        if (w.get<Working>(u)->prop != prop) continue;
        queueLog(langf("MISC_CRAFT_STOPS", { nameOf(w, u) }), LogKind::Other, npcOnly(w, u));
        w.remove<Working>(u);
    }
    if (PropWork* pw = w.get<PropWork>(prop)) {
        pw->progress = 0.0;
        pw->workers = 0;
    }
}

} // namespace

CraftState craftStateOf(World& w, Entity prop) {
    const Prop* p = w.get<Prop>(prop);
    const Storage* st = w.get<Storage>(prop);
    if (!p || propInfo(p->kind).use != PropUse::Crafting) return CraftState::NoInput;
    const PropInfo& info = propInfo(p->kind);
    if (info.input == 0) return CraftState::Ready;   // the water pump and the training dummy need nothing
    if (!st) return CraftState::NoInput;
    for (const ItemStack& stack : st->slots) {
        if (stack.count > 0 && recipeOutput(p->kind, stack.item) > 0) return CraftState::Ready;
    }
    return CraftState::NoInput;
}

bool propInTheWay(World& w, Entity unit, Entity prop) {
    const Transform* a = w.get<Transform>(unit);
    const Transform* b = w.get<Transform>(prop);
    if (!a || !b) return false;
    const float abx = b->x - a->x, abz = b->z - a->z;
    const float len2 = abx * abx + abz * abz;
    for (Entity p : w.view<Prop, Transform>()) {
        if (p == prop) continue;
        const Transform* c = w.get<Transform>(p);
        // The point of the line nearest the other prop's centre
        float t = len2 > 1e-8f ? ((c->x - a->x) * abx + (c->z - a->z) * abz) / len2 : 0.0f;
        t = std::clamp(t, 0.0f, 1.0f);
        const float nx = a->x + abx * t - c->x, nz = a->z + abz * t - c->z;
        if (nx * nx + nz * nz < PROP_BLOCK_RADIUS * PROP_BLOCK_RADIUS) return true;
    }
    return false;
}

Entity propOccupant(World& w, Entity prop) {
    for (Entity u : w.view<Occupying>()) {
        if (w.get<Occupying>(u)->prop == prop) return u;
    }
    return NULL_ENTITY;
}

int propWorkerCount(World& w, Entity prop) {
    int n = workersAt(w, prop);
    for (Entity u : w.view<PropOrder>()) n += w.get<PropOrder>(u)->prop == prop ? 1 : 0;
    return n;
}

void issuePropOrder(World& w, Terrain& t, const std::vector<Entity>& units, Entity prop, PropAction action) {
    const Transform* pt = w.get<Transform>(prop);
    if (!pt || !w.has<Prop>(prop)) return;
    for (Entity u : units) {
        if (!w.has<Commandable>(u) || !w.get<Transform>(u)) continue;
        if (w.has<PlayingDead>(u)) breakFree(w, u);
        if (w.has<Unconscious>(u) || w.has<Carried>(u)) continue;
        if (lockedIn(w, u)) {
            // Locked in: the only order it takes is to unlock the prop it is in, which it does from where it is
            if (action == PropAction::Unlock && occupiedProp(w, u) == prop) w.add(u, PropOrder{ prop, action });
            continue;
        }
        clearOrders(w, u);
        walkSolo(w, t, u, Vec2{ pt->x, pt->z });
        w.add(u, PropOrder{ prop, action });
    }
}

std::vector<PropArrival> propOrderSystem(World& w) {
    std::vector<PropArrival> out;
    // view() returns a copy, so ending an order inside the loop is safe
    for (Entity u : w.view<PropOrder, Transform>()) {
        const Entity prop = w.get<PropOrder>(u)->prop;
        const Transform* pt = w.get<Transform>(prop);
        if (!pt || !w.has<Prop>(prop)) {             // the prop is gone
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
                // An Auto-Haul's step: what the unit carries that the prop takes goes in, if the prop is open to it
                if (!lockShutFor(w, prop, u)) storeCarried(w, u, prop, 0);
                continue;
            }
            PropArrival a;
            a.unit = u;
            a.prop = prop;
            a.action = action;
            a.automatic = automatic;
            const PropInfo& info = propInfo(w.get<Prop>(prop)->kind);
            const Lock* lock = w.get<Lock>(prop);
            if (action == PropAction::Lock) {
                // Lock: shut an open lock again. It stays shut until someone unlocks it. A lock with no owner becomes
                // the locker's faction's. A mob works no locks.
                if (Lock* lk = w.get<Lock>(prop); lk && !lk->locked && !w.has<Mob>(u)) {
                    lk->locked = true;
                    if (lk->faction < 0) {
                        const Faction* f = w.get<Faction>(u);
                        lk->faction = f ? f->id : FACTION_PLAYER;
                    }
                    a.started = true;
                    queueSoundFrom(w, Sfx::LockedIn, prop);
                    queueLog(langf("MISC_LOCKS", { nameOf(w, u), lang(info.nameKey) }), LogKind::Other, npcOnly(w, u));
                    const Entity inside = propOccupant(w, prop);
                    if (inside != NULL_ENTITY && lockedIn(w, inside)) {
                        queueLog(langf("MISC_LOCKED_IN", { nameOf(w, inside), lang(info.nameKey) }), LogKind::Other, npcOnly(w, inside));
                    }
                }
                out.push_back(a);
                continue;
            }
            if (action == PropAction::Unlock && lock && lock->locked && !lockShutFor(w, prop, u) && !w.has<Mob>(u)) {
                // Unlock of a lock that is the unit's own (its faction's, or in its faction's claim): opened with the
                // key, at once, no lockpicking
                w.get<Lock>(prop)->locked = false;
                a.started = true;
                queueSoundFrom(w, Sfx::Unlocked, prop);
                queueLog(langf("MISC_UNLOCKS", { nameOf(w, u), lang(info.nameKey) }), LogKind::Other, npcOnly(w, u));
                out.push_back(a);
                continue;
            }
            if (action == PropAction::Unlock) {
                // Unlock: start picking the lock, if the unit has the skill and nobody else is at it
                a.craft = tryStartUnlock(w, u, prop);
                a.started = a.craft == WorkStart::Started;
                out.push_back(a);
                continue;
            }
            if (lockShutFor(w, prop, u)) {
                // A locked prop can't be opened or used
                a.locked = true;
                out.push_back(a);
                continue;
            }
            if (action == PropAction::Inventory) {
                // Inventory: the game opens the storage or crafting prop's panel
                if (hasStorage(info)) out.push_back(a);
                continue;
            }
            switch (info.use) {
            case PropUse::Storage:
                break;
            case PropUse::Occupy: {
                // Place: the unit places the one it carries on the prop, and stays off it itself. With nobody carried
                // there is nothing to place.
                if (action == PropAction::Place) {
                    const Carrying* cr = w.get<Carrying>(u);
                    if (!cr) break;
                    const Entity carried = cr->unit;
                    const Entity occupant = propOccupant(w, prop);
                    if (w.has<Corpse>(carried)) {
                        a.corpse = true;
                    } else if (propInTheWay(w, u, prop)) {
                        a.unreachable = true;
                    } else if (occupant != NULL_ENTITY) {
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
                        queueLog(langf("MISC_PLACES_IN", { nameOf(w, u), nameOf(w, carried), lang(info.nameKey) }), LogKind::Other, npcOnly(w, u, carried));
                        // A lockable one (the Prisoner Cage) locks behind the unit placed in it
                        if (Lock* lk = w.get<Lock>(prop)) {
                            lk->locked = true;
                            queueLog(langf("MISC_LOCKED_IN", { nameOf(w, carried), lang(info.nameKey) }), LogKind::Other, npcOnly(w, carried));
                        }
                    }
                    break;
                }
                // Occupy. A unit carrying someone is sent to Place instead (the menu offers no Occupy then), but if one
                // gets here anyway it puts the carried unit down where it stands as it occupies the prop.
                // One try, once in reach. Another prop on the straight line to the bed makes it unreachable; another
                // unit already on it makes it taken. Either way the order ends and the unit stays where it is.
                // Otherwise the unit is put on the bed, right on its spot, wherever in reach it stopped.
                const Entity occupant = propOccupant(w, prop);
                if (propInTheWay(w, u, prop)) {
                    a.unreachable = true;
                } else if (occupant != NULL_ENTITY && occupant != u) {
                    a.taken = true;
                } else {
                    // A unit already in it (left in after the lock was opened) does not lock itself in again
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
                    // A lockable one (the Prisoner Cage) locks behind a new unit
                    if (Lock* lk = newOccupant ? w.get<Lock>(prop) : nullptr) {
                        lk->locked = true;
                        queueLog(langf("MISC_LOCKED_IN", { nameOf(w, u), lang(info.nameKey) }), LogKind::Other, npcOnly(w, u));
                    }
                }
                break;
            }
            case PropUse::Resource:
                if (w.has<Mob>(u)) break;   // a mob works no props (mobs.h)
                if (workersAt(w, prop) >= info.workers) {
                    a.taken = true;
                } else {
                    w.add(u, Working{ prop });
                    a.started = true;
                }
                break;
            case PropUse::Crafting:
                // Use: start crafting if the input and output allow it, checked now, at the moment of the try
                a.craft = tryStartWork(w, u, prop);
                a.started = a.craft == WorkStart::Started;
                a.taken = a.craft == WorkStart::Full;
                break;
            case PropUse::PassiveCrafting:   // works by itself: only its Inventory does anything
            case PropUse::None:
                break;
            }
            if (info.use != PropUse::None && info.use != PropUse::Storage && info.use != PropUse::PassiveCrafting) out.push_back(a);
            continue;
        }
        // Stopped short with no route left (the prop can't be reached), so give up
        const SoloMove* so = w.get<SoloMove>(u);
        if (!so || so->next >= so->waypoints.size()) {
            w.remove<PropOrder>(u);
            w.remove<SoloMove>(u);
        }
    }

    // A unit on a bed stays exactly on it: nothing but an order of yours moves it off. If the bed is gone, it gets
    // up. One unit to a bed: should two ever be on one, the first to have been made keeps it and the other gets up.
    // A unit being carried, or on a prop that is no longer one to occupy, is not on it either: its Occupying goes, so
    // it is never left drawn lying on a bed it is not in (Renderer::bedLift).
    std::vector<Entity> lying = w.view<Occupying, Transform>();
    std::sort(lying.begin(), lying.end());
    std::vector<Entity> bedsTaken;
    for (Entity u : lying) {
        const Entity bed = w.get<Occupying>(u)->prop;
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
    // The units working each prop this frame. A downed unit stops working. One pushed out of reach is walking back,
    // so it does not add to the progress until it is in reach again.
    std::map<Entity, std::vector<Entity>> workersOf;
    for (Entity u : w.view<Working, Transform>()) {
        const Working* wk = w.get<Working>(u);
        const Entity prop = wk->prop;
        // Picking a lock that is still shut to this unit, or working a prop that is not
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
    for (Entity e : w.view<PropWork>()) w.get<PropWork>(e)->workers = 0;

    for (auto& [prop, workers] : workersOf) {
        std::sort(workers.begin(), workers.end());
        // Each gain says which kind of prop it came from, for its sound
        const PropKind propKind = w.get<Prop>(prop)->kind;
        auto addGain = [&](WorkGain g) {
            g.propKind = idx(propKind);
            gains.push_back(g);
        };
        const PropInfo& info = propInfo(w.get<Prop>(prop)->kind);
        if (w.get<Working>(workers[0])->unlocking) {
            // Picking the lock: each attempt fills the bar, at the picker's Lockpicking pace, then succeeds with
            // lockpickChance. A failed attempt starts the next one; a success opens the lock and the work is done.
            PropWork& pw = w.has<PropWork>(prop) ? *w.get<PropWork>(prop) : w.add(prop, PropWork{});
            pw.workers = (int)workers.size();
            double perHour = 0.0;
            for (Entity u : workers) {
                perHour += 1.0 / (LOCKPICK_SHARE * workHoursFor(levelOf(statsOf(w, u), StatId::Lockpicking)));
            }
            pw.progress += gameSeconds / 3600.0 * perHour;
            Lock* lock = w.get<Lock>(prop);
            while (pw.progress >= 1.0 && lock->locked) {
                pw.progress -= 1.0;
                ++pw.made;   // the ring hops at the end of each attempt
                const Entity picker = workers[pw.turn % workers.size()];
                ++pw.turn;
                const int level = levelOf(statsOf(w, picker), StatId::Lockpicking);
                if ((double)rng.unit() < lockpickChance(level)) {
                    lock->locked = false;
                    addGain(WorkGain{ picker, 0, WorkGain::Kind::Unlocked });
                    queueLog(langf("MISC_UNLOCKS", { nameOf(w, picker), lang(info.nameKey) }), LogKind::Other, npcOnly(w, picker));
                    grantXp(w, picker, StatId::Lockpicking, LOCKPICK_XP_SUCCESS, lang("MISC_REASON_LOCKPICKING"));
                } else {
                    addGain(WorkGain{ picker, 0, WorkGain::Kind::LockpickFailed });
                    grantXp(w, picker, StatId::Lockpicking, LOCKPICK_XP_FAIL, lang("MISC_REASON_LOCKPICKING"));
                }
            }
            if (!lock->locked) {
                for (Entity u : workers) w.remove<Working>(u);
                pw.progress = 0.0;
                pw.workers = 0;
            }
            continue;
        }
        if (propKind == PropKind::Debris) {
            // Debris: DEBRIS_CLEAR_HOURS of work, shared among those at it, whatever their skill, and it is gone
            PropWork& pw = w.has<PropWork>(prop) ? *w.get<PropWork>(prop) : w.add(prop, PropWork{});
            pw.workers = (int)workers.size();
            pw.progress += gameSeconds / 3600.0 * (double)workers.size() / DEBRIS_CLEAR_HOURS;
            if (pw.progress >= 1.0) {
                for (Entity u : workers) {
                    w.remove<Working>(u);
                    grantXp(w, u, StatId::Strength, DEBRIS_CLEAR_XP, w.has<Commandable>(u) ? lang("MISC_REASON_CLEARING") : std::string());
                }
                if (w.has<Commandable>(workers[0])) queueLog(langf("MISC_CLEARS_DEBRIS", { nameOf(w, workers[0]) }), LogKind::Other);
                queueSoundFrom(w, Sfx::Drop, prop);
                w.destroy(prop);
            }
            continue;
        }
        const bool crafting = info.use == PropUse::Crafting;
        // A crafting prop with none of its input left sends its workers off
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

        // Each worker adds one item per workHoursFor(its level) in-game hours, sped up at a resource prop
        double perHour = 0.0;
        for (Entity u : workers) perHour += resourceSpeedFor(w.get<Prop>(prop)->kind) / workHoursFor(levelOf(statsOf(w, u), info.stat));
        pw.progress += gameSeconds / 3600.0 * perHour;

        while (pw.progress >= 1.0 && !workers.empty()) {
            if (crafting) {
                // What this item is made from and into: the first input in the prop's slots that it has a recipe for
                // (a campfire cooks each kind of raw meat into its own cooked meat), or nothing for a prop that needs
                // no input
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
                // A finished item. The input is checked at this moment: if it is gone, nothing is made and the work
                // stops. The item goes to the next worker in turn that has room for it. If none has, nothing is used
                // up or made: the bar stays full, the workers stop, and the next unit with room to work here finishes it.
                if (craftStateOf(w, prop) != CraftState::Ready) {
                    stopAll();
                    break;
                }
                // A prop that makes nothing (the training dummy) gives the next worker in turn its XP
                Entity got = outItem <= 0 ? workers[pw.turn % workers.size()] : NULL_ENTITY;
                for (size_t k = 0; k < workers.size() && got == NULL_ENTITY; ++k) {
                    const Entity cand = workers[(pw.turn + k) % workers.size()];
                    const Inventory* inv = w.get<Inventory>(cand);
                    if (inv && slotsCanTake(inv->slots.data(), INVENTORY_SLOTS, outItem, 1)) got = cand;
                }
                if (got == NULL_ENTITY) {
                    for (Entity u : workers) {
                        queueLog(langf("MISC_GATHER_NO_ROOM", { nameOf(w, u), outName }), LogKind::Other, npcOnly(w, u));
                        w.remove<Working>(u);
                    }
                    workers.clear();
                    pw.progress = 1.0;   // held full until someone with room works it
                    break;
                }
                if (info.input != 0) {   // the water pump and the training dummy use nothing up
                    Storage* st = w.get<Storage>(prop);
                    if (!st || inItem <= 0 || !takeOne(st->slots.data(), STORAGE_SLOTS, inItem)) {
                        stopAll();
                        break;
                    }
                }
                pw.progress -= 1.0;
                ++pw.made;   // the ring hops for it
                ++pw.turn;
                if (outItem > 0) {
                    giveOrDrop(w, got, outItem, 1);   // checked to fit above; should it not, it falls at the cook's feet
                    addGain(WorkGain{ got, outItem });
                    queueLog(langf("MISC_CRAFTS", { nameOf(w, got), outName }), LogKind::Pickup, npcOnly(w, got));
                    grantXp(w, got, info.stat, WORK_XP_PER_ITEM, lang("MISC_REASON_CRAFTING"));
                } else {
                    // Training: no item, only the skill's XP
                    addGain(WorkGain{ got, idx(info.stat), WorkGain::Kind::Trained, WORK_XP_PER_ITEM });
                    grantXp(w, got, info.stat, WORK_XP_PER_ITEM, lang("MISC_REASON_TRAINING"));
                }
                if (craftStateOf(w, prop) != CraftState::Ready) {
                    stopAll();   // that was the last of the input
                    break;
                }
                continue;
            }
            pw.progress -= 1.0;
            ++pw.made;   // finished, whatever becomes of the item: the ring hops for it
            const size_t pick = pw.turn % workers.size();
            ++pw.turn;
            const Entity got = workers[pick];
            // Now and then a resource prop gives its other item instead (a Poison Mushroom, one time in ten)
            int gives = info.item;
            if (info.altItem > 0 && info.altOneIn > 0 && rng.range(1, info.altOneIn) == 1) gives = info.altItem;
            const std::string itemName = itemNameOf(gives);
            Inventory* inv = w.get<Inventory>(got);
            if (inv && addToInventory(*inv, gives, 1) == 0) {
                // A unit that is not yours (a camp's woodcutter) gathers quietly: no label, log line or sound
                const bool yours = w.has<Commandable>(got);
                if (yours) {
                    addGain(WorkGain{ got, gives });
                    queueLog(langf("MISC_GATHERS", { nameOf(w, got), itemName }), LogKind::Pickup, npcOnly(w, got));
                }
                grantXp(w, got, info.stat, WORK_XP_PER_ITEM, yours ? lang("MISC_REASON_GATHERING") : std::string());
            } else {
                // No room: the item falls at the unit's feet, and the unit stops working
                if (const Transform* ut = w.get<Transform>(got)) {
                    const Entity drop = w.create();
                    w.add(drop, Transform{ ut->x, ut->y, ut->z });
                    w.add(drop, GroundItem{ gives, 1 });
                }
                queueLog(langf("MISC_GATHER_NO_ROOM", { nameOf(w, got), itemName }), LogKind::Drop, npcOnly(w, got));
                queueUnitLabel(got, itemLossLabel(1, itemName), true);   // it fell at the unit's feet
                queueSoundFrom(w, Sfx::Drop, got);
                w.remove<Working>(got);
                workers.erase(workers.begin() + (long)pick);
            }
        }
        pw.workers = (int)workers.size();
        // A resource prop left with no workers keeps what it had, short of a whole item. A crafting prop may be left
        // full, waiting for a unit with room.
        if (workers.empty() && !crafting) pw.progress = std::min(pw.progress, 0.999);
    }
    return gains;
}

int storeCarried(World& w, Entity unit, Entity prop, int item) {
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
            queueLog(langf("MISC_AUTO_HAUL_STORES", { nameOf(w, unit), std::to_string(moved), itemNameOf(movedItem),
                                                    lang(propInfo(p->kind).nameKey) }), LogKind::Pickup, npcOnly(w, unit));
        }
    }
    return moved;
}

WorkStart tryStartWork(World& w, Entity unit, Entity prop) {
    const Prop* p = w.get<Prop>(prop);
    if (!p || !isWorkProp(propInfo(p->kind).use) || !w.get<Transform>(unit)) return WorkStart::TooFar;
    if (w.has<Mob>(unit)) return WorkStart::TooFar;   // a mob works no props (mobs.h)
    if (const Working* wk = w.get<Working>(unit); wk && wk->prop == prop) return WorkStart::Started;   // already at it
    if (flatGap(w, unit, prop) > STORAGE_CLOSE_RANGE) return WorkStart::TooFar;
    if (const Needs* n = w.get<Needs>(unit); n && n->hp <= 0) return WorkStart::TooFar;
    if (lockShutFor(w, prop, unit)) return WorkStart::Locked;
    if (lockedIn(w, unit)) return WorkStart::Locked;   // shut in a cage, it can't get to work
    if (workersAt(w, prop) >= propInfo(p->kind).workers) return WorkStart::Full;
    if (propInfo(p->kind).use == PropUse::Crafting) {
        const CraftState cs = craftStateOf(w, prop);
        if (cs == CraftState::NoInput) return WorkStart::NoInput;
    }
    clearOrders(w, unit);
    w.add(unit, Working{ prop });
    return WorkStart::Started;
}

WorkStart tryStartUnlock(World& w, Entity unit, Entity prop) {
    const Lock* lock = w.get<Lock>(prop);
    if (!lock || !w.has<Prop>(prop) || !w.get<Transform>(unit)) return WorkStart::NotLocked;
    if (w.has<Mob>(unit)) return WorkStart::SkillTooLow;   // a mob has no Lockpicking (mobs.h)
    if (!lockShutFor(w, prop, unit)) return WorkStart::NotLocked;
    if (const Working* wk = w.get<Working>(unit); wk && wk->prop == prop && wk->unlocking) return WorkStart::Started;
    if (flatGap(w, unit, prop) > STORAGE_CLOSE_RANGE) return WorkStart::TooFar;
    if (const Needs* n = w.get<Needs>(unit); n && n->hp <= 0) return WorkStart::TooFar;
    if (levelOf(statsOf(w, unit), StatId::Lockpicking) < lock->level) return WorkStart::SkillTooLow;
    if (workersAt(w, prop) >= 1) return WorkStart::Full;   // one unit at a lock at a time
    const bool inside = occupiedProp(w, unit) == prop;     // picking the cage it is shut in, from the inside
    if (!inside && lockedIn(w, unit)) return WorkStart::Locked;
    w.remove<ActiveSquadMember>(unit);
    w.remove<SoloMove>(unit);
    cancelPickup(w, unit);
    w.remove<AttackOrder>(unit);
    w.remove<TalkOrder>(unit);
    // A caught lockpick: one of your units picking another faction's lock is an attack on that faction, and costs
    // its standing the way knocking one of its units out does
    if (const Faction* f = w.get<Faction>(unit); (!f || f->id == FACTION_PLAYER) && lock->faction >= 0 &&
                                                 lock->faction != FACTION_PLAYER) {
        const int now = changePlayerReputation(lock->faction, -REP_LOCKPICK_PENALTY);
        queueLog(langf("MISC_REPUTATION_DROPS", { factionName(lock->faction), std::to_string(REP_LOCKPICK_PENALTY),
                                                  std::to_string(now) }), LogKind::Other);
    }
    if (inside) {
        // It stays in the cage while it works at the lock, and after the lock opens, until it is ordered out
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

void propStorageChanged(World& w, Entity prop) {
    const Prop* p = w.get<Prop>(prop);
    if (!p || propInfo(p->kind).use != PropUse::Crafting) return;
    if (craftStateOf(w, prop) != CraftState::Ready) stopWorkersAt(w, prop);
}

bool propAcceptsItem(PropKind kind, int item) {
    const PropInfo& info = propInfo(kind);
    if (item <= 0) return false;
    if (info.use == PropUse::PassiveCrafting && info.item == 0) return item == info.input;   // a Vault: its upkeep only
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

Entity occupiedProp(World& w, Entity u) {
    const Occupying* oc = w.get<Occupying>(u);
    return oc ? oc->prop : NULL_ENTITY;
}

bool lockedIn(World& w, Entity u) {
    const Occupying* oc = w.get<Occupying>(u);
    if (!oc) return false;
    if (lockShutFor(w, oc->prop, u)) return true;
    // A mob can't work a lock, even its own faction's: in a locked cage it stays in
    const Lock* lk = w.get<Lock>(oc->prop);
    return lk && lk->locked && w.has<Mob>(u);
}

// The first item in a passive crafting prop's input that it burns: its input item, or at a campfire any raw meat it
// can cook. 0 when there is none.
static int nextPassiveInput(const Storage* st, PropKind kind) {
    if (!st) return 0;
    const PropInfo& info = propInfo(kind);
    for (const ItemStack& s : st->slots) {
        if (s.count <= 0 || s.item <= 0) continue;
        if (info.input == INPUT_COOKABLE ? recipeOutput(kind, s.item) > 0 : s.item == info.input) return s.item;
    }
    return 0;
}

// What a passive crafting prop's next finished item is: its item, or at a campfire the burning meat cooked (or, with
// nothing burning, the next meat in its input cooked). 0 for a Vault, and for a campfire with nothing to cook.
static int nextPassiveOutput(const Storage* st, const Fuel& fuel, PropKind kind) {
    const PropInfo& info = propInfo(kind);
    if (info.input != INPUT_COOKABLE) return info.item;
    if (fuel.item > 0) {
        if (const int made = recipeOutput(kind, fuel.item); made > 0) return made;
    }
    const int next = nextPassiveInput(st, kind);
    return next > 0 ? recipeOutput(kind, next) : 0;
}

PassiveState passiveStateOf(World& w, Entity prop) {
    const Prop* p = w.get<Prop>(prop);
    const Output* out = w.get<Output>(prop);
    const Fuel* fuel = w.get<Fuel>(prop);
    if (!p || !out || !fuel) return PassiveState::NoFuel;
    const Storage* st = w.get<Storage>(prop);
    const int made = nextPassiveOutput(st, *fuel, p->kind);
    if (made > 0 && !slotsCanTake(out->slots.data(), STORAGE_SLOTS, made, 1)) return PassiveState::OutputFull;
    if (fuel->hours > 1e-9) return PassiveState::Burning;
    if (nextPassiveInput(st, p->kind) > 0) return PassiveState::Burning;   // it lights the next one now
    return PassiveState::NoFuel;
}

void passiveCraftSystem(World& w, double gameSeconds) {
    for (Entity e : w.view<Prop, Output, Fuel>()) {
        const PropKind kind = w.get<Prop>(e)->kind;
        const PropInfo& info = propInfo(kind);
        if (info.use != PropUse::PassiveCrafting) continue;
        const bool cooks = info.input == INPUT_COOKABLE;   // the campfire: what it makes is the meat it burns, cooked
        Output& out = *w.get<Output>(e);
        Fuel& fuel = *w.get<Fuel>(e);
        Storage* st = w.get<Storage>(e);
        PropWork& pw = w.has<PropWork>(e) ? *w.get<PropWork>(e) : w.add(e, PropWork{});
        const double craftHours = PASSIVE_CRAFT_HOURS > 0.0 ? PASSIVE_CRAFT_HOURS : 1.0;
        const double perInput = info.fuelHours > 0.0 ? info.fuelHours : 1.0;
        double hours = std::max(0.0, gameSeconds / 3600.0);
        bool burning = false;
        // A long jump of the clock can finish several items, so this goes round until the time is used up or it stops
        for (int guard = 0; guard < 100000; ++guard) {
            // A finished item goes into the output, if it has room. If not, it waits there, finished.
            // A prop with no output item (a Vault's upkeep) finishes nothing: its hour of fuel is all it does.
            if (pw.progress >= 1.0 - 1e-9) {
                // A campfire makes the meat that burned cooked; a forge its item
                const int made = cooks ? (fuel.item > 0 ? recipeOutput(kind, fuel.item) : 0) : info.item;
                if (made > 0) {
                    if (!slotsCanTake(out.slots.data(), STORAGE_SLOTS, made, 1)) break;
                    addToSlots(out.slots.data(), STORAGE_SLOTS, made, 1);
                    ++pw.made;   // the ring hops for it
                    queueSoundFrom(w, cooks ? Sfx::DoneCooking : Sfx::DoneForge, e);
                }
                pw.progress = 0.0;
                if (cooks && fuel.hours <= 1e-9) fuel.item = 0;   // that meat is done
            }
            // No room for the next item: pause, burning nothing
            const int next = nextPassiveOutput(st, fuel, kind);
            if (next > 0 && !slotsCanTake(out.slots.data(), STORAGE_SLOTS, next, 1)) break;
            if (hours <= 1e-12) {
                burning = fuel.hours > 1e-9 || nextPassiveInput(st, kind) > 0;
                break;
            }
            // Out of fuel: the next input item is used up, and burns for its hours
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
        pw.workers = burning ? 1 : 0;   // the ring shows while it burns
    }
}
