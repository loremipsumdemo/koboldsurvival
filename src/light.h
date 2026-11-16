#pragma once
#include <unordered_set>
#include "ecs.h"
#include "terrain.h"

// Light, as the simulation needs it: one yes or no for each tile, and one number for how dark the world is.
//
// A tile is lit when it is within reach of a fire (a Campfire, a Standing Torch, a Wall Torch, a Forge: anything with props.txt's fire
// flag) or of a unit holding a torch (an item tagged torch, in either hand). The tiles are the ones the navmesh and the buildings use, and
// a lit tile is only in a set: nothing is drawn from it, and a light does not stop at a wall. The set is made again twice a second.
//
// The world's darkness is how little of the sun there is: the sky's own daylight (the hour, or the theme's fixed hour) times what the
// weather leaves of it. A fight is harder to land in the dark unless a tile at either end of it is lit (combat.cpp).
struct LightGrid {
    float since = 99.0f;
    float darkness = 0.0f;   // 0 in daylight to 1 at the dead of night, with the weather's share taken off the day
    std::unordered_set<long long> lit;
};

constexpr float LIGHT_REBUILD_SECONDS = 0.5f;
constexpr float HELD_TORCH_RADIUS = 4.0f;        // tiles
constexpr float DARK_ACCURACY_PENALTY = 0.35f;   // the most the dark takes off an attacker's accuracy

// How far in tiles a fire of this size lights (props.txt's fire size)
float fireLightRadius(float fireSize);
// Whether this item is a light to hold (tagged torch)
bool isTorchItem(int item);

// Keeps the lit tiles and the darkness, and lets go of any wall-hung prop whose wall is gone
void lightSystem(World& w, const Terrain& t, double clockSeconds, float dt);

bool tileLit(World& w, int tx, int tz);
// The world's darkness, 0 to 1
float worldDarkness(World& w);
// How much the dark takes off the accuracy of a swing from attacker to defender, 0 to DARK_ACCURACY_PENALTY: none when it is light, when a
// tile at either end is lit, or when the attacker sees in the dark (a Bat)
float darkAccuracyPenalty(World& w, ObjectId attacker, ObjectId defender);
