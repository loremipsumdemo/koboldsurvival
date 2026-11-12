#pragma once
#include "formation.h"
#include <SDL3/SDL.h>
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
// The percent of the window's width and height the world is drawn at
inline constexpr int RENDER_SCALE_CHOICES[] = { 50, 67, 75, 100 };
inline constexpr int RENDER_SCALE_CHOICE_COUNT = 4;

inline constexpr int AUTOSAVE_CHOICES[] = { 0, 1, 2, 5, 10, 15, 30 };
inline constexpr int AUTOSAVE_COUNT = 7;

// The most citizens your faction can have: past the soft cap you are warned, and nothing moves a unit into the citizens
// past the hard cap
inline constexpr int CITIZEN_SOFT_CHOICES[] = { 50, 100, 200, 300, 400, 500, 750, 1000 };
inline constexpr int CITIZEN_SOFT_COUNT = 8;
inline constexpr int CITIZEN_HARD_CHOICES[] = { 64, 128, 256, 384, 512, 768, 1024 };
inline constexpr int CITIZEN_HARD_COUNT = 7;

inline constexpr int UI_SCALE_MIN = 50;
inline constexpr int UI_SCALE_MAX = 200;
inline constexpr int UI_SCALE_STEP = 5;

// The detail distance: how far from the view's centre props, units and the ground's and water's fine detail are drawn
inline constexpr int DETAIL_RANGE_MIN = 16;
inline constexpr int DETAIL_RANGE_MAX = 256;
inline constexpr int DETAIL_RANGE_STEP = 8;
// The view distance: how far from the view's centre the ground's terrain chunks are drawn at all
inline constexpr int VIEW_DISTANCE_MIN = 64;
inline constexpr int VIEW_DISTANCE_MAX = 1024;
inline constexpr int VIEW_DISTANCE_STEP = 32;

struct GameSettings {
    int displayMode = DISPLAY_BORDERLESS;
    int monitor = 0;
    int width = 1280;
    int height = 720;
    int refreshHz = 0;
    bool vsync = true;
    int fpsLimit = 60;
    int detailRange = 256;
    int viewDistance = 1024;
    int renderScale = 100;
    bool renderScaleSet = false;   // the settings file had a render scale (not saved): without one, a weak graphics card is given a lower default
    int formationShape = (int)FormationShape::Rectangle;
    bool formationLoose = false;
    bool showFps = false;
    bool debugLogging = false;    // writes what the game is doing to logs.txt in the user files folder
    bool clouds = true;
    bool cloudShadows = true;
    bool variedSurfaces = true;   // ground and water tiles laid with a random turn and shift each, so they don't repeat

    int autosaveMinutes = 5;
    int citizenSoftCap = 500;
    int citizenHardCap = 512;
    bool showUnitNames = true;
    bool showNpcNames = true;
    int fontScale = 100;
    std::string fontPath;
    std::string language = LANGUAGE_DEFAULT_ID;

    bool edgeScroll = false;
    bool conversationPause = false;

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

// The soft cap never above the hard cap
inline int citizenSoftLimit(const GameSettings& s) { return std::min(s.citizenSoftCap, s.citizenHardCap); }

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
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const std::string val = line.substr(eq + 1);
        if (key == "displayMode")          s.displayMode = parseInt(val, s.displayMode);
        else if (key == "monitor")         s.monitor = parseInt(val, s.monitor);
        else if (key == "width")           s.width = parseInt(val, s.width);
        else if (key == "height")          s.height = parseInt(val, s.height);
        else if (key == "refreshHz")       s.refreshHz = parseInt(val, s.refreshHz);
        else if (key == "vsync")           s.vsync = (val == "1");
        else if (key == "fpsLimit")        s.fpsLimit = parseInt(val, s.fpsLimit);
        else if (key == "viewRange")       s.detailRange = parseInt(val, s.detailRange);   // the older name of the detail distance
        else if (key == "detailRange")     s.detailRange = parseInt(val, s.detailRange);
        else if (key == "viewDistance")    s.viewDistance = parseInt(val, s.viewDistance);
        else if (key == "formationShape")  s.formationShape = parseInt(val, s.formationShape);
        else if (key == "formationLoose")  s.formationLoose = (val == "1");
        else if (key == "showFps")         s.showFps = (val == "1");
        else if (key == "renderScale")     { s.renderScale = parseInt(val, s.renderScale); s.renderScaleSet = true; }
        else if (key == "debugLogging")    s.debugLogging = (val == "1");
        else if (key == "clouds")          s.clouds = (val == "1");
        else if (key == "cloudShadows")    s.cloudShadows = (val == "1");
        else if (key == "variedSurfaces")  s.variedSurfaces = (val == "1");
        else if (key == "autosaveMinutes") s.autosaveMinutes = parseInt(val, s.autosaveMinutes);
        else if (key == "citizenSoftCap")  s.citizenSoftCap = parseInt(val, s.citizenSoftCap);
        else if (key == "citizenHardCap")  s.citizenHardCap = parseInt(val, s.citizenHardCap);
        else if (key == "unitNames")       s.showUnitNames = (val == "1");
        else if (key == "npcNames")        s.showNpcNames = (val == "1");
        else if (key == "fontScale")       s.fontScale = parseInt(val, s.fontScale);
        else if (key == "fontPath")        s.fontPath = val;
        else if (key == "language")        s.language = val;
        else if (key == "edgeScroll")      s.edgeScroll = (val == "1");
        else if (key == "conversationPause") s.conversationPause = (val == "1");
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
        else if (key == "globalVolume")    s.globalVolume = parseInt(val, s.globalVolume);
        else if (key == "globalOn")        s.globalOn = (val == "1");
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

    s.displayMode = std::clamp(s.displayMode, 0, DISPLAY_MODE_COUNT - 1);
    s.monitor = std::clamp(s.monitor, 0, 15);
    s.width = std::clamp(s.width, 640, 15360);
    s.height = std::clamp(s.height, 480, 8640);
    s.refreshHz = std::clamp(s.refreshHz, 0, 1000);
    if (!isChoice(s.fpsLimit, FPS_CHOICES, FPS_CHOICE_COUNT)) s.fpsLimit = GameSettings{}.fpsLimit;
    if (!isChoice(s.renderScale, RENDER_SCALE_CHOICES, RENDER_SCALE_CHOICE_COUNT)) s.renderScale = GameSettings{}.renderScale;
    s.formationShape = std::clamp(s.formationShape, 0, (int)FormationShape::Count - 1);
    s.detailRange = std::clamp(s.detailRange, DETAIL_RANGE_MIN, DETAIL_RANGE_MAX);
    s.detailRange = DETAIL_RANGE_MIN + DETAIL_RANGE_STEP * ((s.detailRange - DETAIL_RANGE_MIN + DETAIL_RANGE_STEP / 2) / DETAIL_RANGE_STEP);
    s.viewDistance = std::clamp(s.viewDistance, VIEW_DISTANCE_MIN, VIEW_DISTANCE_MAX);
    s.viewDistance = VIEW_DISTANCE_MIN + VIEW_DISTANCE_STEP * ((s.viewDistance - VIEW_DISTANCE_MIN + VIEW_DISTANCE_STEP / 2) / VIEW_DISTANCE_STEP);
    if (!isChoice(s.autosaveMinutes, AUTOSAVE_CHOICES, AUTOSAVE_COUNT)) s.autosaveMinutes = 5;
    if (!isChoice(s.citizenSoftCap, CITIZEN_SOFT_CHOICES, CITIZEN_SOFT_COUNT)) s.citizenSoftCap = GameSettings{}.citizenSoftCap;
    if (!isChoice(s.citizenHardCap, CITIZEN_HARD_CHOICES, CITIZEN_HARD_COUNT)) s.citizenHardCap = GameSettings{}.citizenHardCap;
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
    out << "detailRange=" << s.detailRange << "\n";
    out << "viewDistance=" << s.viewDistance << "\n";
    out << "formationShape=" << s.formationShape << "\n";
    out << "formationLoose=" << (s.formationLoose ? 1 : 0) << "\n";
    out << "showFps=" << (s.showFps ? 1 : 0) << "\n";
    out << "renderScale=" << s.renderScale << "\n";
    out << "debugLogging=" << (s.debugLogging ? 1 : 0) << "\n";
    out << "clouds=" << (s.clouds ? 1 : 0) << "\n";
    out << "cloudShadows=" << (s.cloudShadows ? 1 : 0) << "\n";
    out << "variedSurfaces=" << (s.variedSurfaces ? 1 : 0) << "\n";
    out << "autosaveMinutes=" << s.autosaveMinutes << "\n";
    out << "unitNames=" << (s.showUnitNames ? 1 : 0) << "\n";
    out << "npcNames=" << (s.showNpcNames ? 1 : 0) << "\n";
    out << "fontScale=" << s.fontScale << "\n";
    out << "fontPath=" << s.fontPath << "\n";
    out << "language=" << s.language << "\n";
    out << "edgeScroll=" << (s.edgeScroll ? 1 : 0) << "\n";
    out << "conversationPause=" << (s.conversationPause ? 1 : 0) << "\n";
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
    // Antivirus and cloud sync can hold the target open for a moment
    const std::filesystem::path target = std::filesystem::u8path(path);
    for (int attempt = 0; attempt < 10; ++attempt) {
        std::filesystem::rename(tmp, target, ec);
        if (!ec) return true;
        SDL_Delay(50);
    }
    std::filesystem::remove(tmp, ec);
    return false;
}
