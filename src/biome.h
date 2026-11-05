#pragma once
#include <cstdint>

// Ground types. Tiles store them as bytes (chunk.h, map_file.h): idx() of one, or 255 for None.
// Values 0 to 7 are the original types and keep their numbers, so saved maps still load.
// Values from 8 on were added by the world generator.
enum class Biome : uint8_t {
    Grass, Sand, Forest, Desert, Snow, Rock,
    Water, Shelf,
    Beach, Silt, FrozenClay, Clay, Gravel, AridSoil,
    Scree, Floodplain, CliffRock,
    Count,
    None = 255   // no type: drawn by its height instead
};
