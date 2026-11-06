#pragma once
#include "calendar.h"

struct GameState {
    double clockSeconds = START_CLOCK_SECONDS;
    double claimDecayDue = 0.0;   // game seconds since claims last decayed. Not saved
};

