#pragma once
#include <cstdint>
#include <vector>
#include "biome.h"

struct MapData {
    int size = 26;
    std::vector<float> heights;
    std::vector<uint8_t> walkable;
    std::vector<uint8_t> biome;
};

MapData flatMap(int size);
