#include "user_files.h"
#include <SDL3/SDL.h>
#include <filesystem>
#include <system_error>
#include "savegame.h"

namespace fs = std::filesystem;

void setGameFolder() {
    const char* base = SDL_GetBasePath();   // owned by SDL; works before SDL_Init
    if (!base) return;
    std::error_code ec;
    fs::current_path(fs::u8path(base), ec);
}

std::string userFilesDir() {
    static const std::string dir = [] {
        char* path = SDL_GetPrefPath("", "KoboldSurvival");
        const std::string out = path ? path : "";
        SDL_free(path);
        return out;
    }();
    return dir;
}

std::string settingsFilePath() {
    return userFilesDir() + "settings.cfg";
}

void copyOldUserFiles() {
    if (userFilesDir().empty()) return;   // no folder of its own: the files are already where they are looked for
    std::error_code ec;
    const fs::path oldSettings = "settings.cfg", newSettings = fs::u8path(settingsFilePath());
    if (fs::exists(oldSettings, ec) && !fs::exists(newSettings, ec)) {
        fs::copy_file(oldSettings, newSettings, ec);
        if (ec) SDL_Log("Could not copy settings.cfg to %s: %s", settingsFilePath().c_str(), ec.message().c_str());
    }
    const fs::path oldSaves = "saves", newSaves = fs::u8path(saveDir());
    ec.clear();
    if (!fs::is_directory(oldSaves, ec) || fs::exists(newSaves, ec)) return;
    fs::create_directories(newSaves, ec);
    for (const auto& entry : fs::directory_iterator(oldSaves, ec)) {
        if (entry.path().extension() != ".sav") continue;
        std::error_code copyError;
        fs::copy_file(entry.path(), newSaves / entry.path().filename(), copyError);
        if (copyError) SDL_Log("Could not copy save %s: %s", entry.path().u8string().c_str(), copyError.message().c_str());
    }
}
