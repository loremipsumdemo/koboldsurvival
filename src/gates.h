#pragma once
#include <cstdint>
#include <map>
#include <vector>
#include "ecs.h"
#include "factions.h"
#include "geometry.h"
#include "terrain.h"

// Perimeter Gates and who works them.
//  - A gate never opens or shuts by itself: a unit has to do it. The game works out when one should (someone asks to come in, night
//    falls, dawn comes, the way is clear again), and sends a unit of yours, who walks to the gate and works it, which takes a moment.
//  - A gate is worked (opened or shut) only from the inside, from the walkway on top of it, never from the outside, where it has to be
//    picked like any lock (or smashed).
//  - A friendly unit that stands just outside a shut gate asks to be let in. A unit of yours inside or on top opens it for a few seconds
//    for them, unless it can see a hostile unit, and it shuts again once they are through.
//  - At night your gates shut (a unit inside or on top has to be there to do it), and open again at dawn.
//  - At least one Fortified unit holds each gate, standing just inside it, and does not sleep through the night.
// The gate's Lock says whether it is shut (building.h gateShut).

// Why a unit is working a gate
enum class GateJob { Manual, Request, Night, Dawn, Reshut };
inline constexpr float GATE_WORK_SECONDS = 1.5f;   // how long it takes to open or shut a gate, standing at it
// On a unit sent to open or shut a gate (it walks there, then works it). Not saved.
struct GateWork {
    ObjectId gate = NO_OBJECT;
    bool shut = false;                 // what it is to leave it: shut, or open
    GateJob kind = GateJob::Manual;
    float left = GATE_WORK_SECONDS;
};

// On a gate that was opened for someone asking to come in: how long it stays open once nobody is at it. Not saved.
struct GateOpening {
    float left = 0.0f;
};
// On a gate that was shut for the night, so that it is opened at dawn. Not saved.
struct GateNightShut {};
// On a gate someone shut by hand in the open hours: it stays shut until the closed hours come. Not saved.
struct GateHeldShut {};
// On a Fortified unit that holds a gate. Not saved: it is worked out again every second.
struct GateKeeper {
    ObjectId gate = NO_OBJECT;
};

// On a unit shut out of the perimeter (or heading for the gate to ask in): it stands just outside the gate, where a gate keeper answers.
// Not saved.
struct AskingEntry {
    ObjectId gate = NO_OBJECT;
};
// How long before an idle citizen looks again for the way into the perimeter. Not saved.
struct HomewardLook {
    float left = 0.0f;
};

// On a Fortified unit that holds a Guard Post (a prop of yours): it stands there. Below the gate's keeper, above the route. Not saved.
struct GuardPostDuty {
    ObjectId post = NO_OBJECT;
};
// On a Fortified unit marching the perimeter route (below the gate and the posts): where on it, and whether it is on the move, which
// is when it hops and shows the marching emote. Not saved.
struct Patrolling {
    int at = 0;
    bool moving = false;
};
// The route over the walkways of your Perimeter Walls and Gates: the pieces that join, as the largest run of them, in the order a guard walks
// them (the points are on the walkway). A loop if they go all the way round, else back and forth along them. Not available while no
// Slope (or other way up) reaches the walkway. It is worked out again when your pieces are built or destroyed, and, while it is not
// available, once an hour at most (and only while you have Fortified units, whose task it is).
struct PerimeterRoute {
    bool computed = false, available = false, loop = false;
    std::vector<Vec2> points;
    std::map<ObjectId, Vec2> gateTops;   // for each gate a Slope leads up to: a spot on its walkway, where its keeper stands
    std::uint64_t rev = 0;
    long long hour = -1;
};
// Each faction's route (an NPC faction's too: its Fortified march its own walkways)
struct PerimeterRoutes {
    std::map<int, PerimeterRoute> by;
};
const PerimeterRoute& perimeterRoute(World& w, int faction = FACTION_PLAYER);
// A piece the Fortified may build: a Perimeter Wall or Gate, a Slope, a Watchtower or a Guard Post. (Damage mends by itself inside a claim.)
bool isPerimeterWork(World& w, ObjectId piece);
// Whether a unit is up on the walkway of a Perimeter Wall or Gate
bool onPerimeterTop(World& w, ObjectId u);
// Brings a unit down from a walkway to the nearest open ground on the inside of the wall or gate it is on (the side behind its facing)
// at once. False if it is not up on one, or there is no ground to land on.
bool climbDownPerimeter(World& w, const Terrain& t, ObjectId u);

inline constexpr float GATE_REQUEST_RANGE = 3.0f;    // how near outside a gate a friendly unit stands to ask to be let in
inline constexpr float GATE_WORK_RANGE = 9.0f;       // how near inside a gate a unit must be to work it
inline constexpr float GATE_SIGHT_RANGE = 16.0f;     // how far a hostile unit is seen from a gate
inline constexpr float GATE_OPEN_SECONDS = 8.0f;     // how long a gate opened for a request stays open once nobody is at it
inline constexpr float GATE_KEEP_REACH = 1.5f;       // how near its post a keeper stands

// Which way is out: a unit vector along the gate's facing, away from the walkway's inner edge
Vec2 gateOutward(World& w, ObjectId gate);
// The middle of the gate's footprint
Vec2 gateCenter(World& w, ObjectId gate);
// Where a point is along the facing: positive outside the gate's middle, negative inside it
float gateSideOf(World& w, ObjectId gate, float x, float z);
// A spot this far beyond the gate's edge on the outside (positive) or the inside (negative)
Vec2 gateSpot(World& w, ObjectId gate, float beyond);
// How far (across the ground) a point is from the gate's footprint, 0 if it is on it
float gateGap(World& w, ObjectId gate, float x, float z);
// A unit of yours that is awake and inside the gate, or on top of it, close enough to work it. NO_OBJECT if none is.
ObjectId gateOperator(World& w, ObjectId gate);
// Who answers a gate (opens and shuts it as the game calls for): its keeper, a unit with the Fortified task of Guarding the Gate, if it has
// one that is able (NO_OBJECT if it has one that is not); and a gate with none is answered by any unit inside or on top of it
ObjectId gateAnswerer(World& w, ObjectId gate);
// Whether a hostile unit is within sight of the gate
bool hostileNearGate(World& w, ObjectId gate);
// A faction's finished gates (yours, by default), in order
std::vector<ObjectId> ownGates(World& w, int faction = FACTION_PLAYER);
// Opens or shuts a gate the way a player or a unit does by hand: what was waiting on it (a night shut, a request's time) is forgotten
void workGate(World& w, ObjectId gate, bool shut);
// Sends a unit to open or shut a gate: the gate's keeper if it is able, or else the one given (the nearest able unit inside or on top
// if none is given). False if none can, or one is already on its way.
bool assignGateWork(World& w, ObjectId gate, bool shut, GateJob kind, ObjectId worker = NO_OBJECT);
// A unit with a gate to work: walks to it and works it. True while it is busy with that (and its other tasks wait).
bool gateWorkStep(World& w, Terrain& t, ObjectId u, float dt);
