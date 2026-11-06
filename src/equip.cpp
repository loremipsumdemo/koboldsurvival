#include "equip.h"
#include "language.h"
#include <algorithm>
#include <map>
#include <vector>
#include "combat.h"
#include "inventory.h"
#include "items.h"

constexpr float GEAR_CHANGE_XP = 2.0f;

namespace {
std::vector<int> fittingSlots(const ItemDef& d) {
    const std::string& s = d.equipSlot;
    if (s == "head") return { idx(GearSlot::Head) };
    if (s == "torso") return { idx(GearSlot::Torso) };
    if (s == "legs") return { idx(GearSlot::Legs) };
    if (s == "feet") return { idx(GearSlot::Feet) };
    if (s == "accessory") return { idx(GearSlot::Accessory1), idx(GearSlot::Accessory2) };
    if (s == "main_hand" || s == "two_hand") return { idx(GearSlot::MainHand) };
    if (s == "off_hand") return { idx(GearSlot::OffHand) };
    if (s == "either_hand") return { idx(GearSlot::MainHand), idx(GearSlot::OffHand) };
    return {};
}

bool isTwoHanded(int item) {
    const ItemDef* d = findItem(item);
    return d && d->equipSlot == "two_hand";
}
}

std::string gearSlotName(int slot) {
    static const char* const keys[GEAR_SLOTS] = {
        "UILABEL_SLOT_HEAD", "UILABEL_SLOT_TORSO", "UILABEL_SLOT_MAIN_HAND", "UILABEL_SLOT_OFF_HAND",
        "UILABEL_SLOT_LEGS", "UILABEL_SLOT_FEET", "UILABEL_SLOT_ACCESSORY", "UILABEL_SLOT_ACCESSORY",
    };
    return (slot >= 0 && slot < GEAR_SLOTS) ? lang(keys[slot]) : lang("UILABEL_UNKNOWN_SLOT");
}

bool offHandBlocked(const Equipment& worn) {
    return isTwoHanded(worn.items[GearSlot::MainHand]);
}

bool canEquip(const Equipment& worn, int item, int slot, std::string& why) {
    why.clear();
    const ItemDef* d = findItem(item);
    if (!d || d->equipSlot.empty()) {
        why = itemNameOf(item) + " can't be worn or held";
        return false;
    }
    const std::vector<int> fits = fittingSlots(*d);
    if (slot < 0 || slot >= GEAR_SLOTS || std::find(fits.begin(), fits.end(), slot) == fits.end()) {
        why = langf("MISC_NOT_FIT_SLOT", { itemNameOf(item), gearSlotName(slot) });
        return false;
    }
    if (d->equipSlot == "two_hand" && slot == idx(GearSlot::MainHand) && worn.items[GearSlot::OffHand] != 0) {
        why = langf("MISC_EMPTY_OFF_HAND", { itemNameOf(item) });
        return false;
    }
    if (slot == idx(GearSlot::OffHand) && offHandBlocked(worn)) {
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
    if (fitsSlot(idx(GearSlot::MainHand)) || fitsSlot(idx(GearSlot::OffHand))) return { idx(GearSlot::MainHand), idx(GearSlot::OffHand) };
    if (fitsSlot(idx(GearSlot::Accessory1))) return { idx(GearSlot::Accessory1), idx(GearSlot::Accessory2) };
    return fits;
}

std::vector<int> displacedSlots(const Equipment& worn, int item) {
    const ItemDef* d = findItem(item);
    if (!d || d->equipSlot.empty()) return {};
    if (d->equipSlot == "two_hand") return { idx(GearSlot::MainHand), idx(GearSlot::OffHand) };
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
    if (w.has<Mob>(unit)) {
        why = lang("MISC_MOB_NO_GEAR");
        return false;
    }
    Inventory* inv = w.get<Inventory>(unit);
    Equipment* worn = w.get<Equipment>(unit);
    if (!inv || !worn || invSlot < 0 || invSlot >= INVENTORY_SLOTS) return false;
    if (gearSlot < 0 || gearSlot >= GEAR_SLOTS) return false;

    const int item = inv->slots[invSlot].item;
    if (item == 0 || inv->slots[invSlot].count <= 0) return false;
    if (!canEquip(*worn, item, gearSlot, why)) return false;

    const int displaced = worn->items[gearSlot];
    Inventory next = *inv;
    ItemStack& taken = next.slots[invSlot];
    if (--taken.count <= 0) taken = ItemStack{};
    if (displaced != 0 && addToInventory(next, displaced, 1) > 0) {
        why = langf("MISC_NO_ROOM_INV", { itemNameOf(displaced) });
        return false;
    }
    *inv = next;
    worn->items[gearSlot] = item;
    queueSound(w, Sfx::Equip);
    grantXp(w, unit, StatId::Dexterity, GEAR_CHANGE_XP, "MISC_REASON_GEAR");
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
        why = langf("MISC_SLOT_HOLDS", { itemNameOf(worn->items[toSlot]) });
        return false;
    }
    Equipment after = *worn;
    after.items[fromSlot] = 0;
    if (!canEquip(after, item, toSlot, why)) return false;

    worn->items[fromSlot] = 0;
    worn->items[toSlot] = item;
    queueSound(w, Sfx::Equip);
    grantXp(w, unit, StatId::Dexterity, GEAR_CHANGE_XP, "MISC_REASON_GEAR");
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
        why = langf("MISC_NO_ROOM_INV", { itemNameOf(item) });
        return false;
    }
    *inv = next;
    worn->items[gearSlot] = 0;
    queueSound(w, Sfx::Unequip);
    grantXp(w, unit, StatId::Dexterity, GEAR_CHANGE_XP, "MISC_REASON_GEAR");
    return true;
}

void dropAtUnit(World& w, Entity unit, int item, int count) {
    const Transform* tr = w.get<Transform>(unit);
    if (!tr) return;
    queueLog(w, langf("MISC_DROPS", { nameOf(w, unit), std::to_string(count), itemNameOf(item) }), LogKind::Drop,
             npcOnly(w, unit));
    queueUnitLabel(w, unit, itemLossLabelFor(count, item), true);
    queueSound(w, Sfx::Drop);
    spawnGroundItem(w, *tr, item, count);
}

bool dropEquipped(World& w, Entity unit, int gearSlot) {
    Equipment* worn = w.get<Equipment>(unit);
    const Transform* tr = w.get<Transform>(unit);
    if (!worn || !tr || gearSlot < 0 || gearSlot >= GEAR_SLOTS) return false;

    const int item = worn->items[gearSlot];
    if (item == 0) return false;
    worn->items[gearSlot] = 0;
    dropAtUnit(w, unit, item, 1);
    return true;
}

int giveOrDrop(World& w, Entity unit, int item, int count) {
    if (item <= 0 || count <= 0) return 0;
    Inventory* inv = w.get<Inventory>(unit);
    const int left = inv ? addToInventory(*inv, item, count) : count;
    if (left > 0) dropAtUnit(w, unit, item, left);
    return count - left;
}

namespace {
struct GearScore {
    int stat = 0;
    int value = 0;
    int buy = 0;
};
GearPrefs gPrefs;
GearScore scoreOf(int item) {
    GearScore g;
    if (const ItemDef* d = findItem(item)) {
        const int share = std::clamp(gPrefs.attackShare, 0, 100);
        g.stat = d->attackValue * share + d->defenseValue * (100 - share);
        if (gPrefs.valueTies) {
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
int compareScore(const GearScore& a, const GearScore& b) {
    if (a.stat != b.stat) return a.stat < b.stat ? -1 : 1;
    if (a.value != b.value) return a.value < b.value ? -1 : 1;
    if (a.buy != b.buy) return a.buy < b.buy ? -1 : 1;
    return 0;
}

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
}

Equipment bestGear(const Equipment& worn, const Inventory& inv, const GearPrefs& prefs) {
    gPrefs = prefs;
    GearCounts c = gearOnHand(worn, inv);
    Equipment out{};
    auto take = [&](int item) {
        if (item != 0) --c[item];
    };
    const std::pair<int, const char*> body[] = {
        { idx(GearSlot::Head), "head" }, { idx(GearSlot::Torso), "torso" }, { idx(GearSlot::Legs), "legs" }, { idx(GearSlot::Feet), "feet" },
    };
    for (const auto& [slot, kind] : body) {
        out.items[slot] = bestOf(c, { kind }, worn.items[slot]);
        take(out.items[slot]);
    }
    if (prefs.accessories) {
        const int acc1 = bestOf(c, { "accessory" }, worn.items[GearSlot::Accessory1]);
        take(acc1);
        const int acc2 = bestOf(c, { "accessory" }, worn.items[GearSlot::Accessory2]);
        take(acc2);
        out.items[GearSlot::Accessory1] = acc1;
        out.items[GearSlot::Accessory2] = acc2;
        if ((acc1 == worn.items[GearSlot::Accessory2] && acc1 != 0) || (acc2 == worn.items[GearSlot::Accessory1] && acc2 != 0)) {
            std::swap(out.items[GearSlot::Accessory1], out.items[GearSlot::Accessory2]);
        }
    } else {
        out.items[GearSlot::Accessory1] = worn.items[GearSlot::Accessory1];
        out.items[GearSlot::Accessory2] = worn.items[GearSlot::Accessory2];
    }

    std::vector<int> mains{ 0 }, offs{ 0 }, twos;
    for (const auto& [item, n] : c) {
        if (n <= 0) continue;
        if (slotIs(item, "main_hand") || slotIs(item, "either_hand")) mains.push_back(item);
        if (slotIs(item, "off_hand") || slotIs(item, "either_hand")) offs.push_back(item);
        if (slotIs(item, "two_hand") && prefs.twoHanded) twos.push_back(item);
    }
    auto kept = [&](int mainItem, int offItem) {
        return (mainItem == worn.items[GearSlot::MainHand] ? 1 : 0) + (offItem == worn.items[GearSlot::OffHand] ? 1 : 0);
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
            if (m != 0 && m == o && c[m] < 2) continue;
            consider(m, o);
        }
    }
    for (int t : twos) consider(t, 0);
    out.items[GearSlot::MainHand] = bestMain;
    out.items[GearSlot::OffHand] = bestOff;
    return out;
}

namespace {
GearAction wearGear(World& w, Entity unit, const Equipment& target, std::string& why) {
    why.clear();
    if (w.has<Mob>(unit)) return GearAction::NoChange;
    Inventory* inv = w.get<Inventory>(unit);
    Equipment* worn = w.get<Equipment>(unit);
    if (!inv || !worn) return GearAction::NoChange;
    if (worn->items == target.items) return GearAction::NoChange;

    std::map<int, int> was, will;
    for (int item : worn->items) if (item != 0) ++was[item];
    for (int item : target.items) if (item != 0) ++will[item];
    std::vector<int> off, on;
    for (const auto& [item, n] : was) for (int i = will.count(item) ? will[item] : 0; i < n; ++i) off.push_back(item);
    for (const auto& [item, n] : will) for (int i = was.count(item) ? was[item] : 0; i < n; ++i) on.push_back(item);

    Inventory next = *inv;
    for (int item : on) {
        bool found = false;
        for (int s = INVENTORY_SLOTS - 1; s >= 0 && !found; --s) {
            ItemStack& st = next.slots[(size_t)s];
            if (st.item != item || st.count <= 0) continue;
            if (--st.count <= 0) st = ItemStack{};
            found = true;
        }
        if (!found) return GearAction::NoChange;
    }
    for (int item : off) {
        if (addToInventory(next, item, 1) > 0) {
            why = lang("MISC_GEAR_NO_ROOM");
            return GearAction::NoRoom;
        }
    }

    int changed = 0;
    for (int g = 0; g < GEAR_SLOTS; ++g) changed += worn->items[(size_t)g] != target.items[(size_t)g] ? 1 : 0;
    *inv = next;
    worn->items = target.items;
    queueSound(w, Sfx::GearChanged);
    grantXp(w, unit, StatId::Dexterity, GEAR_CHANGE_XP * (float)changed, "MISC_REASON_GEAR");
    return GearAction::Done;
}
}

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

bool dropStack(World& w, Entity unit, int slot) {
    Inventory* inv = w.get<Inventory>(unit);
    const Transform* tr = w.get<Transform>(unit);
    if (!inv || !tr || slot < 0 || slot >= INVENTORY_SLOTS) return false;

    ItemStack& s = inv->slots[slot];
    if (s.item == 0 || s.count <= 0) return false;

    dropAtUnit(w, unit, s.item, s.count);
    s = ItemStack{};
    return true;
}
