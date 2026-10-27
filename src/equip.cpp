#include "equip.h"
#include <algorithm>
#include <vector>
#include "combat.h"
#include "inventory.h"
#include "items.h"

// Dexterity XP for each gear change that goes through: equipping, moving or taking off a piece
constexpr float GEAR_CHANGE_XP = 2.0f;

namespace {

std::string nameOf(int item) {
    const ItemDef* d = findItem(item);
    return d ? d->name : std::string("That");
}

// The gear slots an item fits, from its equip_slot
std::vector<int> fittingSlots(const ItemDef& d) {
    const std::string& s = d.equipSlot;
    if (s == "head") return { GEAR_HEAD };
    if (s == "torso") return { GEAR_TORSO };
    if (s == "legs") return { GEAR_LEGS };
    if (s == "feet") return { GEAR_FEET };
    if (s == "accessory") return { GEAR_ACCESSORY_1, GEAR_ACCESSORY_2 };
    if (s == "main_hand" || s == "two_hand") return { GEAR_MAIN_HAND };
    if (s == "off_hand") return { GEAR_OFF_HAND };
    if (s == "either_hand") return { GEAR_MAIN_HAND, GEAR_OFF_HAND };
    return {};
}

bool isTwoHanded(int item) {
    const ItemDef* d = findItem(item);
    return d && d->equipSlot == "two_hand";
}

} // namespace

const char* gearSlotName(int slot) {
    static const char* const names[GEAR_SLOTS] = {
        "Head", "Torso", "Main hand", "Off hand", "Legs", "Feet", "Accessory", "Accessory",
    };
    return (slot >= 0 && slot < GEAR_SLOTS) ? names[slot] : "Unknown";
}

bool offHandBlocked(const Equipment& worn) {
    return isTwoHanded(worn.items[GEAR_MAIN_HAND]);
}

bool canEquip(const Equipment& worn, int item, int slot, std::string& why) {
    why.clear();
    const ItemDef* d = findItem(item);
    if (!d || d->equipSlot.empty()) {
        why = nameOf(item) + " can't be worn or held";
        return false;
    }
    const std::vector<int> fits = fittingSlots(*d);
    if (slot < 0 || slot >= GEAR_SLOTS || std::find(fits.begin(), fits.end(), slot) == fits.end()) {
        why = nameOf(item) + " doesn't fit the " + gearSlotName(slot) + " slot";
        return false;
    }
    if (d->equipSlot == "two_hand" && slot == GEAR_MAIN_HAND && worn.items[GEAR_OFF_HAND] != 0) {
        why = "Empty the off hand before holding " + nameOf(item);
        return false;
    }
    if (slot == GEAR_OFF_HAND && offHandBlocked(worn)) {
        why = "A two-handed weapon is in the main hand";
        return false;
    }
    return true;
}

int gearSlotFor(const Equipment& worn, int item) {
    const ItemDef* d = findItem(item);
    if (!d) return -1;
    const std::vector<int> fits = fittingSlots(*d);
    for (int slot : fits) {
        std::string why;
        if (worn.items[slot] == 0 && canEquip(worn, item, slot, why)) return slot;
    }
    return fits.empty() ? -1 : fits.front();
}

std::vector<int> comparisonSlots(int item) {
    const ItemDef* d = findItem(item);
    if (!d || d->equipSlot.empty()) return {};
    const std::vector<int> fits = fittingSlots(*d);
    auto fitsSlot = [&](int slot) { return std::find(fits.begin(), fits.end(), slot) != fits.end(); };
    if (fitsSlot(GEAR_MAIN_HAND) || fitsSlot(GEAR_OFF_HAND)) return { GEAR_MAIN_HAND, GEAR_OFF_HAND };
    if (fitsSlot(GEAR_ACCESSORY_1)) return { GEAR_ACCESSORY_1, GEAR_ACCESSORY_2 };
    return fits;
}

std::vector<int> displacedSlots(const Equipment& worn, int item) {
    const ItemDef* d = findItem(item);
    if (!d || d->equipSlot.empty()) return {};
    // A two-handed weapon needs both hands empty first
    if (d->equipSlot == "two_hand") return { GEAR_MAIN_HAND, GEAR_OFF_HAND };
    const int slot = gearSlotFor(worn, item);
    if (slot < 0) return {};
    return { slot };
}

void gearTotals(const Equipment& worn, int& attack, int& defense) {
    attack = 0;
    defense = 0;
    for (int item : worn.items) {
        const ItemDef* d = findItem(item);
        if (!d) continue;
        attack += d->attackValue;
        defense += d->defenseValue;
    }
}

bool equipFromInventory(World& w, Entity unit, int invSlot, int gearSlot, std::string& why) {
    why.clear();
    Inventory* inv = w.get<Inventory>(unit);
    Equipment* worn = w.get<Equipment>(unit);
    if (!inv || !worn || invSlot < 0 || invSlot >= INVENTORY_SLOTS) return false;
    if (gearSlot < 0 || gearSlot >= GEAR_SLOTS) return false;

    const int item = inv->slots[invSlot].item;
    if (item == 0 || inv->slots[invSlot].count <= 0) return false;
    if (!canEquip(*worn, item, gearSlot, why)) return false;

    // Work on a copy, so a full inventory leaves everything as it was
    const int displaced = worn->items[gearSlot];
    Inventory next = *inv;
    ItemStack& taken = next.slots[invSlot];
    if (--taken.count <= 0) taken = ItemStack{};
    if (displaced != 0 && addToInventory(next, displaced, 1) > 0) {
        why = "No room in the inventory for " + nameOf(displaced);
        return false;
    }
    *inv = next;
    worn->items[gearSlot] = item;
    grantXp(w, unit, STAT_DEXTERITY, GEAR_CHANGE_XP, "changing gear");
    return true;
}

bool moveEquipped(World& w, Entity unit, int fromSlot, int toSlot, std::string& why) {
    why.clear();
    Equipment* worn = w.get<Equipment>(unit);
    if (!worn || fromSlot == toSlot) return false;
    if (fromSlot < 0 || fromSlot >= GEAR_SLOTS || toSlot < 0 || toSlot >= GEAR_SLOTS) return false;

    const int item = worn->items[fromSlot];
    if (item == 0) return false;
    if (worn->items[toSlot] != 0) {
        why = "That slot already holds " + nameOf(worn->items[toSlot]);
        return false;
    }
    Equipment after = *worn;
    after.items[fromSlot] = 0;
    if (!canEquip(after, item, toSlot, why)) return false;

    worn->items[fromSlot] = 0;
    worn->items[toSlot] = item;
    grantXp(w, unit, STAT_DEXTERITY, GEAR_CHANGE_XP, "changing gear");
    return true;
}

bool unequipToInventory(World& w, Entity unit, int gearSlot, std::string& why) {
    why.clear();
    Inventory* inv = w.get<Inventory>(unit);
    Equipment* worn = w.get<Equipment>(unit);
    if (!inv || !worn || gearSlot < 0 || gearSlot >= GEAR_SLOTS) return false;

    const int item = worn->items[gearSlot];
    if (item == 0) return false;
    Inventory next = *inv;
    if (addToInventory(next, item, 1) > 0) {
        why = "No room in the inventory for " + nameOf(item);
        return false;
    }
    *inv = next;
    worn->items[gearSlot] = 0;
    grantXp(w, unit, STAT_DEXTERITY, GEAR_CHANGE_XP, "changing gear");
    return true;
}

bool dropEquipped(World& w, Entity unit, int gearSlot) {
    Equipment* worn = w.get<Equipment>(unit);
    const Transform* tr = w.get<Transform>(unit);
    if (!worn || !tr || gearSlot < 0 || gearSlot >= GEAR_SLOTS) return false;

    const int item = worn->items[gearSlot];
    if (item == 0) return false;
    // Copy what's needed before creating an entity, which can move the stored components
    const Transform at = *tr;
    const ItemDef* def = findItem(item);
    queueLog(nameOf(w, unit) + " drops " + (def ? def->name : std::string("an item")), LogKind::Drop);
    worn->items[gearSlot] = 0;
    Entity e = w.create();
    w.add(e, Transform{ at.x, at.y, at.z });
    w.add(e, GroundItem{ item, 1 });
    return true;
}
