#pragma once
#include <map>
#include <string>
#include <set>
#include <utility>
#include <vector>
#include "components.h"
#include "ecs.h"
#include "factions.h"
#include "props.h"
#include "terrain.h"

constexpr double ALTAR_HEALTH = 10.0;
constexpr double ALTAR_RECOVER_PER_HOUR = 1.0;
constexpr double PROP_MEND_SHARE_PER_HOUR = 0.1;   // a prop below its health mends this share of it an hour inside an Altar's claim
constexpr float CLAIM_RADIUS = 25.0f;
// Every complete building (building.h) in an Altar's claim adds this much to its radius, if no other claim is in the way
constexpr float CLAIM_GROWTH_STEP = 2.0f;
constexpr int CLAIM_GROWTH_MOST = 20;
// A camp: a campfire away from its own faction's Altar claim claims this much land around it (it locks nothing)
constexpr float CAMP_CLAIM_RADIUS = 10.0f;
constexpr double ALTAR_DECAY_HOURS = 48.0;
constexpr int BUILT_LOCK_LEVEL = 5;
constexpr int REP_LOCKPICK_PENALTY = 30;
constexpr int REP_DESTROY_PENALTY = 10;

struct Claim {
    ObjectId altar = NO_OBJECT;   // the Altar, or the campfire of a camp
    float x = 0.0f, z = 0.0f;
    int owner = 0;
    float radius = CLAIM_RADIUS;
    bool camp = false;            // a campfire's: smaller, and it locks no stores
};

// On an Altar: how many times its claim has grown. Saved.
struct ClaimGrowth {
    int steps = 0;
};
std::vector<Claim> claimsOf(World& w);
// On an Altar: the tiles beyond its core that its owner drew as the Perimeter, the ground the Fortified defend. Each is a ground floor
// tile, and counts on every floor above it. Always there, and never drawn or erased: the 5 by 5 tiles round the Altar. Saved.
struct Perimeter {
    std::set<std::pair<int, int>> tiles;
};
inline constexpr int PERIMETER_CORE_RADIUS = 2;
// An Altar with no Perimeter drawn for it (no Perimeter component: the Set Perimeter tool's Manual) has its Perimeter kept automatically: once its
// finished walls and gates close a ring round the Altar, the ground inside the ring and the pieces themselves; until then, the default. Worked
// out once an hour (npc_builders.cpp) and not saved (it is worked out again after a load).
struct AutoPerimeters {
    std::map<ObjectId, std::set<std::pair<int, int>>> by;
};
// The Perimeter an Altar has while it is automatic, its core not included
std::set<std::pair<int, int>> autoPerimeter(World& w, ObjectId altar);
// Looks at an Altar's finished walls and gates now and keeps its automatic Perimeter to them: where they close a ring the Altar cannot walk out of,
// the ground inside it and the pieces themselves, else (no ring) the default. True if there is a ring.
bool refreshAutoPerimeter(World& w, const Terrain& t, ObjectId altar);
// Every tile of the Altar's Perimeter, its core included
std::set<std::pair<int, int>> perimeterOf(World& w, ObjectId altar);
// The Perimeter an Altar has until one is drawn for it (the Set Perimeter tool): every tile of its claim but the core, which grows
// with the claim. The same for yours and an NPC faction's, and for an Altar that begins a game as for one placed in it.
std::set<std::pair<int, int>> defaultPerimeter(World& w, ObjectId altar);
bool inPerimeterCore(World& w, ObjectId altar, int x, int z);

// The same claims, kept: worked out again only when a Prop, Owner, Blueprint or ClaimGrowth is added or removed, when
// invalidateClaims says one was changed in place, and at the start of every simulation step. For lookups made many times a
// step (every unit's lock check); the reference is good until the next call that may change a prop, so copy it to hold on.
const std::vector<Claim>& cachedClaims(World& w);
void invalidateClaims(World& w);
// An Altar's claim grows when its faction has more complete buildings in it than it has grown by, and the room is there:
// no other Altar's claim within reach of the larger one. A camp of another faction under the larger claim is destroyed.
// It looks only when there is something earned, and when claims have changed since it last found no room.
void claimGrowthSystem(World& w, float dt);
const Claim* claimAt(const std::vector<Claim>& claims, float x, float z, int owner = -1);
// A spot a faction's Bedroll may lie on (props.txt flag needs_camp): in a claim of the faction's (its Altar's, or a
// camp's), or within a camp's reach of a Campfire that is the faction's or no one's
bool campSheltered(World& w, const std::vector<Claim>& claims, float x, float z, int faction);
// Props that need a camp and have lost theirs fall to Debris (looked at once a second)
void campPropSystem(World& w, float dt);
// Two claims, of these radii, would share land
bool claimsOverlap(float ax, float az, float ar, float bx, float bz, float br);
// The claim this prop holds (an Altar's, or a camp's campfire), or null
const Claim* claimOf(const std::vector<Claim>& claims, ObjectId prop);
bool altarFueled(World& w, ObjectId altar);
bool altarHasUpkeep(World& w, ObjectId altar);
std::string propDisplayName(World& w, ObjectId prop);
bool lockShutFor(World& w, ObjectId prop, ObjectId unit);
void releasePropLinks(World& w, ObjectId prop);
bool lockShutFor(World& w, const std::vector<Claim>& claims, ObjectId prop, ObjectId unit);
bool lockShutForFaction(World& w, ObjectId prop, int faction);
bool lockShutForFaction(World& w, const std::vector<Claim>& claims, ObjectId prop, int faction);
// leaveDebris false: no Debris is left where a building (or anything that leaves one) stood
void breakBuilding(World& w, ObjectId e, bool leaveDebris = true);
// Gone without a trace (no debris): what it held is dropped where it stood, and a claim it held is let go
void removeProp(World& w, ObjectId e);
bool destroyBuilding(World& w, ObjectId e);
// Takes a finished prop down: what it holds drops, a building piece gives back the material it took, and no Debris is left
bool deconstructBuilding(World& w, ObjectId e);
void claimOwnership(World& w, ObjectId prop);
void settleClaimOwnership(World& w);
void claimDecaySystem(World& w, double gameSeconds);
bool propDecaying(World& w, ObjectId e);
