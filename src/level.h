#pragma once
// The maps a game can start on, building a level on one, and turning a running level into a save and back
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

// Game worlds are kept in this folder, beside the game.
std::string mapsDir();
// A folder left as custom_maps by an earlier version moves to maps the first time the game starts
void moveOldMapsFolder();

// A map the New Game list can start: a premade map from data.pak ("data"), a game world folder in
// maps ("world"), or an older .map file in maps ("custom")
struct MapChoice {
    std::string source;      // "data", "world" or "custom"
    std::string stem;        // file or folder name without the .map extension. Saves refer to it.
    std::string name;        // what the list shows: the saved name of a world, or the stem
    long long created = 0;   // when a world was saved, milliseconds since 1970; 0 if unknown
    std::string path;        // where a world folder or .map file is on disk

    std::string pakName() const { return "data/map/" + stem + ".map"; }
};

// The main map: a world folder packed into data.pak at data/map/main. New Game always starts on it.
constexpr const char* MAIN_MAP_STEM = "main";

bool mainMapPacked();
// Every map the New Game list can start, not yet sorted: the main map, the premade maps, then worlds and .map files
std::vector<MapChoice> listMaps();
// Puts a map's ground into the terrain: a premade or custom map whole, or a world folder to be streamed
bool loadTerrain(const MapChoice& c, Terrain& terrain, std::string& err);

// Every item in the database is put on the ground this many times when a level starts
constexpr int ITEM_COPIES_PER_LEVEL = 10;

// Builds a fresh level on a map: the ground, then the entities from the seed, then the items on the
// ground if spawnItems is set. progress, if given, is called as the level loads (0..1), for the loading screen.
bool prepareLevel(const MapChoice& c, World& world, Terrain& terrain, Renderer& renderer, Rng& rng,
                  unsigned seed, bool spawnItems, std::string& err,
                  const std::function<void(float, const std::string&)>& progress = {});

// Open worlds keep the ground loaded around the camera and every unit the player commands
constexpr int STREAM_RADIUS = 4;            // chunks kept loaded around each focus point
constexpr int STREAM_LOADS_PER_FRAME = 4;   // chunks read in per frame while playing, so play never stalls

std::vector<Vec2> streamFocuses(World& w, const Camera& cam);
// Reads in the ground around the camera and the units straight away, so a level starts with its ground in place
void primeStreaming(World& w, Terrain& t, const Camera& cam);

// Rebuilds a level from a save. The ground comes from the map, and the props from the seed.
// The units are then swapped for the saved ones.
bool restoreLevel(World& world, Terrain& terrain, Renderer& renderer, Rng& rng, const SaveData& d,
                  const MapChoice& map, std::string& err);
// Captures what a save needs from the running world, including the camera
SaveData captureWorld(World& w, const GameState& gs, const Camera& cam, unsigned seed,
                      long long ts, const std::string& name, const MapChoice& map);
// Puts the camera back where it was saved. Saves from before the camera was stored keep
// the default focus set by focusCameraOnFirstUnit.
void applySaveCamera(const SaveCamera& c, Camera& cam);
