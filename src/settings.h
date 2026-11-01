#pragma once
#include <algorithm>
#include "language.h"
#include <fstream>
#include <string>

// Game-wide options. These belong to the player, not to any save game, so they are kept in their own file
// and loaded at startup. Whatever the file says, every value is brought into a range the menus can show.

// Display modes
inline constexpr int DISPLAY_WINDOWED = 0;
inline constexpr int DISPLAY_BORDERLESS = 1;
inline constexpr int DISPLAY_FULLSCREEN = 2;
inline constexpr int DISPLAY_MODE_COUNT = 3;
inline constexpr const char* DISPLAY_MODE_NAMES[] = { "UILABEL_WINDOWED", "UILABEL_BORDERLESS", "UILABEL_FULL_SCREEN" };   // language keys

// Frame rate limits in frames per second. 0 is no limit.
inline constexpr int FPS_CHOICES[] = { 0, 30, 60, 90, 120, 144, 165, 240 };
inline constexpr int FPS_CHOICE_COUNT = 8;

// Autosave intervals in minutes. 0 is off.
inline constexpr int AUTOSAVE_CHOICES[] = { 0, 1, 2, 5, 10, 15, 30 };
inline constexpr int AUTOSAVE_COUNT = 7;

// The UI scale range in percent, and the steps the slider moves in
inline constexpr int UI_SCALE_MIN = 50;
inline constexpr int UI_SCALE_MAX = 200;
inline constexpr int UI_SCALE_STEP = 5;

struct GameSettings {
    // Graphics
    int displayMode = DISPLAY_BORDERLESS;
    int monitor = 0;                // index into the monitors, in the order the system lists them
    int width = 1280;               // window size when windowed, and the resolution when fullscreen
    int height = 720;
    int refreshHz = 0;              // fullscreen refresh rate, 0 for the monitor's default
    bool vsync = true;
    int fpsLimit = 60;              // frames per second, 0 for no limit

    // Interface
    int autosaveMinutes = 5;        // 0 for off
    bool showUnitNames = true;      // each of your units' names over its head, in brackets
    bool showNpcNames = false;      // the same for every other unit: neutral and hostile
    bool showPropNames = false;     // each prop's name over it, in purple
    int fontScale = 100;            // UI scale as a percentage of the base sizes, 50 to 200 in steps of 5
    std::string fontPath;           // the UI font file, or empty for Automatic (the built-in fonts)
    std::string language = LANGUAGE_DEFAULT_ID;   // the language's id, e.g. "language_fr"

    // Controls
    bool edgeScroll = false;

    // How Optimize and Optimize All choose gear (GearPrefs in equip.h), as last set in the Optimize All dialog
    int optimizeAttackShare = 50;
    bool optimizeAccessories = true;
    bool optimizeTwoHanded = true;
    bool optimizeValueTies = true;

    // Audio: each bus has a level from 0 to 100, and can be switched off without losing the level. The global level
    // scales all three: a bus is heard at its level times the global level.
    int globalVolume = 50;
    bool globalOn = true;
    int musicVolume = 5;
    bool musicOn = true;
    int ambientVolume = 50;
    bool ambientOn = true;
    int effectsVolume = 50;
    bool effectsOn = true;
    int interfaceVolume = 5;    // the menus' and buttons' sounds
    bool interfaceOn = true;
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
        else if (key == "npcNames")        s.showNpcNames = (val == "1");
        else if (key == "propNames")       s.showPropNames = (val == "1");
        else if (key == "fontScale")       s.fontScale = parseInt(val, s.fontScale);
        else if (key == "fontPath")        s.fontPath = val;
        else if (key == "language") {
            // Older settings files stored 0 for English and 1 for Spanish, and the first language files had longer names
            if (val == "0" || val == "language_english") s.language = LANGUAGE_DEFAULT_ID;
            else if (val == "1") s.language = "language_es";
            else if (val == "language_spanish") s.language = "language_es";
            else if (val == "language_french") s.language = "language_fr";
            else s.language = val;
        }
        else if (key == "edgeScroll")      s.edgeScroll = (val == "1");
        else if (key == "optimizeAttackShare") s.optimizeAttackShare = parseInt(val, s.optimizeAttackShare);
        else if (key == "optimizeAccessories") s.optimizeAccessories = (val == "1");
        else if (key == "optimizeTwoHanded")   s.optimizeTwoHanded = (val == "1");
        else if (key == "optimizeValueTies")   s.optimizeValueTies = (val == "1");
        // Earlier builds called it the master, then the total volume
        else if (key == "globalVolume" || key == "masterVolume" || key == "totalVolume") s.globalVolume = parseInt(val, s.globalVolume);
        else if (key == "globalOn" || key == "masterOn" || key == "totalOn") s.globalOn = (val == "1");
        else if (key == "musicVolume")     s.musicVolume = parseInt(val, s.musicVolume);
        else if (key == "musicOn")         s.musicOn = (val == "1");
        else if (key == "ambientVolume")   s.ambientVolume = parseInt(val, s.ambientVolume);
        else if (key == "ambientOn")       s.ambientOn = (val == "1");
        else if (key == "effectsVolume")   s.effectsVolume = parseInt(val, s.effectsVolume);
        else if (key == "effectsOn")       s.effectsOn = (val == "1");
        else if (key == "interfaceVolume") s.interfaceVolume = parseInt(val, s.interfaceVolume);
        else if (key == "interfaceOn")     s.interfaceOn = (val == "1");
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
    if (!isChoice(s.fpsLimit, FPS_CHOICES, FPS_CHOICE_COUNT)) s.fpsLimit = GameSettings{}.fpsLimit;
    if (!isChoice(s.autosaveMinutes, AUTOSAVE_CHOICES, AUTOSAVE_COUNT)) s.autosaveMinutes = 5;
    s.globalVolume = std::clamp(s.globalVolume, 0, 100);
    s.optimizeAttackShare = std::clamp(s.optimizeAttackShare, 0, 100);
    s.musicVolume = std::clamp(s.musicVolume, 0, 100);
    s.ambientVolume = std::clamp(s.ambientVolume, 0, 100);
    s.effectsVolume = std::clamp(s.effectsVolume, 0, 100);
    s.interfaceVolume = std::clamp(s.interfaceVolume, 0, 100);
    s.fontScale = std::clamp(s.fontScale, UI_SCALE_MIN, UI_SCALE_MAX);
    s.fontScale = UI_SCALE_MIN + 5 * ((s.fontScale - UI_SCALE_MIN + 2) / 5);
    if (!validLanguageId(s.language)) s.language = LANGUAGE_DEFAULT_ID;
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
    out << "npcNames=" << (s.showNpcNames ? 1 : 0) << "\n";
    out << "propNames=" << (s.showPropNames ? 1 : 0) << "\n";
    out << "fontScale=" << s.fontScale << "\n";
    out << "fontPath=" << s.fontPath << "\n";
    out << "language=" << s.language << "\n";
    out << "edgeScroll=" << (s.edgeScroll ? 1 : 0) << "\n";
    out << "optimizeAttackShare=" << s.optimizeAttackShare << "\n";
    out << "optimizeAccessories=" << (s.optimizeAccessories ? 1 : 0) << "\n";
    out << "optimizeTwoHanded=" << (s.optimizeTwoHanded ? 1 : 0) << "\n";
    out << "optimizeValueTies=" << (s.optimizeValueTies ? 1 : 0) << "\n";
    out << "globalVolume=" << s.globalVolume << "\n";
    out << "globalOn=" << (s.globalOn ? 1 : 0) << "\n";
    out << "musicVolume=" << s.musicVolume << "\n";
    out << "musicOn=" << (s.musicOn ? 1 : 0) << "\n";
    out << "ambientVolume=" << s.ambientVolume << "\n";
    out << "ambientOn=" << (s.ambientOn ? 1 : 0) << "\n";
    out << "effectsVolume=" << s.effectsVolume << "\n";
    out << "effectsOn=" << (s.effectsOn ? 1 : 0) << "\n";
    out << "interfaceVolume=" << s.interfaceVolume << "\n";
    out << "interfaceOn=" << (s.interfaceOn ? 1 : 0) << "\n";
    out.close();                       // flushes, so a failed write shows up in the state
    return !out.fail();
}
