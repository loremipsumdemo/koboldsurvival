#pragma once
#include "building.h"
#include <array>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include "components.h"
#include "game_state.h"
#include "squads.h"
#include "ghosts.h"

// Plain text, one record per line. Terrain comes from the named map; everything else is in the file.
// Props, mobs, behaviors and stats are named by their keys, so adding one needs no new version. Whenever a line changes,
// raise SAVE_VERSION and add a step to save_migrate.cpp that turns the old lines into the new, and keep a file of the old
// version in tests/saves. A file from a version older than SAVE_OLDEST_VERSION or newer than this game is not read.
inline constexpr int SAVE_VERSION = 16;
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
    int pushing = -1;    // the Wheelbarrow (a prop index) it pushes
    int upstairs = -1;   // the foundation (a prop index) of the building whose upper floor it stands on
    std::optional<Unconscious> down;
    std::optional<double> corpseHours;
    std::optional<Retreating> retreat;   // from version 15
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
    // From version 16: a walk to another floor, by ladders or stairs: where to, and on which floor (move only names a
    // point, which is walked to on the unit's own floor)
    std::optional<Vec2> floorMove;
    int floorMoveStorey = 0;
    int propOrder = -1;
    int workProp = -1;
    bool workUnlocking = false;
    int propAction = 0;
    int squad = -1;        // a military unit's squad, 0 to 8
    bool citizen = false;
    int citizenGroup = 0;  // the behavior group a citizen of yours follows, 0 for Group 1
};

// A behavior group's task list (a group's index, GROUP_REF_MILITARY or GROUP_REF_CITIZEN). A Group task keeps the ref it
// names in its rule's propKind.
struct SaveGroup {
    int ref = 0;
    std::vector<SaveRule> rules;
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
    std::optional<double> outdoorHours;   // from version 15: a bedroll or campfire out in the open
    std::vector<ItemStack> bonesItems;
    std::optional<Health> health;
    std::optional<Foundation> foundation;
    std::optional<Blueprint> blueprint;
    int material = 0;
    std::optional<std::uint32_t> storeFilter;
    bool bedPublic = false;
    std::optional<std::uint32_t> stallPayment;
    std::vector<std::array<int, 3>> stallRequests;   // a Stall's orders: the store (prop index), item, count
};

struct SaveCamera {
    float fx = 0.0f, fy = 0.0f, fz = 0.0f;
    float yaw = 0.0f, pitch = 0.0f, distance = 0.0f;
};

struct SaveData {
    std::string name;
    bool storesListed = false;   // from version 13: the Citizen Default already has its store hauls
    bool stallHaulListed = false;   // units and the Citizen Default already have Auto-Haul to Stalls
    bool autosave = false;
    long long timestamp = 0;
    std::string mapSource, mapStem;
    GameState gs;
    SaveCamera cam;
    std::string rngState;
    std::map<int, int> reputation;
    std::array<std::string, SQUAD_COUNT> squadNames{};
    int currentSquad = 0;
    std::vector<Ghost> ghosts;
    std::vector<SaveGroup> groups;   // empty in a file from before behavior groups: the defaults are used
    std::vector<SaveProp> props;
    std::vector<SaveGround> ground;
    std::vector<SaveUnit> units;
    std::vector<std::array<float, 3>> groundEdits;   // ground levelled in play: a corner's x, z and height
};

struct SaveSummary {
    std::string path;
    std::string name;
    long long timestamp = 0;
    bool autosave = false;
    std::string mapSource, mapStem;
    std::string mapName;   // filled in by the list that shows it
    double clock = -1.0;   // the game's clock, in seconds; below 0 if not known (a level has none)
};

// The picture kept beside a save or level file: the same path with .png in place of its extension
std::string savePreviewPath(const std::string& path);

bool writeSaveFile(const std::string& path, const SaveData& d);
// False if the file is damaged, truncated or of another version
bool readSaveFile(const std::string& path, SaveData& d);
// Turns the lines of a file of any version this game can read into the current version's. False if it can't.
bool upgradeSaveLines(std::vector<std::string>& lines);
std::vector<SaveSummary> listSaves();
std::string nextAutosavePath();
std::string manualSavePath(long long ts);
