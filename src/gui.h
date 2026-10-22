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

// Bottom panel: a 8-per-row grid of portraits with names, scrolled vertically.
inline UnitPanelLayout layoutUnitPanel(int W, int H, int unitCount, int scrollRow) {
    const float s = H / 720.0f;
    const float cell = 64.0f * s, gap = 6.0f * s, pad = 10.0f * s;
    const float label = 18.0f * s, scrollW = 14.0f * s;
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

// Right sidebar: the inventory of one unit, 8 columns of slots
struct SidebarLayout {
    Rect panel{ 0.0f, 0.0f, 0.0f, 0.0f };
    std::vector<Rect> slots;   // INVENTORY_SLOTS rectangles, row by row
};

inline SidebarLayout layoutInventorySidebar(int W, int H) {
    const float s = H / 720.0f;
    const float width = 380.0f * s, pad = 14.0f * s, gap = 6.0f * s, headerH = 70.0f * s;

    SidebarLayout L;
    L.panel = Rect{ W - width, 0.0f, width, (float)H };
    const float slot = (width - 2.0f * pad - (INVENTORY_COLUMNS - 1) * gap) / INVENTORY_COLUMNS;
    for (int i = 0; i < INVENTORY_SLOTS; ++i) {
        const int c = i % INVENTORY_COLUMNS, r = i / INVENTORY_COLUMNS;
        L.slots.push_back(Rect{ L.panel.x + pad + c * (slot + gap),
                                L.panel.y + headerH + r * (slot + gap),
                                slot, slot });
    }
    return L;
}
