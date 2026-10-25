#pragma once
#include "components.h"

// Rules for what a unit carries. Pure functions, so they can be tested without the game running.

// Most of an item that one slot holds. Unknown items hold 1.
int stackLimit(int item);

// Puts up to `count` of an item into the inventory: first onto matching stacks with room, then
// into empty slots. Returns how many could not fit, so 0 means all of them went in.
int addToInventory(Inventory& inv, int item, int count);

// Total weight of everything carried
float carriedWeight(const Inventory& inv);

// Number of slots holding something
int occupiedSlots(const Inventory& inv);

// Reorders the stacks by item type (items, consumables, weapons, armor, accessories), then by name. Empty slots
// go last. Stacks are not merged or split, so every item is still there.
void sortInventoryByType(Inventory& inv);
