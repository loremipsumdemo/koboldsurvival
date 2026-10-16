#pragma once
#include <random>

// Overall progress
struct GameState {
    int keysCollected = 0;
    int unitsEntered = 0;
    bool won = false;
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
