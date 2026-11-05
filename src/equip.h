#pragma once
#include <string>
#include <vector>
#include "components.h"
#include "ecs.h"

// Gear rules, and the moves between a unit's inventory, its gear slots and the ground. Plain
// functions over the world, so they can be tested without the game running.

// The sidebar's name for a gear slot, e.g. "Main hand"
std::string gearSlotName(int slot);

// Whether an item may go into a gear slot, given what the unit already wears. When it may not,
// why says what to tell the player.
bool canEquip(const Equipment& worn, int item, int slot, std::string& why);

// True while a two-handed weapon is in the main hand, which keeps the off hand empty
bool offHandBlocked(const Equipment& worn);

// The gear slot a right-click sends an item to: the first empty slot it fits, otherwise the first
// slot it fits, which swaps out what is there. -1 if the item fits no slot at all.
int gearSlotFor(const Equipment& worn, int item);

// The gear slots an inventory item is compared with on hover: both hands for a weapon or shield, both
// accessory slots for an accessory, and otherwise the slots it fits. Empty when the item is not gear.
std::vector<int> comparisonSlots(int item);

// The gear slots that equipping the item would empty: both hands for a two-handed weapon, otherwise
// the slot gearSlotFor sends it to. Empty when the item is not gear, or fits no slot.
std::vector<int> displacedSlots(const Equipment& worn, int item);

// Attack and defense added up over everything a unit wears
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

// Puts count of an item into a unit's inventory. Whatever does not fit falls to the ground at the unit's feet, with a
// line in the log and a red "-item" label over it. Returns how many went into the inventory.
int giveOrDrop(World& w, Entity unit, int item, int count);
// Moves the whole stack in one inventory slot onto the ground, under its unit. False if the slot is empty.
bool dropStack(World& w, Entity unit, int slot);
// Items fall where a unit stands, with the log line, the "-Iron Ore" label and the sound
void dropAtUnit(World& w, Entity unit, int item, int count);

// How Optimize chooses, as set in the Optimize All dialog (and kept in the settings file):
//   attackShare: 0 to 100, how much attack counts against defense. 50 weighs them the same; 100 looks only at attack,
//                0 only at defense.
//   accessories: whether it changes the accessory slots; off leaves them as they are.
//   twoHanded: whether it may choose a two-handed weapon; off weighs only main hand and off hand pairs.
//   valueTies: whether a tie goes to the more valuable item; off keeps what is already worn on a tie.
struct GearPrefs {
    int attackShare = 50;
    bool accessories = true;
    bool twoHanded = true;
    bool valueTies = true;
};

// The best gear a unit could wear from what it wears and carries: in each slot, the item with the most attack plus
// defense (weighed by prefs.attackShare); on a tie, the more valuable one (sell value, then buy value); on a tie again,
// what is already there. The hands are weighed together: a two-handed weapon against the best main hand and off hand
// pair.
Equipment bestGear(const Equipment& worn, const Inventory& inv, const GearPrefs& prefs = GearPrefs{});

// What the Gear tab's two buttons did
enum class GearAction { Done, NoChange, NoRoom };
// Optimize: wears bestGear, moving items between the gear and the inventory. It is checked as a whole first: if the
// inventory can't hold everything coming off, nothing changes and why says so (NoRoom). NoChange if the unit already
// wears the best it has.
GearAction optimizeGear(World& w, Entity unit, std::string& why, const GearPrefs& prefs = GearPrefs{});
// Optimize All: optimizeGear for each unit, each on its own (one with no room is left as it was). Counts what happened.
struct OptimizeAllResult {
    int changed = 0;   // units whose gear changed
    int noRoom = 0;    // units that had better gear but not the inventory space to change
    int same = 0;      // units already wearing their best
};
OptimizeAllResult optimizeAllGear(World& w, const std::vector<Entity>& units, const GearPrefs& prefs);
// Remove All: takes off every gear item into the inventory, checked as a whole the same way. NoChange if it wears none.
GearAction removeAllGear(World& w, Entity unit, std::string& why);
