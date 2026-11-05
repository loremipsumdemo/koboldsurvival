#pragma once
#include <string>
#include <vector>
#include "components.h"
#include "ecs.h"
#include "factions.h"
#include "props.h"
#include "terrain.h"

// Claims, ownership, decay and breakage of the buildings the player (and later other factions) put down.
//   A flag projects a circular claim of CLAIM_RADIUS tiles around itself, tagged to its faction. Two claims may not
//   overlap: where they do, the older flag (the lower entity id) holds, and the newer one decays.
//   A vault on a foundation in a claim seals the claim to its faction: only that faction may build there. Each claim
//   holds one vault, and a faction has at most VAULT_CAP vaults. A vault burns logs as upkeep. While it is dry, its
//   claim's flag and the foundations inside the claim decay. A vault outside any flag's circle gives no upkeep.
//   Foundations decay on their own outside a claim of their faction. Props on a foundation break when it goes, and
//   their items fall to the ground. Props inside a claim default to locked to its faction (Lock::faction).
//   A broken or destroyed building becomes Debris.

constexpr double FLAG_HEALTH = 10.0;                // a flag's health when new: it breaks at 0 (see Health)
constexpr double FLAG_RECOVER_PER_HOUR = 1.0;       // health a flag regains each in-game hour while its claim has a vault
constexpr float CLAIM_RADIUS = 25.0f;               // tiles: a flag's claim
constexpr int VAULT_CAP = 3;                        // vaults one faction may have standing
constexpr double FLAG_DECAY_HOURS = 48.0;           // in-game hours for an unheld flag to wear down from whole to broken
constexpr double FOUNDATION_DECAY_HOURS = 72.0;     // and for an unheld foundation
constexpr int BUILT_LOCK_LEVEL = 5;                 // the Lockpicking an outsider needs to open a built prop's lock
constexpr int REP_LOCKPICK_PENALTY = 30;            // a caught lockpick costs the owner's faction this much standing
constexpr int REP_DESTROY_PENALTY = 10;             // demolishing another faction's building costs this much

// A flag's claim, as the world holds it now
struct Claim {
    Entity flag = NULL_ENTITY;
    float x = 0.0f, z = 0.0f;
    int owner = 0;
    Entity vault = NULL_ENTITY;   // the vault standing in the circle, or NULL_ENTITY when none does
    int sealer = -1;              // the faction that vault belongs to: it seals the claim (-1 with no vault)
};

// Every claim, oldest flag first (lower entity id first)
std::vector<Claim> claimsOf(World& w);
// The oldest claim whose circle holds (x, z), of the given owner (-1: of any owner), or nullptr
const Claim* claimAt(const std::vector<Claim>& claims, float x, float z, int owner = -1);
// The faction that seals the ground at (x, z): the owner of the vault in its claim, or -1 when none seals it
int sealingFactionAt(const std::vector<Claim>& claims, float x, float z);
// Whether two flags at these points have overlapping claim circles
bool claimsOverlap(float ax, float az, float bx, float bz);
// Whether a vault is burning upkeep: its fuel is lit, or it holds logs to light
bool vaultFueled(World& w, Entity vault);
// Whether a prop's lock keeps this unit out: it is locked, the unit is not of the lock's faction, and the prop does not
// stand inside a claim of the unit's faction (a faction's own claim is open to it, locks and all). NULL_ENTITY: the player.
bool lockShutFor(World& w, Entity prop, Entity unit);
// The same, for any unit of this faction
bool lockShutForFaction(World& w, Entity prop, int faction);
// Takes a prop down: whoever lies or sits on it gets off (a cage's prisoner goes free), whoever works it or is
// walking to it stops, an Auto-Haul to it is dropped, its items fall where it stood, and it becomes Debris. A foundation
// takes the props on it too. Debris itself is just removed.
void breakBuilding(World& w, Entity e);
// The Destroy tool: takes any prop down (breakBuilding). False only if e is not a prop.
bool destroyBuilding(World& w, Entity e);
// Gives a new prop to the faction whose claim it stands in, and locks a storage prop there to it, as anything with
// contents in a claim is. Flags and vaults are left as they are: they make the claims. A prop outside every claim
// keeps the owner it was given (a built prop its builder's, a level's prop none). Every new prop goes through this.
void claimOwnership(World& w, Entity prop);
// Runs claimOwnership on every prop. A new game runs this once its vaults are down, so each sealed claim's props are
// its vault's faction's.
void settleClaimOwnership(World& w);
// Wear and breakage for the game time that passed (gameSeconds): flags and foundations wear down as described above,
// then whatever is worn through breaks, and props whose foundation is gone break with it
void claimDecaySystem(World& w, double gameSeconds);

// Whether a flag or foundation is wearing down now, by claimDecaySystem's rules. claims: claimsOf(w).
bool propDecaying(World& w, Entity e, const std::vector<Claim>& claims);
// Whether a vault has any of its upkeep item (logs) in its inventory
bool vaultHasUpkeep(World& w, Entity vault);
