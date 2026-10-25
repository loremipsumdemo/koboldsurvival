#pragma once

// The game's world format. A world is a folder holding:
//   world.txt             plain text: format, name, created, size in tiles, chunk size, height scale, seabed depth
//   look.json             the look the world was saved with: sky, sun, rock and water colors, water motion
//   surface.ksrf          the two surface tiles the ground and the sea are drawn with
//   r.<rx>.<rz>.kreg      region files, each holding 16 x 16 chunks (1024 tiles)
// A chunk is read on its own, so a large world never has to be in memory at once.
// Worlds are kept in the maps folder beside the game, one folder per world.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "chunk.h"
#include "theme/theme.hpp"

struct WorldInfo {
    std::string name;            // the name the world was saved under
    long long created = 0;       // when it was saved, milliseconds since 1970; 0 if unknown
    int size = 0;                // tiles per side
    float heightScale = 1.0f;    // stored heights are the world's units times this
    float floorY = -1.0f;        // height of the seabed beyond the map, in stored units
};

// The look a world is drawn with, and the surface tiles the ground and sea use
struct WorldSurface {
    wm::Look look;               // the look, from look.json
    std::vector<uint8_t> ground; // ground tile, RGBA8, from surface.ksrf
    std::vector<uint8_t> water;  // sea tile, RGBA8
};

// One world in a maps folder
struct WorldEntry {
    std::string folder;          // the world's folder, as a path
    std::string name;            // its saved name
    long long created = 0;       // when it was saved, milliseconds since 1970; 0 if unknown
};

// Chunks per side for a world of this size
int chunksPerSide(int size);

bool readWorldInfo(const std::string& folder, WorldInfo& out, std::string& err);

// Every world in a maps folder: each subfolder holding a readable world.txt. Not sorted.
std::vector<WorldEntry> listWorlds(const std::string& mapsFolder);

// Sorts by name (ignoring case, then newest first) or by created date (ties by name)
void sortWorldEntries(std::vector<WorldEntry>& worlds, bool byName, bool ascending);

// Reads chunk (cx, cz). Returns false if the chunk is not in the folder.
bool readChunk(const std::string& folder, const WorldInfo& info, int cx, int cz, ChunkData& out);

// Writes a whole world. makeChunk fills each chunk in turn, starting from an empty chunk.
// Chunks past the edge of the map are written empty.
bool writeWorld(const std::string& folder, const WorldInfo& info,
                const std::function<void(int cx, int cz, ChunkData& c)>& makeChunk, std::string& err);

// Writes the look and surface tiles into a world folder. Call after writeWorld.
bool writeWorldSurface(const std::string& folder, const WorldSurface& s, std::string& err);

// Reads the look and surface tiles back. Both are needed: a folder without them is not a world.
bool readWorldSurface(const std::string& folder, WorldSurface& out, std::string& err);
