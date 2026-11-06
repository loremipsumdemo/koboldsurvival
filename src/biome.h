#pragma once
#include <cstdint>

enum class Biome : uint8_t {
    Grass, Sand, Forest, Desert, Snow, Rock,
    Water, Shelf,
    Beach, Silt, FrozenClay, Clay, Gravel, AridSoil,
    Scree, Floodplain, CliffRock,
    Count,
    None = 255
};
