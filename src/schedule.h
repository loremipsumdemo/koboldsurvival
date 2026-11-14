#pragma once
#include <array>
#include <cmath>
#include <map>
#include <string>
#include "behavior.h"
#include "ecs.h"

// The daily schedule: each hour of the day is Sleep, Work or Recreation. A unit's tasks follow it:
//  - Sleep: the Sleep task takes the unit to a bed (Rest and the other Needs tasks still come first whenever they are due)
//  - Work: building, hauling, harvesting, crafting, patrolling and the like
//  - Recreation: Socialize (with others of its faction that are at recreation too) and Sit Down (on a seat)
// Needs tasks (eating, healing, resting, rescuing, dealing with the downed) are not scheduled: they happen whenever they are due.
// The Citizen Default is the global schedule. A behavior group follows it unless the group has its own (override) set.
// Units of other factions follow the global schedule too, and wild mobs none.
enum class Activity : unsigned char { Sleep = 0, Work = 1, Recreation = 2 };
inline constexpr int SCHEDULE_HOURS = 24;
inline constexpr int ACTIVITY_COUNT = 3;
inline constexpr const char* ACTIVITY_KEYS[] = { "UILABEL_ACTIVITY_SLEEP", "UILABEL_ACTIVITY_WORK", "UILABEL_ACTIVITY_RECREATION" };

struct Schedule {
    std::array<unsigned char, SCHEDULE_HOURS> hours{};   // an Activity for each hour
    bool override = false;                               // a group's own schedule is in use, in place of the global one
};
// Sleep from 22:00 to 5:59, work 6:00 to 13:59, recreation 14:00 to 21:59: eight hours of each
Schedule defaultSchedule();
// A schedule as 24 digits (0 sleep, 1 work, 2 recreation), for saves
std::string scheduleText(const Schedule& s);
bool scheduleFromText(const std::string& text, Schedule& out);

struct BehaviorGroups;
// The schedule for a ref: -1 the global one, a group's index its own (made when asked for)
Schedule& scheduleOfRef(BehaviorGroups& g, int ref);

// What the unit is meant to be doing this hour: its group's own schedule if it follows one of its own, else the global
Activity activityOf(World& w, ObjectId u);
Activity activityAtHour(World& w, ObjectId u, int hour);
// Units of the other factions follow schedules of their own, which you do not change: most the default one, and up to 1 + 10% of a
// faction's units (when it has more than one) the Irregular ones, so that they are awake when the rest sleep.
//   Default: sleep 22:00 to 5:59, work 6:00 to 13:59, recreation 14:00 to 21:59
//   Irregular 1 (Group 2): the default with Sleep and Recreation swapped
//   Irregular 2 (Group 3): the default with Sleep and Work swapped
inline constexpr int NPC_SCHEDULE_KINDS = 3;
struct NpcSchedule {
    int kind = 0;   // 0 default, 1 and 2 the Irregular ones. Saved when not 0.
};
Schedule npcSchedule(int kind);
// The most units of a faction of this many that are on the Irregular schedules, together: 0 for one unit, else 1 + 10%
int npcIrregularMost(int units);
// Sees to each faction's units (every NPC_SCHEDULE_LOOK_SECONDS): they keep to the default schedule but for the most that may be irregular
void npcScheduleSystem(World& w, float dt);
inline constexpr float NPC_SCHEDULE_LOOK_SECONDS = 5.0f;
struct NpcScheduleClock {
    float left = 0.0f;
};
// Whether a task may be started or go on now: Needs tasks always; Sleep in Sleep hours; Work tasks in Work hours; Socialize and
// Sit Down in Recreation hours. A wild mob is never held to the schedule.
bool scheduleAllows(World& w, ObjectId u, BehaviorKind kind);
// A task of the Work hours (building, hauling, harvesting, crafting, patrolling and the like)
inline bool isWorkTask(BehaviorKind kind) {
    const TaskCategory c = taskCategory(kind);
    return c == TaskCategory::Building || c == TaskCategory::Hauling || c == TaskCategory::Harvesting || c == TaskCategory::Movement;
}
// How long a chat or a sit lasts, in real seconds, and how long before the same task is taken up again
inline constexpr float SOCIAL_CHAT_MIN = 20.0f, SOCIAL_CHAT_MAX = 50.0f;
inline constexpr float SOCIAL_SIT_MIN = 60.0f, SOCIAL_SIT_MAX = 120.0f;
inline constexpr float SOCIAL_PAUSE_MIN = 15.0f, SOCIAL_PAUSE_MAX = 40.0f;
inline constexpr float SOCIAL_NEAR = 2.2f;          // how close it stands to talk
inline constexpr float SOCIAL_SEARCH_RANGE = 40.0f;
// Comfort a unit gains an hour, socializing
inline constexpr double SOCIAL_COMFORT_PER_HOUR = 15.0;
// ... playing at a Game Table, and dancing
inline constexpr double PLAY_COMFORT_PER_HOUR = 20.0, DANCE_COMFORT_PER_HOUR = 10.0;
// A Game Table has a place on each of its four sides, this far from its middle
inline constexpr float TABLE_SEAT_DISTANCE = 1.15f;
// Dancing round a Campfire inside an Altar's claim: up to 8 on a Circle formation with this much between neighbours; round an
// Altar up to 32 on the larger one. The whole circle swings clockwise while it widens, then back counterclockwise while it
// narrows, and so on, only while someone is dancing on it.
inline constexpr float DANCE_CAMPFIRE_SPACING = 1.55f, DANCE_ALTAR_SPACING = 1.0f;
inline constexpr int DANCE_CAMPFIRE_DANCERS = 8, DANCE_ALTAR_DANCERS = 32;
inline constexpr float DANCE_PERIOD = 12.0f;   // real seconds for the circle to swing out and back
inline constexpr float DANCE_TURN = 0.6f;      // radians it swings each way
inline constexpr float DANCE_GROW = 0.25f;     // share of its size it widens and narrows by
// How long each Campfire or Altar's circle has been going, in real seconds. A circle with no dancer on it is not here: it is at rest.
struct DanceRings {
    std::map<ObjectId, double> seconds;
};
// A dancer hops as it goes round: this long for each hop, this high (in tiles), with a light thud as the foot lands. The hop clock runs
// with the game, so the hops stop while it is paused; each dancer is a little out of step with the rest, by its own offset.
inline constexpr float DANCE_HOP_SECONDS = 0.5f, DANCE_HOP_HEIGHT = 0.22f;
struct DanceHops {
    double clock = 0.0;
};
// Where in its hop a dancer is, 0 to 1: 0 is the foot on the ground
inline float danceHopPhase(double clock, ObjectId dancer) {
    const double offset = (double)(dancer * 2654435761u % 1000u) / 1000.0;
    const double t = clock / (double)DANCE_HOP_SECONDS + offset;
    return (float)(t - std::floor(t));
}
// On a unit that is dancing on a circle: it ignores the crowd's collision until it stops
struct Dancing {
    // The Campfire or Altar it dances round, and where that stands, kept from step to step: when the dance ends the
    // dancer goes to open ground by it, even if it is gone by then. Not known until the dancer's first step on the circle.
    ObjectId center = NO_OBJECT;
    float centerX = 0.0f, centerZ = 0.0f;
    bool known = false;
};
// On a unit that has just stopped dancing. It is already at its new place, for everything the game works out, but for
// this long it is drawn going there from where it danced, and does nothing else: it stays put and takes up no task.
inline constexpr float DANCE_LEAVE_SECONDS = 0.1f;
struct LeavingDance {
    float fromX = 0.0f, fromY = 0.0f, fromZ = 0.0f;
    float toX = 0.0f, toY = 0.0f, toZ = 0.0f;
    float left = DANCE_LEAVE_SECONDS;
    // How far along it is drawn, 0 (where it danced) to 1 (its new place)
    float along() const { return 1.0f - (left < 0.0f ? 0.0f : left) / DANCE_LEAVE_SECONDS; }
};
inline constexpr float DANCE_MIN = 40.0f, DANCE_MAX = 90.0f;   // real seconds

// A wild mob's sleep (wild_ai.cpp). In its sleep hours it checks once an hour that no hostile is within MOB_SLEEP_DANGER_RANGE: if
// none is, it sleeps (it stays put until its sleep hours end, it is hurt or it is disturbed); if one is, it moves away from the nearest
// by MOB_SLEEP_MOVE_MIN to MOB_SLEEP_MOVE_MAX tiles and stays awake till the next check. Not saved: a loaded mob looks again.
inline constexpr float MOB_SLEEP_DANGER_RANGE = 10.0f, MOB_SLEEP_MOVE_MIN = 1.0f, MOB_SLEEP_MOVE_MAX = 10.0f;
struct MobAsleep {
    long long checked = -1;   // the in-game hour (counted from the start) it last looked
    bool asleep = false;
    int hp = 0;               // its health when it last slept; hurt below this wakes it
};
