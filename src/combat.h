#pragma once
#include <string>
#include <vector>
#include "components.h"
#include "ecs.h"
#include "resources.h"

// Combat: the formulas, the fight that runs each frame, and the text it writes (the log and the damage
// numbers). The formulas are plain functions over their inputs, so the tests can check them directly.

// ---- Tuning ----
constexpr int BASE_HP = 50;                 // max health = BASE_HP + HP_PER_VITALITY per Vitality level
constexpr int HP_PER_VITALITY = 5;
constexpr float UNARMED_ATTACK = 3.0f;
constexpr float UNARMED_WEIGHT = 0.5f;
constexpr float STRENGTH_ATTACK = 0.2f;     // attack added per Strength level
constexpr float MITIGATION_K = 50.0f;       // share of damage taken is K / (K + defence)
constexpr float BLOCK_DAMAGE = 0.25f;       // damage kept on a blocked hit
constexpr float ATTACK_REACH = 1.4f;        // tiles between unit centres that allow a swing
constexpr float AGGRO_RANGE = 6.0f;         // a hostile can notice a player unit this close
constexpr float LEASH_RANGE = 12.0f;        // a hostile gives up a chase past this distance
constexpr float HOSTILE_WALK_SPEED = 2.2f;  // tiles per second
constexpr float CHASE_STOP_SHARE = 0.8f;    // a hostile stops walking once it is this share of reach away
constexpr float ENCUMBRANCE_XP_FROM = 0.5f; // Strength XP starts once encumbrance reaches this
constexpr float ENDURANCE_XP_PER_TILE = 0.1f;
constexpr float FLOAT_LIFE = 1.0f;          // seconds a damage number stays on screen
constexpr size_t LOG_LIMIT = 500;           // lines the combat log keeps

// What a unit fights with, worked out from its stats, worn gear and buffs. Never stored: it is
// recalculated whenever it is needed, so it always matches the current stats and gear.
struct CombatNumbers {
    int maxHp = 0;
    float attack = 0.0f;       // main hand (or unarmed), plus 0.2 per Strength level, plus any elixir
    float defense = 0.0f;      // defence of everything worn
    float mitigation = 1.0f;   // share of damage that gets through
    float accuracy = 0.0f;     // chance a swing lands before dodging
    float dodge = 0.0f;        // chance to dodge a swing that would land
    float blockChance = 0.0f;  // chance to block a hit that lands; 0 with nothing to block with
    float swingTime = 1.0f;    // seconds between swings
    float maxStamina = 0.0f;
};

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
// unit with no stamina left walks at half speed; a downed unit walks at zero. Hostiles keep their
// base speed.
float walkSpeedOf(World& w, Entity e);

// Strength XP for walking: nothing below ENCUMBRANCE_XP_FROM, then rising to 0.5 XP a second at a
// full load. Only counts while the unit is moving, which the caller checks.
float strengthXpWhileWalking(float encumbrance, float seconds);

// ---- Experience ----

// Gives a unit fractional XP in one stat. Whole points go to the stat, and the rest waits in the unit's
// XpBuffer until it adds up to a point. Only player units gain XP.
void grantXp(World& w, Entity e, int stat, float amount);

// ---- Log and damage numbers ----

// Green when a player unit damages a hostile, red when a hostile damages a player unit, gray otherwise
enum class Tint { Gray, Green, Red };
Tint tintFor(World& w, Entity attacker, Entity target);

struct LogLine {
    std::string text;
    Tint tint = Tint::Gray;
};

// Every line the fight has written, oldest first, up to LOG_LIMIT
struct CombatLog {
    std::vector<LogLine> lines;
    void add(const std::string& text, Tint tint = Tint::Gray);
};

// A damage number over a unit's head, at the place the hit landed. age counts up from 0.
struct FloatingNumber {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    int amount = 0;
    Tint tint = Tint::Gray;
    float age = 0.0f;
};

// ---- Units and the fight ----

// A unit's name, for the log
std::string nameOf(World& w, Entity e);

// True when a unit's health is 0 or less. Downed units can't walk or swing, and the next hit kills them.
bool isDowned(World& w, Entity e);

// Adds the Combat component to a unit that doesn't have one
void ensureCombat(World& w, Entity e);

// Random gear for a hostile: each slot about half the time, with something that fits it
Equipment randomHostileGear(Rng& rng);

// Takes a unit out of the world, and out of its squad's count
void removeUnit(World& w, Entity e);

// Runs once each frame, before movement. Updates timers, stamina and health; notices and spotting;
// target choice; potions and elixirs; and every swing that is ready and in reach. Units that die are
// removed here. Damage numbers are added to floats, and what happened is written to log.
void combatSystem(World& w, Rng& rng, float dt, CombatLog& log, std::vector<FloatingNumber>& floats);
