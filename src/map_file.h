#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "biome.h"

// The older single-file map format (data/map/<name>.map), still read for premade maps; worlds now use the folder
// format in world_format.h. A map is a square of tiles. Heights are per corner: (size+1) x (size+1) values in world units,
// row by row (z, then x). Walkability and biome are per tile: size x size values, row by row.
// The file format, version 2:
//   4 bytes   magic "KMAP"
//   4 bytes   format version, 2
//   4 bytes   tiles per side
//   (size+1)^2 little-endian float32 heights
//   size^2 bytes walkability, 1 walkable and 0 blocked
//   size^2 bytes biome, Biome::None for none
// Version 1 files (heights only) are still read, with every tile walkable and no biome.
// Heights are clamped to +/- MAX_HEIGHT when read, so a broken value can't break the mesh.
struct MapData {
    int size = 26;
    std::vector<float> heights;      // (size+1)^2 corners
    std::vector<uint8_t> walkable;   // size^2 tiles
    std::vector<uint8_t> biome;      // size^2 tiles
};

// A flat map of the given size: height zero, every tile walkable, no biome
MapData flatMap(int size);

// Parses a map from bytes. Returns false if the magic, version or size don't match.
bool parseMap(const unsigned char* bytes, size_t total, MapData& out);

// Serializes a map to the version 2 format above
std::vector<unsigned char> encodeMap(const MapData& m);

bool readMapFile(const std::string& path, MapData& out);
bool writeMapFile(const std::string& path, const MapData& m);
