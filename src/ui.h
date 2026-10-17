#pragma once
#include <string>

// Build version shown on the title screen
inline constexpr const char* GAME_VERSION = "0.1.0";

// Which screen is showing
enum class Screen { Title, Settings, Playing, Pause };

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
    DeleteNo              // Cancel on the confirm dialog
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
