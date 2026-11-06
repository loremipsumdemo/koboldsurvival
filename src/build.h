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
    Blocked,        // it would share ground with another prop (a vault or shop may sit on a foundation; nothing else)
    Unwalkable,     // some of its shape is ground no unit can stand on
    NoFoundation,   // a vault whose shape is not wholly on a foundation
    NoClaim,        // a foundation or vault outside any claim of the player's faction
    EnemySealed,    // the ground is sealed by another faction's vault
    ClaimOverlap,   // a flag whose claim circle would overlap another flag's
    VaultTaken,     // the claim already has a vault
    VaultCap,       // the player's faction has VAULT_CAP vaults standing
};

// The centre a prop of this kind snaps to under the ground point (gx, gz), so its edges sit on tile edges
void snapBuildSpot(PropKind kind, float gx, float gz, float& x, float& z);

// Whether a prop of this kind, centred at (x, z) and turned by yaw, may stand there. Blocked: it would overlap another
// prop (a foundation can hold only a vault); Unwalkable: some of its shape is ground no unit can stand on;
// NoFoundation: a vault whose shape does not lie wholly inside a foundation.
BuildCheck checkBuildSpot(World& w, const Terrain& t, PropKind kind, float x, float z, float yaw);

// Whether a Build tab kind can be picked anywhere: Destroy, the Bedroll and the Campfire. Every other kind can be picked
// only while a selected unit of yours stands inside one of your claims (selectionInClaim).
bool buildAnywhere(const BuildPick& pick);
// Whether any selected unit of yours stands inside a claim of the Player Faction
bool selectionInClaim(World& w);

// The language key of the notice for a refused spot (empty for Ok)
const char* buildRefusalKey(BuildCheck c);

// Puts a built prop down: its transform, its Prop (serial -1: not placed by a level seed) and the parts it starts with.
// owner is the faction whose unit placed it (the Player Faction for yours, a camp's own for its starting kit). A vault
// placed in a claim that already has one is not placed: the caller checks it (checkBuildSpot) first.
Entity placeBuiltProp(World& w, const Terrain& t, PropKind kind, float x, float z, float yaw, int owner = FACTION_PLAYER);
// Puts a saved built prop back as it was: with the parts of its kind, and its owner (-1 for none). What it holds, its
// lock, wear and foundation are the save's to set afterward. Nothing is worked out again from the claims around it.
Entity restoreBuiltProp(World& w, PropKind kind, float x, float y, float z, float yaw, int owner);
