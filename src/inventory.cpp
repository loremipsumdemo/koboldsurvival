#include "inventory.h"
#include <algorithm>
#include "items.h"

int stackLimit(int item) {
    const ItemDef* d = findItem(item);
    return d ? std::max(1, d->maxStackSize) : 1;
}

int addToInventory(Inventory& inv, int item, int count) {
    if (item <= 0 || count <= 0) return std::max(0, count);
    const int limit = stackLimit(item);
    int left = count;

    // Top up stacks of the same item first
    for (ItemStack& s : inv.slots) {
        if (left <= 0) break;
        if (s.item == item && s.count < limit) {
            const int put = std::min(left, limit - s.count);
            s.count += put;
            left -= put;
        }
    }
    // Then start new stacks in empty slots
    for (ItemStack& s : inv.slots) {
        if (left <= 0) break;
        if (s.item == 0) {
            const int put = std::min(left, limit);
            s.item = item;
            s.count = put;
            left -= put;
        }
    }
    return left;
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
