#pragma once
#include "claims.h"
#include "components.h"
#include "ecs.h"
#include "props.h"
#include "terrain.h"

enum class BuildCheck {
    Ok,
    Blocked,
    Unwalkable,
    EnemySealed,
    ClaimOverlap,
    AltarTaken,
};

void snapBuildSpot(PropKind kind, float gx, float gz, float& x, float& z);

BuildCheck checkBuildSpot(World& w, const Terrain& t, PropKind kind, float x, float z);

bool buildAnywhere(const BuildPick& pick);
bool selectionInClaim(World& w);

const char* buildRefusalKey(BuildCheck c);

// A prop with the parts its kind starts with
Entity makeProp(World& w, PropKind kind, const Transform& at, float yaw = 0.0f);
Entity placeBuiltProp(World& w, const Terrain& t, PropKind kind, float x, float z, float yaw, int owner = FACTION_PLAYER);
