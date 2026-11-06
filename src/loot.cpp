// Looting an Unconscious unit or a corpse, and what a unit may hold.
#include "systems_internal.h"

using namespace systems_detail;

bool issueLootOrder(World& w, Terrain& t, const std::vector<Entity>& units, Entity target) {
    const Transform* tt = w.get<Transform>(target);
    if (!outCold(w, target) || !tt) return false;
    for (Entity u : units) {
        if (u == target || !w.has<Commandable>(u) || !w.has<Inventory>(u) || !w.get<Transform>(u) || cannotTakeOrder(w, u)) continue;
        if (w.has<Mob>(u)) continue;   // a mob takes nothing (mobs.h)
        clearOrders(w, u);
        walkSolo(w, t, u, Vec2{ tt->x, tt->z });
        w.add(u, LootOrder{ target });
        return true;
    }
    return false;
}

// Walking to loot: in reach, the game opens the panel
void systems_detail::lootOrderSystem(World& w, CarryEvents& ev) {
    for (Entity u : w.view<LootOrder, Transform>()) {
        const Entity target = w.get<LootOrder>(u)->target;
        if (!outCold(w, target) || outCold(w, u)) {
            w.remove<LootOrder>(u);
            w.remove<SoloMove>(u);
            continue;
        }
        if (flatGap(w, u, target) <= CARRY_REACH) {
            w.remove<LootOrder>(u);
            w.remove<SoloMove>(u);
            ev.loot.push_back({ u, target });
            continue;
        }
        const SoloMove* so = w.get<SoloMove>(u);
        if (!so || so->next >= so->waypoints.size()) {
            w.remove<LootOrder>(u);
            w.remove<SoloMove>(u);
        }
    }
}

bool canHoldItem(World& w, Entity unit, int item) {
    const Mob* m = w.get<Mob>(unit);
    return !m || mobCanHold(m->kind, item);
}

bool inventoryLocked(World& w, Entity unit) {
    return w.has<Mob>(unit) && !outCold(w, unit);
}

bool lootTake(World& w, Entity looter, Entity target, bool gear, int slot, std::string& why) {
    Inventory* into = w.get<Inventory>(looter);
    if (!into || !outCold(w, target)) return false;
    if (w.has<Mob>(looter)) {   // a mob takes nothing but what it was born with
        why = lang("MISC_MOB_CANT_HOLD");
        return false;
    }
    int item = 0, count = 0;
    if (gear) {
        Equipment* worn = w.get<Equipment>(target);
        if (!worn || slot < 0 || slot >= GEAR_SLOTS || worn->items[slot] == 0) return false;
        item = worn->items[slot];
        count = 1;
        if (addToInventory(*into, item, 1) > 0) {
            why = lang("MISC_LOOT_NO_ROOM");
            return false;
        }
        worn->items[slot] = 0;
    } else {
        Inventory* from = w.get<Inventory>(target);
        if (!from || slot < 0 || slot >= INVENTORY_SLOTS || from->slots[slot].item == 0) return false;
        ItemStack& st = from->slots[slot];
        item = st.item;
        const int left = addToInventory(*into, st.item, st.count);
        count = st.count - left;
        if (count <= 0) {
            why = lang("MISC_LOOT_NO_ROOM");
            return false;
        }
        if (left > 0) st.count = left;
        else st = ItemStack{};
    }
    queueUnitLabel(looter, itemGainLabelFor(count, item));
    queueSound(Sfx::Take);
    // Anything taken out of an Unconscious mob kills it: it goes to 0 health and its Dying completes at once
    if (w.has<Mob>(target) && !w.has<Corpse>(target)) {
        if (Needs* n = w.get<Needs>(target)) {
            n->hp = 0;
            n->changed |= NEEDS_HP_CHANGED;
        }
        queueLog(langf("MISC_MEAT_TAKEN_DIES", { nameOf(w, target) }), LogKind::Other, npcOnly(w, target));
        finishDying(w, target);
        queueSoundFrom(w, Sfx::Dies, target);
    }
    return true;
}

int lootAll(World& w, Entity looter, Entity target) {
    int moved = 0;
    std::string why;
    for (int g = 0; g < GEAR_SLOTS; ++g) moved += lootTake(w, looter, target, true, g, why) ? 1 : 0;
    for (int i = 0; i < INVENTORY_SLOTS; ++i) moved += lootTake(w, looter, target, false, i, why) ? 1 : 0;
    return moved;
}
