#pragma once
#include <string>
#include "build_plan.h"
#include "ecs.h"
#include "rng.h"
#include "terrain.h"

// Factions other than yours that have an Altar build houses in its claim, from the plans in MAP/BLUEPRINT (build_plan.h).
//  - A faction with an Altar has up to NPC_BUILDINGS_MOST buildings in its claim: 3 of wood, and once those 3 are complete 2 of
//    stone and 1 of metal. One is under construction at a time, and a new one stands at least a tile from every other building
//    so that the two are never taken for one.
//  - It chooses a plan it has the material for in its stores, a turn and a place in its claim where the game lets the foundation
//    go, and lays that foundation as a blueprint. Its citizens build it by Auto-Construct, as yours do, fetching the material
//    from the faction's stores. When what has been laid is finished, the next stage of the plan is laid (the walls and door on the
//    foundation, then the upper floor, and so on), until the plan is built.
//  - A stage that is not finished in PLAN_STAGE_GIVE_UP_HOURS is given up on: what is built stays, and the plan is let go.
inline constexpr int NPC_BUILDINGS_WOOD = 3;
inline constexpr int NPC_BUILDINGS_STONE = 2;
inline constexpr int NPC_BUILDINGS_METAL = 1;
inline constexpr int NPC_BUILDINGS_MOST = NPC_BUILDINGS_WOOD + NPC_BUILDINGS_STONE + NPC_BUILDINGS_METAL;
inline constexpr double PLAN_STAGE_GIVE_UP_HOURS = 24.0;
inline constexpr float NPC_BUILDERS_LOOK_SECONDS = 10.0f;   // how often a faction looks at what it could build
inline constexpr int NPC_PLACE_TRIES = 48;

// On the first foundation tile of a building laid from a plan: how far it has come. Saved.
struct PlanSite {
    std::string plan;     // the plan's name
    int x = 0, z = 0;     // where the plan's corner is, in tiles, once turned
    int turns = 0;        // how many quarter turns the plan was given
    int material = 0;     // the BuildMaterial it is made of
    int stage = 0;        // the last stage laid
    double hours = 0.0;   // game hours since it was laid
};

// What the buildings of a faction's claim add up to, by material
struct NpcBuildCount {
    int total = 0;
    int wood = 0, stone = 0, metal = 0;
    int woodComplete = 0;
};
// Which material the faction builds next, if any: wood until it has 3 wood buildings, then stone and metal once 3 are complete
bool npcNextMaterial(const NpcBuildCount& have, BuildMaterial& out);

void npcBuilderSystem(World& w, Terrain& t, Rng& rng, float realSeconds, double gameSeconds);
