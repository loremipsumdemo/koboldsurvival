#pragma once
#include <string>
#include <vector>
#include "components.h"
#include "ecs.h"
#include "rng.h"
#include "terrain.h"

// A level is what is placed on a map, in a text file of its own in MAP/LEVEL: entities (units and mobs), props and items.
// While it is being edited the World is the document: they are the same objects a game has, however they got there, and
// saving reads them back.
inline constexpr const char* LEVEL_FOLDER = "MAP/LEVEL";

struct LevelObject {
    enum class Type { Unit, Mob, Prop, Item };
    Type type = Type::Prop;
    int id = 0;
    int kind = 0;       // a unit's faction, a MobKind, a PropKind or an item id
    int count = 1;      // items
    int owner = -1;     // props and mobs: their faction, or -1 (a prop of none, a mob of its own kind's)
    float x = 0.0f, z = 0.0f, yaw = 0.0f;
    int storey = 0;     // placed in a game only: the floor of a building it is put on (0 the ground). Not kept in a level file.
    PropVisual visual;  // props: how it is drawn apart from where it stands (plain when it isn't)
};

struct LevelData {
    std::string map;
    bool hasSpawn = false;
    float spawnX = 0.0f, spawnZ = 0.0f;
    int nextId = 1;
    std::vector<LevelObject> objects;
};

std::string levelFilePath(const std::string& name);
std::string levelToText(const LevelData& d);
bool levelFromText(const std::string& text, LevelData& out, std::string& err);
bool writeLevelFile(const std::string& path, const LevelData& d, std::string& err);
bool readLevelFile(const std::string& path, LevelData& out, std::string& err);
// The names of the levels made for this map
std::vector<std::string> listLevelFiles(const std::string& map);

// One object as a game has it
ObjectId placeLevelObject(World& w, const Terrain& t, Rng& rng, const LevelObject& o);
// Replaces the world with what the level says
void applyLevel(World& w, const Terrain& t, Rng& rng, const LevelData& d);
// Reads the level back out of the world. Objects new since the last time get their ids here.
LevelData captureLevel(World& w, const std::string& map);
