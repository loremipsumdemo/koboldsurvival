#pragma once
#include "ecs.h"
#include "terrain.h"

// Units of other factions looking after themselves:
//  - Beaten (knocked out, then come round), a unit retreats: it keeps out of fights and raids and goes to its faction's
//    Altar or camp, or away from your claims to make a camp, and rests in a bed or bedroll till it is back to full health.
//  - A camp is a campfire with no one but the faction's own units within CAMP_CLEAR_RANGE; it claims CAMP_CLAIM_RADIUS
//    of land (claims.h), and each unit that comes to rest there lays a bedroll around it.
//  - Below half hunger and not busy, a unit goes for food: on the ground, or in its own faction's stores. Your units
//    (military and citizens) do the same when their Eat task is on and they are below its percent with no food on them.
//  - A bedroll or campfire out in the open (off buildings, outside every Altar's claim) is gone after
//    OUTDOOR_DECAY_HOURS, with no debris; what a campfire held is dropped.

inline constexpr float CAMP_CLEAR_RANGE = 20.0f;          // no one of another faction this near, to make a camp
inline constexpr float CAMP_BEDROLL_RING = 3.0f;          // bedrolls are laid this far round the campfire
inline constexpr int CAMP_BEDROLLS_MOST = 8;
inline constexpr float RETREAT_AWAY = 45.0f;              // how far past the edge of your nearest claim a camp is made
inline constexpr double RETREAT_MOST_HOURS = 48.0;        // a retreat is given up after this long
inline constexpr double OUTDOOR_DECAY_HOURS = 72.0;
inline constexpr int FORAGE_BELOW_PERCENT = 50;
inline constexpr int HUNGER_FLOOR_PERCENT = 10;           // other factions' units never go hungrier, unless caged
inline constexpr float FORAGE_RANGE = 40.0f;
inline constexpr float FORAGE_SECONDS = 60.0f;            // a trip for food is given up after this long

// A unit of a faction that keeps camps and retreats (not yours, not a mob's, not the Factionless)
bool campNpc(World& w, ObjectId u);
// Called as a unit comes round: one of another faction starts retreating
void startRetreat(World& w, ObjectId u);
void npcCampSystem(World& w, Terrain& t, float dt, double gameSeconds);
void outdoorDecaySystem(World& w, double gameSeconds);
