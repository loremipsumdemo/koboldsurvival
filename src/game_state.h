#pragma once
#include "calendar.h"

// Overall progress. The in-game clock and calendar are in calendar.h.
struct GameState {
    double clockSeconds = START_CLOCK_SECONDS;   // in-game seconds since Day 1, First Month, Year 500, 00:00
};

