#pragma once
#include <string>

// Build version shown on the title screen
inline constexpr const char* GAME_VERSION = "0.1.0";

// Which screen is showing
enum class Screen { Title, Settings, Playing, Pause, MapSelect, MapEditor, DevTools };

// Tools in the map editor's left panel, in button order

// What a button does when clicked
enum class Action {
    Continue, NewGame, LoadGame, Settings, Quit,
    Resume, Save, Load, LoadSlot, ReturnToTitle, Back,
    CycleDisplay, CycleResolution, ToggleEdge, CycleAutosave,
    ConfirmName,      // Save button in the save/load popout
    CancelName,       // Close button in the save/load popout
    OverwriteYes, OverwriteNo,
    LoadYes, LoadNo,     // "unsaved progress will be lost" question before loading
    SortName, SortTime,  // column headers in the save popout
    ScrollUp, ScrollDown, // the "..." rows at the ends of the save list
    DeleteSave,           // Delete button on a save row, which asks to confirm
    DeleteHold,           // Delete button on the confirm dialog, which is held down to confirm
    DeleteNo,             // Cancel on the confirm dialog
    OpenMapEditor,        // Map Editor button on the title screen
    PickMap,              // a map in the New Game list; param is its index
    DevOpen,              // Dev Tools button in the Esc menu
    DevTime,              // moves the in-game clock; param is minutes
    DevClock,             // starts or stops the clock during play
    DevStars,             // shows or hides the stars
    DevMoon,              // shows or hides the moon
    DevSunBright,         // param is -1 or +1
    DevStarBright,        // param is -1 or +1
    DevColorTarget,       // param is -1 or +1, picks which colour the R/G/B buttons edit
    DevColor,             // param is channel * 2, plus 1 for the plus button
    DevReset,             // restores the default atmosphere
    MapSortName, MapSortTime,   // column headers in the New Game list
    MapScrollUp, MapScrollDown  // the "..." rows at the ends of the New Game list
};

// Pixel rectangle, origin at the top-left of the window
struct Rect {
    float x, y, w, h;
    bool contains(float px, float py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
};

struct Button {
    std::string label;
    Rect rect;
    Action action;
    bool enabled = true;
    int param = 0;      // for LoadSlot: index into the save list
};
