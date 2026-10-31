#include "equip.h"
#include "language.h"
#include <algorithm>
#include <map>
#include <vector>
#include "combat.h"
#include "inventory.h"
#include "items.h"

// Dexterity XP for each gear change that goes through: equipping, moving or taking off a piece
constexpr float GEAR_CHANGE_XP = 2.0f;

namespace {

std::string nameOf(int item) {
    const ItemDef* d = findItem(item);
    return d ? itemDisplayName(*d) : std::string(lang("MISC_THAT_ITEM"));
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

std::string gearSlotName(int slot) {
    static const char* const keys[GEAR_SLOTS] = {
        "UILABEL_SLOT_HEAD", "UILABEL_SLOT_TORSO", "UILABEL_SLOT_MAIN_HAND", "UILABEL_SLOT_OFF_HAND",
        "UILABEL_SLOT_LEGS", "UILABEL_SLOT_FEET", "UILABEL_SLOT_ACCESSORY", "UILABEL_SLOT_ACCESSORY",
    };
    return (slot >= 0 && slot < GEAR_SLOTS) ? lang(keys[slot]) : lang("UILABEL_UNKNOWN_SLOT");
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
        why = langf("MISC_NOT_FIT_SLOT", { nameOf(item), gearSlotName(slot) });
        return false;
    }
    if (d->equipSlot == "two_hand" && slot == GEAR_MAIN_HAND && worn.items[GEAR_OFF_HAND] != 0) {
        why = langf("MISC_EMPTY_OFF_HAND", { nameOf(item) });
        return false;
    }
    if (slot == GEAR_OFF_HAND && offHandBlocked(worn)) {
        why = lang("MISC_TWO_HANDED_IN_MAIN");
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
        why = langf("MISC_NO_ROOM_INV", { nameOf(displaced) });
        return false;
    }
    *inv = next;
    worn->items[gearSlot] = item;
    grantXp(w, unit, STAT_DEXTERITY, GEAR_CHANGE_XP, lang("MISC_REASON_GEAR"));
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
        why = langf("MISC_SLOT_HOLDS", { nameOf(worn->items[toSlot]) });
        return false;
    }
    Equipment after = *worn;
    after.items[fromSlot] = 0;
    if (!canEquip(after, item, toSlot, why)) return false;

    worn->items[fromSlot] = 0;
    worn->items[toSlot] = item;
    grantXp(w, unit, STAT_DEXTERITY, GEAR_CHANGE_XP, lang("MISC_REASON_GEAR"));
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
        why = langf("MISC_NO_ROOM_INV", { nameOf(item) });
        return false;
    }
    *inv = next;
    worn->items[gearSlot] = 0;
    grantXp(w, unit, STAT_DEXTERITY, GEAR_CHANGE_XP, lang("MISC_REASON_GEAR"));
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
    queueLog(langf("MISC_DROPS", { nameOf(w, unit), "1", def ? itemDisplayName(*def) : lang("MISC_AN_ITEM") }), LogKind::Drop);
    queueUnitLabel(unit, itemLossLabelFor(1, item), true);
    worn->items[gearSlot] = 0;
    Entity e = w.create();
    w.add(e, Transform{ at.x, at.y, at.z });
    w.add(e, GroundItem{ item, 1 });
    return true;
}

int giveOrDrop(World& w, Entity unit, int item, int count) {
    if (item <= 0 || count <= 0) return 0;
    Inventory* inv = w.get<Inventory>(unit);
    const int left = inv ? addToInventory(*inv, item, count) : count;
    if (left > 0) {
        // No room for the rest: it falls where the unit stands
        if (const Transform* tr = w.get<Transform>(unit)) {
            const Transform at = *tr;   // creating an entity can move the stored components
            queueLog(langf("MISC_DROPS", { nameOf(w, unit), std::to_string(left), nameOf(item) }), LogKind::Drop);
            queueUnitLabel(unit, itemLossLabel(left, nameOf(item)), true);
            const Entity e = w.create();
            w.add(e, Transform{ at.x, at.y, at.z });
            w.add(e, GroundItem{ item, left });
        }
    }
    return count - left;
}

namespace {

// How good an item is in a gear slot: its attack and defense together, then what it is worth. Higher is better.
struct GearScore {
    int stat = 0;
    int value = 0;
    int buy = 0;
};
// The preferences bestGear is working with, for scoreOf
GearPrefs gPrefs;
GearScore scoreOf(int item) {
    GearScore g;
    if (const ItemDef* d = findItem(item)) {
        const int share = std::clamp(gPrefs.attackShare, 0, 100);
        g.stat = d->attackValue * share + d->defenseValue * (100 - share);
        if (gPrefs.valueTies) {   // otherwise a tie stays a tie, and what is worn keeps its place
            g.value = d->sellValue;
            g.buy = d->buyValue;
        }
    }
    return g;
}
GearScore operator+(GearScore a, const GearScore& b) {
    a.stat += b.stat;
    a.value += b.value;
    a.buy += b.buy;
    return a;
}
// -1 if a is worse than b, 1 if better, 0 if they are as good
int compareScore(const GearScore& a, const GearScore& b) {
    if (a.stat != b.stat) return a.stat < b.stat ? -1 : 1;
    if (a.value != b.value) return a.value < b.value ? -1 : 1;
    if (a.buy != b.buy) return a.buy < b.buy ? -1 : 1;
    return 0;
}

// The gear on hand: how many of each gear item the unit wears and carries
using GearCounts = std::map<int, int>;

GearCounts gearOnHand(const Equipment& worn, const Inventory& inv) {
    GearCounts c;
    auto isGear = [](int item) {
        const ItemDef* d = findItem(item);
        return d && !d->equipSlot.empty();
    };
    for (int item : worn.items) {
        if (item != 0 && isGear(item)) ++c[item];
    }
    for (const ItemStack& s : inv.slots) {
        if (s.item != 0 && s.count > 0 && isGear(s.item)) c[s.item] += s.count;
    }
    return c;
}

bool slotIs(int item, const char* equipSlot) {
    const ItemDef* d = findItem(item);
    return d && d->equipSlot == equipSlot;
}

// The best item on hand whose equip_slot is one of these, or 0. A tie goes to the item already in prefer.
int bestOf(const GearCounts& c, std::initializer_list<const char*> kinds, int prefer) {
    int best = 0;
    for (const auto& [item, n] : c) {
        if (n <= 0) continue;
        bool fits = false;
        for (const char* k : kinds) fits = fits || slotIs(item, k);
        if (!fits) continue;
        if (best == 0) {
            best = item;
            continue;
        }
        const int cmp = compareScore(scoreOf(item), scoreOf(best));
        if (cmp > 0 || (cmp == 0 && item == prefer && best != prefer)) best = item;
    }
    return best;
}

} // namespace

Equipment bestGear(const Equipment& worn, const Inventory& inv, const GearPrefs& prefs) {
    gPrefs = prefs;
    GearCounts c = gearOnHand(worn, inv);
    Equipment out{};
    auto take = [&](int item) {
        if (item != 0) --c[item];
    };
    // The body slots, one item kind each
    const std::pair<int, const char*> body[] = {
        { GEAR_HEAD, "head" }, { GEAR_TORSO, "torso" }, { GEAR_LEGS, "legs" }, { GEAR_FEET, "feet" },
    };
    for (const auto& [slot, kind] : body) {
        out.items[slot] = bestOf(c, { kind }, worn.items[slot]);
        take(out.items[slot]);
    }
    // Two accessories, the two best. Each stays in the slot it was already in, if it was. With accessories off, they
    // stay as they are.
    if (prefs.accessories) {
        const int acc1 = bestOf(c, { "accessory" }, worn.items[GEAR_ACCESSORY_1]);
        take(acc1);
        const int acc2 = bestOf(c, { "accessory" }, worn.items[GEAR_ACCESSORY_2]);
        take(acc2);
        out.items[GEAR_ACCESSORY_1] = acc1;
        out.items[GEAR_ACCESSORY_2] = acc2;
        if ((acc1 == worn.items[GEAR_ACCESSORY_2] && acc1 != 0) || (acc2 == worn.items[GEAR_ACCESSORY_1] && acc2 != 0)) {
            std::swap(out.items[GEAR_ACCESSORY_1], out.items[GEAR_ACCESSORY_2]);
        }
    } else {
        out.items[GEAR_ACCESSORY_1] = worn.items[GEAR_ACCESSORY_1];
        out.items[GEAR_ACCESSORY_2] = worn.items[GEAR_ACCESSORY_2];
    }

    // The hands. Every main hand and off hand pair on hand is weighed, an empty hand included, and the best pair is
    // set against the best two-handed weapon. On a tie, the choice that keeps more of the current hands wins.
    std::vector<int> mains{ 0 }, offs{ 0 }, twos;
    for (const auto& [item, n] : c) {
        if (n <= 0) continue;
        if (slotIs(item, "main_hand") || slotIs(item, "either_hand")) mains.push_back(item);
        if (slotIs(item, "off_hand") || slotIs(item, "either_hand")) offs.push_back(item);
        if (slotIs(item, "two_hand") && prefs.twoHanded) twos.push_back(item);
    }
    auto kept = [&](int mainItem, int offItem) {
        return (mainItem == worn.items[GEAR_MAIN_HAND] ? 1 : 0) + (offItem == worn.items[GEAR_OFF_HAND] ? 1 : 0);
    };
    int bestMain = 0, bestOff = 0;
    GearScore bestScore;
    int bestKept = -1;
    auto consider = [&](int mainItem, int offItem) {
        const GearScore sc = scoreOf(mainItem) + scoreOf(offItem);
        const int k = kept(mainItem, offItem);
        const int cmp = compareScore(sc, bestScore);
        if (bestKept < 0 || cmp > 0 || (cmp == 0 && k > bestKept)) {
            bestMain = mainItem;
            bestOff = offItem;
            bestScore = sc;
            bestKept = k;
        }
    };
    for (int m : mains) {
        for (int o : offs) {
            if (m != 0 && m == o && c[m] < 2) continue;   // one item can't be in both hands
            consider(m, o);
        }
    }
    for (int t : twos) consider(t, 0);
    out.items[GEAR_MAIN_HAND] = bestMain;
    out.items[GEAR_OFF_HAND] = bestOff;
    return out;
}

namespace {

// Changes a unit's gear to target, moving what comes off into the inventory and what goes on out of it. All of it is
// tried on a copy of the inventory first: if what comes off doesn't fit, nothing changes.
GearAction wearGear(World& w, Entity unit, const Equipment& target, std::string& why) {
    why.clear();
    Inventory* inv = w.get<Inventory>(unit);
    Equipment* worn = w.get<Equipment>(unit);
    if (!inv || !worn) return GearAction::NoChange;
    if (worn->items == target.items) return GearAction::NoChange;

    // What comes off and what goes on, item by item (a piece that only moves between hands or accessory slots does
    // neither)
    std::map<int, int> was, will;
    for (int item : worn->items) if (item != 0) ++was[item];
    for (int item : target.items) if (item != 0) ++will[item];
    std::vector<int> off, on;
    for (const auto& [item, n] : was) for (int i = will.count(item) ? will[item] : 0; i < n; ++i) off.push_back(item);
    for (const auto& [item, n] : will) for (int i = was.count(item) ? was[item] : 0; i < n; ++i) on.push_back(item);

    Inventory next = *inv;
    for (int item : on) {
        // Out of the inventory: the last stack of it, so the first ones stay where they are
        bool found = false;
        for (int s = INVENTORY_SLOTS - 1; s >= 0 && !found; --s) {
            ItemStack& st = next.slots[(size_t)s];
            if (st.item != item || st.count <= 0) continue;
            if (--st.count <= 0) st = ItemStack{};
            found = true;
        }
        if (!found) return GearAction::NoChange;   // not on hand after all: change nothing
    }
    for (int item : off) {
        if (addToInventory(next, item, 1) > 0) {
            why = lang("MISC_GEAR_NO_ROOM");
            return GearAction::NoRoom;
        }
    }

    // It all fits: make the change. The inventory is set from the copy, and the gear from the target.
    int changed = 0;
    for (int g = 0; g < GEAR_SLOTS; ++g) changed += worn->items[(size_t)g] != target.items[(size_t)g] ? 1 : 0;
    *inv = next;
    worn->items = target.items;
    grantXp(w, unit, STAT_DEXTERITY, GEAR_CHANGE_XP * (float)changed, lang("MISC_REASON_GEAR"));
    return GearAction::Done;
}

} // namespace

GearAction optimizeGear(World& w, Entity unit, std::string& why, const GearPrefs& prefs) {
    const Inventory* inv = w.get<Inventory>(unit);
    const Equipment* worn = w.get<Equipment>(unit);
    if (!inv || !worn) return GearAction::NoChange;
    return wearGear(w, unit, bestGear(*worn, *inv, prefs), why);
}

OptimizeAllResult optimizeAllGear(World& w, const std::vector<Entity>& units, const GearPrefs& prefs) {
    OptimizeAllResult r;
    for (Entity u : units) {
        std::string why;
        switch (optimizeGear(w, u, why, prefs)) {
        case GearAction::Done: ++r.changed; break;
        case GearAction::NoRoom: ++r.noRoom; break;
        case GearAction::NoChange: ++r.same; break;
        }
    }
    return r;
}

GearAction removeAllGear(World& w, Entity unit, std::string& why) {
    return wearGear(w, unit, Equipment{}, why);
}
