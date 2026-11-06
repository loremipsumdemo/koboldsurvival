#pragma once
#include <utility>
#include <vector>
#include "ecs.h"
#include "game_log.h"
#include "game_state.h"
#include "rng.h"
#include "sounds.h"
#include "systems.h"
#include "terrain.h"
#include "world_labels.h"

// Everything the world says happened since the last takeEvents, for whoever shows it to the player. The simulation
// only adds to this, and it lives in the World, so a new World starts with none.
struct SimEvents {
    std::vector<QueuedSound> sounds;
    std::vector<LogLine> log;
    std::vector<TradeLabel> labels;
    std::vector<FloatingNumber> floats;
    std::vector<PropArrival> arrivals;
    std::vector<WorkGain> gains;
    std::vector<std::pair<Entity, Entity>> lootOpened;   // looter, looted
    WakeChanges wakes;
};

SimEvents takeEvents(World& w);

// One fixed step of the game. Needs no window, and with the same world, seed and steps it does the same thing.
void simStep(World& w, Terrain& t, Rng& rng, GameState& gs, float dt, bool running);
