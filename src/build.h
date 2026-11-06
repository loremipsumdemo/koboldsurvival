#pragma once
#include "claims.h"
#include "components.h"
#include "ecs.h"
#include "props.h"
#include "terrain.h"

// Placing a prop the player builds from the Build tab. The placement preview and the click that places both call
// these, so a spot that looks green is the spot a click places.

// Why a prop can't go where it was pointed at
enum class BuildCheck {
    Ok,
    Blocked,        // it would share ground with another prop
    Unwalkable,     // some of its shape is ground no unit can stand on
    EnemySealed,    // the ground is in another faction's claim
    ClaimOverlap,   // an Altar whose claim circle would overlap another's
    AltarTaken,     // the player's faction already has its Altar
};

// The centre a prop of this kind snaps to under the ground point (gx, gz), so its edges sit on tile edges
void snapBuildSpot(PropKind kind, float gx, float gz, float& x, float& z);

// Whether a prop of this kind, centred at (x, z), may stand there. Blocked: it would overlap another prop; Unwalkable:
// some of its shape is ground no unit can stand on.
BuildCheck checkBuildSpot(World& w, const Terrain& t, PropKind kind, float x, float z);

// Whether a Build tab kind can be picked anywhere: Destroy, the Bedroll, the Campfire and the Altar. Every other kind can be picked
// only while a selected unit of yours stands inside one of your claims (selectionInClaim).
bool buildAnywhere(const BuildPick& pick);
// Whether any selected unit of yours stands inside a claim of the Player Faction
bool selectionInClaim(World& w);

// The language key of the notice for a refused spot (empty for Ok)
const char* buildRefusalKey(BuildCheck c);

// Puts a built prop down: its transform, its Prop (serial -1: not placed by a level seed) and the parts it starts with.
// owner is the faction whose unit placed it (the Player Faction for yours, a camp's own for its starting kit). The caller
// checks the spot (checkBuildSpot) first.
Entity placeBuiltProp(World& w, const Terrain& t, PropKind kind, float x, float z, float yaw, int owner = FACTION_PLAYER);
// Puts a saved built prop back as it was: with the parts of its kind, and its owner (-1 for none). What it holds, its
// lock and health are the save's to set afterward. Nothing is worked out again from the claims around it.
Entity restoreBuiltProp(World& w, PropKind kind, float x, float y, float z, float yaw, int owner);
