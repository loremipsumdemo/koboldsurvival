#pragma once
// The title and pause menus, the save/load popout and the New Game list: their layouts, sorting, and music
#include <cstddef>
#include <optional>
#include <string>
#include <vector>
#include "level.h"
#include "savegame.h"
#include "ui_types.h"

constexpr size_t MIN_SAVE_ROWS = 5;          // the save list shows between these many rows,
constexpr size_t MAX_SAVE_ROWS = 10;         // depending on how tall the window is

// Save list columns, as fractions of the list width. The name column starts at the left.
constexpr float COL_TYPE = 0.54f;
constexpr float COL_TIME = 0.70f;

// What the save list is sorted by. Autosaves always sit above manual saves, whatever the sort.
enum class SortKey { Name, Time };

// Which question the popout is asking, if any
enum class Ask { None, Overwrite, LoadUnsaved, Delete };

// The name with leading and trailing spaces removed
std::string trimmedName(const std::string& s);
// Removes the last character. Names can hold UTF-8, so skip its continuation bytes too.
void popUtf8Char(std::string& s);
// The save with exactly this name, if there is one
std::optional<SaveSummary> findSaveByName(const std::vector<SaveSummary>& saves, const std::string& name);
// Sorts the save list. Autosaves always come first, then manual saves. Each group is sorted
// by the chosen column; ties fall back to the other column.
void sortSaves(std::vector<SaveSummary>& saves, SortKey key, bool ascending);
// What a map is, as the list's Type column shows it
std::string mapTypeLabel(const MapChoice& m);
// Sorts the New Game list. The premade map always comes first. The rest sort by name, or by the date
// they were saved, newest first unless ascending.
void sortMaps(std::vector<MapChoice>& maps, bool byName, bool ascending);
// The map a save was made on. Saves made before the premade maps were cut down to text_map name map_01.
std::optional<MapChoice> findMap(const std::vector<MapChoice>& maps, const std::string& source, std::string stem);

// How many save rows fit the window height. The rows take whatever space is left under the fixed
// parts of the popout, kept between MIN_SAVE_ROWS and MAX_SAVE_ROWS.
size_t visibleSaveRows(int H);

// The save/load popout, in pixels. It is drawn over the screen underneath and leaves it in place.
struct PanelGeometry {
    Rect box;
    float pad;
    Rect nameBox;     // the save name field
    Rect saveBtn;     // Save button, beside the name field
    float headerY;    // column headers
    float headerH;
    Rect aboveBand;   // "..." row for saves hidden above, between the headers and the first row
    float listTop;    // first save row
    Rect belowBand;   // "..." row for saves hidden below, between the last row and Close
    float closeY;     // Close button
    float btnH;       // height of the name field, Save, and Close
    float rowH;       // height of one save row
    float rowPitch;   // distance from one save row to the next
};
PanelGeometry panelGeometry(int W, int H);

// The question shown in the middle of the popout
struct DialogGeometry {
    Rect box;
    Rect yes;
    Rect no;
};
DialogGeometry questionGeometry(int W, int H);

// Buttons for the popout, in pixels. Drawing and clicking both use this, so they always match.
// While a question is up, only its Yes and No buttons are returned.
// Save rows carry their index in the list as param. Their text is drawn in columns by the renderer code.
std::vector<Button> layoutPanel(int W, int H, bool saveMode, Ask ask,
                                const std::vector<SaveSummary>& saves, size_t listOffset, bool fromTitle);
// Buttons for the New Game list, in the same popout as the load list: the column headers, one row
// per visible map, the "..." rows at the ends, and Close.
std::vector<Button> layoutMapPanel(int W, int H, const std::vector<MapChoice>& maps, size_t listOffset);
// Buttons for the main menus, in pixels
std::vector<Button> layoutButtons(Screen screen, int W, int H, const std::vector<SaveSummary>& saves);

// The tracks a screen plays, as file names in the BGM folder. A null track keeps whatever is playing on its bus,
// so the menus opened from the title (New Game, Load Game, Settings) do not restart it. "" is silence. Both
// tracks are matched to the current screen once per frame.
struct Soundtrack {
    const char* music = nullptr;     // the music bus
    const char* ambient = nullptr;   // the ambient bus
};
Soundtrack soundtrackFor(Screen screen);

// The game's name is in the language files, in two words: GAME_TITLE_FIRST (shown green on the title screen) and
// GAME_TITLE_SECOND. gameTitle() is the two together, for the window's title bar.
std::string gameTitle();
// The title screen's notice board: the lines of data/motd.txt (the Message of the Day) in the pak, read once. Edit that
// file for update notes. Missing, the board is empty.
const std::vector<std::string>& messageOfTheDay();
