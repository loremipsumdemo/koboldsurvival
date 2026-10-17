#pragma once
#include <algorithm>
#include <fstream>
#include <string>
#include <SDL3/SDL.h>

// Game-wide options. These belong to the player, not to any save game, so they
// are kept in their own file and loaded at startup.

struct Resolution { int w, h; };
inline constexpr Resolution RESOLUTIONS[] = { {1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440} };
inline constexpr int RESOLUTION_COUNT = 4;

inline constexpr const char* DISPLAY_MODE_NAMES[] = { "Windowed", "Borderless", "Fullscreen" };
inline constexpr int DISPLAY_MODE_COUNT = 3;
inline constexpr int DISPLAY_BORDERLESS = 1;

inline constexpr int AUTOSAVE_CHOICES[] = { 1, 2, 5, 10, 15 };
inline constexpr int AUTOSAVE_COUNT = 5;

struct GameSettings {
    int displayMode = DISPLAY_BORDERLESS;   // 0 windowed, 1 borderless, 2 fullscreen
    int resolutionIndex = 1;                // index into RESOLUTIONS
    bool edgeScroll = false;
    int autosaveMinutes = 5;
};

inline int parseInt(const std::string& v, int fallback) {
    try { return std::stoi(v); } catch (...) { return fallback; }
}

// Reads key=value lines. A missing file or bad value falls back to the defaults.
inline GameSettings loadSettings(const std::string& path) {
    GameSettings s;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        if (key == "displayMode")          s.displayMode = parseInt(val, s.displayMode);
        else if (key == "resolution")      s.resolutionIndex = parseInt(val, s.resolutionIndex);
        else if (key == "edgeScroll")      s.edgeScroll = (val == "1");
        else if (key == "autosaveMinutes") s.autosaveMinutes = parseInt(val, s.autosaveMinutes);
    }

    s.displayMode = std::clamp(s.displayMode, 0, DISPLAY_MODE_COUNT - 1);
    s.resolutionIndex = std::clamp(s.resolutionIndex, 0, RESOLUTION_COUNT - 1);
    bool validAutosave = false;
    for (int m : AUTOSAVE_CHOICES) if (m == s.autosaveMinutes) validAutosave = true;
    if (!validAutosave) s.autosaveMinutes = 5;
    return s;
}

inline bool saveSettings(const GameSettings& s, const std::string& path) {
    std::ofstream out(path);
    if (!out) return false;
    out << "displayMode=" << s.displayMode << "\n";
    out << "resolution=" << s.resolutionIndex << "\n";
    out << "edgeScroll=" << (s.edgeScroll ? 1 : 0) << "\n";
    out << "autosaveMinutes=" << s.autosaveMinutes << "\n";
    return true;
}

// Applies the display mode and resolution to the window
inline void applyDisplay(SDL_Window* window, const GameSettings& s) {
    const Resolution& r = RESOLUTIONS[s.resolutionIndex];

    if (s.displayMode == DISPLAY_BORDERLESS) {
        SDL_SetWindowFullscreen(window, false);
        SDL_SetWindowBordered(window, false);
        const SDL_DisplayMode* desktop = SDL_GetDesktopDisplayMode(SDL_GetDisplayForWindow(window));
        if (desktop) {
            SDL_SetWindowSize(window, desktop->w, desktop->h);
            SDL_SetWindowPosition(window, 0, 0);
        }
    } else if (s.displayMode == 2) {
        // Fullscreen: pick the closest mode the monitor supports to the chosen resolution
        SDL_DisplayMode closest{};
        if (SDL_GetClosestFullscreenDisplayMode(SDL_GetDisplayForWindow(window), r.w, r.h, 0.0f, true, &closest)) {
            SDL_SetWindowFullscreenMode(window, &closest);
        }
        SDL_SetWindowFullscreen(window, true);
    } else {
        SDL_SetWindowFullscreen(window, false);
        SDL_SetWindowBordered(window, true);
        SDL_SetWindowSize(window, r.w, r.h);
        SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }
}
