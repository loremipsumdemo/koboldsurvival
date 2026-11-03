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
constexpr float CLAIM_RADIUS = 25.0f;               // tiles: a flag's claim (tier one; claiminfo.txt will set the tiers)
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
// Takes a building down: its items fall where it stood, it becomes Debris, and a foundation takes the props on it too
void breakBuilding(World& w, Entity e);
// The Destroy tool: takes a building down. Refuses flags, debris and anything that is not a built prop.
bool destroyBuilding(World& w, Entity e);
// The starting kit a faction's camp is given (claims.h): a flag, a small foundation with a vault on it holding three days
// of upkeep, four bedrolls around a campfire, and a barrel. Put down with the faction as its owner.
// Two trees stand just past the bedrolls, for the camp's wood. Each piece takes the next number from *serial (when
// given), so saves can name the camp's props. Returns what was placed; placed is false (and nothing is put down) when
// the ground can't hold all of it.
struct CampKit {
    bool placed = false;
    float x = 0.0f, z = 0.0f;          // the flag's spot
    Entity flag = NULL_ENTITY;
    Entity vault = NULL_ENTITY;
    std::vector<Entity> trees;
};
CampKit placeCampKit(World& w, Terrain& t, int faction, float cx, float cz, float yaw, int* serial = nullptr);
// Whether the whole kit fits around (cx, cz): every piece's tile walkable and free of props
bool campKitFits(World& w, const Terrain& t, float cx, float cz);
// Serials of the camps' props start here, after any a level places, so they never meet
constexpr int CAMP_SERIAL_BASE = 1000000;
// The day's worth of logs a new vault starts with, in-game hours
constexpr int CAMP_UPKEEP_DAYS = 3;
// Wear and breakage for the game time that passed (gameSeconds): flags and foundations wear down as described above,
// then whatever is worn through breaks, and props whose foundation is gone break with it
void claimDecaySystem(World& w, double gameSeconds);

// The lines the hover list shows for a flag: its faction, its health and whether its claim has a vault. Empty for any
// entity that is not a flag.
std::vector<std::string> flagInfoLines(World& w, Entity e);

// The lines Show Prop Info puts under a prop's name: who owns it, its health or condition and whether it is decaying,
// its lock (and whether it is open to the player), and a vault's upkeep. Empty when there is nothing to say.
std::vector<std::string> propInfoLines(World& w, Entity e);
