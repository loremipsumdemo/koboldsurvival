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
    Entity altar = NULL_ENTITY;
    float x = 0.0f, z = 0.0f;
    int owner = 0;
};

std::vector<Claim> claimsOf(World& w);
const Claim* claimAt(const std::vector<Claim>& claims, float x, float z, int owner = -1);
bool claimsOverlap(float ax, float az, float bx, float bz);
bool altarFueled(World& w, Entity altar);
bool altarHasUpkeep(World& w, Entity altar);
std::string propDisplayName(World& w, Entity prop);
bool lockShutFor(World& w, Entity prop, Entity unit);
bool lockShutFor(World& w, const std::vector<Claim>& claims, Entity prop, Entity unit);
bool lockShutForFaction(World& w, Entity prop, int faction);
bool lockShutForFaction(World& w, const std::vector<Claim>& claims, Entity prop, int faction);
void breakBuilding(World& w, Entity e);
bool destroyBuilding(World& w, Entity e);
void claimOwnership(World& w, Entity prop);
void settleClaimOwnership(World& w);
void claimDecaySystem(World& w, double gameSeconds);
bool propDecaying(World& w, Entity e);
