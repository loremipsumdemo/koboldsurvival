#pragma once
#include <string>

// Which screen is showing
enum class Screen { Title, Settings, Playing, Pause, MapSelect, ScenarioSelect, PartySetup };

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
    PartyAddMob,      // opens the list of regular mob kinds; picking one adds a mob of it to the party
    PartyMobBox,      // a mob member's kind; in Custom, opens the list of regular mob kinds to change it
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

// Item pictures (data/item/item_<n>.png) are 64 x 64 pixels and are always drawn at exactly that size, on whole
// pixels, wherever an item shows in a panel: inventories, gear, storage, the trade and a dragged stack. Every cell that
// holds an item is exactly ITEM_CELL_PX across, the picture with a 4 pixel border, whatever the window's or the
// panel's size: a wider panel has more room around its grid, never bigger cells. (On the ground an item is drawn in
// the world, half a unit's height.)
constexpr float ITEM_PICTURE_PX = 64.0f;
constexpr float ITEM_CELL_PX = 72.0f;

struct Button {
    std::string label;
    Rect rect;
    Action action;
    bool enabled = true;
    int param = 0;      // for LoadSlot: index into the save list
};
