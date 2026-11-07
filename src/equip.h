#pragma once
#include <string>
#include <vector>
#include "components.h"
#include "ecs.h"

std::string gearSlotName(int slot);

bool canEquip(const Equipment& worn, int item, int slot, std::string& why);

bool offHandBlocked(const Equipment& worn);

int gearSlotFor(const Equipment& worn, int item);

std::vector<int> comparisonSlots(int item);

std::vector<int> displacedSlots(const Equipment& worn, int item);

void gearTotals(const Equipment& worn, int& attack, int& defense);

bool equipFromInventory(World& w, ObjectId unit, int invSlot, int gearSlot, std::string& why);

bool moveEquipped(World& w, ObjectId unit, int fromSlot, int toSlot, std::string& why);

bool unequipToInventory(World& w, ObjectId unit, int gearSlot, std::string& why);

bool dropEquipped(World& w, ObjectId unit, int gearSlot);

int giveOrDrop(World& w, ObjectId unit, int item, int count);
bool dropStack(World& w, ObjectId unit, int slot);
void dropAtUnit(World& w, ObjectId unit, int item, int count);

struct GearPrefs {
    int attackShare = 50;
    bool accessories = true;
    bool twoHanded = true;
    bool valueTies = true;
};

Equipment bestGear(const Equipment& worn, const Inventory& inv, const GearPrefs& prefs = GearPrefs{});

enum class GearAction { Done, NoChange, NoRoom };
GearAction optimizeGear(World& w, ObjectId unit, std::string& why, const GearPrefs& prefs = GearPrefs{});
struct OptimizeAllResult {
    int changed = 0;
    int noRoom = 0;
    int same = 0;
};
OptimizeAllResult optimizeAllGear(World& w, const std::vector<ObjectId>& units, const GearPrefs& prefs);
GearAction removeAllGear(World& w, ObjectId unit, std::string& why);
