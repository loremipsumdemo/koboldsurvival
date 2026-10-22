#pragma once

// The game's world format. A world is a folder holding:
//   world.txt             plain text: format, name, size in tiles, chunk size, height scale
//   r.<rx>.<rz>.kreg      region files, each holding 16 x 16 chunks (1024 tiles)
// A chunk is read on its own, so a large world never has to be in memory at once.

#include <functional>
#include <string>

#include "chunk.h"

struct WorldInfo {
    std::string name;
    int size = 0;                // tiles per side
    float heightScale = 1.0f;    // how the stored heights were scaled from the tool (for reference)
};

// Chunks per side for a world of this size
int chunksPerSide(int size);

bool readWorldInfo(const std::string& folder, WorldInfo& out, std::string& err);

// Reads chunk (cx, cz). Returns false if the chunk is not in the folder.
bool readChunk(const std::string& folder, const WorldInfo& info, int cx, int cz, ChunkData& out);

// Writes a whole world. makeChunk fills each chunk in turn, starting from an empty chunk.
// Chunks past the edge of the map are written empty.
bool writeWorld(const std::string& folder, const WorldInfo& info,
                const std::function<void(int cx, int cz, ChunkData& c)>& makeChunk, std::string& err);
