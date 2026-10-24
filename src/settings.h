#pragma once
#include <algorithm>
#include <fstream>
#include <string>

// Game-wide options. These belong to the player, not to any save game, so they are kept in their own file
// and loaded at startup. Whatever the file says, every value is brought into a range the menus can show.

// Display modes
inline constexpr int DISPLAY_WINDOWED = 0;
inline constexpr int DISPLAY_BORDERLESS = 1;
inline constexpr int DISPLAY_FULLSCREEN = 2;
inline constexpr int DISPLAY_MODE_COUNT = 3;
inline constexpr const char* DISPLAY_MODE_NAMES[] = { "Windowed", "Borderless", "Full screen" };

// Frame rate limits in frames per second. 0 is no limit.
inline constexpr int FPS_CHOICES[] = { 0, 30, 60, 90, 120, 144, 165, 240 };
inline constexpr int FPS_CHOICE_COUNT = 8;

// Autosave intervals in minutes. 0 is off.
inline constexpr int AUTOSAVE_CHOICES[] = { 0, 1, 2, 5, 10, 15, 30 };
inline constexpr int AUTOSAVE_COUNT = 7;

struct GameSettings {
    // Graphics
    int displayMode = DISPLAY_BORDERLESS;
    int monitor = 0;                // index into the monitors, in the order the system lists them
    int width = 1280;               // window size when windowed, and the resolution when fullscreen
    int height = 720;
    int refreshHz = 0;              // fullscreen refresh rate, 0 for the monitor's default
    bool vsync = true;
    int fpsLimit = 0;               // 0 for no limit

    // Interface
    int autosaveMinutes = 5;        // 0 for off
    bool showUnitNames = true;      // each unit's name over its head, in brackets

    // Controls
    bool edgeScroll = false;

    // Audio: each bus has a level from 0 to 100, and can be switched off without losing the level
    int musicVolume = 80;
    bool musicOn = true;
    int ambientVolume = 80;
    bool ambientOn = true;
    int effectsVolume = 80;
    bool effectsOn = true;
};

inline int parseInt(const std::string& v, int fallback) {
    try { return std::stoi(v); } catch (...) { return fallback; }
}

inline bool isChoice(int v, const int* choices, int count) {
    return std::find(choices, choices + count, v) != choices + count;
}

// Reads key=value lines. A missing file or a bad value falls back to the defaults. Files from before the
// window size was stored have a resolution preset index instead, which is converted here.
inline GameSettings loadSettings(const std::string& path) {
    GameSettings s;
    std::ifstream in(path);
    std::string line;
    int legacyPreset = -1;
    bool sawWidth = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const std::string val = line.substr(eq + 1);
        if (key == "displayMode")          s.displayMode = parseInt(val, s.displayMode);
        else if (key == "monitor")         s.monitor = parseInt(val, s.monitor);
        else if (key == "width")         { s.width = parseInt(val, s.width); sawWidth = true; }
        else if (key == "height")          s.height = parseInt(val, s.height);
        else if (key == "resolution")      legacyPreset = parseInt(val, -1);
        else if (key == "refreshHz")       s.refreshHz = parseInt(val, s.refreshHz);
        else if (key == "vsync")           s.vsync = (val == "1");
        else if (key == "fpsLimit")        s.fpsLimit = parseInt(val, s.fpsLimit);
        else if (key == "autosaveMinutes") s.autosaveMinutes = parseInt(val, s.autosaveMinutes);
        else if (key == "unitNames")       s.showUnitNames = (val == "1");
        else if (key == "edgeScroll")      s.edgeScroll = (val == "1");
        else if (key == "musicVolume")     s.musicVolume = parseInt(val, s.musicVolume);
        else if (key == "musicOn")         s.musicOn = (val == "1");
        else if (key == "ambientVolume")   s.ambientVolume = parseInt(val, s.ambientVolume);
        else if (key == "ambientOn")       s.ambientOn = (val == "1");
        else if (key == "effectsVolume")   s.effectsVolume = parseInt(val, s.effectsVolume);
        else if (key == "effectsOn")       s.effectsOn = (val == "1");
    }

    // Old files: the preset index used the four sizes that were offered before
    if (!sawWidth && legacyPreset >= 0 && legacyPreset < 4) {
        static const int legacyW[] = { 1280, 1600, 1920, 2560 };
        static const int legacyH[] = { 720, 900, 1080, 1440 };
        s.width = legacyW[legacyPreset];
        s.height = legacyH[legacyPreset];
    }

    s.displayMode = std::clamp(s.displayMode, 0, DISPLAY_MODE_COUNT - 1);
    s.monitor = std::clamp(s.monitor, 0, 15);
    s.width = std::clamp(s.width, 640, 15360);
    s.height = std::clamp(s.height, 480, 8640);
    s.refreshHz = std::clamp(s.refreshHz, 0, 1000);
    if (!isChoice(s.fpsLimit, FPS_CHOICES, FPS_CHOICE_COUNT)) s.fpsLimit = 0;
    if (!isChoice(s.autosaveMinutes, AUTOSAVE_CHOICES, AUTOSAVE_COUNT)) s.autosaveMinutes = 5;
    s.musicVolume = std::clamp(s.musicVolume, 0, 100);
    s.ambientVolume = std::clamp(s.ambientVolume, 0, 100);
    s.effectsVolume = std::clamp(s.effectsVolume, 0, 100);
    return s;
}

inline bool saveSettings(const GameSettings& s, const std::string& path) {
    std::ofstream out(path);
    if (!out) return false;
    out << "displayMode=" << s.displayMode << "\n";
    out << "monitor=" << s.monitor << "\n";
    out << "width=" << s.width << "\n";
    out << "height=" << s.height << "\n";
    out << "refreshHz=" << s.refreshHz << "\n";
    out << "vsync=" << (s.vsync ? 1 : 0) << "\n";
    out << "fpsLimit=" << s.fpsLimit << "\n";
    out << "autosaveMinutes=" << s.autosaveMinutes << "\n";
    out << "unitNames=" << (s.showUnitNames ? 1 : 0) << "\n";
    out << "edgeScroll=" << (s.edgeScroll ? 1 : 0) << "\n";
    out << "musicVolume=" << s.musicVolume << "\n";
    out << "musicOn=" << (s.musicOn ? 1 : 0) << "\n";
    out << "ambientVolume=" << s.ambientVolume << "\n";
    out << "ambientOn=" << (s.ambientOn ? 1 : 0) << "\n";
    out << "effectsVolume=" << s.effectsVolume << "\n";
    out << "effectsOn=" << (s.effectsOn ? 1 : 0) << "\n";
    out.close();                       // flushes, so a failed write shows up in the state
    return !out.fail();
}
