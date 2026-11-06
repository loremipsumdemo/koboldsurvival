#pragma once
#include <random>

struct Rng {
    std::mt19937 gen;

    int range(int lo, int hi) {
        std::uniform_int_distribution<int> dist(lo, hi);
        return dist(gen);
    }

    float unit() {
        return std::uniform_real_distribution<float>(0.0f, 1.0f)(gen);
    }
};
