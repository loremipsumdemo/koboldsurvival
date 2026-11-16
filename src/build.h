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
    NoCamp,
    NoFloor,
    Indoors,
    BesideFoundation,   // a Perimeter Wall or Gate may not touch a foundation
    NoWall,             // a wall decoration (a Wall Torch, a Painting) needs a finished wall on its tile, on its floor
    WallBetween,        // ...and on a side of it that no other wall touches: a wall's end, where the next wall joins, has no face
};

// Wall decorations (props.txt's wall_mount flag) hang on the side of a wall tile: side 0 faces +z, 1 +x, 2 -z, 3 -x (a prop's yaw is the side
// times a quarter turn). The wall in a tile on this floor (finished or not), or NO_OBJECT; it is a wall only if its kind's wallRule says so.
inline constexpr int SIDE_DX[4] = { 0, 1, 0, -1 };
inline constexpr int SIDE_DZ[4] = { 1, 0, -1, 0 };
ObjectId wallTileAt(World& w, int tx, int tz, int storey);
// Whether this side of the wall tile is bare: no wall or door on the tile it faces (their arms would join this one's there)
bool wallSideBare(World& w, int tx, int tz, int storey, int side);
// How far from the middle of its tile a decoration's own middle is, toward its side: the wall's face is 0.15 out
float wallDecorOut(const PropInfo& info);

void snapBuildSpot(PropKind kind, float gx, float gz, float& x, float& z);

// storey: the floor it goes on (0 the ground; above that, it must stand on a finished upper floor of yours)
// builder: the faction placing it (yours by default)
// claims: the claims in the world when the caller has them at hand (claimsOf), else they are worked out here
// yaw: the way it is turned (a Perimeter piece, covering several tiles, is turned in quarters)
// nearby: the props that may overlap it, when the caller has the props round the spot at hand (propsNear) and is checking many spots in a row,
//         keeping the list up as it adds props to it; else they are looked up here. Entries that no longer exist are passed over.
BuildCheck checkBuildSpot(World& w, const Terrain& t, PropKind kind, float x, float z, int storey = 0, int builder = FACTION_PLAYER,
                          const std::vector<Claim>* claims = nullptr, float yaw = 0.0f, const std::vector<ObjectId>* nearby = nullptr);

bool buildAnywhere(const BuildPick& pick);
bool selectionInClaim(World& w);
bool selectionInClaim(World& w, const std::vector<Claim>& claims);

const char* buildRefusalKey(BuildCheck c);

// A prop with the parts its kind starts with
// Every prop comes into being here, however it does: built, placed, spawned, scattered by a new level, left as debris
// or bones, or read back from a save. It ends by calling onPropSpawned.
ObjectId makeProp(World& w, PropKind kind, const Transform& at, float yaw = 0.0f);
// What happens to any prop as it comes into being, whatever made it: one flagged random_yaw in props.txt (a tree, a
// boulder, debris) is turned to face a way of its own. Checks for a new prop belong here.
void onPropSpawned(World& w, ObjectId e);
ObjectId placeBuiltProp(World& w, const Terrain& t, PropKind kind, float x, float z, float yaw, int owner = FACTION_PLAYER,
                        int storey = 0);
