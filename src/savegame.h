#pragma once
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>
#include "resources.h"

// Save files are plain text. The terrain comes from the map the save was made on, and the
// building from the seed. A save stores what changes: game progress, units, and keys still on the map.

inline constexpr const char* SAVE_DIR = "saves";
inline constexpr int AUTOSAVE_SLOTS = 3;   // autosave0, autosave1, autosave2 rotate

struct SaveUnit {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    bool hostile = false;
    int sprite = 1;
    std::string name;
};

struct SaveKey {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

// Where the camera was looking when saved. Older saves have no camera line, so present stays false.
struct SaveCamera {
    bool present = false;
    float fx = 0.0f, fy = 0.0f, fz = 0.0f;   // focus point
    float yaw = 0.0f, pitch = 0.0f, distance = 0.0f;
};

struct SaveData {
    std::string name;
    bool autosave = false;            // true for autosave0..2, false for manual saves
    long long timestamp = 0;          // milliseconds since 1970, used for ordering
    unsigned seed = 0;                // places the building, units and keys
    std::string mapSource;            // "data" for a premade map, "custom" for one in custom_maps
    std::string mapStem;              // the map's file name without .map
    GameState gs;
    bool doorUnlocked = false;
    SaveCamera cam;
    std::vector<SaveUnit> units;
    std::vector<SaveKey> keys;
};

struct SaveSummary {
    std::string path;
    std::string name;
    long long timestamp = 0;
    bool autosave = false;
};

// ISO 8601 style, 24-hour local time, with milliseconds: "2026-10-17_08:24:05.123".
// ms is milliseconds since 1970.
inline std::string timeString(long long ms) {
    std::time_t t = (std::time_t)(ms / 1000);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char date[32];
    std::strftime(date, sizeof(date), "%Y-%m-%d_%H:%M:%S", &tmv);
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%s.%03lld", date, ms % 1000);
    return buf;
}

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
    out << "keysCollected=" << d.gs.keysCollected << "\n";
    out << "unitsEntered=" << d.gs.unitsEntered << "\n";
    out << "won=" << (d.gs.won ? 1 : 0) << "\n";
    out << "dayMinutes=" << d.gs.dayMinutes << "\n";
    out << "doorUnlocked=" << (d.doorUnlocked ? 1 : 0) << "\n";
    out << "autosave=" << (d.autosave ? 1 : 0) << "\n";
    out << "camera " << d.cam.fx << " " << d.cam.fy << " " << d.cam.fz << " "
        << d.cam.yaw << " " << d.cam.pitch << " " << d.cam.distance << "\n";
    for (const SaveUnit& u : d.units) {
        out << "unit " << u.x << " " << u.y << " " << u.z << " "
            << (u.hostile ? 1 : 0) << " " << u.sprite << " " << u.name << "\n";
    }
    for (const SaveKey& k : d.keys) {
        out << "key " << k.x << " " << k.y << " " << k.z << "\n";
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
            else if (line.rfind("keysCollected=", 0) == 0)    d.gs.keysCollected = std::stoi(line.substr(14));
            else if (line.rfind("unitsEntered=", 0) == 0)    d.gs.unitsEntered = std::stoi(line.substr(13));
            else if (line.rfind("won=", 0) == 0)              d.gs.won = (line.substr(4) == "1");
            else if (line.rfind("dayMinutes=", 0) == 0)       d.gs.dayMinutes = std::stof(line.substr(11));
            else if (line.rfind("doorUnlocked=", 0) == 0)    d.doorUnlocked = (line.substr(13) == "1");
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
                u.hostile = hostile != 0;
                if (u.name.empty()) u.name = "Unnamed Unit";
                d.units.push_back(u);
            }
            else if (line.rfind("key ", 0) == 0) {
                std::istringstream ss(line.substr(4));
                SaveKey k;
                ss >> k.x >> k.y >> k.z;
                d.keys.push_back(k);
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
