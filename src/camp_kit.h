#pragma once
#include <vector>
#include "ecs.h"
#include "terrain.h"

// The starting kit a faction's camp is given: an Altar holding CAMP_UPKEEP_DAYS of logs, four bedrolls around a campfire,
// and a barrel. Put down with the faction as its owner. Two trees stand just past the bedrolls, for the camp's wood. Each
// piece takes the next number from *serial (when given), so saves can name the camp's props. Returns what was placed;
// placed is false (and nothing is put down) when the ground can't hold all of it.
struct CampKit {
    bool placed = false;
    int faction = 0;
    float x = 0.0f, z = 0.0f;          // the Altar's spot
    float yaw = 0.0f;
    Entity altar = NULL_ENTITY;
    std::vector<Entity> trees;
};
CampKit placeCampKit(World& w, Terrain& t, int faction, float cx, float cz, float yaw, int* serial = nullptr);
// Whether the whole kit fits around (cx, cz): every piece's tile walkable and free of props
bool campKitFits(World& w, const Terrain& t, float cx, float cz);
// Serials of the camps' props start here, after any a level places, so they never meet
constexpr int CAMP_SERIAL_BASE = 1000000;
// The days' worth of logs a new camp's Altar starts with
constexpr int CAMP_UPKEEP_DAYS = 3;
