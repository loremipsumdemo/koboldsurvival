// Writing and reading save files, and finding them in the saves folder (savegame.h)
#include "savegame.h"
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
#include <system_error>

bool writeSaveFile(const std::string& path, const SaveData& d) {
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
        if (u.unconscious) out << "down " << i << " " << u.dying << " " << (u.corpse ? 1 : 0) << "\n";
        if (u.unconscious && u.koFaction >= 0) out << "koby " << i << " " << u.koFaction << "\n";
        if (u.unconscious) out << "conscious " << i << " " << u.conscious << "\n";
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
        out << "fuel " << f.prop << " " << std::setprecision(15) << f.hours << " " << f.progress << std::setprecision(9)
            << " " << f.item << "\n";
    }
    return (bool)out;
}

bool readSaveFile(const std::string& path, SaveData& d) {
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
            else if (line.rfind("conscious ", 0) == 0) {
                std::istringstream ss(line.substr(10));
                size_t i = 0;
                float conscious = 0.0f;
                ss >> i >> conscious;
                if (!ss.fail() && i < d.units.size()) d.units[i].conscious = std::clamp(conscious, 0.0f, 100.0f);
            }
            // Older saves counted down in-game hours (at most 1) to waking instead
            else if (line.rfind("downrest ", 0) == 0) {
                std::istringstream ss(line.substr(9));
                size_t i = 0;
                float wake = 0.0f;
                ss >> i >> wake;
                if (!ss.fail() && i < d.units.size()) d.units[i].conscious = std::clamp((1.0f - wake) * 100.0f, 0.0f, 100.0f);
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
                float dying = 0.0f;
                int corpse = 0, carrier = -1;
                ss >> i >> dying >> corpse;
                if (!ss.fail() && i < d.units.size()) {
                    d.units[i].unconscious = true;
                    d.units[i].dying = std::clamp(dying, 0.0f, 100.0f);
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
                if (!ss.fail()) {
                    if (!(ss >> f.item)) f.item = 0;   // older saves end before the item
                    d.fuel.push_back(f);
                }
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

std::vector<SaveSummary> listSaves() {
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

std::string nextAutosavePath() {
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

std::string manualSavePath(long long ts) {
    return std::string(SAVE_DIR) + "/manual_" + std::to_string(ts) + ".sav";
}
