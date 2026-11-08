#pragma once
#include <array>
#include <map>
#include <string>
#include <vector>
#include "ecs.h"
#include "ui_types.h"

// Your altar's roster: your units in two columns, Military on the left and Citizen on the right. A unit moved across
// shows as a ghost in its new column, and nothing changes until Apply.
constexpr int ALTAR_MILITARY = 0, ALTAR_CITIZEN = 1;

struct AltarUi {
    bool open = false;
    std::map<ObjectId, int> moves;   // a unit and the column it is moving to
    std::array<int, 2> scroll{};
    ObjectId pinned = NO_OBJECT;     // a unit whose card stays open after a click on its row
    ObjectId menuUnit = NO_OBJECT;   // the unit whose right-click menu is open, over the roster
    float menuX = 0.0f, menuY = 0.0f;
};

// Appease Ghosts: the Altar's bones on the left and the Graveyard on the right, laid out as the roster is
struct GhostsUi {
    bool open = false;
    ObjectId altar = NO_OBJECT;
    std::array<int, 2> scroll{};
};

// Behavior Groups: the list picked (a group's index, or GROUP_REF_MILITARY or GROUP_REF_CITIZEN), and whether the tasks
// that may be added to it are shown in place of its own
struct BehaviorGroupsUi {
    bool open = false;
    ObjectId altar = NO_OBJECT;
    int ref = 0;
    bool adding = false;
    int refScroll = 0, rowScroll = 0;
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
    int military = 0, citizens = 0;
    int softCap = 0, hardCap = 0;
};

struct AltarRow {
    int index = 0;   // into its column's list
    Rect row, portrait, move;
    Rect group;      // a citizen's behavior group: click for the next, right-click for the one before
};

struct AltarLayout {
    Rect box, title, close, totals;
    std::array<Rect, 2> heads{}, lists{};
    std::array<std::vector<AltarRow>, 2> rows;
    std::array<int, 2> first{}, visible{};
    Rect apply, cancel;
};
// A Graveyard row's Appease button
Rect ghostButton(const AltarRow& r, int H);
AltarLayout layoutAltar(int W, int H, const std::array<int, 2>& counts, const std::array<int, 2>& scroll);
int altarMaxScroll(const AltarLayout& L, int column, int count);

// A unit's full card, beside the row it is for and always inside the window
struct UnitCardLayout {
    Rect box, portrait, head, stats, gearHead, items;
    std::vector<Rect> gear, slots;
    float line = 0.0f;
    int statColumns = 3;
};
UnitCardLayout layoutUnitCard(const Rect& beside, int W, int H, int statLines);
