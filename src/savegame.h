#pragma once
#include <map>
#include <string>
#include <vector>
#include "components.h"
#include "mobs.h"
#include "game_state.h"
#include "timefmt.h"

// Save files are plain text. The terrain comes from the map the save was made on, and the props
// and red units from the seed. A save stores what changes: the clock, the units, what they carry,
// their behavior rules, the items on the ground and the camera.

inline constexpr const char* SAVE_DIR = "saves";
inline constexpr int AUTOSAVE_SLOTS = 3;   // autosave0, autosave1, autosave2 rotate

// One of a unit's behaviors. An Auto-Haul's prop is named by where it stands (x, z), since a camp's props are placed
// again from the seed on the same spots, and a built prop has no serial.
struct SaveBehavior {
    BehaviorRule rule;
    float x = 0.0f, z = 0.0f;
};

// One occupied inventory slot of a unit
struct SaveStack {
    int slot = 0;
    ItemStack stack;
};

// One worn gear item of a unit: the gear slot and the item in it
struct SaveGear {
    int slot = 0;
    int item = 0;
};

struct SaveUnit {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    bool hostile = false;
    bool neutral = false;
    int sprite = 1;
    // Older saves have no body type, so it is worked out from the name (bodyForSavedName)
    int mob = 0;   // a mob's kind (mobs.h); 0 for a person, and for every unit of an older save
    // Patrol Spawn: where it was placed, and whether it is walking back there (SpawnHome)
    bool hasHome = false;
    float homeX = 0.0f, homeZ = 0.0f;
    bool homeReturning = false;
    bool hasBody = false;
    int body = 0;
    bool hasFaction = false;   // older saves have none: the faction comes from the unit's side
    int faction = 0;
    int occupying = -1;        // the serial of the bed, chair or cage it is on, or -1
    // Unconscious (components.h): its Doom and whether it is a corpse. Older saves have none, so their units are awake.
    bool unconscious = false;
    float doom = 0.0f;
    bool corpse = false;
    int koFaction = -1;        // the faction that knocked it out, or -1 (Unconscious::koFaction)
    bool downRested = false;   // knocked out well rested: no Doom, and wakes when wake (in-game hours) runs out
    float wake = 0.0f;
    // Carried: the index of the unit carrying it, or -1, and whether it is playing dead
    int carriedBy = -1;
    bool playingDead = false;
    std::string name;
    // Older saves have no needs or items. Those units start at full health and hunger, empty-handed.
    bool hasNeeds = false;
    Needs needs;
    std::vector<SaveStack> stacks;
    std::vector<SaveGear> gear;   // older saves have none, so their gear slots start empty
    // Older saves have no stats. Those units get defaultStats().
    bool hasStats = false;
    Stats stats;
    // Older saves have no behaviors, or the rules that came before them. Those units get defaultBehaviors().
    bool hasBehaviors = false;
    // Its list is kept whole as saved, any needs behavior it lacks having been deleted. An older save's list may be
    // missing needs behaviors that came later, so it gets them back.
    bool behaviorsWhole = false;
    std::vector<SaveBehavior> behaviors;   // highest priority first
    // Its fighting modes from the Behavior tab. Older saves have none: all off.
    bool passive = false, block = false, sneak = false;
    // An older save's routine, which became Auto-Haul to the store prop and Auto-Harvest of the gather prop's kind. The
    // props are named by where they stand. Only read, never written.
    bool hasRoutine = false;
    bool routineOn = true;
    int routineItem = 0;
    float storeX = 0.0f, storeZ = 0.0f, gatherX = 0.0f, gatherZ = 0.0f;
};

// An item lying on the ground when the game was saved
struct SaveGround {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    int item = 0;
    int count = 1;
};

// One stack in a storage prop (a barrel). The prop is named by its serial (components.h), which is the same each time
// the level is built from its seed.
struct SaveStorage {
    int prop = 0;
    int slot = 0;
    int item = 0;
    int count = 0;
};

// Whether a lockable prop, named by its serial, is locked. Older saves have none, so their locks start locked.
struct SaveLock {
    int prop = 0;
    bool locked = true;
};

// Where the camera was looking when saved. Older saves have no camera line, so present stays false.
// A passive crafting prop's (a forge's) fuel hours left and progress toward its next item, by serial. Older saves have
// none, so their forges start cold.
struct SaveFuel {
    int prop = 0;
    double hours = 0.0;
    double progress = 0.0;
    int item = 0;   // the input item burning (a campfire's meat). Older saves have none: 0
};

struct SaveCamera {
    bool present = false;
    float fx = 0.0f, fy = 0.0f, fz = 0.0f;   // focus point
    float yaw = 0.0f, pitch = 0.0f, distance = 0.0f;
};

struct SaveData {
    std::string name;
    bool autosave = false;            // true for autosave0..2, false for manual saves
    long long timestamp = 0;          // milliseconds since 1970, used for ordering
    unsigned seed = 0;                // places the red units and the props
    std::string mapSource;            // "data" for a premade map, "world" for a game world, "custom" for an older .map file
    std::string mapStem;              // the map's file name without .map
    GameState gs;
    SaveCamera cam;
    std::vector<SaveUnit> units;
    // Older saves have no item lines. Their items are placed again from the seed when they load.
    bool hasItems = false;
    // The prop layout the save was made with (PROP_LAYOUT in props.h). Older saves have 0. When it differs from the
    // game's, the saved prop states (what storages hold, locks, forges, who sits where) name other props, so they are
    // left out.
    int propLayout = 0;
    std::vector<SaveGround> ground;
    std::vector<SaveStorage> storage;   // what the barrels hold. Older saves have none, so their barrels start empty.
    std::vector<SaveLock> locks;
    std::vector<SaveStorage> output;    // what the forges' outputs hold
    std::vector<SaveFuel> fuel;
    // The Player Faction's reputation with each faction. Older saves have none: every one starts at its default.
    std::map<int, int> reputation;
    // Whether the level has your camp (setupWorld). It is rebuilt from the seed, so this must match. Older saves all have one.
    bool playerCamp = true;
};

struct SaveSummary {
    std::string path;
    std::string name;
    long long timestamp = 0;
    bool autosave = false;
};

bool writeSaveFile(const std::string& path, const SaveData& d);
bool readSaveFile(const std::string& path, SaveData& d);
// Every save in the folder, newest first
std::vector<SaveSummary> listSaves();
// The next autosave slot: an empty one first, otherwise the oldest
std::string nextAutosavePath();
std::string manualSavePath(long long ts);
