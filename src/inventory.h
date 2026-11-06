#pragma once
#include "components.h"

int stackLimit(int item);

int addToInventory(Inventory& inv, int item, int count);
int addToSlots(ItemStack* slots, int slotCount, int item, int count);
bool slotsCanTake(const ItemStack* slots, int slotCount, int item, int count);
int slotsRoomFor(const ItemStack* slots, int slotCount, int item);

float carriedWeight(const Inventory& inv);

int occupiedSlots(const Inventory& inv);

int carriedCount(const Inventory& inv, int item);

void sortInventoryByType(Inventory& inv);
