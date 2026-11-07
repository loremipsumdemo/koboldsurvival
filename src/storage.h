#pragma once
#include <string>
#include "components.h"
#include "ecs.h"
#include "props.h"
#include "ui_layout.h"

std::string propNameOf(World& w, ObjectId prop);
bool ownedLockOf(World& w, ObjectId prop);
StorageLayout storageLayout(World& w, int W, int H);
ItemStack* storageSlot(World& w, int slot);
bool takeFromStorage(World& w, int slot, std::string& why);
bool dropFromStorage(World& w, int slot);
std::string refusalFor(World& w, int item);
bool storeCountInStorage(World& w, int clickedSlot, int count, std::string& why);
