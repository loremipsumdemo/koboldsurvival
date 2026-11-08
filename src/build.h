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
    NoFloor,
};

void snapBuildSpot(PropKind kind, float gx, float gz, float& x, float& z);

// storey: the floor it goes on (0 the ground; above that, it must stand on a finished upper floor of yours)
BuildCheck checkBuildSpot(World& w, const Terrain& t, PropKind kind, float x, float z, int storey = 0);

bool buildAnywhere(const BuildPick& pick);
bool selectionInClaim(World& w);

const char* buildRefusalKey(BuildCheck c);

// A prop with the parts its kind starts with
ObjectId makeProp(World& w, PropKind kind, const Transform& at, float yaw = 0.0f);
ObjectId placeBuiltProp(World& w, const Terrain& t, PropKind kind, float x, float z, float yaw, int owner = FACTION_PLAYER,
                        int storey = 0);
