#pragma once
#include <vector>
#include "ecs.h"
#include "terrain.h"

// The starting kit a faction's camp is given: a flag, a small foundation for a vault, four bedrolls around a
// campfire, and a barrel. Put down with the faction as its owner. Two trees stand just past the bedrolls, for the camp's
// wood. Each piece takes the next number from *serial (when given), so saves can name the camp's props. Returns what was
// placed; placed is false (and nothing is put down) when the ground can't hold all of it.
// The vault is not part of it: a new game puts every camp's vault down last, with placeCampVault, once every other prop
// stands, and then settles who owns what with settleClaimOwnership.
struct CampKit {
    bool placed = false;
    int faction = 0;
    float x = 0.0f, z = 0.0f;          // the flag's spot
    float yaw = 0.0f;
    Entity flag = NULL_ENTITY;
    Entity foundation = NULL_ENTITY;   // where the vault goes
    Entity vault = NULL_ENTITY;        // NULL_ENTITY until placeCampVault puts it down
    std::vector<Entity> trees;
};
CampKit placeCampKit(World& w, Terrain& t, int faction, float cx, float cz, float yaw, int* serial = nullptr);
// Puts a placed kit's vault on its foundation, holding CAMP_UPKEEP_DAYS of logs, and sets kit.vault. The vault takes
// the next number from *serial (when given).
void placeCampVault(World& w, Terrain& t, CampKit& kit, int* serial = nullptr);
// Whether the whole kit fits around (cx, cz): every piece's tile walkable and free of props
bool campKitFits(World& w, const Terrain& t, float cx, float cz);
// Serials of the camps' props start here, after any a level places, so they never meet
constexpr int CAMP_SERIAL_BASE = 1000000;
// The day's worth of logs a new vault starts with, in-game hours
constexpr int CAMP_UPKEEP_DAYS = 3;
