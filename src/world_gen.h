#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "chunk_source.h"
#include "map_theme.h"

// A map is a recipe, not terrain: these numbers, in a few hundred bytes, say the whole ground. The generator works in whole
// numbers, so the same recipe gives the same ground on every machine. If what a recipe makes has to change, raise
// WORLD_GEN_VERSION and keep the old way working: a map names the version that made it.
inline constexpr int WORLD_GEN_VERSION = 1;
inline constexpr int WORLD_GEN_SIZES[] = { 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536 };
// The biggest size the game is made and tested for. World Generation's Size + goes past it only once a warning has been held through (the
// map might not work properly), and then up the same doubling to the most a map file can say.
inline constexpr int WORLD_GEN_SAFE_SIZE = 8192;
inline constexpr int WORLD_GEN_MAX_SIZE = 65536;
static_assert(WORLD_GEN_SIZES[sizeof(WORLD_GEN_SIZES) / sizeof(WORLD_GEN_SIZES[0]) - 1] == WORLD_GEN_MAX_SIZE, "the sizes end at the most a map may be");
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
    // The ground of the whole map at res by res corners (see ChunkSource::farField). False if cancel was set.
    bool farField(int res, std::vector<float>& heights, std::vector<uint8_t>& rgb, const std::atomic<bool>& cancel) const;
    // The ground of a window of the map at res by res corners spacing tiles apart, the first at tile (x0, z0) (ChunkSource::farFieldWindow). A
    // corner past the edge takes the nearest point on it. False if cancel was set.
    bool farFieldWindow(int x0, int z0, int spacing, int res, std::vector<float>& heights, std::vector<uint8_t>& rgb, const std::atomic<bool>& cancel) const;

private:
    // Both of the above: the corner (i, j) is at tile (atX(i), atZ(j)); helpers are the threads that work besides the caller
    bool farFieldSampled(int res, const std::function<int(int)>& atX, const std::function<int(int)>& atZ, int helpers, std::vector<float>& heights,
                         std::vector<uint8_t>& rgb, const std::atomic<bool>& cancel) const;
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
// True if both make the same ground: they differ in their theme at most, which changes nothing about the ground
bool sameGround(const WorldGenSettings& a, const WorldGenSettings& b);
bool mapFromText(const std::string& text, WorldGenSettings& out, std::string& err);
bool writeMapFile(const std::string& path, const WorldGenSettings& settings, std::string& err);
bool readMapFile(const std::string& path, WorldGenSettings& out, std::string& err);
// The names of the maps in the folder
std::vector<std::string> listMapFiles();
