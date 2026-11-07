#pragma once
#include <array>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include "components.h"
#include "game_state.h"
#include "squads.h"

// Plain text, one record per line. Terrain comes from the named map; everything else is in the file.
// Props, mobs, behaviors and stats are named by their keys, so adding one needs no new version. Whenever a line changes,
// raise SAVE_VERSION and add a step to save_migrate.cpp that turns the old lines into the new, and keep a file of the old
// version in tests/saves. A file from a version older than SAVE_OLDEST_VERSION or newer than this game is not read.
inline constexpr int SAVE_VERSION = 8;
inline constexpr int SAVE_OLDEST_VERSION = 6;
inline constexpr int AUTOSAVE_SLOTS = 3;

std::string saveDir();

struct SaveSlot {
    int slot = 0;
    ItemStack stack;
};

struct SaveGear {
    int slot = 0;
    int item = 0;
};

struct SaveRule {
    BehaviorRule rule;
    int prop = -1;
};

struct SaveUnit {
    Transform at;
    int sprite = 1;
    int faction = 0;
    int mob = 0;
    std::string name;
    std::optional<int> body;
    std::optional<SpawnHome> home;
    int occupying = -1;
    std::optional<Unconscious> down;
    std::optional<double> corpseHours;
    int carriedBy = -1;
    bool playingDead = false;
    Needs needs;
    std::vector<SaveSlot> stacks;
    std::vector<SaveGear> gear;
    std::optional<Stats> stats;
    std::optional<XpBuffer> xpParts;
    std::vector<SaveRule> rules;
    bool passive = false, block = false, sneak = false;
    int stepKind = -1;
    float buffLeft = 0.0f, buffAttack = 0.0f;
    std::vector<Provoked::Entry> provoked;
    int followUnit = -1, defendUnit = -1, attackUnit = -1;
    std::optional<Vec2> move;
    int propOrder = -1;
    int workProp = -1;
    bool workUnlocking = false;
    int propAction = 0;
    int squad = -1;        // a military unit's squad, 0 to 8
    bool citizen = false;
};

struct SaveGround {
    Transform at;
    GroundItem item;
};

struct SaveProp {
    int kind = 0;
    Transform at;
    float yaw = 0.0f;
    int owner = -1;
    std::optional<Lock> lock;
    std::vector<SaveSlot> storage, output;
    std::optional<Fuel> fuel;
    std::optional<double> workProgress;
    unsigned workTurn = 0;
    std::optional<double> bonesHours;
    std::vector<ItemStack> bonesItems;
    std::optional<Health> health;
};

struct SaveCamera {
    float fx = 0.0f, fy = 0.0f, fz = 0.0f;
    float yaw = 0.0f, pitch = 0.0f, distance = 0.0f;
};

struct SaveData {
    std::string name;
    bool autosave = false;
    long long timestamp = 0;
    std::string mapSource, mapStem;
    GameState gs;
    SaveCamera cam;
    std::string rngState;
    std::map<int, int> reputation;
    std::array<std::string, SQUAD_COUNT> squadNames{};
    int currentSquad = 0;
    std::vector<SaveProp> props;
    std::vector<SaveGround> ground;
    std::vector<SaveUnit> units;
};

struct SaveSummary {
    std::string path;
    std::string name;
    long long timestamp = 0;
    bool autosave = false;
};

bool writeSaveFile(const std::string& path, const SaveData& d);
// False if the file is damaged, truncated or of another version
bool readSaveFile(const std::string& path, SaveData& d);
// Turns the lines of a file of any version this game can read into the current version's. False if it can't.
bool upgradeSaveLines(std::vector<std::string>& lines);
std::vector<SaveSummary> listSaves();
std::string nextAutosavePath();
std::string manualSavePath(long long ts);
