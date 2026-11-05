#pragma once
#include <cstdint>

// Ground types. A tile with BIOME_NONE has no type, and is drawn by its height instead.
// Values 0 to 7 are the original types and keep their numbers, so saved maps still load.
// Values from 8 on were added by the world generator.
enum Biome : uint8_t {
    BIOME_GRASS, BIOME_SAND, BIOME_FOREST, BIOME_DESERT, BIOME_SNOW, BIOME_ROCK,
    BIOME_WATER, BIOME_SHELF,
    BIOME_BEACH, BIOME_SILT, BIOME_FROZEN_CLAY, BIOME_CLAY, BIOME_GRAVEL, BIOME_ARID_SOIL,
    BIOME_SCREE, BIOME_FLOODPLAIN, BIOME_CLIFF_ROCK,
    BIOME_COUNT
};
constexpr uint8_t BIOME_NONE = 255;
