#pragma once
#include <string>
#include "components.h"
#include "ecs.h"

// Gear rules, and the moves between a unit's inventory, its gear slots and the ground. Plain
// functions over the world, so they can be tested without the game running.

// The sidebar's name for a gear slot, e.g. "Main hand"
const char* gearSlotName(int slot);

// Whether an item may go into a gear slot, given what the unit already wears. When it may not,
// why says what to tell the player.
bool canEquip(const Equipment& worn, int item, int slot, std::string& why);

// True while a two-handed weapon is in the main hand, which keeps the off hand empty
bool offHandBlocked(const Equipment& worn);

// The gear slot a right-click sends an item to: the first empty slot it fits, otherwise the first
// slot it fits, which swaps out what is there. -1 if the item fits no slot at all.
int gearSlotFor(const Equipment& worn, int item);

// Attack and defence added up over everything a unit wears
void gearTotals(const Equipment& worn, int& attack, int& defense);

// Moves one gear item from an inventory slot into a gear slot. Whatever was in the gear slot goes
// into the inventory. False, with why set, when the move can't happen.
bool equipFromInventory(World& w, Entity unit, int invSlot, int gearSlot, std::string& why);

// Moves a gear item from one gear slot to another empty one that it fits
bool moveEquipped(World& w, Entity unit, int fromSlot, int toSlot, std::string& why);

// Takes a gear item off into the first inventory space that fits. False, with why set, if none fits.
bool unequipToInventory(World& w, Entity unit, int gearSlot, std::string& why);

// Drops a gear item on the ground under the unit
bool dropEquipped(World& w, Entity unit, int gearSlot);
