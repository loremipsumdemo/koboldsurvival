#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "chunk_source.h"
#include "map_theme.h"

// A map is a recipe, not terrain: these numbers, in a few hundred bytes, say the whole ground. The generator works in whole
// numbers, so the same recipe gives the same ground on every machine. If what a recipe makes has to change, raise
// WORLD_GEN_VERSION and keep the old way working: a map names the version that made it.
inline constexpr int WORLD_GEN_VERSION = 1;
inline constexpr int WORLD_GEN_SIZES[] = { 512, 1024, 2048, 4096, 8192 };
inline constexpr float WORLD_GEN_FLOOR_Y = -26.0f;

// The kind of ground a map has, chosen before it is generated. A style also starts the map on a theme, which can be changed.
enum class MapStyle : int { Overworld = 0, WalledValley, Archipelago, Cavern, GalaxySea, VoidIslands, Caldera, Count };
struct MapStyleInfo {
    const char* fileName;
    const char* labelKey;
    const char* aboutKey;
    int themePreset;
    bool walled;
};
const MapStyleInfo& mapStyleInfo(int style);

struct WorldGenSettings {
    uint32_t seed = 1;
    int size = 2048;
    int style = (int)MapStyle::Overworld;
    // Each from 0 to 100
    int land = 60;
    int continents = 50;
    int warp = 50;
    int hills = 40;
    int mountains = 45;
    int moisture = 50;
    int temperature = 50;
    int forests = 50;
    int themePreset = 0;
    MapTheme theme;
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
    // Empty if cancel was set before it finished. Uses most of the cores.
    std::vector<uint8_t> preview(int px, const std::atomic<bool>* cancel = nullptr) const;

private:
    int overworldHeight(int x, int z) const;
    int styledHeight(int x, int z) const;
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
