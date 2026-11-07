#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "chunk_source.h"

// A map is a recipe, not terrain: these numbers, in a few hundred bytes, say the whole ground. The generator works in whole
// numbers, so the same recipe gives the same ground on every machine. If what a recipe makes has to change, raise
// WORLD_GEN_VERSION and keep the old way working: a map names the version that made it.
inline constexpr int WORLD_GEN_VERSION = 1;
inline constexpr int WORLD_GEN_SIZES[] = { 512, 1024, 2048, 4096, 8192 };
inline constexpr float WORLD_GEN_FLOOR_Y = -26.0f;

struct WorldGenSettings {
    uint32_t seed = 1;
    int size = 2048;
    // Each from 0 to 100
    int land = 60;
    int continents = 50;
    int warp = 50;
    int hills = 40;
    int mountains = 45;
    int moisture = 50;
    int temperature = 50;
    int forests = 50;
};

// What the ground is at one tile, from the recipe alone
class WorldGenerator {
public:
    explicit WorldGenerator(const WorldGenSettings& settings) : s_(settings) {}
    const WorldGenSettings& settings() const { return s_; }
    // Thousandths of a tile, at a corner of a tile. Sea level is 0.
    int heightAt(int x, int z) const;
    void fill(int cx, int cz, ChunkData& out) const;
    // A picture of the whole map, px pixels across, as RGBA
    std::vector<uint8_t> preview(int px) const;

private:
    WorldGenSettings s_;
};

std::shared_ptr<const ChunkSource> generatedChunkSource(const WorldGenSettings& settings);

// The folder beside the game that maps go in
inline constexpr const char* MAP_FOLDER = "MAP";
std::string mapFilePath(const std::string& name);
// Letters, digits, spaces, - and _ only, so a name is always a file name
std::string safeFileName(const std::string& name);

std::string mapFileText(const WorldGenSettings& settings);
bool mapFromText(const std::string& text, WorldGenSettings& out, std::string& err);
bool writeMapFile(const std::string& path, const WorldGenSettings& settings, std::string& err);
bool readMapFile(const std::string& path, WorldGenSettings& out, std::string& err);
// The names of the maps in the folder
std::vector<std::string> listMapFiles();
