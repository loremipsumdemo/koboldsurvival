#pragma once
#include <string>
#include <vector>
#include "components.h"
#include "ecs.h"
#include "factions.h"
#include "props.h"
#include "terrain.h"

// Claims, ownership and decay.
//   An Altar projects a circular claim of CLAIM_RADIUS tiles around itself, for its faction. A faction has one Altar, and
//   claims may not overlap. Only the claim's faction may build inside it. Props inside a claim belong to its faction, and
//   its storage props default to locked to it (Lock::faction).
//   An Altar keeps Wood Logs as upkeep, and burns them. It has no lock and can't be picked: its faction opens it, and
//   nobody else can. While it has no upkeep its health falls. At 0 it breaks: the claim is gone, and the props in the circle
//   belong to nobody (a lock stays shut, and whoever can pick it may).
//   A broken or destroyed building becomes Debris.

constexpr double ALTAR_HEALTH = 10.0;               // an Altar's health when new: it breaks at 0 (see Health)
constexpr double ALTAR_RECOVER_PER_HOUR = 1.0;      // health an Altar regains each in-game hour while it has upkeep
constexpr float CLAIM_RADIUS = 25.0f;               // tiles: an Altar's claim
constexpr double ALTAR_DECAY_HOURS = 48.0;          // in-game hours for an Altar with no upkeep to wear from whole to broken
constexpr int BUILT_LOCK_LEVEL = 5;                 // the Lockpicking an outsider needs to open a built prop's lock
constexpr int REP_LOCKPICK_PENALTY = 30;            // a caught lockpick costs the owner's faction this much standing
constexpr int REP_DESTROY_PENALTY = 10;             // demolishing another faction's building costs this much

// An Altar's claim, as the world holds it now
struct Claim {
    Entity altar = NULL_ENTITY;
    float x = 0.0f, z = 0.0f;
    int owner = 0;
};

// Every claim, oldest Altar first (lower entity id first)
std::vector<Claim> claimsOf(World& w);
// The oldest claim whose circle holds (x, z), of the given owner (-1: of any owner), or nullptr
const Claim* claimAt(const std::vector<Claim>& claims, float x, float z, int owner = -1);
// Whether two Altars at these points have overlapping claim circles
bool claimsOverlap(float ax, float az, float bx, float bz);
// Whether an Altar is burning upkeep: its fuel is lit, or it holds logs to light
bool altarFueled(World& w, Entity altar);
// Whether an Altar has any of its upkeep item (logs) in its inventory
bool altarHasUpkeep(World& w, Entity altar);
// A prop's name in the language in use. An Altar names its faction: "Altar of Red Faction".
std::string propDisplayName(World& w, Entity prop);
// Whether a prop keeps this unit out. An Altar keeps out everyone not of its faction. Any other prop does when it is locked,
// the unit is not of the lock's faction, and the prop does not stand inside a claim of the unit's faction (a faction's own
// claim is open to it, locks and all). NULL_ENTITY: the player.
bool lockShutFor(World& w, Entity prop, Entity unit);
// The same, for any unit of this faction
bool lockShutForFaction(World& w, Entity prop, int faction);
// Takes a prop down: whoever lies or sits on it gets off (a cage's prisoner goes free), whoever works it or is
// walking to it stops, an Auto-Haul to it is dropped, its items fall where it stood, and it becomes Debris. An Altar's
// claim goes with it. Debris itself is just removed, and Bones leave only what they held and the Bones item.
void breakBuilding(World& w, Entity e);
// The Destroy tool: takes any prop down (breakBuilding). False only if e is not a prop.
bool destroyBuilding(World& w, Entity e);
// Gives a new prop to the faction whose claim it stands in, and locks a storage prop there to it, as anything with
// contents in a claim is. An Altar is left as it is: it makes the claim. A prop outside every claim keeps the owner it
// was given (a built prop its builder's, a level's prop none). Every new prop goes through this.
void claimOwnership(World& w, Entity prop);
// Runs claimOwnership on every prop. A new game runs this once every prop stands, so each claim's props are its faction's.
void settleClaimOwnership(World& w);
// Each faction keeps its oldest Altar, and any other breaks. Only a save from before a faction was held to one can have more.
void resolveDuplicateAltars(World& w);
// Upkeep and breakage for the game time that passed (gameSeconds), as described above
void claimDecaySystem(World& w, double gameSeconds);
// Whether an Altar is wearing down now, by claimDecaySystem's rules
bool propDecaying(World& w, Entity e);
