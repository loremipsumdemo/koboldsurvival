// The maps a game can start on.
#include "level.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>
#include "language.h"
#include "map_file.h"
#include "pak.h"
#include "textcase.h"
#include "world_format.h"


// Game worlds are kept in this folder, beside the game.
std::string mapsDir() {
    const char* base = SDL_GetBasePath();
    return std::string(base ? base : "") + "maps";
}

// A folder left as custom_maps by an earlier version moves to maps the first time the game starts
void moveOldMapsFolder() {
    const char* base = SDL_GetBasePath();
    const std::filesystem::path oldDir = std::string(base ? base : "") + "custom_maps";
    const std::filesystem::path newDir = mapsDir();
    std::error_code ec;
    if (!std::filesystem::is_directory(oldDir, ec) || std::filesystem::exists(newDir, ec)) return;
    std::filesystem::rename(oldDir, newDir, ec);
    if (ec) SDL_Log("Could not move custom_maps to maps: %s", ec.message().c_str());
}

bool mainMapPacked() {
    const unsigned char* bytes = nullptr;
    size_t size = 0;
    return pakFind(std::string("data/map/") + MAIN_MAP_STEM + "/world.txt", bytes, size);
}

// Every map the New Game list can start, not yet sorted: the main map, the premade maps, then worlds and .map files
std::vector<MapChoice> listMaps() {
    std::vector<MapChoice> out;
    if (mainMapPacked()) {
        out.push_back(MapChoice{ "packed", MAIN_MAP_STEM, MAIN_MAP_STEM, 0,
                                 std::string(PAK_WORLD_PREFIX) + "data/map/" + MAIN_MAP_STEM });
    }
    const std::string prefix = "data/map/", suffix = ".map";
    for (const std::string& pak : pakNames(prefix, suffix)) {
        const std::string stem = pak.substr(prefix.size(), pak.size() - prefix.size() - suffix.size());
        out.push_back(MapChoice{ "data", stem, stem, 0, "" });
    }

    const std::string dir = mapsDir();
    for (const WorldEntry& e : listWorlds(dir)) {
        const std::string stem = std::filesystem::path(e.folder).filename().string();
        out.push_back(MapChoice{ "world", stem, e.name, e.created, e.folder });
    }

    std::error_code ec;
    if (std::filesystem::is_directory(dir, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (entry.path().extension() != ".map") continue;
            const std::string stem = entry.path().stem().string();
            out.push_back(MapChoice{ "custom", stem, stem, 0, entry.path().string() });
        }
    }
    return out;
}
