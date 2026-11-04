#pragma once
#include <map>
#include <algorithm>
#include "language.h"
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <cstdlib>
#include <string>
#include <system_error>
#include <vector>
#include "components.h"
#include "mobs.h"
#include "resources.h"
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

inline bool writeSaveFile(const std::string& path, const SaveData& d) {
    std::error_code ec;
    std::filesystem::create_directories(SAVE_DIR, ec);

    std::ofstream out(path);
    if (!out) return false;
    out << std::setprecision(9);
    out << "name=" << d.name << "\n";
    out << "timestamp=" << d.timestamp << "\n";
    out << "seed=" << d.seed << "\n";
    out << "playerCamp=" << (d.playerCamp ? 1 : 0) << "\n";
    out << "map " << d.mapSource << " " << d.mapStem << "\n";
    out << "clockSeconds=" << std::setprecision(15) << d.gs.clockSeconds << std::setprecision(9) << "\n";
    out << "autosave=" << (d.autosave ? 1 : 0) << "\n";
    out << "camera " << d.cam.fx << " " << d.cam.fy << " " << d.cam.fz << " "
        << d.cam.yaw << " " << d.cam.pitch << " " << d.cam.distance << "\n";
    for (const SaveUnit& u : d.units) {
        out << "unit " << u.x << " " << u.y << " " << u.z << " "
            << (u.hostile ? 1 : u.neutral ? 2 : 0) << " " << u.sprite << " " << u.name << "\n";
    }
    // Lines that refer to a unit by its position in the unit list above
    for (size_t i = 0; i < d.units.size(); ++i) {
        const SaveUnit& u = d.units[i];
        if (u.hasNeeds) {
            out << "needs " << i << " " << u.needs.hp << " " << u.needs.maxHp << " "
                << u.needs.hunger << " " << u.needs.maxHunger << "\n";
            out << "rest " << i << " " << u.needs.rested << " " << u.needs.maxRested << " "
                << u.needs.comfort << " " << u.needs.maxComfort << "\n";
        }
        for (const SaveStack& s : u.stacks) {
            out << "slot " << i << " " << s.slot << " " << s.stack.item << " " << s.stack.count << "\n";
        }
        for (const SaveGear& g : u.gear) {
            out << "gear " << i << " " << g.slot << " " << g.item << "\n";
        }
        if (u.hasStats) {
            out << "stats " << i;
            for (int xp : u.stats.xp) out << " " << xp;
            out << "\n";
        }
        if (u.hasBody) out << "body " << i << " " << u.body << "\n";
        if (u.mob > 0) out << "mob " << i << " " << u.mob << "\n";
        if (u.hasHome) out << "home " << i << " " << u.homeX << " " << u.homeZ << " " << (u.homeReturning ? 1 : 0) << "\n";
        if (u.hasFaction) out << "faction " << i << " " << u.faction << "\n";
        if (u.occupying >= 0) out << "occupy " << i << " " << u.occupying << "\n";
        if (u.unconscious) out << "down " << i << " " << u.doom << " " << (u.corpse ? 1 : 0) << "\n";
        if (u.unconscious && u.koFaction >= 0) out << "koby " << i << " " << u.koFaction << "\n";
        if (u.unconscious && u.downRested) out << "downrest " << i << " " << u.wake << "\n";
        if (u.playingDead && u.carriedBy < 0) out << "still " << i << "\n";
        if (u.passive || u.block || u.sneak) {
            out << "modes " << i << " " << (u.passive ? 1 : 0) << " " << (u.block ? 1 : 0) << " " << (u.sneak ? 1 : 0) << "\n";
        }
        if (u.carriedBy >= 0) out << "carried " << i << " " << u.carriedBy << " " << (u.playingDead ? 1 : 0) << "\n";
        if (u.hasBehaviors) {
            out << "behaviors " << i << " " << (u.behaviorsWhole ? 1 : 0) << "\n";
            for (const SaveBehavior& b : u.behaviors) {
                out << "task " << i << " " << (b.rule.on ? 1 : 0) << " " << (int)b.rule.kind << " " << b.rule.propKind
                    << " " << b.x << " " << b.z << " " << b.rule.percent << "\n";
            }
        }
    }
    if (d.propLayout != 0) out << "propLayout=" << d.propLayout << "\n";
    if (d.hasItems) {
        out << "items=1\n";
        for (const SaveGround& g : d.ground) {
            out << "ground " << g.x << " " << g.y << " " << g.z << " " << g.item << " " << g.count << "\n";
        }
    }
    for (const SaveStorage& st : d.storage) {
        out << "storage " << st.prop << " " << st.slot << " " << st.item << " " << st.count << "\n";
    }
    for (const SaveLock& l : d.locks) out << "lock " << l.prop << " " << (l.locked ? 1 : 0) << "\n";
    for (const auto& [faction, value] : d.reputation) out << "rep " << faction << " " << value << "\n";
    for (const SaveStorage& st : d.output) {
        out << "output " << st.prop << " " << st.slot << " " << st.item << " " << st.count << "\n";
    }
    for (const SaveFuel& f : d.fuel) {
        out << "fuel " << f.prop << " " << std::setprecision(15) << f.hours << " " << f.progress << std::setprecision(9) << "\n";
    }
    return (bool)out;
}

inline bool readSaveFile(const std::string& path, SaveData& d) {
    std::ifstream in(path);
    if (!in) return false;

    try {
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();

            if (line.rfind("name=", 0) == 0)                   d.name = line.substr(5);
            else if (line.rfind("timestamp=", 0) == 0)        d.timestamp = std::stoll(line.substr(10));
            else if (line.rfind("seed=", 0) == 0)             d.seed = (unsigned)std::stoul(line.substr(5));
            else if (line.rfind("playerCamp=", 0) == 0)       d.playerCamp = line.substr(11) != "0";
            else if (line.rfind("map ", 0) == 0) {
                std::istringstream ss(line.substr(4));
                ss >> d.mapSource;
                std::getline(ss >> std::ws, d.mapStem);
            }
            else if (line.rfind("clockSeconds=", 0) == 0)    d.gs.clockSeconds = std::stod(line.substr(13));
            // Saves from before the calendar stored minutes into the day, starting at Day 1
            else if (line.rfind("dayMinutes=", 0) == 0)       d.gs.clockSeconds = std::stod(line.substr(11)) * 60.0;
            else if (line.rfind("autosave=", 0) == 0)        d.autosave = (line.substr(9) == "1");
            else if (line.rfind("camera ", 0) == 0) {
                std::istringstream ss(line.substr(7));
                ss >> d.cam.fx >> d.cam.fy >> d.cam.fz >> d.cam.yaw >> d.cam.pitch >> d.cam.distance;
                d.cam.present = !ss.fail();
            }
            else if (line.rfind("unit ", 0) == 0) {
                std::istringstream ss(line.substr(5));
                SaveUnit u;
                int hostile = 0;
                ss >> u.x >> u.y >> u.z >> hostile >> u.sprite;
                std::getline(ss >> std::ws, u.name);
                u.hostile = hostile == 1;
                u.neutral = hostile == 2;
                if (u.name.empty()) u.name = lang("UILABEL_UNNAMED_UNIT");
                d.units.push_back(u);
            }
            else if (line.rfind("items=", 0) == 0)           d.hasItems = (line.substr(6) == "1");
            else if (line.rfind("propLayout=", 0) == 0)      d.propLayout = std::atoi(line.c_str() + 11);
            else if (line.rfind("lock ", 0) == 0) {
                std::istringstream ss(line.substr(5));
                SaveLock l;
                int locked = 1;
                ss >> l.prop >> locked;
                l.locked = locked != 0;
                if (!ss.fail()) d.locks.push_back(l);
            }
            else if (line.rfind("storage ", 0) == 0) {
                std::istringstream ss(line.substr(8));
                SaveStorage st;
                ss >> st.prop >> st.slot >> st.item >> st.count;
                if (!ss.fail()) d.storage.push_back(st);
            }
            else if (line.rfind("ground ", 0) == 0) {
                std::istringstream ss(line.substr(7));
                SaveGround g;
                ss >> g.x >> g.y >> g.z >> g.item >> g.count;
                if (!ss.fail()) d.ground.push_back(g);
            }
            else if (line.rfind("needs ", 0) == 0) {
                std::istringstream ss(line.substr(6));
                size_t i = 0;
                Needs n;
                ss >> i >> n.hp >> n.maxHp >> n.hunger >> n.maxHunger;   // older saves add a progress value, ignored
                if (!ss.fail() && i < d.units.size()) {
                    d.units[i].hasNeeds = true;
                    d.units[i].needs = n;
                }
            }
            else if (line.rfind("rest ", 0) == 0) {
                // Older saves have no line: their units are fully rested and comfortable
                std::istringstream ss(line.substr(5));
                size_t i = 0;
                int rested = 100, maxRested = 100, comfort = 100, maxComfort = 100;
                ss >> i >> rested >> maxRested >> comfort >> maxComfort;
                if (!ss.fail() && i < d.units.size() && maxRested > 0 && maxComfort > 0) {
                    Needs& n = d.units[i].needs;
                    n.maxRested = maxRested;
                    n.rested = std::clamp(rested, 0, maxRested);
                    n.maxComfort = maxComfort;
                    n.comfort = std::clamp(comfort, 0, maxComfort);
                }
            }
            else if (line.rfind("downrest ", 0) == 0) {
                std::istringstream ss(line.substr(9));
                size_t i = 0;
                float wake = 0.0f;
                ss >> i >> wake;
                if (!ss.fail() && i < d.units.size()) {
                    d.units[i].downRested = true;
                    d.units[i].wake = std::max(0.0f, wake);
                }
            }
            else if (line.rfind("still ", 0) == 0) {
                std::istringstream ss(line.substr(6));
                size_t i = 0;
                ss >> i;
                if (!ss.fail() && i < d.units.size()) d.units[i].playingDead = true;
            }
            else if (line.rfind("slot ", 0) == 0) {
                std::istringstream ss(line.substr(5));
                size_t i = 0;
                SaveStack s;
                ss >> i >> s.slot >> s.stack.item >> s.stack.count;
                if (!ss.fail() && i < d.units.size()) d.units[i].stacks.push_back(s);
            }
            else if (line.rfind("gear ", 0) == 0) {
                std::istringstream ss(line.substr(5));
                size_t i = 0;
                SaveGear g;
                ss >> i >> g.slot >> g.item;
                if (!ss.fail() && i < d.units.size() && g.slot >= 0 && g.slot < GEAR_SLOTS && g.item > 0) {
                    d.units[i].gear.push_back(g);
                }
            }
            else if (line.rfind("down ", 0) == 0) {
                std::istringstream ss(line.substr(5));
                size_t i = 0;
                float doom = 0.0f;
                int corpse = 0, carrier = -1;
                ss >> i >> doom >> corpse;
                if (!ss.fail() && i < d.units.size()) {
                    d.units[i].unconscious = true;
                    d.units[i].doom = std::clamp(doom, 0.0f, 100.0f);
                    d.units[i].corpse = corpse != 0;
                    // A save from the first version of this line also named the carrier here
                    if (ss >> carrier) d.units[i].carriedBy = carrier;
                }
            }
            else if (line.rfind("carried ", 0) == 0) {
                std::istringstream ss(line.substr(8));
                size_t i = 0;
                int by = -1, dead = 0;
                ss >> i >> by >> dead;
                if (!ss.fail() && i < d.units.size()) {
                    d.units[i].carriedBy = by;
                    d.units[i].playingDead = dead != 0;
                }
            }
            else if (line.rfind("occupy ", 0) == 0) {
                std::istringstream ss(line.substr(7));
                size_t i = 0;
                int serial = -1;
                ss >> i >> serial;
                if (!ss.fail() && i < d.units.size() && serial >= 0) d.units[i].occupying = serial;
            }
            else if (line.rfind("output ", 0) == 0) {
                std::istringstream ss(line.substr(7));
                SaveStorage st;
                ss >> st.prop >> st.slot >> st.item >> st.count;
                if (!ss.fail()) d.output.push_back(st);
            }
            else if (line.rfind("fuel ", 0) == 0) {
                std::istringstream ss(line.substr(5));
                SaveFuel f;
                ss >> f.prop >> f.hours >> f.progress;
                if (!ss.fail()) d.fuel.push_back(f);
            }
            else if (line.rfind("faction ", 0) == 0) {
                std::istringstream ss(line.substr(8));
                size_t i = 0;
                int f = 0;
                ss >> i >> f;
                if (!ss.fail() && i < d.units.size()) {
                    d.units[i].hasFaction = true;
                    d.units[i].faction = std::max(0, f);   // an id no longer listed is mapped when the unit loads
                }
            }
            else if (line.rfind("koby ", 0) == 0) {
                std::istringstream ss(line.substr(5));
                size_t i = 0;
                int f = -1;
                ss >> i >> f;
                if (!ss.fail() && i < d.units.size()) d.units[i].koFaction = f;
            }
            else if (line.rfind("routine ", 0) == 0) {
                std::istringstream ss(line.substr(8));
                size_t i = 0;
                int on = 1, item = 0;
                float sx = 0.0f, sz = 0.0f, gx = 0.0f, gz = 0.0f;
                ss >> i >> on >> item >> sx >> sz >> gx >> gz;
                if (!ss.fail() && i < d.units.size() && item > 0) {
                    SaveUnit& u = d.units[i];
                    u.hasRoutine = true;
                    u.routineOn = on != 0;
                    u.routineItem = item;
                    u.storeX = sx;
                    u.storeZ = sz;
                    u.gatherX = gx;
                    u.gatherZ = gz;
                }
            }
            else if (line.rfind("modes ", 0) == 0) {
                std::istringstream ss(line.substr(6));
                size_t i = 0;
                int pa = 0, bl = 0, sn = 0;
                ss >> i >> pa >> bl >> sn;
                if (!ss.fail() && i < d.units.size()) {
                    d.units[i].passive = pa != 0;
                    d.units[i].block = bl != 0;
                    d.units[i].sneak = sn != 0;
                }
            }
            else if (line.rfind("rep ", 0) == 0) {
                std::istringstream ss(line.substr(4));
                int f = 0, v = 0;
                ss >> f >> v;
                if (!ss.fail() && f > 0) d.reputation[f] = std::clamp(v, -100, 100);
            }
            else if (line.rfind("mob ", 0) == 0) {
                std::istringstream ss(line.substr(4));
                size_t i = 0;
                int kind = 0;
                ss >> i >> kind;
                if (!ss.fail() && i < d.units.size() && kind >= 1 && kind <= MOB_KIND_COUNT) d.units[i].mob = kind;
            }
            else if (line.rfind("home ", 0) == 0) {
                std::istringstream ss(line.substr(5));
                size_t i = 0;
                float x = 0.0f, z = 0.0f;
                int back = 0;
                ss >> i >> x >> z >> back;
                if (!ss.fail() && i < d.units.size()) {
                    d.units[i].hasHome = true;
                    d.units[i].homeX = x;
                    d.units[i].homeZ = z;
                    d.units[i].homeReturning = back != 0;
                }
            }
            else if (line.rfind("body ", 0) == 0) {
                std::istringstream ss(line.substr(5));
                size_t i = 0;
                int b = 0;
                ss >> i >> b;
                if (!ss.fail() && i < d.units.size()) {
                    d.units[i].hasBody = true;
                    d.units[i].body = std::clamp(b, 0, 2);
                }
            }
            else if (line.rfind("stats ", 0) == 0) {
                std::istringstream ss(line.substr(6));
                size_t i = 0;
                Stats st;
                ss >> i;
                for (int& xp : st.xp) ss >> xp;
                if (!ss.fail() && i < d.units.size()) {
                    d.units[i].hasStats = true;
                    d.units[i].stats = st;
                }
            }
            else if (line.rfind("behaviors ", 0) == 0) {
                std::istringstream ss(line.substr(10));
                size_t i = 0;
                ss >> i;
                if (!ss.fail() && i < d.units.size()) {
                    d.units[i].hasBehaviors = true;
                    int whole = 0;   // older saves have no flag
                    if (ss >> whole) d.units[i].behaviorsWhole = whole != 0;
                }
            }
            // An older save's "behavior" lines were the rules that came before: they are left out, and the unit gets
            // the default behaviors
            else if (line.rfind("task ", 0) == 0) {
                std::istringstream ss(line.substr(5));
                size_t i = 0;
                int on = 0, kind = 0, propKind = -1;
                SaveBehavior b;
                ss >> i >> on >> kind >> propKind >> b.x >> b.z;
                if (!ss.fail() && i < d.units.size() && d.units[i].behaviors.size() < (size_t)BEHAVIOR_LIMIT) {
                    int percent = BEHAVIOR_NEED_PERCENT;   // a save from before the percent could be set has none
                    if (!(ss >> percent)) percent = BEHAVIOR_NEED_PERCENT;
                    b.rule = behaviorFromFields(on, kind, propKind, percent);
                    d.units[i].behaviors.push_back(b);
                }
            }
        }
    } catch (...) {
        return false;   // a bad number in the file
    }
    return true;
}

// Every save in the folder, newest first
inline std::vector<SaveSummary> listSaves() {
    std::vector<SaveSummary> out;
    std::error_code ec;
    if (!std::filesystem::is_directory(SAVE_DIR, ec)) return out;

    for (const auto& entry : std::filesystem::directory_iterator(SAVE_DIR, ec)) {
        if (entry.path().extension() != ".sav") continue;
        SaveData d;
        if (!readSaveFile(entry.path().string(), d)) continue;
        out.push_back(SaveSummary{ entry.path().string(), d.name, d.timestamp, d.autosave });
    }
    std::sort(out.begin(), out.end(),
              [](const SaveSummary& a, const SaveSummary& b) { return a.timestamp > b.timestamp; });
    return out;
}

// The next autosave slot: an empty one first, otherwise the oldest
inline std::string nextAutosavePath() {
    std::string best = std::string(SAVE_DIR) + "/autosave0.sav";
    long long oldest = std::numeric_limits<long long>::max();
    for (int i = 0; i < AUTOSAVE_SLOTS; ++i) {
        std::string p = std::string(SAVE_DIR) + "/autosave" + std::to_string(i) + ".sav";
        if (!std::filesystem::exists(p)) return p;
        SaveData d;
        long long ts = readSaveFile(p, d) ? d.timestamp : 0;
        if (ts < oldest) { oldest = ts; best = p; }
    }
    return best;
}

inline std::string manualSavePath(long long ts) {
    return std::string(SAVE_DIR) + "/manual_" + std::to_string(ts) + ".sav";
}
