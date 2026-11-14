#pragma once
#include <array>
#include <map>
#include <string>
#include <vector>
#include "ecs.h"
#include "ui_types.h"

// Your altar's roster: your units in two columns, Military on the left and Citizen on the right. A unit moved across
// shows as a ghost in its new column, and nothing changes until Apply.
constexpr int ALTAR_MILITARY = 0, ALTAR_CITIZEN = 1, ALTAR_FORTIFIED = 2;

struct AltarUi {
    bool open = false;
    std::map<ObjectId, int> moves;   // a unit and the column it is moving to
    int faction = 0;                 // whose roster: yours, or (view only) an NPC faction's
    bool readOnly = false;
    std::array<int, 3> scroll{};
    ObjectId pinned = NO_OBJECT;     // unused now: a unit's card shows only while the mouse is over its row
    ObjectId menuUnit = NO_OBJECT;   // the unit whose right-click menu is open, over the roster
    float menuX = 0.0f, menuY = 0.0f;
};

// Manage Squads, from the Faction tab: two columns, each showing a squad picked with its arrows (or Unsorted, a place
// to hold units while shuffling them). A unit moved across shows as a ghost in its new squad, and nothing changes until
// Apply, which waits until nobody is left in Unsorted.
inline constexpr int SQUAD_UNSORTED = -1;

struct SquadsUi {
    bool open = false;
    std::map<ObjectId, int> moves;    // a unit and the squad it is moving to, or SQUAD_UNSORTED
    std::array<int, 2> view{ 0, 1 };  // the squad each column shows, or SQUAD_UNSORTED
    std::array<int, 3> scroll{};
    ObjectId pinned = NO_OBJECT;      // a unit whose card stays open after a click on its row
};

// Appease Ghosts: the Altar's bones on the left and the Graveyard on the right, laid out as the roster is
struct GhostsUi {
    bool open = false;
    ObjectId altar = NO_OBJECT;
    std::array<int, 3> scroll{};
};

// Set Perimeter, from an Altar's menu: a small window of tools, over the world where the tiles are painted
struct PerimeterLayout {
    Rect panel, title, draw, erase, reset, confirm, cancel;
};
// x, y: where the window is (-1: the top middle until it is moved)
PerimeterLayout layoutPerimeter(int W, int H, float x, float y);

// The "You have no units selected." window, shown while no unit of yours is in a squad: a title bar to drag it by, a line of text and the
// three ways back to something of yours. x, y: where the window is (-1: the top middle until it is moved)
struct NoUnitsLayout {
    Rect panel, title, message, altar, unit, claim;
};
NoUnitsLayout layoutNoUnits(int W, int H, float x, float y);

// A general store's Allowed Items window
struct StoreFilterUi {
    bool open = false;
    ObjectId prop = NO_OBJECT;
};

// A Stall's Stock window: the store picked on the left, how the list is sorted (0 by kind, 1 fullest, 2 fullest for
// its size), and how far each side is scrolled
struct StallUi {
    bool open = false;
    ObjectId stall = NO_OBJECT;
    ObjectId store = NO_OBJECT;
    int sort = 0;
    int listScroll = 0;
    int itemScroll = 0;
};

// Behavior Groups: the list picked (a group's index, or GROUP_REF_MILITARY or GROUP_REF_CITIZEN), and whether the tasks
// that may be added to it are shown in place of its own
struct BehaviorGroupsUi {
    bool open = false;
    ObjectId altar = NO_OBJECT;
    int ref = 0;
    bool adding = false;
    int refScroll = 0, rowScroll = 0;
    int tab = 0;          // 0 the task lists, 1 the Schedule
    int paint = 1;        // the Schedule's brush: an Activity
    int schedScroll = 0;  // the Schedule's first row shown
    int faction = 0;             // whose lists and schedule: yours, or (view only) an NPC faction's
    bool readOnly = false;
    bool scheduleOnly = false;   // the Schedule window (its own, from the Altar's menu), with no task lists
    int perimPaint = 1;   // what a drag over the Perimeter Closed row sets its hours to: 1 closed, 0 open
};

// Renaming one of your units, from the Squad Panel or the roster
struct RenamePrompt {
    bool open = false;
    ObjectId unit = NO_OBJECT;
    std::string text;
};

struct RenameLayout {
    Rect box, title, field, ok, cancel;
};
RenameLayout layoutRename(int W, int H);

// How many units each side will have once the moves are applied, and the citizen caps from Settings
struct AltarTotals {
    int military = 0, citizens = 0, fortified = 0;   // citizens counts the Fortified too
    int softCap = 0, hardCap = 0;
};

struct AltarRow {
    int index = 0;   // into its column's list
    Rect row, portrait, move;
    Rect group;      // a citizen's behavior group: click for the next, right-click for the one before
    Rect back;       // a citizen's other arrow, left of its group: back to the military (only in the Altar's three columns)
};

struct AltarLayout {
    Rect box, title, close, totals;
    std::array<Rect, 3> heads{}, lists{};
    std::array<std::vector<AltarRow>, 3> rows;
    std::array<int, 3> first{}, visible{};
    Rect apply, cancel;
};
// A Graveyard row's Appease button
Rect ghostButton(const AltarRow& r, int H);
// columns: 2 (the Ghosts window and Manage Squads), or 3 (the Altar's roster: Military, Citizens and Fortified)
AltarLayout layoutAltar(int W, int H, const std::array<int, 3>& counts, const std::array<int, 3>& scroll, int columns = 2);
int altarMaxScroll(const AltarLayout& L, int column, int count);

// Manage Squads: the roster's layout, with arrows at the right of each column's head that pick the squad it shows
struct SquadsLayout {
    AltarLayout roster;
    std::array<Rect, 2> prev{}, next{};
};
SquadsLayout layoutSquads(int W, int H, const std::array<int, 3>& counts, const std::array<int, 3>& scroll);

// A unit's full card, beside the row it is for and always inside the window
struct UnitCardLayout {
    Rect box, portrait, head, stats, gearHead, items;
    std::vector<Rect> gear, slots;
    float line = 0.0f;
    int statColumns = 3;
};
UnitCardLayout layoutUnitCard(const Rect& beside, int W, int H, int statLines);
