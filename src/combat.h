#pragma once
#include <string>
#include <vector>
#include "components.h"
#include "ecs.h"
#include "stats.h"
#include "rng.h"
#include "sounds.h"
#include "game_log.h"
#include "world_labels.h"
#include "encumbrance.h"
#include "experience.h"

class Terrain;
#include "hostility.h"
#include "spatial.h"

// Takes a unit out of its active squad, if it is in one. The squad's size follows, which sets its formation's spacing.
inline void leaveSquad(World& w, Entity u) {
    if (const ActiveSquadMember* sm = w.get<ActiveSquadMember>(u)) {
        if (ActiveSquad* sq = w.get<ActiveSquad>(sm->squad); sq && sq->size > 0) --sq->size;
        w.remove<ActiveSquadMember>(u);
    }
}

// Combat: the formulas and the fight that runs each frame. What the fight writes goes to the game log (game_log.h)
// and the damage numbers over units (world_labels.h). The formulas are plain functions over their inputs.

// ---- Tuning ----
constexpr int BASE_HP = 50;                 // max health = BASE_HP + HP_PER_VITALITY per Vitality level
constexpr int HP_PER_VITALITY = 5;
constexpr float UNARMED_ATTACK = 3.0f;
constexpr float UNARMED_WEIGHT = 0.5f;
constexpr float STRENGTH_ATTACK = 0.2f;     // attack added per Strength level
constexpr float MITIGATION_K = 50.0f;       // share of damage taken is K / (K + defense)
constexpr float BLOCK_DAMAGE = 0.25f;       // damage kept on a blocked hit
// Tiles between unit centers that allow a swing: more than a tile's diagonal, so units on their one-tile squares
// (steerUnits) can reach a foe that stands corner to corner with them
constexpr float ATTACK_REACH = 1.6f;
constexpr float TALK_RANGE = 1.0f;           // tiles between a unit and an NPC that allow a talk
constexpr float AGGRO_RANGE = 6.0f;         // a hostile can notice a player unit this close
constexpr float LEASH_RANGE = 12.0f;        // a hostile gives up a chase past this distance
constexpr float WITNESS_RANGE = 9.0f;       // units this close to an attack see it, and may join in
constexpr float SNEAK_NOTICE_RANGE = 0.6f;  // and can only be noticed from this share of the usual range
constexpr float SNEAK_NOTICE_FAIL = 0.20f;  // and this share of the notices that would catch it fail
constexpr float BLOCK_MODE_FACTOR = 2.0f;   // a unit in Block mode blocks this many times as often
constexpr float BLOCK_MODE_CAP = 0.75f;
constexpr float HOSTILE_WALK_SPEED = 2.2f;  // tiles per second
constexpr float CHASE_STOP_SHARE = 0.8f;    // a hostile stops walking once it is this share of reach away

// What a unit fights with, worked out from its stats, worn gear and buffs. Never stored: it is
// recalculated whenever it is needed, so it always matches the current stats and gear.
struct CombatNumbers {
    int maxHp = 0;
    float attack = 0.0f;       // main hand (or unarmed), plus 0.2 per Strength level, plus any elixir
    float defense = 0.0f;      // defense of everything worn
    float mitigation = 1.0f;   // share of damage that gets through
    float accuracy = 0.0f;     // chance a swing lands before dodging
    float dodge = 0.0f;        // chance to dodge a swing that would land
    float blockChance = 0.0f;  // chance to block a hit that lands; 0 with nothing to block with
    float swingTime = 1.0f;    // seconds between swings (or shots)
    bool ranged = false;       // a ranged weapon is in the main hand: it shoots, with Projectiles in place of Melee
    float reach = ATTACK_REACH;   // tiles from which it can hit: ATTACK_REACH, or its ranged weapon's range
};

// ---- Ranged weapons ----
// A bow or a crossbow in the main hand shoots instead of swinging, from its range, using up one piece of its ammo (an
// item tagged with its ammo tag) for each shot. The ammo's attack_value is added to the shot's attack. With no ammo
// left it fights hand to hand. Which weapon is which comes from its tags in data/itemdb.txt:
//   bow_short: Short Bow, a shorter range and more accurate; arrows
//   bow_long:  Long Bow, a longer range and less accurate; arrows
//   crossbow:  Crossbow, like the Short Bow but half the damage and twice as fast; crossbow bolts (tag bolt)
struct RangedProfile {
    float range = 0.0f;          // tiles
    float accuracyAdd = 0.0f;    // added to the chance to hit
    float damageMul = 1.0f;
    float shotTimeMul = 1.0f;    // times the seconds between shots
    const char* ammoTag = "";
};
// The ranged profile of a weapon item, or nullptr for anything that is not a ranged weapon
const RangedProfile* rangedProfileOf(int item);
inline bool isRangedWeapon(int item) { return rangedProfileOf(item) != nullptr; }

// The numbers for a unit from its stats, its worn gear and the attack its elixir adds
CombatNumbers deriveCombat(const Stats& s, const Equipment& worn, float buffAttack);

// Health a unit can have at most: BASE_HP plus HP_PER_VITALITY for each Vitality level
int maxHealthFor(const Stats& s);

// ---- Swings ----

enum class Outcome { Miss, Dodge, Hit };

struct SwingResult {
    Outcome outcome = Outcome::Miss;
    bool blocked = false;   // a hit that the defender blocked down to BLOCK_DAMAGE
    int damage = 0;         // 0 unless the swing hit
};

// Resolves one swing from three rolls, each in [0, 1): whether it lands (hit, dodge or miss), whether
// a hit is blocked, and where the damage falls in its spread. A downed defender can't dodge or block.
SwingResult resolveSwing(const CombatNumbers& attacker, const CombatNumbers& defender, bool defenderDown,
                         float landRoll, float blockRoll, float spreadRoll);

// ---- Units and the fight ----

// A unit's name, for the log
std::string nameOf(World& w, Entity e);

// True when a unit's health is 0 or less. Downed units can't walk or swing, and the next hit kills them.
bool isDowned(World& w, Entity e);

// Adds the Combat component to a unit that doesn't have one
void ensureCombat(World& w, Entity e);

// A unit's fighting modes from its Behavior tab (Behaviors): never false for a unit without one
bool isPassive(World& w, Entity e);
bool isBlocking(World& w, Entity e);
bool isSneaking(World& w, Entity e);
// The tiles from which a unit can hit now: its ranged weapon's range while it has ammo for it, else its melee reach
float reachOf(World& w, Entity e);

// Random gear for a hostile: each slot about half the time, with something that fits it
Equipment randomHostileGear(Rng& rng);

// Runs once each frame, before movement. Updates timers and health; notices and spotting;
// target choice; potions and elixirs; and every swing that is ready and in reach. Units that die are
// removed here. Damage numbers are added to floats, and what happened is written to log.
void combatSystem(World& w, const Terrain& t, Rng& rng, float dt, GameLog& log, std::vector<FloatingNumber>& floats);
// Whether the ground leaves one unit in sight of another. Nothing hits or notices what it can't see.
bool canSee(World& w, const Terrain& t, Entity a, Entity b);
