#include "user_files.h"
#include <SDL3/SDL.h>
#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

void setGameFolder() {
    const char* base = SDL_GetBasePath();
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
