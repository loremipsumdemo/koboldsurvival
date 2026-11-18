#pragma once
#include <vector>
#include "ecs.h"
#include "props.h"
#include "terrain.h"

struct CampKit {
    bool placed = false;
    int faction = 0;
    float x = 0.0f, z = 0.0f;
    float yaw = 0.0f;
    ObjectId altar = NO_OBJECT;
    std::vector<ObjectId> trees;
};
// What each stockpile of a camp starts full of: this many of what it holds (scenario.h can change it)
constexpr int KIT_STOCK_DEFAULT = 500;
// altarOnly: the Altar alone, with none of the rest of the kit round it. stock: how much each stockpile of the kit starts with.
CampKit placeCampKit(World& w, Terrain& t, int faction, float cx, float cz, float yaw, bool altarOnly = false, int stock = KIT_STOCK_DEFAULT);
bool campKitFits(World& w, const Terrain& t, float cx, float cz, bool altarOnly = false);
// A stockpile of the kit starts with `count` of what it holds: the same for one put down about a camp. True if it was one; other props are
// left as they are.
bool fillKitStock(World& w, ObjectId e, PropKind kind, int count = KIT_STOCK_DEFAULT);
constexpr int CAMP_UPKEEP_DAYS = 3;
