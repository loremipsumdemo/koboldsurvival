#pragma once
#include <algorithm>
#include <vector>
#include "components.h"
#include "ui.h"

// Layout for the in-game panels. Pure functions of the window size and the state,
// so the drawing code and the mouse handling use the same rectangles.

constexpr int UNITS_PER_ROW = 8;

// One visible unit in the bottom panel: its index in the player's unit list, and its rectangle
struct UnitCell {
    int index = 0;
    Rect rect{ 0.0f, 0.0f, 0.0f, 0.0f };
};

struct UnitPanelLayout {
    Rect panel{ 0.0f, 0.0f, 0.0f, 0.0f };
    Rect track{ 0.0f, 0.0f, 0.0f, 0.0f };   // scroll bar track
    Rect thumb{ 0.0f, 0.0f, 0.0f, 0.0f };   // scroll bar thumb
    std::vector<UnitCell> cells;            // visible units only
    int rowCount = 0;                       // total rows of 8 units
    int rowsVisible = 0;
    int scrollRow = 0;                      // first visible row, clamped to the list

    bool scrollable() const { return rowCount > rowsVisible; }
};

// Each cell: the portrait square, then a thin health bar, then the name. The cell's rectangle
// covers all three; the bar and name sit in the strip under the portrait.
inline float unitCellLabelHeight(float s) { return 34.0f * s; }

// Bottom panel: a 8-per-row grid of portraits with names, scrolled vertically.
inline UnitPanelLayout layoutUnitPanel(int W, int H, int unitCount, int scrollRow) {
    const float s = H / 720.0f;
    const float cell = 64.0f * s, gap = 6.0f * s, pad = 10.0f * s;
    const float label = unitCellLabelHeight(s), scrollW = 14.0f * s;
    const int rowsVisible = 2;

    UnitPanelLayout L;
    L.rowsVisible = rowsVisible;
    L.rowCount = (unitCount + UNITS_PER_ROW - 1) / UNITS_PER_ROW;
    const int maxScroll = std::max(0, L.rowCount - rowsVisible);
    L.scrollRow = std::clamp(scrollRow, 0, maxScroll);

    const float cellsW = UNITS_PER_ROW * cell + (UNITS_PER_ROW - 1) * gap;
    const float panelW = pad + cellsW + gap + scrollW + pad;
    const float panelH = pad + rowsVisible * (cell + label) + (rowsVisible - 1) * gap + pad;
    L.panel = Rect{ (W - panelW) * 0.5f, H - panelH - 8.0f * s, panelW, panelH };

    for (int r = 0; r < rowsVisible; ++r) {
        for (int c = 0; c < UNITS_PER_ROW; ++c) {
            const int idx = (L.scrollRow + r) * UNITS_PER_ROW + c;
            if (idx >= unitCount) break;
            const float x = L.panel.x + pad + c * (cell + gap);
            const float y = L.panel.y + pad + r * (cell + label + gap);
            L.cells.push_back(UnitCell{ idx, Rect{ x, y, cell, cell + label } });
        }
    }

    L.track = Rect{ L.panel.x + panelW - pad - scrollW, L.panel.y + pad, scrollW, panelH - 2.0f * pad };
    if (!L.scrollable()) {
        L.thumb = L.track;
    } else {
        const float th = L.track.h * rowsVisible / L.rowCount;
        const float ty = L.track.y + (L.track.h - th) * L.scrollRow / maxScroll;
        L.thumb = Rect{ L.track.x, ty, L.track.w, th };
    }
    return L;
}

// Right sidebar for the one selected unit. From the top: a profile card with the unit's picture,
// name, stats and carry bar, then the gear, then the inventory grid of 8 columns of slots. The
// whole sidebar sits on a see-through panel, and each section has a background of its own.
struct SidebarLayout {
    Rect panel{ 0.0f, 0.0f, 0.0f, 0.0f };      // the whole sidebar, drawn see-through
    Rect profile{ 0.0f, 0.0f, 0.0f, 0.0f };    // profile card at the top
    Rect portrait{ 0.0f, 0.0f, 0.0f, 0.0f };   // the unit's picture, inside the card on the left
    Rect statsButton{ 0.0f, 0.0f, 0.0f, 0.0f };// opens the stats popout, top right of the card
    Rect gearPanel{ 0.0f, 0.0f, 0.0f, 0.0f };  // background behind the gear slots and their totals
    std::vector<Rect> gear;                    // GEAR_SLOTS rectangles in GearSlot order, four per row
    Rect gridPanel{ 0.0f, 0.0f, 0.0f, 0.0f };  // background behind the inventory grid
    std::vector<Rect> slots;                   // INVENTORY_SLOTS rectangles, row by row
};

inline SidebarLayout layoutInventorySidebar(int W, int H) {
    const float s = H / 720.0f;
    const float width = 380.0f * s, pad = 14.0f * s, gap = 6.0f * s;
    const float cardH = 200.0f * s, cardInset = 8.0f * s, headH = 30.0f * s, innerPad = 10.0f * s;
    const float sectionGap = 12.0f * s;
    const float innerW = width - 2.0f * pad - 2.0f * innerPad;

    SidebarLayout L;
    L.panel = Rect{ W - width, 0.0f, width, (float)H };

    L.profile = Rect{ L.panel.x + pad, pad, width - 2.0f * pad, cardH };
    const float side = cardH - 2.0f * cardInset;
    L.portrait = Rect{ L.profile.x + cardInset, L.profile.y + cardInset, side, side };
    const float btnW = 66.0f * s, btnH = 24.0f * s;
    L.statsButton = Rect{ L.profile.x + L.profile.w - cardInset - btnW, L.profile.y + cardInset, btnW, btnH };

    // Gear: four slots to a row, two rows, then a line of attack and defence totals
    const int gearCols = 4, gearRows = GEAR_SLOTS / 4;
    const float gearH = 44.0f * s, totalsH = 26.0f * s;
    const float gearTop = L.profile.y + cardH + sectionGap;
    const float gearW = (innerW - (gearCols - 1) * gap) / gearCols;
    L.gearPanel = Rect{ L.panel.x + pad, gearTop, width - 2.0f * pad,
                        headH + gearRows * gearH + gearRows * gap + totalsH + innerPad };
    for (int i = 0; i < GEAR_SLOTS; ++i) {
        const int c = i % gearCols, r = i / gearCols;
        L.gear.push_back(Rect{ L.gearPanel.x + innerPad + c * (gearW + gap),
                               gearTop + headH + r * (gearH + gap),
                               gearW, gearH });
    }

    // Grid: columns fill the width inside the grid background, rows follow
    const float rows = (float)(INVENTORY_SLOTS / INVENTORY_COLUMNS);
    const float gridTop = L.gearPanel.y + L.gearPanel.h + sectionGap;
    const float slot = (width - 2.0f * pad - 2.0f * innerPad - (INVENTORY_COLUMNS - 1) * gap) / INVENTORY_COLUMNS;
    L.gridPanel = Rect{ L.panel.x + pad, gridTop, width - 2.0f * pad,
                        headH + rows * slot + (rows - 1.0f) * gap + innerPad };
    for (int i = 0; i < INVENTORY_SLOTS; ++i) {
        const int c = i % INVENTORY_COLUMNS, r = i / INVENTORY_COLUMNS;
        L.slots.push_back(Rect{ L.gridPanel.x + innerPad + c * (slot + gap),
                                gridTop + headH + r * (slot + gap),
                                slot, slot });
    }
    return L;
}

// Whether a point is over the in-game panels: the unit list along the bottom, or the inventory
// sidebar when it is showing. Clicks and wheel turns there belong to the panels, never to the
// ground behind them.
inline bool overGamePanels(const UnitPanelLayout& unitPanel, const SidebarLayout& sidebar,
                           bool sidebarShown, float x, float y) {
    return unitPanel.panel.contains(x, y) || (sidebarShown && sidebar.panel.contains(x, y));
}

// Index of the gear slot under a point (in GearSlot order), or -1 if it is over no gear slot
inline int gearSlotAt(const SidebarLayout& L, float x, float y) {
    for (size_t i = 0; i < L.gear.size(); ++i) {
        if (L.gear[i].contains(x, y)) return (int)i;
    }
    return -1;
}

// Index of the inventory slot under a point, or -1 if it is over no slot
inline int sidebarSlotAt(const SidebarLayout& L, float x, float y) {
    for (size_t i = 0; i < L.slots.size(); ++i) {
        if (L.slots[i].contains(x, y)) return (int)i;
    }
    return -1;
}

// The stats popout, centred on the window. Two columns of stats, each row showing the level, the
// total XP and the XP still needed for the next level. rowH is the height of one line of text and
// titleH the height of the title, both from the fonts, so the box fits the text.
struct StatsPanelLayout {
    Rect box{ 0.0f, 0.0f, 0.0f, 0.0f };
    Rect close{ 0.0f, 0.0f, 0.0f, 0.0f };
    Rect left{ 0.0f, 0.0f, 0.0f, 0.0f };     // Attributes and Combat, the first row being the column headings
    Rect right{ 0.0f, 0.0f, 0.0f, 0.0f };    // Resource Gathering, Crafting and Utility
};

inline StatsPanelLayout layoutStatsPanel(int W, int H, float rowH, float titleH) {
    const float s = H / 720.0f;
    const float colW = 420.0f, gap = 36.0f, pad = 24.0f;   // columns are fixed width, like the text
    const int longestColumn = 17;                           // Attributes and Combat, headings included
    const float bodyH = (longestColumn + 1) * rowH;         // one extra row for the column headings
    const float closeH = rowH + 8.0f * s, closeW = 120.0f * s;
    const float w = std::min(2.0f * colW + gap + 2.0f * pad, W - 20.0f);
    const float h = pad + titleH + 12.0f * s + bodyH + 12.0f * s + closeH + pad;

    StatsPanelLayout L;
    L.box = Rect{ (W - w) * 0.5f, std::max(4.0f, (H - h) * 0.5f), w, h };
    const float bodyY = L.box.y + pad + titleH + 12.0f * s;
    L.left = Rect{ L.box.x + pad, bodyY, colW, bodyH };
    L.right = Rect{ L.box.x + pad + colW + gap, bodyY, colW, bodyH };
    L.close = Rect{ L.box.x + (w - closeW) * 0.5f, bodyY + bodyH + 12.0f * s, closeW, closeH };
    return L;
}

// The right-click menu for an inventory item: one text button per action, top to bottom. It opens
// at the mouse and is kept inside the window.
struct ItemMenuLayout {
    Rect box{ 0.0f, 0.0f, 0.0f, 0.0f };
    std::vector<Rect> buttons;
};

inline ItemMenuLayout layoutItemMenu(float x, float y, int buttonCount, float rowH, int W, int H) {
    const float s = H / 720.0f;
    const float pad = 4.0f * s, gap = 3.0f * s;
    const float btnW = 104.0f * s, btnH = rowH + 8.0f * s;
    const float w = btnW + 2.0f * pad;
    const float h = 2.0f * pad + buttonCount * btnH + (buttonCount - 1) * gap;

    ItemMenuLayout L;
    const float bx = std::clamp(x, 0.0f, std::max(0.0f, W - w));
    const float by = std::clamp(y, 0.0f, std::max(0.0f, H - h));
    L.box = Rect{ bx, by, w, h };
    for (int i = 0; i < buttonCount; ++i) {
        L.buttons.push_back(Rect{ bx + pad, by + pad + i * (btnH + gap), btnW, btnH });
    }
    return L;
}

// Index of the menu button under a point, or -1
inline int itemMenuButtonAt(const ItemMenuLayout& L, float x, float y) {
    for (size_t i = 0; i < L.buttons.size(); ++i) {
        if (L.buttons[i].contains(x, y)) return (int)i;
    }
    return -1;
}
