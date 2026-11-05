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
constexpr float STEALTH_RANGE = 2.0f;        // tiles from a hostile within which an unnoticed unit earns stealth XP
constexpr float AGGRO_RANGE = 6.0f;         // a hostile can notice a player unit this close
constexpr float LEASH_RANGE = 12.0f;        // a hostile gives up a chase past this distance
constexpr float WITNESS_RANGE = 9.0f;       // units this close to an attack see it, and may join in
constexpr float SNEAK_SPEED = 0.5f;         // a sneaking unit walks at this share of its speed, and never runs
constexpr float SNEAK_NOTICE_RANGE = 0.6f;  // and can only be noticed from this share of the usual range
constexpr float SNEAK_FAIL_BASE = 0.20f;    // its share of notices that fail: this, plus twice the usual for Stealth
constexpr float SNEAK_FAIL_CAP = 0.80f;
constexpr float BLOCK_MODE_FACTOR = 2.0f;   // a unit in Block mode blocks this many times as often
constexpr float BLOCK_MODE_CAP = 0.75f;
constexpr float HOSTILE_WALK_SPEED = 2.2f;  // tiles per second
constexpr float CHASE_STOP_SHARE = 0.8f;    // a hostile stops walking once it is this share of reach away
constexpr float ENCUMBRANCE_XP_FROM = 0.5f; // Strength XP starts once encumbrance reaches this
constexpr float ENDURANCE_XP_PER_TILE = 0.1f;
constexpr float RUN_SPEED_BONUS = 0.5f;     // running is this much faster than walking, before Athletics
constexpr float RUN_ATHLETICS_PER_LEVEL = 0.01f; // and each Athletics level above 1 adds this much more to run speed

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
    float maxStamina = 0.0f;
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

// ---- Encumbrance and movement ----

// Weight of everything worn
float wornWeight(const Equipment& worn);

// Weight carried: the inventory plus everything worn
float carriedWeightOf(World& w, Entity e);

// 0..1: carried weight against the carry limit that Strength gives. At or above the limit it is 1.
float encumbranceFor(float carried, const Stats& s);
float encumbranceOf(World& w, Entity e);

// Tiles per second a unit can walk now. Its base speed, Athletics, and the encumbrance all count; a
// unit with no stamina left walks at half speed; a downed unit walks at zero. Every unit uses the same
// formula, so a red unit's base speed and stats count the same way. Running is faster again:
// RUN_SPEED_BONUS, plus RUN_ATHLETICS_PER_LEVEL for each Athletics level above 1. Only a player unit can
// run, since the run and walk choice is the player's.
float walkSpeedOf(World& w, Entity e, bool running = false);

// Strength XP for walking: nothing below ENCUMBRANCE_XP_FROM, then rising to 0.5 XP a second at a
// full load. Only counts while the unit is moving, which the caller checks.
float strengthXpWhileWalking(float encumbrance, float seconds);

// ---- Experience ----

// Gives a unit fractional XP in one stat. Whole points go to the stat, and the rest waits in the unit's
// XpBuffer until it adds up to a point. Only player units gain XP.
// why names the action the XP came from, for the log; nullptr writes no message. kind is the Game tab filter its
// message falls under: Xp sorts it by the stat (Athletics and Endurance have their own), or a kind given here.
void grantXp(World& w, Entity e, StatId stat, float amount, const std::string& why = std::string(), LogKind kind = LogKind::Xp);
Stats statsOf(World& w, Entity e);   // base stats plus the gear and buffs on the unit

// ---- Units and the fight ----

// A unit's name, for the log
std::string nameOf(World& w, Entity e);

// True when a unit's health is 0 or less. Downed units can't walk or swing, and the next hit kills them.
bool isDowned(World& w, Entity e);

// Adds the Combat component to a unit that doesn't have one
void ensureCombat(World& w, Entity e);

// ---- Factions in the fight ----
// A unit's faction id (factions.h); the Player Faction for one with none
int factionOf(World& w, Entity e);
// Whether two units fight each other: their factions are hostile, or one was provoked by the other's faction. Units of
// the same faction never fight.
bool unitsHostile(World& w, Entity a, Entity b);
// Whether a unit that is not yours fights your units: the Hostile marker it carries (and Neutral when not)
bool hostileToPlayer(World& w, Entity e);
// Empties every unit's list of the hostiles it has spotted, once each in-game hour: the list stays short, and a hostile
// seen again in a later hour gives Perception XP again
void forgetSpotted(World& w);
// Makes a unit treat a faction as hostile until it has seen none of its units for PROVOKED_SECONDS (Provoked)
void provoke(World& w, Entity unit, int faction);
// Each frame: counts down each provocation while the unit sees none of that faction's units (and starts it over while
// it sees one), and keeps every other unit's Hostile or Neutral marker in step with its faction's reputation with yours
// and with Provoked. A unit that turns hostile, or calms down again, says so in the log.
void factionSystem(World& w, float dt);
// A unit's fighting modes from its Behavior tab (Behaviors): never false for a unit without one
bool isPassive(World& w, Entity e);
bool isBlocking(World& w, Entity e);
bool isSneaking(World& w, Entity e);
// The tiles from which a unit can hit now: its ranged weapon's range while it has ammo for it, else its melee reach
float reachOf(World& w, Entity e);
// How big a unit is against a person (1): a mob kind's size (mobSize), else 1
float unitSize(World& w, Entity e);
// Melee reach: ATTACK_REACH, longer for a unit bigger than a person (by the square root of its size), never shorter
float meleeReachOf(World& w, Entity e);

// Random gear for a hostile: each slot about half the time, with something that fits it
Equipment randomHostileGear(Rng& rng);

// Takes a unit out of the world, and out of its active squad's count
void removeUnit(World& w, Entity e);

// Runs once each frame, before movement. Updates timers, stamina and health; notices and spotting;
// target choice; potions and elixirs; and every swing that is ready and in reach. Units that die are
// removed here. Damage numbers are added to floats, and what happened is written to log.
void combatSystem(World& w, Rng& rng, float dt, GameLog& log, std::vector<FloatingNumber>& floats);
