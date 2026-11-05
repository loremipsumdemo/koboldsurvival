#pragma once
// The storage panel's actions on the open storage prop (play::storageProp) and the unit using it (play::storageUnit)
#include <string>
#include "components.h"
#include "ecs.h"
#include "props.h"
#include "ui_layout.h"

// The name of a prop in the language in use
std::string propNameOf(World& w, Entity prop);
// What the open storage panel's prop is used for: PropUse::Storage for a barrel, PropUse::Crafting for a stove
PropUse storageUse(World& w);
// A lock the player's faction owns: its owner may flip it open or shut from the storage panel
bool ownedLockOf(World& w, Entity prop);
// The storage panel's layout: one grid for a barrel or a stove, an input and an output grid for a forge or a campfire
StorageLayout storageLayout(World& w, int W, int H);
// A panel slot of the open storage (see play::storageDrag), or null if there is no such slot
ItemStack* storageSlot(World& w, int slot);
// Moves a whole stack from the storage into its unit's inventory, if all of it fits. Otherwise nothing moves.
bool takeFromStorage(World& w, int slot, std::string& why);
// Drops a whole stack from the storage on the ground where its unit stands, not where the prop is
bool dropFromStorage(World& w, int slot);
// Why the open storage won't take an item: a crafting prop takes only its input, a filtered storage only its sort
// (props.h: ItemFilter). Empty if it takes it.
std::string refusalFor(World& w, int item);
// How many of an item an inventory holds, over all its stacks
int carriedCount(const Inventory& inv, int item);
// Moves `count` of the clicked stack's item from the storage unit into the storage: the clicked stack first, then the
// unit's other stacks of that item. Nothing moves unless all of them fit.
bool storeCountInStorage(World& w, int clickedSlot, int count, std::string& why);
