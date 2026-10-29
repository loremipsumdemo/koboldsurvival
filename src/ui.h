#pragma once
#include <string>

// Which screen is showing
enum class Screen { Title, Settings, Playing, Pause, MapSelect, PartySetup };

// What a button does when clicked
enum class Action {
    Continue, NewGame, LoadGame, Settings, Language, Quit,
    Resume, Save, Load, LoadSlot, ReturnToTitle, Back,
    ConfirmName,      // Save button in the save/load popout
    CancelName,       // Close button in the save/load popout
    OverwriteYes, OverwriteNo,
    LoadYes, LoadNo,     // "unsaved progress will be lost" question before loading
    SortName, SortTime,  // column headers in the save popout
    ScrollUp, ScrollDown, // the "..." rows at the ends of the save list
    DeleteSave,           // Delete button on a save row, which asks to confirm
    DeleteHold,           // Delete button on the confirm dialog, which is held down to confirm
    DeleteNo,             // Cancel on the confirm dialog
    PickMap,              // a map in the New Game list; param is its index
    MapSortName, MapSortTime,   // column headers in the New Game list
    MapScrollUp, MapScrollDown,  // the "..." rows at the ends of the New Game list
    // The starting party menu, after a map is picked in New Game
    PartyPick,        // a unit in the left column; param is its index
    PartyTabPick,     // a tab on the right; param is the tab
    PartyNameField,   // the name box, which takes typed text while it is being edited
    PartySpriteBox,   // opens the sprite dropdown
    PartyBodyBox,     // opens the body type dropdown
    PartyRandomUnit,  // gives the picked unit a new random member
    PartyAddUnit,     // adds a random unit to the end of the party
    PartyRemoveUnit,  // removes the picked unit, down to an empty party (Start needs one)
    PartyGearSlot,    // opens the item dropdown for a gear slot; param is the slot
    PartyStatLabel,   // a stat's name and level, drawn only; param is the stat
    PartyStep,        // a stat's minus or plus; param is stat * 2 for minus, stat * 2 + 1 for plus
    PartyHeading,     // a group heading, drawn only
    PartyNote,        // a line of text, drawn only
    PartyInvSlot,     // an inventory slot in the Items tab; param is the slot
    PartyCount,       // the count of the picked stack; param is -1 or +1
    PartyDropPick,    // an entry in the open dropdown; param is its index
    PartyDropScroll,  // the scroll rows of the open dropdown; param is -1 or +1
    PartyDropBack,    // the background of the open dropdown, which takes clicks and does nothing
    PartyRandomAll,   // a new random party
    PartyConfirm,     // starts the level with the party
    PartyMessage      // the line under the menu, drawn only
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
