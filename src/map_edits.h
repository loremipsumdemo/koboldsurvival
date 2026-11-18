#pragma once
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include "chunk_source.h"

// What the Map Editor changes of a generated map, kept beside it as MAP/<name>.edits: corner heights and ground types (biomes) laid over what
// the map's recipe makes. Opening the map (loadLevelGround) lays them over its ground, so a game or a level on it has the edited ground.
// Kept by chunk, so that making a chunk looks only at its own.
struct MapEdits {
    struct Bucket {
        std::unordered_map<int, float> corners;    // a corner's height, by its place in the chunk (row * CHUNK_TILES + column)
        std::unordered_map<int, uint8_t> tiles;    // a tile's Biome
    };
    mutable std::mutex lock;   // chunks are made on other threads while the editor changes things
    std::unordered_map<long long, Bucket> buckets;

    static long long bucketKey(int bx, int bz) { return (long long)bz * 100003LL + bx; }
    bool cornerEdit(int x, int z, float& h) const;
    void setCorner(int x, int z, float h);
    void clearCorner(int x, int z);
    bool tileEdit(int x, int z, uint8_t& biome) const;
    void setTile(int x, int z, uint8_t biome);
    void clearTile(int x, int z);
    bool empty() const;
};

std::string mapEditsPath(const std::string& stem);
bool readMapEdits(const std::string& path, MapEdits& out);
bool writeMapEdits(const std::string& path, const MapEdits& edits, std::string& err);
// The same ground with the edits laid over it (heights, ground types, and the walkability that follows from them)
std::shared_ptr<const ChunkSource> editedSource(std::shared_ptr<const ChunkSource> inner, std::shared_ptr<MapEdits> edits);
