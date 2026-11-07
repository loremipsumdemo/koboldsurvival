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

inline void leaveSquad(World& w, ObjectId u) {
    if (const ActiveSquadMember* sm = w.get<ActiveSquadMember>(u)) {
        if (ActiveSquad* sq = w.get<ActiveSquad>(sm->squad); sq && sq->size > 0) --sq->size;
        w.remove<ActiveSquadMember>(u);
    }
}

constexpr int BASE_HP = 50;
constexpr int HP_PER_VITALITY = 5;
constexpr float UNARMED_ATTACK = 3.0f;
constexpr float UNARMED_WEIGHT = 0.5f;
constexpr float STRENGTH_ATTACK = 0.2f;
constexpr float MITIGATION_K = 50.0f;
constexpr float BLOCK_DAMAGE = 0.25f;
constexpr float ATTACK_REACH = 1.6f;
constexpr float TALK_RANGE = 1.3f;   // a little more than two units touching
constexpr float AGGRO_RANGE = 6.0f;
constexpr float LEASH_RANGE = 12.0f;
constexpr float WITNESS_RANGE = 9.0f;
constexpr float SNEAK_NOTICE_RANGE = 0.6f;
constexpr float SNEAK_NOTICE_FAIL = 0.20f;
constexpr float BLOCK_MODE_FACTOR = 2.0f;
constexpr float BLOCK_MODE_CAP = 0.75f;
constexpr float HOSTILE_WALK_SPEED = 2.2f;  // tiles per second
constexpr float CHASE_STOP_SHARE = 0.8f;
constexpr float CHASE_RESUME_SHARE = 0.95f;   // melee: stops at the share above, and only sets off again past this one

// At most this many melee fighters can attack one target. The rest wait in a loose ring outside, and look for another foe.
constexpr int ATTACK_SLOTS = 5;
constexpr float ATTACK_SLOT_RADIUS = 1.2f;
constexpr float ATTACK_SLOT_CLAIM_RANGE = 6.0f;   // from further off, a fighter just heads for the target
constexpr float ATTACK_WAIT_RADIUS = 2.6f;
constexpr float ATTACK_WAIT_NEAR = 1.9f;
constexpr float ATTACK_WAIT_FAR = 3.2f;
constexpr float ATTACK_WAIT_LOOK_SECONDS = 0.5f;

// Taking damage slows movement for a moment. More hits restart the moment, but don't slow it further.
constexpr float DAMAGE_SLOW_SECONDS = 0.6f;
constexpr float DAMAGE_SLOW_FACTOR = 0.5f;

struct CombatNumbers {
    int maxHp = 0;
    float attack = 0.0f;
    float defense = 0.0f;
    float mitigation = 1.0f;
    float accuracy = 0.0f;
    float dodge = 0.0f;
    float blockChance = 0.0f;
    float swingTime = 1.0f;
    bool ranged = false;
    float reach = ATTACK_REACH;
};

struct RangedProfile {
    float range = 0.0f;
    float accuracyAdd = 0.0f;
    float damageMul = 1.0f;
    float shotTimeMul = 1.0f;
    const char* ammoTag = "";
};
const RangedProfile* rangedProfileOf(int item);
inline bool isRangedWeapon(int item) { return rangedProfileOf(item) != nullptr; }

CombatNumbers deriveCombat(const Stats& s, const Equipment& worn, float buffAttack);
CombatNumbers numbersOf(World& w, ObjectId e);

int maxHealthFor(const Stats& s);

enum class Outcome { Miss, Dodge, Hit };

struct SwingResult {
    Outcome outcome = Outcome::Miss;
    bool blocked = false;
    int damage = 0;
};

SwingResult resolveSwing(const CombatNumbers& attacker, const CombatNumbers& defender, bool defenderDown,
                         float landRoll, float blockRoll, float spreadRoll);

std::string nameOf(World& w, ObjectId e);

bool isDowned(World& w, ObjectId e);

bool inPrisonerCage(World& w, ObjectId e);

void ensureCombat(World& w, ObjectId e);

bool isPassive(World& w, ObjectId e);
bool isBlocking(World& w, ObjectId e);
bool isSneaking(World& w, ObjectId e);
float reachOf(World& w, ObjectId e);

Equipment randomHostileGear(Rng& rng);

void combatSystem(World& w, const Terrain& t, Rng& rng, float dt);
bool canSee(World& w, const Terrain& t, ObjectId a, ObjectId b);
Vec2 attackSlotSpot(const Transform& target, int slot);
