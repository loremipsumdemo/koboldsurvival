#pragma once
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
#include "resources.h"
#include "timefmt.h"

// Save files are plain text. The terrain comes from the map the save was made on, and the props
// and red units from the seed. A save stores what changes: the clock, the units, what they carry,
// their behavior rules, the items on the ground and the camera.

inline constexpr const char* SAVE_DIR = "saves";
inline constexpr int AUTOSAVE_SLOTS = 3;   // autosave0, autosave1, autosave2 rotate

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
    bool hasBody = false;
    int body = 0;
    bool hasFaction = false;   // older saves have none: the faction comes from the unit's side
    int faction = 0;
    int occupying = -1;        // the serial of the bed, chair or cage it is on, or -1
    // Unconscious (components.h): its Doom, whether it is a corpse, and the index of the unit carrying it, or -1.
    // Older saves have none of these, so their units are awake.
    bool unconscious = false;
    float doom = 0.0f;
    bool corpse = false;
    int carriedBy = -1;
    std::string name;
    // Older saves have no needs or items. Those units start at full health and hunger, empty-handed.
    bool hasNeeds = false;
    Needs needs;
    std::vector<SaveStack> stacks;
    std::vector<SaveGear> gear;   // older saves have none, so their gear slots start empty
    // Older saves have no stats. Those units get defaultStats().
    bool hasStats = false;
    Stats stats;
    // Older saves have no behaviors. Those units get defaultBehaviors().
    bool hasBehaviors = false;
    std::vector<BehaviorRule> behaviors;   // highest priority first
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
        if (u.hasFaction) out << "faction " << i << " " << u.faction << "\n";
        if (u.occupying >= 0) out << "occupy " << i << " " << u.occupying << "\n";
        if (u.unconscious) {
            out << "down " << i << " " << u.doom << " " << (u.corpse ? 1 : 0) << " " << u.carriedBy << "\n";
        }
        if (u.hasBehaviors) {
            out << "behaviors " << i << "\n";
            for (const BehaviorRule& b : u.behaviors) {
                out << "behavior " << i << " " << (b.on ? 1 : 0) << " " << (int)b.conditionTarget << " "
                    << (int)b.condition << " " << (int)b.op << " " << b.percent << " " << (int)b.action << " "
                    << (int)b.actionTarget << "\n";
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
                ss >> i >> doom >> corpse >> carrier;
                if (!ss.fail() && i < d.units.size()) {
                    d.units[i].unconscious = true;
                    d.units[i].doom = std::clamp(doom, 0.0f, 100.0f);
                    d.units[i].corpse = corpse != 0;
                    d.units[i].carriedBy = carrier;
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
                    d.units[i].faction = std::clamp(f, 0, (int)FACTION_COUNT - 1);
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
                if (!ss.fail() && i < d.units.size()) d.units[i].hasBehaviors = true;
            }
            else if (line.rfind("behavior ", 0) == 0) {
                std::istringstream ss(line.substr(9));
                size_t i = 0;
                int on = 0, conditionTarget = 0, condition = 0, op = 0, percent = 0, action = 0, actionTarget = 0;
                ss >> i >> on >> conditionTarget >> condition >> op >> percent >> action >> actionTarget;
                if (!ss.fail() && i < d.units.size() &&
                    d.units[i].behaviors.size() < (size_t)BEHAVIOR_LIMIT) {
                    d.units[i].behaviors.push_back(ruleFromFields(on, conditionTarget, condition, op, percent,
                                                                  action, actionTarget));
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
