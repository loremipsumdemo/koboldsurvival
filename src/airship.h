#pragma once
#include "ecs.h"
#include "geometry.h"
#include "terrain.h"

// An Airship is a prop (props.txt flag "airship") that floats above the ground. Up to its workers count of units occupy
// it, and a move order for them flies the Airship instead, in a straight line over any ground, water or void. Its occupants
// can only get off over open ground, and if it is destroyed with them aboard they die.
inline constexpr float AIRSHIP_SPEED = 4.0f;    // tiles per second
inline constexpr float AIRSHIP_LIFT = 2.2f;     // how high above the ground (or the water) it floats
inline constexpr float AIRSHIP_DECK = 0.7f;     // where its occupants stand, above its keel
inline constexpr float AIRSHIP_ARRIVED = 0.15f;
inline constexpr int AIRSHIP_LAND_TILES = 64;  // land this big that can't be walked to gets you an Airship at the start

// On the Airship prop: where it is flying to, and whether there is open ground below it
struct AirshipState {
    bool flying = false;
    float tx = 0.0f, tz = 0.0f;
    bool overOpen = true;
};

bool isAirship(World& w, ObjectId prop);
// How many can occupy this prop at once: an Airship's workers, and one for any other prop
int occupyCapacity(World& w, ObjectId prop);
int occupantCount(World& w, ObjectId prop);
// The Airship this unit is aboard, or NO_OBJECT
ObjectId airshipOf(World& w, ObjectId u);
// Aboard an Airship that is not over open ground: the unit may not get off
bool stuckAboard(World& w, ObjectId u);
void flyAirship(World& w, ObjectId airship, Vec2 to);
// Flies the Airships and keeps their occupants on deck
void airshipSystem(World& w, const Terrain& t, float dt);
// The unit gets off its Airship. False if it is not over open ground.
bool disembark(World& w, Terrain& t, ObjectId u);
// The Airship is being destroyed: everyone aboard dies
void airshipLost(World& w, ObjectId airship);
// True if the map has open ground, at least minTiles of it in one piece, that can't be walked to from (x, z)
bool unreachableLand(const Terrain& t, float x, float z, int minTiles);
// A new game on such a map: an Airship of yours near the start. NO_OBJECT if there is no room.
ObjectId placeStartingAirship(World& w, Terrain& t, float x, float z);
