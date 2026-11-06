#include "world_labels.h"
#include "items.h"
#include "language.h"

std::string itemGainLabel(int count, const std::string& itemName) {
    if (count <= 1) return langf("MISC_ITEM_GAINED", { itemName });
    return langf("MISC_ITEMS_GAINED", { std::to_string(count), itemName });
}

std::string itemLossLabel(int count, const std::string& itemName) {
    if (count <= 1) return langf("MISC_ITEM_LOST", { itemName });
    return langf("MISC_ITEMS_LOST", { std::to_string(count), itemName });
}

std::string itemGainLabelFor(int count, int item) { return itemGainLabel(count, itemNameOf(item)); }
std::string itemLossLabelFor(int count, int item) { return itemLossLabel(count, itemNameOf(item)); }
