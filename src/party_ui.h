#pragma once
#include <string>
#include <vector>
#include "party.h"
#include "ui.h"

// The starting party menu, shown after a map is picked in New Game and before the level starts.
// Left: the three units. Middle: the picked unit's picture and totals. Right: tabs for its name and
// sprite, its stats, its gear and its starting items. Bottom: Randomize all, Back and Confirm.
// The layout is pure, so the drawing and the mouse handling use the same rectangles.

enum class PartyTab : int { Identity = 0, Stats, Gear, Items };
inline constexpr int PARTY_TAB_COUNT = 4;

// Which dropdown list is open, and what picking an entry sets
enum class DropKind { None, Sprite, Body, Gear, Item };

// What the menu shows and what it is editing. The party itself is kept by the caller.
struct PartyUi {
    int selected = 0;                      // the unit picked in the left column
    PartyTab tab = PartyTab::Identity;
    bool editingName = false;              // typed text goes into the picked unit's name
    std::string composition;               // text an input method is composing, shown until it is committed
    int stackSlot = 0;                     // the inventory slot picked in the Items tab
    DropKind dropKind = DropKind::None;    // the open dropdown, if any
    int dropSlot = 0;                      // the gear slot or inventory slot it fills
    int dropScroll = 0;                    // the first entry it shows
    int listScroll = 0;                    // the first unit shown in the left column
    bool listDrag = false;                 // the left column's scroll bar thumb is held
    float listGrab = 0.0f;                 // where on the thumb it was taken, from its top
    bool resizable = false;                // Add Unit and Remove Unit are offered (the Custom scenario only)
    bool editable = false;                 // stats, gear and items can be changed (the Custom scenario only)
};

// One entry in an open dropdown: its text, and the value it sets (a sprite number or an item id,
// where 0 means Empty)
struct DropEntry {
    std::string label;
    int value = 0;
};

// Entries shown at once in a dropdown. Longer lists scroll.
inline constexpr int DROP_VISIBLE_ROWS = 10;

// The unit the menu is editing
const PartyMember& pickedMember(const Party& p, const PartyUi& ui);
PartyMember& pickedMember(Party& p, const PartyUi& ui);

// Every entry of the open dropdown, or none if it is closed
std::vector<DropEntry> dropdownEntries(const Party& p, const PartyUi& ui);

// A scroll position kept within a dropdown of count entries
int clampDropScroll(int scroll, int count);

// Where the parts of the menu sit, in pixels
struct PartyPanels {
    float topH = 0.0f;           // the title strip above the panels
    Rect left{ 0.0f, 0.0f, 0.0f, 0.0f };
    Rect center{ 0.0f, 0.0f, 0.0f, 0.0f };
    Rect right{ 0.0f, 0.0f, 0.0f, 0.0f };
    Rect bottom{ 0.0f, 0.0f, 0.0f, 0.0f };
};
PartyPanels partyPanels(int W, int H);

// The room kept at the foot of the left column for its hints, in pixels at 720 high
inline constexpr float PARTY_HINT_H = 130.0f;

// How many units the left column shows at once, and the most it can scroll by
int partyListVisible(int H);
int partyListMaxScroll(int count, int H);
// The left column's scroll bar, down its right edge, when it has more units than fit; both rects empty otherwise
struct PartyListBar {
    Rect track{ 0.0f, 0.0f, 0.0f, 0.0f };
    Rect thumb{ 0.0f, 0.0f, 0.0f, 0.0f };
};
PartyListBar partyListBar(int W, int H, int count, int scroll);
// The scroll that puts the thumb's top at thumbTop (the pointer's height less where the thumb was taken), in range
int partyListScrollAt(int W, int H, int count, float thumbTop);

// Every part of the menu that can be clicked, or that is drawn as text, in draw order. Later
// buttons draw over earlier ones and take a click first, so an open dropdown comes last.
// drop must be dropdownEntries(p, ui).
std::vector<Button> layoutPartySetup(int W, int H, const Party& p, const PartyUi& ui,
                                     const std::vector<DropEntry>& drop);

// The line under the menu: why Confirm is off, or what Confirm does
std::string partyMessage(const Party& p);
