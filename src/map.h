#pragma once
#include <cstddef>
#include <string>
#include <vector>

// A map is the ground's corner heights, VERT_SIZE x VERT_SIZE values in world units, row by row
// (z, then x). The file format is:
//   4 bytes   magic "KMAP"
//   4 bytes   format version, currently 1
//   4 bytes   tiles per side, which must equal MAP_SIZE
//   then VERT_SIZE * VERT_SIZE little-endian float32 heights
// Heights are clamped to +/- HEIGHT_SCALE when read, so a hand-edited file can't break the mesh.
struct MapData {
    std::vector<float> heights;
};

// A flat map at height zero
MapData flatMap();

// Parses a map from bytes. Returns false if the magic, version, size or dimensions don't match.
bool parseMap(const unsigned char* bytes, size_t size, MapData& out);

// Serialises a map to the file format above
std::vector<unsigned char> encodeMap(const MapData& m);

bool readMapFile(const std::string& path, MapData& out);
bool writeMapFile(const std::string& path, const MapData& m);
