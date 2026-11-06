#pragma once
#include <string>
#include <vector>
#include "display.h"
#include "settings.h"
#include "ui_types.h"
#include "language.h"

class Renderer;

enum class SettingsTab : int { Graphics = 0, Interface, Language, Controls, Audio };
inline constexpr int SETTINGS_TAB_COUNT = 5;

inline std::string settingsTabName(SettingsTab tab) {
    switch (tab) {
    case SettingsTab::Graphics:  return lang("UILABEL_GRAPHICS");
    case SettingsTab::Interface: return lang("UILABEL_INTERFACE");
    case SettingsTab::Language:  return lang("UILABEL_LANGUAGE_TAB");
    case SettingsTab::Controls:  return lang("UILABEL_CONTROLS");
    case SettingsTab::Audio:     return lang("UILABEL_AUDIO");
    }
    return "";
}

enum class RowId : int {
    Display, Monitor, Resolution, Refresh, VSync, FpsLimit, ViewRange, ShowFps,
    Autosave, UnitNames, NpcNames, PropNames, PropInfo, FontScale, ResetPanels,
    Language, Font,
    EdgeScroll, Reference,
    GlobalLevel, MusicLevel, AmbientLevel, EffectsLevel, InterfaceLevel, CraftGatherSounds
};

enum class RowKind { Dropdown, Toggle, Level, Scale, Text, Action };

struct DropOption {
    std::string label;
    int value = 0;
    int w = 0, h = 0;
    bool enabled = true;
};

struct RowSpec {
    RowId id = RowId::Display;
    RowKind kind = RowKind::Dropdown;
    std::string label;
    std::string text;
    bool enabled = true;
    std::vector<DropOption> options;
    int current = 0;
    bool on = true;
    bool sub = false;
};

struct ControlNote {
    const char* keys;
    const char* does;
};
inline constexpr ControlNote CONTROL_NOTES[] = {
    { "CONTROL_KEYS_1", "CONTROL_DOES_1" },
    { "CONTROL_KEYS_2", "CONTROL_DOES_2" },
    { "CONTROL_KEYS_3", "CONTROL_DOES_3" },
    { "CONTROL_KEYS_4", "CONTROL_DOES_4" },
    { "CONTROL_KEYS_5", "CONTROL_DOES_5" },
    { "CONTROL_KEYS_6", "CONTROL_DOES_6" },
    { "CONTROL_KEYS_7", "CONTROL_DOES_7" },
    { "CONTROL_KEYS_8", "CONTROL_DOES_8" },
    { "CONTROL_KEYS_9", "CONTROL_DOES_9" },
};
inline constexpr int CONTROL_NOTE_COUNT = (int)(sizeof(CONTROL_NOTES) / sizeof(CONTROL_NOTES[0]));

inline constexpr int SETTINGS_DROP_ROWS = 10;

struct SettingsMenuState {
    SettingsTab tab = SettingsTab::Graphics;
    int openRow = -1;
    int dropFirst = 0;
    int dragRow = -1;
};

struct RowLayout {
    Rect band{ 0, 0, 0, 0 };
    Rect control{ 0, 0, 0, 0 };
    Rect track{ 0, 0, 0, 0 };
    Rect value{ 0, 0, 0, 0 };
    Rect toggle{ 0, 0, 0, 0 };
    Rect text{ 0, 0, 0, 0 };
};

struct DropLayout {
    bool open = false;
    int row = -1;
    Rect box{ 0, 0, 0, 0 };
    std::vector<Rect> items;
    int first = 0;
    int count = 0;
};

struct SettingsLayout {
    Rect panel{ 0, 0, 0, 0 };
    std::vector<Rect> tabs;
    std::vector<RowLayout> rows;
    DropLayout drop;
    Rect defaults{ 0, 0, 0, 0 };
    Rect back{ 0, 0, 0, 0 };
};

enum class ClickKind {
    None, Tab, OpenDropdown, CloseDropdown, PickOption, ToggleRow, LevelToggle, LevelDrag, Defaults, Back, Action
};

struct SettingsClick {
    ClickKind kind = ClickKind::None;
    int row = -1;
    int index = -1;
};

struct SettingsChange {
    bool display = false;
    bool vsync = false;
    bool audio = false;
    bool fonts = false;
    bool language = false;
    bool any = false;
};

std::vector<RowSpec> settingsRows(SettingsTab tab, const GameSettings& s, const DisplayInfo& d);

SettingsLayout layoutSettings(int W, int H, const std::vector<RowSpec>& rows, const SettingsMenuState& st);

SettingsClick hitSettings(const SettingsLayout& L, const std::vector<RowSpec>& rows, float x, float y);

int levelAtX(const Rect& track, float x);

void settingsSelectTab(SettingsMenuState& st, SettingsTab tab);
void settingsOpenDropdown(SettingsMenuState& st, const std::vector<RowSpec>& rows, int row);
void settingsCloseDropdown(SettingsMenuState& st);
void settingsScrollDropdown(SettingsMenuState& st, const SettingsLayout& L, int notches);

SettingsChange pickOption(RowId id, const DropOption& option, GameSettings& s);
SettingsChange setRowOn(RowId id, bool on, GameSettings& s);
SettingsChange setRowLevel(RowId id, int level, GameSettings& s);
SettingsChange resetAllSettings(GameSettings& s);

void drawSettingsMenu(Renderer& r, const SettingsLayout& L, const std::vector<RowSpec>& rows,
                      const SettingsMenuState& st, float mx, float my);
