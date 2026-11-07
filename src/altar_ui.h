#pragma once
#include <array>
#include <map>
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
};

// How many units each side will have once the moves are applied, and the citizen caps from Settings
struct AltarTotals {
    int military = 0, citizens = 0;
    int softCap = 0, hardCap = 0;
};

struct AltarRow {
    int index = 0;   // into its column's list
    Rect row, portrait, move;
};

struct AltarLayout {
    Rect box, title, close, totals;
    std::array<Rect, 2> heads{}, lists{};
    std::array<std::vector<AltarRow>, 2> rows;
    std::array<int, 2> first{}, visible{};
    Rect apply, cancel;
};
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
