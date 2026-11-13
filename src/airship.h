#pragma once
#include "ecs.h"
#include "geometry.h"
#include "terrain.h"

// An Airship is a prop (props.txt flag "airship") that floats above the ground. Up to its workers count of units occupy
// it, and a move order for them flies the Airship instead, in a straight line over any ground, water or void. Its occupants
// can only get off over open ground, and if it is destroyed with them aboard they die.
inline constexpr float AIRSHIP_SPEED = 4.0f;    // tiles per second
inline constexpr float AIRSHIP_LIFT = 2.54f;    // how high it floats above the ground (or the water), or the top floor
                                                // built on its tile: about a floor's height (building.h FLOOR_TOP)
inline constexpr float AIRSHIP_CLIMB = 4.0f;    // how fast it rises or sinks to that height, per second
inline constexpr float AIRSHIP_DECK = 0.7f;     // where its occupants stand, above its keel
inline constexpr float TOWER_DECK = 3.2f;       // where a Watchtower's occupant stands, above its base
inline constexpr float AIRSHIP_ARRIVED = 0.15f;
inline constexpr int AIRSHIP_LAND_TILES = 64;  // land this big that can't be walked to gets you an Airship at the start
inline constexpr float AIRSHIP_BOARD_SECONDS = 5.0f;
inline constexpr float AIRSHIP_LEAVE_SECONDS = 1.0f;
inline constexpr float AIRSHIP_BOARD_REACH = 2.5f;   // boarding stops if the unit or the Airship moves further apart
inline constexpr float FLEE_MARGIN = 3.0f;           // a fleeing unit keeps this far outside its attacker's reach

// On the Airship prop: where it is flying to, and whether there is open ground below it
struct AirshipState {
    bool flying = false;
    float tx = 0.0f, tz = 0.0f;
    bool overOpen = true;
    // Not saved: the tile its height was last worked out for, the top floor there, and whether it has settled at all yet
    int tileX = -1, tileZ = -1;
    int topFloor = 0;
    float recheck = 0.0f;
    bool settled = false;
};

// An Airship, or a Watchtower, which is occupied the same way: one unit up top, out of melee reach, able to shoot and be shot
bool isAirship(World& w, ObjectId prop);
// How many can occupy this prop at once: an Airship's workers, and one for any other prop
int occupyCapacity(World& w, ObjectId prop);
int occupantCount(World& w, ObjectId prop);
// A unit getting on (5 seconds) or off (1 second) an Airship. Not saved: a reload simply drops it.
struct Boarding {
    ObjectId ship = NO_OBJECT;
    float left = 0.0f;
    bool leaving = false;
};

// A unit shot at from an Airship that can't shoot back: it runs out of reach, and only settles once out of reach for as
// long as it takes to calm down (PROVOKED_SECONDS). Not saved.
struct Fleeing {
    ObjectId from = NO_OBJECT;
    float left = 0.0f;
};

// The Airship this unit is aboard, or NO_OBJECT
ObjectId airshipOf(World& w, ObjectId u);
// Aboard an Airship that is not over open ground: the unit may not get off
bool stuckAboard(World& w, ObjectId u);
void flyAirship(World& w, ObjectId airship, Vec2 to);
// Flies the Airships, keeps their occupants on deck, and lets units get on and off
void airshipSystem(World& w, Terrain& t, float dt);
// The unit, at the Airship, starts getting on. False if there is no room.
bool startBoarding(World& w, ObjectId u, ObjectId ship);
// The unit starts getting off its Airship. False if it is not over open ground.
bool startLeaving(World& w, ObjectId u);
// The unit gets off its Airship at once. False if it is not over open ground.
bool disembark(World& w, Terrain& t, ObjectId u);
// Aboard an Airship or on a building's upper floor
bool elevated(World& w, ObjectId u);
// Whether attacker can hit target at all: melee only reaches a unit on its own level (the ground, the same Airship's
// deck, or the same upper floor). Ranged reaches anyone.
bool canStrike(World& w, ObjectId attacker, ObjectId target);
// Hit from above (an Airship or an upper floor) by an attacker it can't strike back: the unit flees (units of yours are
// left to you)
void fleeFromAirship(World& w, ObjectId attacker, ObjectId target);
// Runs the fleeing units out of reach, and settles them once they have been clear long enough
void fleeSystem(World& w, Terrain& t, float dt);
// The Airship is being destroyed: everyone aboard dies
void airshipLost(World& w, ObjectId airship);
// True if the map has open ground, at least minTiles of it in one piece, that can't be walked to from (x, z)
bool unreachableLand(const Terrain& t, float x, float z, int minTiles);
// A new game on such a map: an Airship of yours near the start. NO_OBJECT if there is no room.
ObjectId placeStartingAirship(World& w, Terrain& t, float x, float z);
