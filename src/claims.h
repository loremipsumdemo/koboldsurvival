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
constexpr float CLAIM_RADIUS = 25.0f;
// A camp: another faction's campfire, away from its Altar's claim, claims this much land around it (it locks nothing)
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

std::vector<Claim> claimsOf(World& w);
const Claim* claimAt(const std::vector<Claim>& claims, float x, float z, int owner = -1);
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
void breakBuilding(World& w, ObjectId e);
// Gone without a trace (no debris): what it held is dropped where it stood, and a claim it held is let go
void removeProp(World& w, ObjectId e);
bool destroyBuilding(World& w, ObjectId e);
void claimOwnership(World& w, ObjectId prop);
void settleClaimOwnership(World& w);
void claimDecaySystem(World& w, double gameSeconds);
bool propDecaying(World& w, ObjectId e);
