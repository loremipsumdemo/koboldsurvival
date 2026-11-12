#pragma once
#include <string>
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
