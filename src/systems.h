#pragma once
#include <vector>
#include "components.h"
#include "ecs.h"
#include "rng.h"
#include "terrain.h"

// A unit of this faction with the parts every unit has: yours, or hostile or neutral by its faction's reputation
Entity makeUnit(World& w, const Transform& at, int faction);
Dialogue defaultDialogue();
void setupWorld(World& w, Terrain& t, Rng& rng, bool playerCamp = true);

void ensurePlayerUnits(World& w, Terrain& t, Rng& rng, int count);
void ensurePlayerMobs(World& w, Terrain& t, Rng& rng, const std::vector<MobKind>& kinds, int firstSlot);
Entity spawnMob(World& w, const Terrain& t, Rng& rng, MobKind kind, int faction, float x, float z);
Stats mobStats(MobKind kind, Rng& rng);
void finishDying(World& w, Entity u);
bool canHoldItem(World& w, Entity unit, int item);
bool inventoryLocked(World& w, Entity unit);

Stats randomStartingStats(Rng& rng);

BodyType randomBodyType(Rng& rng);
std::string randomUnitName(Rng& rng, BodyType body, const std::vector<std::string>& taken);

void spawnGroundItems(World& w, Terrain& t, Rng& rng, int perItem);

void issueMoveOrder(World& w, Terrain& t, const std::vector<Entity>& units, Vec2 clickPoint);
void steerUnits(World& w, const std::vector<Entity>& units, Vec2 target);

void issuePickupOrder(World& w, Terrain& t, const std::vector<Entity>& units, Entity item);
void issuePickupChain(World& w, Terrain& t, const std::vector<Entity>& units, const std::vector<Entity>& items);
void pickupChainSystem(World& w, Terrain& t);

void issueAttackOrder(World& w, const std::vector<Entity>& units, Entity target);
void issueTalkOrder(World& w, const std::vector<Entity>& units, Entity npc, bool trade = false, bool shop = false);
void issueFollowOrder(World& w, const std::vector<Entity>& units, Entity target);
void issueDefendOrder(World& w, const std::vector<Entity>& units, Entity ward);
void defendSystem(World& w);
void makeHostile(World& w, Entity unit);
void clearTalkOrders(World& w);

void squadSystem(World& w, float dt, bool running = false);
void steeringSystem(World& w, Terrain& t, float dt, bool running = false);
int pickupSystem(World& w, Terrain& t);
void cleanupActiveSquads(World& w);
void needsSystem(World& w, int hours, double gameSeconds = 0.0);

void issuePropOrder(World& w, Terrain& t, const std::vector<Entity>& units, Entity prop, PropAction action);
enum class WorkStart { Started, TooFar, Full, NoInput, Locked, NotLocked, SkillTooLow };
WorkStart tryStartWork(World& w, Entity unit, Entity prop);
struct PropArrival {
    Entity unit = NULL_ENTITY;
    Entity prop = NULL_ENTITY;
    PropAction action = PropAction::Use;
    bool taken = false;
    bool started = false;
    bool unreachable = false;
    bool locked = false;
    bool placed = false;
    bool corpse = false;
    bool occupyBanned = false;
    WorkStart craft = WorkStart::Started;
    bool automatic = false;
};
std::vector<PropArrival> propOrderSystem(World& w, Terrain& t);
int storeCarried(World& w, Entity unit, Entity prop, int item);

constexpr int AUTO_LOAD = 10;
constexpr float AUTO_RESUME_SECONDS = 8.0f;
void automationSystem(World& w, Terrain& t, float dt, Rng& rng);
void wildMobSystem(World& w, Terrain& t, float dt);
struct WorkGain {
    enum class Kind { Item, Unlocked, LockpickFailed, Trained };
    Entity unit = NULL_ENTITY;
    int item = 0;
    Kind kind = Kind::Item;
    float xp = 0.0f;
    int propKind = -1;
};
std::vector<WorkGain> workSystem(World& w, double gameSeconds, Rng& rng);
WorkStart tryStartUnlock(World& w, Entity unit, Entity prop);
enum class CraftState { Ready, NoInput };
CraftState craftStateOf(World& w, Entity prop);
void propStorageChanged(World& w, Entity prop);
bool propInTheWay(World& w, Entity unit, Entity prop);
Entity propOccupant(World& w, Entity prop);
bool propAcceptsItem(PropKind kind, int item);
Storage startingStorage(PropKind kind, Rng& rng);
int recipeOutput(PropKind kind, int input);
bool lockedIn(World& w, Entity u);
Entity occupiedProp(World& w, Entity u);
bool cannotAct(World& w, Entity u);
bool cannotTakeOrder(World& w, Entity u);
void breakFree(World& w, Entity u);
bool canBeCarried(World& w, Entity target);

constexpr float CARRY_REACH = 1.2f;
constexpr int RECOVERY_HEALTH = 10;
void knockOut(World& w, Entity u);
void downedSystem(World& w);
constexpr double CORPSE_BONES_HOURS = 24.0;
void corpseDecaySystem(World& w, double gameSeconds);
constexpr double BONES_DECAY_HOURS = 24.0;
void bonesDecaySystem(World& w, double gameSeconds);
constexpr double GROUND_FOOD_HOURS = 24.0;
void groundFoodSystem(World& w, double gameSeconds);
struct WakeChanges {
    std::vector<Entity> woke, died;
};
WakeChanges unconsciousSystem(World& w, double gameSeconds);
inline bool canPlayDead(World& w, Entity u) { return w.has<Commandable>(u) && !w.has<Mob>(u); }
bool hostileNear(World& w, Entity u);
void playDeadSystem(World& w);
void leaveOpenLocksSystem(World& w, Terrain& t);
void occupyBanSystem(World& w, float dt);
void patrolSpawnSystem(World& w, Terrain& t, float dt);
bool issueCarryOrder(World& w, Terrain& t, const std::vector<Entity>& units, Entity target);
void putDown(World& w, Entity carrier);
bool issueLootOrder(World& w, Terrain& t, const std::vector<Entity>& units, Entity target);
constexpr float LOOT_RANGE = 2.0f;
bool lootTake(World& w, Entity looter, Entity target, bool gear, int slot, std::string& why);
int lootAll(World& w, Entity looter, Entity target);
struct CarryEvents {
    std::vector<Entity> picked;
    std::vector<std::pair<Entity, Entity>> loot;
};
CarryEvents carrySystem(World& w);

void passiveCraftSystem(World& w, double gameSeconds);
enum class PassiveState { Burning, NoFuel, OutputFull };
PassiveState passiveStateOf(World& w, Entity prop);

std::vector<EmoteId> emotesOf(World& w, Entity u);
std::string taskNameOf(World& w, Entity u);
bool unitIdle(World& w, Entity u);

bool disbandUnit(World& w, Entity u);
