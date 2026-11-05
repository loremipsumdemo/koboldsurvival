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
#include "systems.h"
#include "textcase.h"
#include "ui_layout.h"
#include "play_state.h"

using namespace play;

// The name of a prop in the language in use
std::string propNameOf(World& w, Entity prop) {
    const Prop* p = w.get<Prop>(prop);
    return p ? lang(propInfo(p->kind).nameKey) : std::string();
}

// What the open storage panel's prop is used for: PropUse::Storage for a barrel, PropUse::Crafting for a stove
PropUse storageUse(World& w) {
    const Prop* p = w.get<Prop>(storageProp);
    return p ? propInfo(p->kind).use : PropUse::None;
}

// The storage panel's layout: one grid for a barrel or a stove, an input and an output grid for a forge or a campfire
// A lock the player's faction owns: its owner may flip it open or shut from the storage panel
bool ownedLockOf(World& w, Entity prop) {
    const Lock* l = w.get<Lock>(prop);
    return l && w.has<Owner>(prop) && l->faction == FACTION_PLAYER;
}

StorageLayout storageLayout(World& w, int W, int H) {
    return layoutStorage(W, H, w.has<Output>(storageProp), ownedLockOf(w, storageProp));
}

// A panel slot of the open storage (see storageDrag), or null if there is no such slot
ItemStack* storageSlot(World& w, int slot) {
    if (slot < 0) return nullptr;
    if (slot < STORAGE_SLOTS) {
        Storage* st = w.get<Storage>(storageProp);
        return st ? &st->slots[(size_t)slot] : nullptr;
    }
    Output* out = w.get<Output>(storageProp);
    return out && slot < 2 * STORAGE_SLOTS ? &out->slots[(size_t)(slot - STORAGE_SLOTS)] : nullptr;
}

// Moves a whole stack from the storage into its unit's inventory, if all of it fits. Otherwise nothing moves.
bool takeFromStorage(World& w, int slot, std::string& why) {
    ItemStack* from = storageSlot(w, slot);
    Inventory* inv = w.get<Inventory>(storageUnit);
    if (!from || !inv || from->item == 0) return false;
    const ItemStack stack = *from;
    if (!slotsCanTake(inv->slots.data(), INVENTORY_SLOTS, stack.item, stack.count)) {
        why = langf("MISC_NO_ROOM_INV", { itemNameOf(stack.item) });
        return false;
    }
    *from = ItemStack{};
    giveOrDrop(w, storageUnit, stack.item, stack.count);   // checked to fit above; anything that didn't would fall
    queueSound(Sfx::Take);
    propStorageChanged(w, storageProp);   // a campfire that lost its input stops its work
    queueUnitLabel(storageUnit, itemGainLabel(stack.count, itemNameOf(stack.item)));
    return true;
}

// Drops a whole stack from the storage on the ground where its unit stands, not where the prop is
bool dropFromStorage(World& w, int slot) {
    ItemStack* from = storageSlot(w, slot);
    const Transform* ut = w.get<Transform>(storageUnit);
    if (!from || !ut || from->item == 0) return false;
    const ItemStack stack = *from;
    queueLog(langf("MISC_DROPS", { nameOf(w, storageUnit), std::to_string(stack.count),
                                   itemNameOf(stack.item) }), LogKind::Drop, npcOnly(w, storageUnit));
    spawnGroundItem(w, Transform{ ut->x, ut->y, ut->z }, stack.item, stack.count);
    *from = ItemStack{};
    queueSound(Sfx::Drop);
    propStorageChanged(w, storageProp);   // a campfire that lost its input stops its work
    return true;
}

// Why the open storage won't take an item: a crafting prop takes only its input, a filtered storage only its sort
// (props.h: ItemFilter). Empty if it takes it.
std::string refusalFor(World& w, int item) {
    const Prop* prop = w.get<Prop>(storageProp);
    if (!prop || propAcceptsItem(prop->kind, item)) return std::string();
    const PropInfo& info = propInfo(prop->kind);
    if (info.use == PropUse::Storage) return langf("MISC_STORAGE_ONLY", { lang(itemFilterKey(info.accepts)) });
    return langf("MISC_CRAFT_WRONG_ITEM", { itemNameOf(item) });
}

// Moves `count` of the clicked stack's item from the storage unit into the storage: the clicked stack first, then the
// unit's other stacks of that item. Nothing moves unless all of them fit.
bool storeCountInStorage(World& w, int clickedSlot, int count, std::string& why) {
    Storage* st = w.get<Storage>(storageProp);
    Inventory* inv = w.get<Inventory>(storageUnit);
    if (!st || !inv || clickedSlot < 0 || clickedSlot >= INVENTORY_SLOTS || count <= 0) return false;
    const int item = inv->slots[clickedSlot].item;
    if (item == 0 || carriedCount(*inv, item) < count) return false;
    // A barrel takes anything into any slot. A campfire or a forge takes only its input item, and a weapon rack and the
    // like only their sort of item.
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
    propStorageChanged(w, storageProp);
    queueUnitLabel(storageUnit, itemLossLabelFor(count, item), true);   // "-3x Iron Ore" over the unit that stored it
    queueSound(Sfx::Store);
    return true;
}
