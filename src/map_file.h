#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "biome.h"

struct MapData {
    int size = 26;
    std::vector<float> heights;
    std::vector<uint8_t> walkable;
    std::vector<uint8_t> biome;
};

MapData flatMap(int size);

bool parseMap(const unsigned char* bytes, size_t total, MapData& out);

bool readMapFile(const std::string& path, MapData& out);
