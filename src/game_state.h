#pragma once
#include "calendar.h"

// Overall progress. The in-game clock and calendar are in calendar.h.
struct GameState {
    double clockSeconds = START_CLOCK_SECONDS;   // in-game seconds since Day 1, First Month, Year 500, 00:00
};

// Mouse drag rectangle in screen pixels, for box selection
struct Selection {
    bool dragging = false;
    float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
};
