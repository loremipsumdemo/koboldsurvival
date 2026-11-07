#pragma once
#include <vector>
#include "ecs.h"
#include "terrain.h"

struct CampKit {
    bool placed = false;
    int faction = 0;
    float x = 0.0f, z = 0.0f;
    float yaw = 0.0f;
    ObjectId altar = NO_OBJECT;
    std::vector<ObjectId> trees;
};
CampKit placeCampKit(World& w, Terrain& t, int faction, float cx, float cz, float yaw);
bool campKitFits(World& w, const Terrain& t, float cx, float cz);
constexpr int CAMP_UPKEEP_DAYS = 3;
