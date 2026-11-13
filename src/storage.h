#pragma once
#include <string>
#include "components.h"
#include "ecs.h"
#include "props.h"
#include "ui_layout.h"

std::string propNameOf(World& w, ObjectId prop);
bool ownedLockOf(World& w, ObjectId prop);
StorageLayout storageLayout(World& w, int W, int H);
// Window cells: the input cells shown (scrolled to session.storageScroll), then the output cells
ItemStack* storageSlot(World& w, int slot);
int storageShown(World& w);
int storageScrollMax(World& w);
// The slot of the open store that an input cell of its window is (counting the rows scrolled past), or -1 for an output
// cell or one past the store's slots
int storageAbsoluteSlot(World& w, int cell);
// Moves, merges or swaps stacks between two slots of a store, if `from` still holds what `expected` says (see moveSlotStack)
bool moveStorageStack(World& w, ObjectId prop, int from, int to, const ItemStack& expected);
// The faction a unit steals from by taking from this store (its owner's, or the claim's it stands in), or -1
int stolenFromBy(World& w, ObjectId prop, ObjectId unit);
// An item taken: marked stolen from that faction, or unmarked if it was stolen from the taker's own
int markTaken(World& w, int item, int stolenFrom, ObjectId taker);
bool takeFromStorage(World& w, int slot, std::string& why);
bool dropFromStorage(World& w, int slot);
std::string refusalFor(World& w, int item);
bool storeCountInStorage(World& w, int clickedSlot, int count, std::string& why);
