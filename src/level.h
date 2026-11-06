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

class Renderer;

std::string mapsDir();
struct MapChoice {
    std::string source;
    std::string stem;
    std::string name;
    long long created = 0;
    std::string path;

    std::string pakName() const { return "data/map/" + stem + ".map"; }
};

constexpr const char* MAIN_MAP_STEM = "main";

bool mainMapPacked();
std::vector<MapChoice> listMaps();
using LoadProgress = std::function<void(float, const std::string&)>;

// Reads a map into the terrain. On failure the terrain and the running game are left as they were.
bool loadLevelGround(const MapChoice& c, Terrain& terrain, Renderer& renderer, std::string& err, const LoadProgress& progress = {});
void buildLevel(World& world, Terrain& terrain, Rng& rng, unsigned seed, bool playerCamp, const LoadProgress& progress = {});

constexpr int ITEM_COPIES_PER_LEVEL = 10;
constexpr int STREAM_RADIUS = 4;
constexpr int STREAM_LOADS_PER_FRAME = 4;

std::vector<Vec2> streamFocuses(World& w, const Camera& cam);
void primeStreaming(World& w, Terrain& t, const Camera& cam);

void restoreWorld(World& world, Terrain& terrain, Rng& rng, const SaveData& d);
SaveData captureWorld(World& w, const GameState& gs, const Camera& cam, const Rng& rng,
                      long long ts, const std::string& name, const MapChoice& map);
void applySaveCamera(const SaveCamera& c, Camera& cam);
