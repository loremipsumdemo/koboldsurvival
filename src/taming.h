#pragma once
#include "behavior.h"
#include "components.h"
#include "ecs.h"
#include "terrain.h"

// Taming: an animal (a mob) knocked out and carried into a Taming Cage (props.txt flag "taming") is fed raw meat by the
// units of the cage's faction that have the Tame Animals task. The task fetches raw meat from a store of theirs when
// they carry none, takes it to the cage, and feeds the animal inside.
inline constexpr double TAME_FEED_HOURS = 6.0;      // an animal is fed at most this often, game hours
inline constexpr int TAME_MEAT_CARRIED = 3;         // the most raw meat a trip fetches

// On an animal in a Taming Cage: how often it has been fed there, and game hours since the last time
struct Taming {
    int fed = 0;
    double sinceFed = 1e9;
};

// A Tame Animals trip under way: fetching raw meat from a store, then feeding it to the animal in the cage
struct TameJob {
    enum class Phase { Fetch, Feed };
    Phase phase = Phase::Feed;
    ObjectId cage = NO_OBJECT;
    ObjectId animal = NO_OBJECT;
    ObjectId source = NO_OBJECT;
    int item = 0;
    float stuck = 0.0f;
};

bool isRawMeat(int item);
// The Taming Cage this unit is locked in, or NO_OBJECT
ObjectId tamingCageOf(World& w, ObjectId animal);
// Starts a Tame Animals trip, or says why it waits (r.why)
bool startTameStep(World& w, Terrain& t, ObjectId u, Behaviors& b, BehaviorRule& r);
// Runs the trips under way, and counts the hours since each caged animal was fed
void tameJobSystem(World& w, Terrain& t, float dt, double gameSeconds);
