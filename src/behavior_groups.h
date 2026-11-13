#pragma once
#include <string>
#include <vector>
#include "behavior.h"
#include "ecs.h"
#include "schedule.h"

// Behavior groups, kept by your Altar. Each of your citizens follows one group (Group 1 unless moved). A group is a task
// list like a unit's, which may also hold Group tasks: one stands for the whole list of another group or of a Default
// Behavior, at its place, so a group can take the usual behavior from one and add its own tasks below it. Group 1 starts
// as just the Citizen Default Behavior. The two Default Behaviors are kept here too but aren't groups: the Military one is
// what a unit of yours gets on joining the military, the Citizen one is what groups take in.
inline constexpr int BEHAVIOR_GROUP_MAX = 64;
inline constexpr int BEHAVIOR_GROUP_TASKS = BEHAVIOR_TASKS_MAX;  // tasks in one group's list (a unit's own list holds BEHAVIOR_LIMIT)

struct BehaviorGroups {
    std::vector<BehaviorRule> military, citizen;     // the Default Behaviors, which hold no Group tasks
    std::vector<std::vector<BehaviorRule>> groups;   // Group 1 is groups[0]
    Schedule schedule = defaultSchedule();           // the global schedule (schedule.h)
    std::vector<Schedule> groupSchedules;            // each group's own, by index: made when asked for (scheduleOfRef)
    int version = 0;                                 // raised on every change, so citizens take their lists again
    bool ready = false;
    // Your stores already given a haul task in the Citizen Default Behavior (sorted), so one you remove stays removed
    std::vector<ObjectId> listedStores;
    bool storesListed = false;
    int syncTick = 0;
};
inline constexpr int STORE_SYNC_STEPS = 30;   // simulation steps between looks for new or lost stores

// The World's groups, set up with their defaults the first time
BehaviorGroups& behaviorGroups(World& w);
void resetBehaviorGroups(BehaviorGroups& g);
// The tasks a citizen starts with: needs, sleeping, building, hauling, crafting, patrolling and the recreation tasks. What your Citizen
// group starts as, and what an NPC faction's citizens are given.
std::vector<BehaviorRule> defaultCitizenBehaviors();

// The list a ref names (a group's index, GROUP_REF_MILITARY or GROUP_REF_CITIZEN), or nullptr
std::vector<BehaviorRule>* groupList(BehaviorGroups& g, int ref);
const std::vector<BehaviorRule>* groupList(const BehaviorGroups& g, int ref);
bool validGroupRef(const BehaviorGroups& g, int ref);

// The tasks a unit following ref does: Group tasks replaced by the lists they name (each list only once, so groups that
// name each other in a ring end), and a task that comes up again kept only where it first came
std::vector<BehaviorRule> expandGroup(const BehaviorGroups& g, int ref);

// Whether the list of from may hold a Group task naming to: not itself, not a Default Behavior's list, and not a group
// whose own tasks already lead back to from
bool canIncludeGroup(const BehaviorGroups& g, int from, int to);

// A new group at the end, empty. Its index, or -1 once there are BEHAVIOR_GROUP_MAX.
int addBehaviorGroup(World& w);
// Removes a group (never Group 1): its citizens move to Group 1, Group tasks naming it go, and those after it move up one
bool removeBehaviorGroup(World& w, int group);
// Marks the groups changed, so every citizen takes its list again
void behaviorGroupsChanged(World& w);

// Moves one of your citizens to a group
bool setCitizenGroup(World& w, ObjectId u, int group);
int citizensInGroup(World& w, int group);

// Gives each of your citizens its group's tasks whenever the groups or its group changed, keeping how long each task it
// already had waits
void behaviorGroupSystem(World& w);

// Your stores (storage, and the Altar) in your claim
std::vector<ObjectId> groupStores(World& w);
// Each store of yours not yet listed gets a haul task in the Citizen Default Behavior, and haul tasks to stores that
// are gone leave every list. True if anything changed.
bool storeHaulSync(World& w);
// Counts every store there is now as listed (a loaded game whose lists already say what they haul)
void markStoresListed(World& w);
