#pragma once
#include "ecs.h"
#include "rng.h"
#include "terrain.h"
#include <string>
#include <vector>

// Units that cannot get where they are going, because they are shut in (walls round them, a flight of stairs in the way). Any unit,
// yours and every other faction's:
//  - Where a unit last set out for is kept (PathGoal). A unit that has stopped short of it, with no way there, stands idle.
//  - Idle for STUCK_IDLE_SECONDS, it moves about at random for a few seconds, and then tries for the goal again.
//  - Having done that STUCK_ROUNDS_BEFORE_BREAKING times with no way found, it breaks what is in its way: on the way that costs
//    least, walls and doors (cost 20) before ladders (40) and stairs (60), one swing a second, the health of what it breaks running
//    out. (Props that stand about do not shut a way, so they never are what is in it.) Health mends by itself inside an Altar's
//    claim (claims.h).
//  - Still shut in a day (STUCK_FAINT_HOURS) after it began, it loses consciousness, and the day starts again.
//  - A unit that is walking but not getting anywhere (it keeps its place for STUCK_STILL_SECONDS) is helped along before any of that: it
//    plans its way again with the tile in its way shut for that one plan, then steps aside at random for a moment and plans again, and
//    the third time gives up the walk (what follows is as for a unit that has stopped). Each look costs a few comparisons, twice a second,
//    and only for a unit with a goal.
// (Only a walk that is planned with walkSoloTo, and your own move orders, are noticed.)
inline constexpr float STUCK_IDLE_SECONDS = 10.0f;
inline constexpr float STUCK_WANDER_MIN_SECONDS = 3.0f;
inline constexpr float STUCK_WANDER_MAX_SECONDS = 5.0f;
inline constexpr int STUCK_ROUNDS_BEFORE_BREAKING = 5;
inline constexpr double STUCK_FAINT_HOURS = 24.0;
inline constexpr float STUCK_SWING_SECONDS = 1.0f;
inline constexpr float STUCK_STILL_SECONDS = 2.5f;   // walking, but this long without going anywhere
inline constexpr float STUCK_STILL_DISTANCE = 0.3f;   // a tile's share it must go to count as getting somewhere
inline constexpr float STUCK_SIDESTEP_SECONDS = 1.5f;
inline constexpr float STUCK_LOOK_SECONDS = 0.5f;   // how often a unit with a goal is looked at

// Where a unit last set out for. Not saved.
struct PathGoal {
    Vec2 goal{ 0.0f, 0.0f };
    float elapsed = 0.0f;
    Vec2 mark{ 0.0f, 0.0f };   // where it last went somewhere from
    bool hasMark = false;
    float still = 0.0f;        // seconds walking from the mark without going anywhere
    int tries = 0;             // helps given since it last went somewhere
    float sidestep = 0.0f;     // seconds of stepping aside left
};
// A unit that is shut in: set once it stands idle with no way to its goal. Not saved.
struct Stuck {
    float idle = 0.0f;         // seconds idle in this round
    float wander = 0.0f;       // seconds of moving about left; 0 when it is not
    int rounds = 0;            // rounds with no way found
    double hours = 0.0;        // game hours since it began
    ObjectId breaking = NO_OBJECT;
    float swing = 0.0f;
};

// Notes where a unit set out for
void setPathGoal(World& w, ObjectId u, Vec2 goal);
// Whether the goal can be walked to from here over open ground (and doors), looking no further than a large area: more open ground
// than that counts as a yes
bool stuckReachable(World& w, const Terrain& t, Vec2 from, Vec2 goal);
// What is in the way: the wall, door, ladder or stairs on the cheapest way from here to the goal (walls and doors cost 20, ladders
// 40, stairs 60), or NO_OBJECT if no way is shut by one
ObjectId stuckBlocker(World& w, const Terrain& t, Vec2 from, Vec2 goal);

// A unit found standing in a closed tile (inside a wall, say), seen once; found there again a second later it is moved to the
// nearest open spot and the Game tab says so. Not saved.
inline constexpr float FREE_TRAPPED_SECONDS = 1.0f;
struct Trapped {};

void stuckSystem(World& w, Terrain& t, Rng& rng, float dt);

// Planning a way, paced. A unit whose way was planned may not have one planned again for REPATH_SECONDS, unless the walk is an
// order of yours (anything asked for outside the simulation step). A plan asked for sooner is kept and made once the time is up:
// meanwhile the unit goes on with the walk it has, or stands and waits (SoloMove::held), and counts as walking either way.
// A plan that finds no way to its goal is noted in the Information Panel ("Failed pathfinding"), as is a task a unit gives up
// ("Failed task"); both filters start off.
inline constexpr float REPATH_SECONDS = 0.1f;
inline constexpr float PATH_FAIL_QUIET_SECONDS = 5.0f;   // the same goal failing again is not noted again this soon
inline constexpr float PATH_FAIL_GAP = 1.0f;             // a way that ends this far short of its goal has failed

// Not saved.
struct PathPace {
    float wait = 0.0f;          // seconds before another way may be planned
    bool pending = false;       // a plan is being kept
    bool whole = false;         // the kept plan is a walkSoloTo (else a planSoloMove)
    Vec2 target{ 0.0f, 0.0f };
    int storey = -1;
    Vec2 failed{ 0.0f, 0.0f };  // the goal last noted as failed
    float quiet = 0.0f;
};
// Set while the simulation steps: a walk planned then is the game's own, not an order of yours
struct SimStepping {
    bool on = false;
};

// A way was planned for the unit just now
void notePathfind(World& w, ObjectId u);
// True if the plan must wait: it is kept, and the unit is left walking or waiting
bool deferPath(World& w, ObjectId u, Vec2 target, int storey, bool whole);
// Notes the failure if the way found does not get to the goal
void notePathResult(World& w, const Terrain& t, ObjectId u, Vec2 from, Vec2 target, const std::vector<Vec2>& path);
void notePathFailed(World& w, ObjectId u, Vec2 target);
void noteTaskFailed(World& w, ObjectId u, const std::string& task);
void pathPaceSystem(World& w, Terrain& t, float dt);
