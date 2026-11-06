#pragma once
#include <map>
#include <optional>
#include <string>
#include <vector>
#include "components.h"
#include "game_state.h"

// Plain text, one record per line. Terrain comes from the named map; everything else is in the file.
// Raise SAVE_VERSION whenever a line changes: files of another version are not listed or read.
inline constexpr int SAVE_VERSION = 5;
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
    int propAction = 0;
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
std::vector<SaveSummary> listSaves();
std::string nextAutosavePath();
std::string manualSavePath(long long ts);
