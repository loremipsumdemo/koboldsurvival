#pragma once
#include <algorithm>
#include "language.h"
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <string>

inline constexpr int DISPLAY_WINDOWED = 0;
inline constexpr int DISPLAY_BORDERLESS = 1;
inline constexpr int DISPLAY_FULLSCREEN = 2;
inline constexpr int DISPLAY_MODE_COUNT = 3;
inline constexpr const char* DISPLAY_MODE_NAMES[] = { "UILABEL_WINDOWED", "UILABEL_BORDERLESS", "UILABEL_FULL_SCREEN" };

inline constexpr int FPS_CHOICES[] = { 0, 30, 60, 90, 120, 144, 165, 240 };
inline constexpr int FPS_CHOICE_COUNT = 8;

inline constexpr int AUTOSAVE_CHOICES[] = { 0, 1, 2, 5, 10, 15, 30 };
inline constexpr int AUTOSAVE_COUNT = 7;

inline constexpr int UI_SCALE_MIN = 50;
inline constexpr int UI_SCALE_MAX = 200;
inline constexpr int UI_SCALE_STEP = 5;

inline constexpr int VIEW_RANGE_MIN = 16;
inline constexpr int VIEW_RANGE_MAX = 224;
inline constexpr int VIEW_RANGE_STEP = 8;

struct GameSettings {
    int displayMode = DISPLAY_BORDERLESS;
    int monitor = 0;
    int width = 1280;
    int height = 720;
    int refreshHz = 0;
    bool vsync = true;
    int fpsLimit = 60;
    int viewRange = 96;
    bool showFps = false;

    int autosaveMinutes = 5;
    bool showUnitNames = true;
    bool showNpcNames = true;
    bool showPropNames = true;
    bool showPropInfo = false;
    int fontScale = 100;
    std::string fontPath;
    std::string language = LANGUAGE_DEFAULT_ID;

    bool edgeScroll = false;

    int optimizeAttackShare = 50;
    bool optimizeAccessories = true;
    bool optimizeTwoHanded = true;
    bool optimizeValueTies = true;

    int globalVolume = 50;
    bool globalOn = true;
    int musicVolume = 50;
    bool musicOn = true;
    int ambientVolume = 50;
    bool ambientOn = true;
    int effectsVolume = 50;
    bool effectsOn = true;
    int interfaceVolume = 50;
    bool interfaceOn = true;
    bool craftGatherSounds = false;

    std::array<std::array<float, 4>, 3> panels{};
};

inline const char* panelSettingKey(int i) {
    static const char* keys[3] = { "panelSquad", "panelUnit", "panelInfo" };
    return keys[i];
}

inline int parseInt(const std::string& v, int fallback) {
    try { return std::stoi(v); } catch (...) { return fallback; }
}

inline bool isChoice(int v, const int* choices, int count) {
    return std::find(choices, choices + count, v) != choices + count;
}

inline GameSettings loadSettings(const std::string& path) {
    GameSettings s;
    std::ifstream in(std::filesystem::u8path(path));
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
        else if (key == "viewRange")       s.viewRange = parseInt(val, s.viewRange);
        else if (key == "showFps")         s.showFps = (val == "1");
        else if (key == "autosaveMinutes") s.autosaveMinutes = parseInt(val, s.autosaveMinutes);
        else if (key == "unitNames")       s.showUnitNames = (val == "1");
        else if (key == "npcNames")        s.showNpcNames = (val == "1");
        else if (key == "propNames")       s.showPropNames = (val == "1");
        else if (key == "propInfo")        s.showPropInfo = (val == "1");
        else if (key == "fontScale")       s.fontScale = parseInt(val, s.fontScale);
        else if (key == "fontPath")        s.fontPath = val;
        else if (key == "language")        s.language = val;
        else if (key == "edgeScroll")      s.edgeScroll = (val == "1");
        else if (key == "optimizeAttackShare") s.optimizeAttackShare = parseInt(val, s.optimizeAttackShare);
        else if (key == panelSettingKey(0) || key == panelSettingKey(1) || key == panelSettingKey(2)) {
            const int i = key == panelSettingKey(0) ? 0 : key == panelSettingKey(1) ? 1 : 2;
            float v[4] = {};
            if (std::sscanf(val.c_str(), "%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3]) == 4) {
                for (int k = 0; k < 4; ++k) s.panels[(size_t)i][(size_t)k] = std::clamp(v[k], -20000.0f, 20000.0f);
                s.panels[(size_t)i][2] = std::max(0.0f, s.panels[(size_t)i][2]);
                s.panels[(size_t)i][3] = std::max(0.0f, s.panels[(size_t)i][3]);
            }
        }
        else if (key == "optimizeAccessories") s.optimizeAccessories = (val == "1");
        else if (key == "optimizeTwoHanded")   s.optimizeTwoHanded = (val == "1");
        else if (key == "optimizeValueTies")   s.optimizeValueTies = (val == "1");
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
        else if (key == "craftGatherSounds") s.craftGatherSounds = (val == "1");
    }

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
    s.viewRange = std::clamp(s.viewRange, VIEW_RANGE_MIN, VIEW_RANGE_MAX);
    s.viewRange = VIEW_RANGE_MIN + VIEW_RANGE_STEP * ((s.viewRange - VIEW_RANGE_MIN + VIEW_RANGE_STEP / 2) / VIEW_RANGE_STEP);
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
    const std::filesystem::path tmp = std::filesystem::u8path(path + ".tmp");
    std::ofstream out(tmp);
    if (!out) return false;
    out << "displayMode=" << s.displayMode << "\n";
    out << "monitor=" << s.monitor << "\n";
    out << "width=" << s.width << "\n";
    out << "height=" << s.height << "\n";
    out << "refreshHz=" << s.refreshHz << "\n";
    out << "vsync=" << (s.vsync ? 1 : 0) << "\n";
    out << "fpsLimit=" << s.fpsLimit << "\n";
    out << "viewRange=" << s.viewRange << "\n";
    out << "showFps=" << (s.showFps ? 1 : 0) << "\n";
    out << "autosaveMinutes=" << s.autosaveMinutes << "\n";
    out << "unitNames=" << (s.showUnitNames ? 1 : 0) << "\n";
    out << "npcNames=" << (s.showNpcNames ? 1 : 0) << "\n";
    out << "propNames=" << (s.showPropNames ? 1 : 0) << "\n";
    out << "propInfo=" << (s.showPropInfo ? 1 : 0) << "\n";
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
    out << "craftGatherSounds=" << (s.craftGatherSounds ? 1 : 0) << "\n";
    for (int i = 0; i < 3; ++i) {
        const std::array<float, 4>& p = s.panels[(size_t)i];
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%.1f,%.1f,%.1f,%.1f", p[0], p[1], p[2], p[3]);
        out << panelSettingKey(i) << "=" << buf << "\n";
    }
    out.close();
    std::error_code ec;
    if (out.fail()) {
        std::filesystem::remove(tmp, ec);
        return false;
    }
    std::filesystem::rename(tmp, std::filesystem::u8path(path), ec);
    return !ec;
}
