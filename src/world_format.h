#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "chunk.h"
#include "look.h"

struct WorldInfo {
    std::string name;
    long long created = 0;
    int size = 0;
    float heightScale = 1.0f;
    float floorY = -1.0f;
};

struct WorldSurface {
    Look look;
    std::vector<uint8_t> ground;
    std::vector<uint8_t> water;
};

struct WorldEntry {
    std::string folder;
    std::string name;
    long long created = 0;
};

int chunksPerSide(int size);

inline constexpr const char* PAK_WORLD_PREFIX = "pak:";

bool readWorldInfo(const std::string& folder, WorldInfo& out, std::string& err);

std::vector<WorldEntry> listWorlds(const std::string& mapsFolder);

bool readChunk(const std::string& folder, const WorldInfo& info, int cx, int cz, ChunkData& out);

bool readWorldSurface(const std::string& folder, WorldSurface& out, std::string& err);
