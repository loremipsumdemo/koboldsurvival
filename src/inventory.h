#pragma once
#include "components.h"

int stackLimit(int item);

int addToInventory(Inventory& inv, int item, int count);
int addToSlots(ItemStack* slots, int slotCount, int item, int count);
bool slotsCanTake(const ItemStack* slots, int slotCount, int item, int count);
int slotsRoomFor(const ItemStack* slots, int slotCount, int item);

// A prop's store: room for the item within its slots and its capacity, and adding to it (what didn't fit is returned)
int storedCount(const Storage& st);
int storeRoomFor(const Storage& st, int item);
bool storeCanTake(const Storage& st, int item, int count);
int addToStore(Storage& st, int item, int count);
// Takes up to count of the item out of the slots: how many it took
int takeFromSlots(ItemStack* slots, int slotCount, int item, int count);
int countInSlots(const ItemStack* slots, int slotCount, int item);

float carriedWeight(const Inventory& inv);

int occupiedSlots(const Inventory& inv);

int carriedCount(const Inventory& inv, int item);

void sortInventoryByType(Inventory& inv);
