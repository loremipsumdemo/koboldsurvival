#pragma once
#include <random>

// The random number generator every system draws from, so a seeded game can be played again the same way
struct Rng {
    std::mt19937 gen;

    int range(int lo, int hi) {
        std::uniform_int_distribution<int> dist(lo, hi);
        return dist(gen);
    }

    // A number from 0 up to, but not including, 1
    float unit() {
        return std::uniform_real_distribution<float>(0.0f, 1.0f)(gen);
    }
};
