#pragma once
#include <string>
#include <vector>
#include "display.h"
#include "settings.h"
#include "ui.h"
#include "language.h"

// The Settings screen: four tabs, each a list of rows. A row is a dropdown, an on/off toggle, a volume level
// with an on/off button, or a line of text. Everything here is pure: the layout, the clicks and the changes
// are worked out from the settings and the display, so they can be tested without a window. Drawing is in
// settings_draw.cpp.

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

// What a row changes
enum class RowId : int {
    Display, Monitor, Resolution, Refresh, VSync, FpsLimit,     // Graphics
    Autosave, UnitNames, NpcNames, PropNames, PropInfo, FontScale, ResetPanels,   // Interface
    Language, Font,                                             // Language
    EdgeScroll, Reference,                                      // Controls: the toggle, then text lines
    GlobalLevel, MusicLevel, AmbientLevel, EffectsLevel, InterfaceLevel   // Audio
};

enum class RowKind { Dropdown, Toggle, Level, Scale, Text, Action };   // Scale: a slider with no on/off button

// One choice in a dropdown. A resolution option carries its size in w and h; the other dropdowns use value.
// A disabled option is shown greyed and cannot be picked.
struct DropOption {
    std::string label;
    int value = 0;
    int w = 0, h = 0;
    bool enabled = true;
};

struct RowSpec {
    RowId id = RowId::Display;
    RowKind kind = RowKind::Dropdown;
    std::string label;                 // the left column
    std::string text;                  // dropdown: the current choice; toggle: On or Off; level: the percentage;
                                       // text row: the right column
    bool enabled = true;
    std::vector<DropOption> options;   // dropdown only
    int current = 0;                   // dropdown: index of the current option; level: 0 to 100
    bool on = true;                    // toggle: its state; level: whether the bus is heard
    bool sub = false;                  // a sub-option of the row above: its label is indented
};

// The Controls tab's description of the game's controls. These match the input code in main.cpp.
struct ControlNote {
    const char* keys;      // language keys, looked up when the tab is drawn
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

// Entries shown at once in an open dropdown. Longer lists scroll, and the list is cut shorter if the window is
// too small to fit it.
inline constexpr int SETTINGS_DROP_ROWS = 10;

// The menu's state between clicks. Rows are named by their index in the current tab's list.
struct SettingsMenuState {
    SettingsTab tab = SettingsTab::Graphics;
    int openRow = -1;      // the dropdown that is open, or -1
    int dropFirst = 0;     // the first option that open dropdown shows (clamped by the layout)
    int dragRow = -1;      // the level row whose slider is being dragged, or -1
};

// Where each part of a row is, in window pixels
struct RowLayout {
    Rect band{ 0, 0, 0, 0 };        // the whole row
    Rect control{ 0, 0, 0, 0 };     // dropdown box, toggle button, or a level's slider zone
    Rect track{ 0, 0, 0, 0 };       // level: the slider track
    Rect value{ 0, 0, 0, 0 };       // level: the percentage
    Rect toggle{ 0, 0, 0, 0 };      // level: the on/off button
    Rect text{ 0, 0, 0, 0 };        // the right column, where a text row's description goes
};

// An open dropdown's list, drawn over the rest of the screen
struct DropLayout {
    bool open = false;
    int row = -1;
    Rect box{ 0, 0, 0, 0 };
    std::vector<Rect> items;        // one per visible option, starting with options[first]
    int first = 0;                  // index of the first option shown
    int count = 0;                  // how many options the dropdown has
};

struct SettingsLayout {
    Rect panel{ 0, 0, 0, 0 };
    std::vector<Rect> tabs;         // SETTINGS_TAB_COUNT rectangles
    std::vector<RowLayout> rows;
    DropLayout drop;
    Rect defaults{ 0, 0, 0, 0 };    // restores every setting
    Rect back{ 0, 0, 0, 0 };
};

enum class ClickKind {
    None, Tab, OpenDropdown, CloseDropdown, PickOption, ToggleRow, LevelToggle, LevelDrag, Defaults, Back, Action
};

// What a click means: the row it hit, and for a tab, an option or a slider, its index. For LevelDrag the index
// is the level under the pointer.
struct SettingsClick {
    ClickKind kind = ClickKind::None;
    int row = -1;
    int index = -1;
};

// Which of the settings a change touched, so the caller knows what to apply and whether to save
struct SettingsChange {
    bool display = false;     // display mode, monitor, resolution or refresh rate
    bool vsync = false;
    bool audio = false;
    bool fonts = false;       // the text size or font, so the fonts are reopened
    bool language = false;    // the language, so its text file is reloaded
    bool any = false;         // anything at all, so the settings are saved
};

// The rows of a tab, for the settings and the display they are shown on
std::vector<RowSpec> settingsRows(SettingsTab tab, const GameSettings& s, const DisplayInfo& d);

// Where everything is drawn, for the window size, the rows and the state
SettingsLayout layoutSettings(int W, int H, const std::vector<RowSpec>& rows, const SettingsMenuState& st);

// What a click at (x, y) means. An open dropdown takes the click first: a click on an enabled option picks it,
// and any other click just closes the dropdown.
SettingsClick hitSettings(const SettingsLayout& L, const std::vector<RowSpec>& rows, float x, float y);

// The level a slider track reads at a window x coordinate, 0 to 100
int levelAtX(const Rect& track, float x);

// Menu state changes
void settingsSelectTab(SettingsMenuState& st, SettingsTab tab);
void settingsOpenDropdown(SettingsMenuState& st, const std::vector<RowSpec>& rows, int row);
void settingsCloseDropdown(SettingsMenuState& st);
// Wheel scrolling of the open dropdown. Positive notches scroll up, toward the first option.
void settingsScrollDropdown(SettingsMenuState& st, const SettingsLayout& L, int notches);

// Setting changes. Each returns what it touched. The caller applies the change with applyDisplay, which also
// normalizes the video values, so these do not need the monitor's modes.
SettingsChange pickOption(RowId id, const DropOption& option, GameSettings& s);
SettingsChange setRowOn(RowId id, bool on, GameSettings& s);
SettingsChange setRowLevel(RowId id, int level, GameSettings& s);
SettingsChange resetAllSettings(GameSettings& s);

// Draws the menu. Call between beginUi and endUi. (mx, my) is the mouse, for hover. settings_draw.cpp.
void drawSettingsMenu(Renderer& r, const SettingsLayout& L, const std::vector<RowSpec>& rows,
                      const SettingsMenuState& st, float mx, float my);
