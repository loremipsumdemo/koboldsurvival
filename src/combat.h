#pragma once
#include <string>
#include <vector>
#include "components.h"
#include "ecs.h"
#include "stats.h"
#include "resources.h"
#include "sounds.h"

// Combat: the formulas, the fight that runs each frame, and the text it writes (the log and the damage
// numbers). The formulas are plain functions over their inputs, so the tests can check them directly.

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
constexpr float FLOAT_LIFE = 1.0f;          // seconds a damage number stays on screen
constexpr size_t LOG_LIMIT = 500;           // messages each kind keeps, and the talk log keeps: the newest are kept

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

// What a game message is about, so the Game tab can hide each kind. Other messages are never hidden.
// XpAthletics and XpEndurance are experience in those two stats, which comes from walking and running so often that
// it has its own filter toggles, off at first. XpCarrying is the Strength gained for carrying a load while walking,
// which comes as often, and is off at first too.
enum class LogKind { Other, Seen, Hit, Miss, Damage, Xp, Pickup, Drop, Loot, XpAthletics, XpEndurance, XpCarrying };
constexpr int LOG_KIND_COUNT = 12;

// Gives a unit fractional XP in one stat. Whole points go to the stat, and the rest waits in the unit's
// XpBuffer until it adds up to a point. Only player units gain XP.
// why names the action the XP came from, for the log; nullptr writes no message. kind is the Game tab filter its
// message falls under: Xp sorts it by the stat (Athletics and Endurance have their own), or a kind given here.
void grantXp(World& w, Entity e, int stat, float amount, const std::string& why = std::string(), LogKind kind = LogKind::Xp);
Stats statsOf(World& w, Entity e);   // base stats plus the gear and buffs on the unit

// ---- Log and damage numbers ----

// Green when a player unit damages a hostile, red when a hostile damages a player unit, gray otherwise
enum class Tint { Gray, Green, Red };
Tint tintFor(World& w, Entity attacker, Entity target);


struct LogLine {
    std::string text;
    Tint tint = Tint::Gray;
    LogKind kind = LogKind::Other;
};

// Which kinds the Game tab shows. All are on at first, except Athletics and Endurance experience and the Strength
// experience for carrying a load.
struct LogFilter {
    bool on[LOG_KIND_COUNT];
    LogFilter() {
        for (bool& b : on) b = true;
        on[(int)LogKind::XpAthletics] = false;
        on[(int)LogKind::XpEndurance] = false;
        on[(int)LogKind::XpCarrying] = false;
    }
};

// Every line the fight has written, oldest first, up to LOG_LIMIT
struct CombatLog {
    std::vector<LogLine> lines;
    // Swings that landed, and swings that did not (missed or dodged), since the level began. The caller compares
    // these before and after a frame, to play a sound once for what happened in it.
    size_t hits = 0;
    size_t misses = 0;
    // What was said in talks, as "Name: text", for the Message tab
    std::vector<LogLine> spoken;
    // Messages of each kind held now, so a kind over LOG_LIMIT drops its own oldest and nothing else
    size_t kindCount[LOG_KIND_COUNT] = {};
    // Adds a line to the talk log, keeping the newest LOG_LIMIT
    void say(const std::string& text);
    void add(const std::string& text, Tint tint = Tint::Gray, LogKind kind = LogKind::Other);
};

// Game messages written outside a fight (XP, pickups, drops). They wait until flushQueuedLog adds them to a log, on
// the next frame. discardQueuedLog drops them, when a new level starts.
void queueLog(const std::string& text, LogKind kind);
void flushQueuedLog(CombatLog& log);
void discardQueuedLog();

// The lines an Information Panel tab shows: 0 what was said in talks, 1 the game messages that pass the filter
std::vector<const LogLine*> infoLines(const CombatLog& log, int tab, const LogFilter& f);

// A damage number over a unit's head, at the place the hit landed. age counts up from 0.
struct FloatingNumber {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    int amount = 0;
    Tint tint = Tint::Gray;
    float age = 0.0f;
};

// A line of text over a unit for a moment, such as the value of a trade
constexpr float TRADE_LABEL_LIFE = 2.0f;     // seconds
struct TradeLabel {
    Entity unit = NULL_ENTITY;
    std::string text;
    float age = 0.0f;
    bool red = false;   // an item lost ("-Iron Ore"), drawn in red; the rest are yellow
};

// Labels any system can put over a unit, like the trade's: an item gained ("+Iron Ore", "+3x Iron Ore"), or what a
// used item did ("Health +15"). The game takes them each frame and shows them with its trade labels.
void queueUnitLabel(Entity unit, const std::string& text, bool red = false);
std::vector<TradeLabel> takeQueuedLabels();
// A world sound from where a unit or prop stands (sounds.h); heard in full when it has no place
void queueSoundFrom(World& w, Sfx s, Entity e);
void discardQueuedLabels();
// The label for count of an item gained: "+Iron Ore" for one, "+3x Iron Ore" for more
std::string itemGainLabel(int count, const std::string& itemName);
// The label for count of an item that left a unit's inventory: "-Iron Ore", "-3x Iron Ore". Queue it with red set.
std::string itemLossLabel(int count, const std::string& itemName);
// The same, for an item id, with the item's name in the language in use
std::string itemGainLabelFor(int count, int item);
std::string itemLossLabelFor(int count, int item);

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
// The tiles from which a unit can hit now: its ranged weapon's range while it has ammo for it, else ATTACK_REACH
float reachOf(World& w, Entity e);

// Random gear for a hostile: each slot about half the time, with something that fits it
Equipment randomHostileGear(Rng& rng);

// Takes a unit out of the world, and out of its squad's count
void removeUnit(World& w, Entity e);

// Runs once each frame, before movement. Updates timers, stamina and health; notices and spotting;
// target choice; potions and elixirs; and every swing that is ready and in reach. Units that die are
// removed here. Damage numbers are added to floats, and what happened is written to log.
void combatSystem(World& w, Rng& rng, float dt, CombatLog& log, std::vector<FloatingNumber>& floats);
