#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "chunk.h"
#include "look.h"

struct WorldInfo {
    int size = 0;
    float heightScale = 1.0f;
    float floorY = -1.0f;
};

struct WorldSurface {
    Look look;
    std::vector<uint8_t> ground;
    std::vector<uint8_t> water;
    // The world's seed, which lays out the clouds and how the ground and water tiles are turned and shifted. Never saved:
    // a generated world has its own, a world folder 0.
    uint32_t seed = 0;
};

int chunksPerSide(int size);

inline constexpr const char* PAK_WORLD_PREFIX = "pak:";

bool readWorldInfo(const std::string& folder, WorldInfo& out, std::string& err);

bool readChunk(const std::string& folder, const WorldInfo& info, int cx, int cz, ChunkData& out);

bool readWorldSurface(const std::string& folder, WorldSurface& out, std::string& err);
