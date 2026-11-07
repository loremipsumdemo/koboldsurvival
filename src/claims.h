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
constexpr double ALTAR_DECAY_HOURS = 48.0;
constexpr int BUILT_LOCK_LEVEL = 5;
constexpr int REP_LOCKPICK_PENALTY = 30;
constexpr int REP_DESTROY_PENALTY = 10;

struct Claim {
    ObjectId altar = NO_OBJECT;
    float x = 0.0f, z = 0.0f;
    int owner = 0;
};

std::vector<Claim> claimsOf(World& w);
const Claim* claimAt(const std::vector<Claim>& claims, float x, float z, int owner = -1);
bool claimsOverlap(float ax, float az, float bx, float bz);
bool altarFueled(World& w, ObjectId altar);
bool altarHasUpkeep(World& w, ObjectId altar);
std::string propDisplayName(World& w, ObjectId prop);
bool lockShutFor(World& w, ObjectId prop, ObjectId unit);
bool lockShutFor(World& w, const std::vector<Claim>& claims, ObjectId prop, ObjectId unit);
bool lockShutForFaction(World& w, ObjectId prop, int faction);
bool lockShutForFaction(World& w, const std::vector<Claim>& claims, ObjectId prop, int faction);
void breakBuilding(World& w, ObjectId e);
bool destroyBuilding(World& w, ObjectId e);
void claimOwnership(World& w, ObjectId prop);
void settleClaimOwnership(World& w);
void claimDecaySystem(World& w, double gameSeconds);
bool propDecaying(World& w, ObjectId e);
