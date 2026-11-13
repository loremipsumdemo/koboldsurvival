#include "storage.h"
#include "spatial.h"
#include "mobs.h"
#include "hostility.h"
#include "factions.h"
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

std::string propNameOf(World& w, ObjectId prop) {
    return propDisplayName(w, prop);
}

bool ownedLockOf(World& w, ObjectId prop) {
    const Lock* l = w.get<Lock>(prop);
    return l && w.has<Owner>(prop) && l->faction == FACTION_PLAYER;
}

StorageLayout storageLayout(World& w, int W, int H) {
    const Storage* st = w.get<Storage>(session.storageProp);
    return layoutStorage(W, H, w.has<Output>(session.storageProp), ownedLockOf(w, session.storageProp),
                         st ? (int)st->slots.size() : STORAGE_SLOTS);
}

int storageShown(World& w) {
    const Storage* st = w.get<Storage>(session.storageProp);
    return st ? visibleStorageSlots((int)st->slots.size()) : STORAGE_SLOTS;
}

int storageScrollMax(World& w) {
    const Storage* st = w.get<Storage>(session.storageProp);
    if (!st) return 0;
    const int rows = ((int)st->slots.size() + STORAGE_COLUMNS - 1) / STORAGE_COLUMNS;
    return std::max(0, rows - STORAGE_VISIBLE_ROWS);
}

int storageAbsoluteSlot(World& w, int cell) {
    const Storage* st = w.get<Storage>(session.storageProp);
    if (!st || cell < 0 || cell >= storageShown(w)) return -1;
    const size_t at = (size_t)(std::clamp(session.storageScroll, 0, storageScrollMax(w)) * STORAGE_COLUMNS + cell);
    return at < st->slots.size() ? (int)at : -1;
}

bool moveStorageStack(World& w, ObjectId prop, int from, int to, const ItemStack& expected) {
    Storage* st = w.get<Storage>(prop);
    if (!st || !w.has<Prop>(prop)) return false;
    if (!moveSlotStack(st->slots.data(), (int)st->slots.size(), from, to, expected)) return false;
    propStorageChanged(w, prop);
    return true;
}

// A window cell: the shown input cells first (from the row scrolled to), then the output cells
ItemStack* storageSlot(World& w, int slot) {
    if (slot < 0) return nullptr;
    const int shown = storageShown(w);
    if (slot < shown) {
        Storage* st = w.get<Storage>(session.storageProp);
        if (!st) return nullptr;
        const size_t at = (size_t)(std::clamp(session.storageScroll, 0, storageScrollMax(w)) * STORAGE_COLUMNS + slot);
        return at < st->slots.size() ? &st->slots[at] : nullptr;
    }
    Output* out = w.get<Output>(session.storageProp);
    return out && slot - shown < STORAGE_SLOTS ? &out->slots[(size_t)(slot - shown)] : nullptr;
}

int stolenFromBy(World& w, ObjectId prop, ObjectId unit) {
    int owner = -1;
    if (const Owner* own = w.get<Owner>(prop)) owner = own->faction;
    else if (const Transform* pt = w.get<Transform>(prop)) {
        if (const Claim* c = claimAt(claimsOf(w), pt->x, pt->z)) owner = c->owner;
    }
    const int taker = factionOf(w, unit);
    if (owner < 0 || owner == taker || factionsAllied(w, owner, taker) || isMobFaction(owner)) return -1;
    return owner;
}

int markTaken(World& w, int item, int stolenFrom, ObjectId taker) {
    // Your own goods taken back are yours again
    if (isStolen(item) && stolenFromOf(item) == factionOf(w, taker)) return baseItemId(item);
    if (stolenFrom >= 0 && !isStolen(item)) return markStolen(item, stolenFrom);
    return item;
}

bool takeFromStorage(World& w, int slot, std::string& why) {
    ItemStack* from = storageSlot(w, slot);
    Inventory* inv = w.get<Inventory>(session.storageUnit);
    if (!from || !inv || from->item == 0) return false;
    ItemStack stack = *from;
    stack.item = markTaken(w, stack.item, stolenFromBy(w, session.storageProp, session.storageUnit), session.storageUnit);
    if (!slotsCanTake(inv->slots.data(), INVENTORY_SLOTS, stack.item, stack.count)) {
        why = langf("MISC_NO_ROOM_INV", { itemNameOf(stack.item) });
        return false;
    }
    *from = ItemStack{};
    giveOrDrop(w, session.storageUnit, stack.item, stack.count);
    queueSound(w, Sfx::Take);
    propStorageChanged(w, session.storageProp);
    queueItemMove(w, session.storageProp, session.storageUnit, stack.item, stack.count);
    return true;
}

bool dropFromStorage(World& w, int slot) {
    ItemStack* from = storageSlot(w, slot);
    const Transform* ut = w.get<Transform>(session.storageUnit);
    if (!from || !ut || from->item == 0) return false;
    ItemStack stack = *from;
    stack.item = markTaken(w, stack.item, stolenFromBy(w, session.storageProp, session.storageUnit), session.storageUnit);
    spawnGroundItem(w, *ut, stack.item, stack.count, storeyOf(w, session.storageUnit));
    queueUnitLabel(w, session.storageProp, itemLossLabelFor(stack.count, stack.item), true);
    queueSound(w, Sfx::Drop);
    *from = ItemStack{};
    propStorageChanged(w, session.storageProp);
    return true;
}

std::string refusalFor(World& w, int item) {
    const Prop* prop = w.get<Prop>(session.storageProp);
    if (!prop || propTakesItem(w, session.storageProp, item)) return std::string();
    const PropInfo& info = propInfo(prop->kind);
    if (hasStoreFilter(prop->kind) && propAcceptsItem(prop->kind, item)) {
        return langf("MISC_STORE_NOT_ALLOWED", { itemNameOf(item), lang(STORE_CATEGORY_KEYS[(int)storeCategoryOf(item)]) });
    }
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
    if (!storeCanTake(*st, item, count)) {
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
    addToStore(*st, item, count);
    propStorageChanged(w, session.storageProp);
    queueItemMove(w, session.storageUnit, session.storageProp, item, count);
    queueSound(w, Sfx::Store);
    return true;
}
