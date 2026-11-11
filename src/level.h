#pragma once
#include <functional>
#include <string>
#include <vector>
#include "camera.h"
#include "ecs.h"
#include "game_state.h"
#include "geometry.h"
#include "rng.h"
#include "savegame.h"
#include "terrain.h"

struct LevelData;

class Renderer;

struct MapChoice {
    std::string source;
    std::string stem;
    std::string name;
    std::string path;
};

// A map made by World Generation, from a file in the MAP folder
constexpr const char* MAP_SOURCE_FILE = "file";

std::vector<MapChoice> listMaps();
using LoadProgress = std::function<void(float, const std::string&)>;

// Reads a map into the terrain. On failure the terrain and the running game are left as they were.
bool loadLevelGround(const MapChoice& c, Terrain& terrain, Renderer& renderer, std::string& err, const LoadProgress& progress = {});
void buildLevel(World& world, Terrain& terrain, Rng& rng, unsigned seed, bool playerCamp, const LoadProgress& progress = {});
// The same, with what a level file says in place of the random set-up
void buildLevelFrom(World& world, Terrain& terrain, Rng& rng, unsigned seed, const LevelData& level);

// A new random level lays this many items on the ground, of random kinds, within this many tiles of where you start
constexpr int START_ITEM_COUNT = 32;
constexpr float START_ITEM_RANGE = 20.0f;
constexpr int STREAM_RADIUS = 4;
constexpr int STREAM_LOADS_PER_FRAME = 4;

std::vector<Vec2> streamFocuses(World& w, const Camera& cam);
void primeStreaming(World& w, Terrain& t, const Camera& cam);

void restoreWorld(World& world, Terrain& terrain, Rng& rng, const SaveData& d);
SaveData captureWorld(World& w, const GameState& gs, const Camera& cam, const Rng& rng,
                      long long ts, const std::string& name, const MapChoice& map);
void applySaveCamera(const SaveCamera& c, Camera& cam);
