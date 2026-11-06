#include "storage.h"
#include "trade.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include "language.h"
#include "components.h"
#include "ecs.h"
#include "items.h"
#include "inventory.h"
#include "equip.h"
#include "combat.h"
#include "stats.h"
#include "props.h"
#include "claims.h"
#include "systems.h"
#include "textcase.h"
#include "ui_layout.h"
#include "play_state.h"

using namespace play;

std::string propNameOf(World& w, Entity prop) {
    return propDisplayName(w, prop);
}

bool ownedLockOf(World& w, Entity prop) {
    const Lock* l = w.get<Lock>(prop);
    return l && w.has<Owner>(prop) && l->faction == FACTION_PLAYER;
}

StorageLayout storageLayout(World& w, int W, int H) {
    return layoutStorage(W, H, w.has<Output>(session.storageProp), ownedLockOf(w, session.storageProp));
}

ItemStack* storageSlot(World& w, int slot) {
    if (slot < 0) return nullptr;
    if (slot < STORAGE_SLOTS) {
        Storage* st = w.get<Storage>(session.storageProp);
        return st ? &st->slots[(size_t)slot] : nullptr;
    }
    Output* out = w.get<Output>(session.storageProp);
    return out && slot < 2 * STORAGE_SLOTS ? &out->slots[(size_t)(slot - STORAGE_SLOTS)] : nullptr;
}

bool takeFromStorage(World& w, int slot, std::string& why) {
    ItemStack* from = storageSlot(w, slot);
    Inventory* inv = w.get<Inventory>(session.storageUnit);
    if (!from || !inv || from->item == 0) return false;
    const ItemStack stack = *from;
    if (!slotsCanTake(inv->slots.data(), INVENTORY_SLOTS, stack.item, stack.count)) {
        why = langf("MISC_NO_ROOM_INV", { itemNameOf(stack.item) });
        return false;
    }
    *from = ItemStack{};
    giveOrDrop(w, session.storageUnit, stack.item, stack.count);
    queueSound(w, Sfx::Take);
    propStorageChanged(w, session.storageProp);
    queueUnitLabel(w, session.storageUnit, itemGainLabel(stack.count, itemNameOf(stack.item)));
    return true;
}

bool dropFromStorage(World& w, int slot) {
    ItemStack* from = storageSlot(w, slot);
    const Transform* ut = w.get<Transform>(session.storageUnit);
    if (!from || !ut || from->item == 0) return false;
    const ItemStack stack = *from;
    dropAtUnit(w, session.storageUnit, stack.item, stack.count);
    *from = ItemStack{};
    propStorageChanged(w, session.storageProp);
    return true;
}

std::string refusalFor(World& w, int item) {
    const Prop* prop = w.get<Prop>(session.storageProp);
    if (!prop || propAcceptsItem(prop->kind, item)) return std::string();
    const PropInfo& info = propInfo(prop->kind);
    if (info.use == PropUse::Storage) return langf("MISC_STORAGE_ONLY", { lang(itemFilterKey(info.accepts)) });
    return langf("MISC_CRAFT_WRONG_ITEM", { itemNameOf(item) });
}

bool storeCountInStorage(World& w, int clickedSlot, int count, std::string& why) {
    Storage* st = w.get<Storage>(session.storageProp);
    Inventory* inv = w.get<Inventory>(session.storageUnit);
    if (!st || !inv || clickedSlot < 0 || clickedSlot >= INVENTORY_SLOTS || count <= 0) return false;
    const int item = inv->slots[clickedSlot].item;
    if (item == 0 || carriedCount(*inv, item) < count) return false;
    if (const std::string no = refusalFor(w, item); !no.empty()) {
        why = no;
        return false;
    }
    if (!slotsCanTake(st->slots.data(), STORAGE_SLOTS, item, count)) {
        why = lang("MISC_STORAGE_FULL");
        return false;
    }
    int left = count;
    std::vector<int> order{ clickedSlot };
    for (int i = 0; i < INVENTORY_SLOTS; ++i) {
        if (i != clickedSlot && inv->slots[i].item == item) order.push_back(i);
    }
    for (int slot : order) {
        if (left <= 0) break;
        const int take = std::min(left, inv->slots[slot].count);
        takeFromSlot(*inv, slot, take);
        left -= take;
    }
    addToSlots(st->slots.data(), STORAGE_SLOTS, item, count);
    propStorageChanged(w, session.storageProp);
    queueUnitLabel(w, session.storageUnit, itemLossLabelFor(count, item), true);
    queueSound(w, Sfx::Store);
    return true;
}
