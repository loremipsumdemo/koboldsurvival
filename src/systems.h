#pragma once
#include <vector>
#include "components.h"
#include "ecs.h"
#include "rng.h"
#include "terrain.h"

// A unit or a mob of this faction with the parts every entity has: yours, or hostile or neutral by its faction's reputation
ObjectId makeEntity(World& w, const Transform& at, int faction);
Dialogue defaultDialogue();
void setupWorld(World& w, Terrain& t, Rng& rng, bool playerCamp = true);

void ensurePlayerUnits(World& w, Terrain& t, Rng& rng, int count);
void ensurePlayerMobs(World& w, Terrain& t, Rng& rng, const std::vector<MobKind>& kinds, int firstSlot);
ObjectId spawnMob(World& w, const Terrain& t, Rng& rng, MobKind kind, int faction, float x, float z);
// A whole unit of this faction, standing just here: a player unit, or an NPC with its gear and its faction's patrol
ObjectId spawnUnit(World& w, const Terrain& t, Rng& rng, int faction, float x, float z);
Stats mobStats(MobKind kind, Rng& rng);
void finishDying(World& w, ObjectId u);
bool canHoldItem(World& w, ObjectId unit, int item);
bool inventoryLocked(World& w, ObjectId unit);

Stats randomStartingStats(Rng& rng);

BodyType randomBodyType(Rng& rng);
std::string randomUnitName(Rng& rng, BodyType body, const std::vector<std::string>& taken);

void spawnGroundItems(World& w, Terrain& t, Rng& rng, int perItem);

void issueMoveOrder(World& w, Terrain& t, const std::vector<ObjectId>& units, Vec2 clickPoint);
void issueFormationOrder(World& w, Terrain& t, const std::vector<ObjectId>& units, Vec2 point, const FormationSpec& spec);
// Units take up the shape where they are, or if they are on the way somewhere, on their way there
void reformUnits(World& w, Terrain& t, const std::vector<ObjectId>& units, const FormationChoice& choice);
// What dragging from a to b asks for: stand across the drag, looking away from where the units are now
FormationSpec formationFromDrag(World& w, const std::vector<ObjectId>& units, Vec2 a, Vec2 b, const FormationChoice& choice, Vec2& point);
// Where an order would put them, to show while dragging
std::vector<Vec2> previewFormation(World& w, const Terrain& t, const std::vector<ObjectId>& units, Vec2 point, const FormationSpec& spec);
void steerUnits(World& w, const std::vector<ObjectId>& units, Vec2 target);

void issuePickupOrder(World& w, Terrain& t, const std::vector<ObjectId>& units, ObjectId item);
void issuePickupChain(World& w, Terrain& t, const std::vector<ObjectId>& units, const std::vector<ObjectId>& items);
void pickupChainSystem(World& w, Terrain& t);

void issueAttackOrder(World& w, const std::vector<ObjectId>& units, ObjectId target);
void issueTalkOrder(World& w, const std::vector<ObjectId>& units, ObjectId npc, bool trade = false, bool shop = false);
void issueFollowOrder(World& w, const std::vector<ObjectId>& units, ObjectId target);
void issueDefendOrder(World& w, const std::vector<ObjectId>& units, ObjectId ward);
void defendSystem(World& w);
void makeHostile(World& w, ObjectId unit);
void clearTalkOrders(World& w);

void squadSystem(World& w, float dt, bool running = false);
void steeringSystem(World& w, Terrain& t, float dt, bool running = false);
int pickupSystem(World& w, Terrain& t);
void cleanupActiveSquads(World& w);
void needsSystem(World& w, int hours, double gameSeconds = 0.0);

void issuePropOrder(World& w, Terrain& t, const std::vector<ObjectId>& units, ObjectId prop, PropAction action);
enum class WorkStart { Started, TooFar, Full, NoInput, Locked, NotLocked, SkillTooLow };
WorkStart tryStartWork(World& w, ObjectId unit, ObjectId prop);
struct PropArrival {
    ObjectId unit = NO_OBJECT;
    ObjectId prop = NO_OBJECT;
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
int storeCarried(World& w, ObjectId unit, ObjectId prop, int item);

constexpr int AUTO_LOAD = 10;
constexpr float AUTO_RESUME_SECONDS = 8.0f;
void automationSystem(World& w, Terrain& t, float dt, Rng& rng);
void wildMobSystem(World& w, Terrain& t, float dt);
struct WorkGain {
    enum class Kind { Item, Unlocked, LockpickFailed, Trained };
    ObjectId unit = NO_OBJECT;
    int item = 0;
    Kind kind = Kind::Item;
    float xp = 0.0f;
    int propKind = -1;
};
std::vector<WorkGain> workSystem(World& w, double gameSeconds, Rng& rng);
WorkStart tryStartUnlock(World& w, ObjectId unit, ObjectId prop);
enum class CraftState { Ready, NoInput };
CraftState craftStateOf(World& w, ObjectId prop);
void propStorageChanged(World& w, ObjectId prop);
bool propInTheWay(World& w, ObjectId unit, ObjectId prop);
ObjectId propOccupant(World& w, ObjectId prop);
bool propAcceptsItem(PropKind kind, int item);
Storage startingStorage(PropKind kind, Rng& rng);
int recipeOutput(PropKind kind, int input);
bool lockedIn(World& w, ObjectId u);
ObjectId occupiedProp(World& w, ObjectId u);
bool cannotAct(World& w, ObjectId u);
bool cannotTakeOrder(World& w, ObjectId u);
void breakFree(World& w, ObjectId u);
bool canBeCarried(World& w, ObjectId target);

constexpr float CARRY_REACH = 1.2f;
constexpr int RECOVERY_HEALTH = 10;
void knockOut(World& w, ObjectId u);
void downedSystem(World& w);
constexpr double CORPSE_BONES_HOURS = 24.0;
void corpseDecaySystem(World& w, double gameSeconds);
constexpr double BONES_DECAY_HOURS = 24.0;
void bonesDecaySystem(World& w, double gameSeconds);
constexpr double GROUND_FOOD_HOURS = 24.0;
void groundFoodSystem(World& w, double gameSeconds);
struct WakeChanges {
    std::vector<ObjectId> woke, died;
};
WakeChanges unconsciousSystem(World& w, double gameSeconds);
inline bool canPlayDead(World& w, ObjectId u) { return w.has<Commandable>(u) && !w.has<Mob>(u); }
bool hostileNear(World& w, ObjectId u);
void playDeadSystem(World& w);
void leaveOpenLocksSystem(World& w, Terrain& t);
void occupyBanSystem(World& w, float dt);
void patrolSpawnSystem(World& w, Terrain& t, float dt);
bool issueCarryOrder(World& w, Terrain& t, const std::vector<ObjectId>& units, ObjectId target);
void putDown(World& w, ObjectId carrier);
bool issueLootOrder(World& w, Terrain& t, const std::vector<ObjectId>& units, ObjectId target);
constexpr float LOOT_RANGE = 2.0f;
bool lootTake(World& w, ObjectId looter, ObjectId target, bool gear, int slot, std::string& why);
int lootAll(World& w, ObjectId looter, ObjectId target);
struct CarryEvents {
    std::vector<ObjectId> picked;
    std::vector<std::pair<ObjectId, ObjectId>> loot;
};
CarryEvents carrySystem(World& w);

void passiveCraftSystem(World& w, double gameSeconds);
enum class PassiveState { Burning, NoFuel, OutputFull };
PassiveState passiveStateOf(World& w, ObjectId prop);

std::vector<EmoteId> emotesOf(World& w, ObjectId u);
std::string taskNameOf(World& w, ObjectId u);
bool unitIdle(World& w, ObjectId u);

bool disbandUnit(World& w, ObjectId u);
