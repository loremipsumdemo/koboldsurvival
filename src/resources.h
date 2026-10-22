#pragma once
#include <random>

// Overall progress
// In-game clock. One real second is one in-game minute, so a day lasts 24 real minutes.
constexpr float DAY_LENGTH_MINUTES = 1440.0f;
constexpr float MINUTES_PER_SECOND = 1.0f;

struct GameState {
    float dayMinutes = 360.0f;   // time of day, 0..1440. Starts at 06:00.
};

// Mouse drag rectangle in screen pixels, for box selection
struct Selection {
    bool dragging = false;
    float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
};

struct Rng {
    std::mt19937 gen;

    int range(int lo, int hi) {
        std::uniform_int_distribution<int> dist(lo, hi);
        return dist(gen);
    }
};
