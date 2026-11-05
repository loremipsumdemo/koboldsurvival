#include "world_labels.h"
#include "items.h"
#include "language.h"

namespace {
std::vector<TradeLabel> gQueuedLabels;
}

void queueUnitLabel(Entity unit, const std::string& text, bool red) {
    if (unit == NULL_ENTITY || text.empty()) return;
    gQueuedLabels.push_back(TradeLabel{ unit, text, 0.0f, red });
}

std::vector<TradeLabel> takeQueuedLabels() {
    std::vector<TradeLabel> out;
    out.swap(gQueuedLabels);
    return out;
}

void discardQueuedLabels() {
    gQueuedLabels.clear();
}

std::string itemGainLabel(int count, const std::string& itemName) {
    if (count <= 1) return langf("MISC_ITEM_GAINED", { itemName });
    return langf("MISC_ITEMS_GAINED", { std::to_string(count), itemName });
}

std::string itemLossLabel(int count, const std::string& itemName) {
    if (count <= 1) return langf("MISC_ITEM_LOST", { itemName });
    return langf("MISC_ITEMS_LOST", { std::to_string(count), itemName });
}

namespace {
std::string labelItemName(int item) {
    const ItemDef* def = findItem(item);
    return def ? itemDisplayName(*def) : std::string(lang("MISC_AN_ITEM"));
}
}

std::string itemGainLabelFor(int count, int item) { return itemGainLabel(count, labelItemName(item)); }
std::string itemLossLabelFor(int count, int item) { return itemLossLabel(count, labelItemName(item)); }
