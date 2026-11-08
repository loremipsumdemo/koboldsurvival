#include "inventory.h"
#include <algorithm>
#include <string>
#include <tuple>
#include "items.h"

int stackLimit(int item) {
    const ItemDef* d = findItem(item);
    return d ? std::max(1, d->maxStackSize) : 1;
}

int addToSlots(ItemStack* slots, int slotCount, int item, int count) {
    if (item <= 0 || count <= 0) return std::max(0, count);
    const int limit = stackLimit(item);
    int left = count;

    for (int i = 0; i < slotCount && left > 0; ++i) {
        ItemStack& s = slots[i];
        if (s.item == item && s.count < limit) {
            const int put = std::min(left, limit - s.count);
            s.count += put;
            left -= put;
        }
    }
    for (int i = 0; i < slotCount && left > 0; ++i) {
        ItemStack& s = slots[i];
        if (s.item == 0) {
            const int put = std::min(left, limit);
            s.item = item;
            s.count = put;
            left -= put;
        }
    }
    return left;
}

int addToInventory(Inventory& inv, int item, int count) {
    return addToSlots(inv.slots.data(), INVENTORY_SLOTS, item, count);
}

int slotsRoomFor(const ItemStack* slots, int slotCount, int item) {
    if (item <= 0) return 0;
    const int limit = stackLimit(item);
    int room = 0;
    for (int i = 0; i < slotCount; ++i) {
        if (slots[i].item == item) room += std::max(0, limit - slots[i].count);
        else if (slots[i].item == 0) room += limit;
    }
    return room;
}

bool slotsCanTake(const ItemStack* slots, int slotCount, int item, int count) {
    if (item <= 0 || count <= 0) return true;
    const int limit = stackLimit(item);
    int room = 0;
    for (int i = 0; i < slotCount; ++i) {
        if (slots[i].item == item) room += std::max(0, limit - slots[i].count);
        else if (slots[i].item == 0) room += limit;
        if (room >= count) return true;
    }
    return false;
}

int storedCount(const Storage& st) {
    int n = 0;
    for (const ItemStack& s : st.slots) n += s.item > 0 ? s.count : 0;
    return n;
}

int storeRoomFor(const Storage& st, int item) {
    int room = slotsRoomFor(st.slots.data(), (int)st.slots.size(), item);
    if (st.capacity > 0) room = std::min(room, std::max(0, st.capacity - storedCount(st)));
    return room;
}

bool storeCanTake(const Storage& st, int item, int count) {
    if (count <= 0) return true;
    if (st.capacity > 0 && storedCount(st) + count > st.capacity) return false;
    return slotsCanTake(st.slots.data(), (int)st.slots.size(), item, count);
}

int addToStore(Storage& st, int item, int count) {
    if (count <= 0) return 0;
    const int fits = st.capacity > 0 ? std::min(count, std::max(0, st.capacity - storedCount(st))) : count;
    const int left = addToSlots(st.slots.data(), (int)st.slots.size(), item, fits);
    return left + (count - fits);
}

int takeFromSlots(ItemStack* slots, int slotCount, int item, int count) {
    int took = 0;
    for (int i = 0; i < slotCount && took < count; ++i) {
        ItemStack& s = slots[i];
        if (s.item != item || s.count <= 0) continue;
        const int n = std::min(s.count, count - took);
        s.count -= n;
        took += n;
        if (s.count <= 0) s = ItemStack{};
    }
    return took;
}

int countInSlots(const ItemStack* slots, int slotCount, int item) {
    int n = 0;
    for (int i = 0; i < slotCount; ++i) n += slots[i].item == item ? slots[i].count : 0;
    return n;
}

float carriedWeight(const Inventory& inv) {
    float total = 0.0f;
    for (const ItemStack& s : inv.slots) {
        if (s.item == 0) continue;
        const ItemDef* d = findItem(s.item);
        if (d) total += d->weight * (float)s.count;
    }
    return total;
}

int occupiedSlots(const Inventory& inv) {
    int n = 0;
    for (const ItemStack& s : inv.slots) {
        if (s.item != 0) ++n;
    }
    return n;
}

namespace {
int typeRank(const std::string& typeId) {
    static const char* const order[] = { "item", "consumable", "weapon", "armor", "accessory" };
    for (int i = 0; i < (int)(sizeof(order) / sizeof(order[0])); ++i) {
        if (typeId == order[i]) return i;
    }
    return (int)(sizeof(order) / sizeof(order[0]));
}
}

void sortInventoryByType(Inventory& inv) {
    auto key = [](const ItemStack& s) {
        const ItemDef* def = findItem(s.item);
        return std::make_tuple(typeRank(def ? def->typeId : std::string()), def ? itemDisplayName(*def) : std::string(), s.item);
    };
    std::stable_sort(inv.slots.begin(), inv.slots.end(), [&](const ItemStack& a, const ItemStack& b) {
        const bool aEmpty = a.item == 0 || a.count <= 0;
        const bool bEmpty = b.item == 0 || b.count <= 0;
        if (aEmpty != bEmpty) return bEmpty;
        if (aEmpty) return false;
        return key(a) < key(b);
    });
}

int carriedCount(const Inventory& inv, int item) {
    int total = 0;
    for (const ItemStack& s : inv.slots) {
        if (s.item == item && s.count > 0) total += s.count;
    }
    return total;
}
