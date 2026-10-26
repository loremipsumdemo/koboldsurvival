#pragma once
#include <algorithm>
#include <array>
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

// The [-] or [+] button at the right end of a title bar, which folds its panel up or down
inline Rect BarToggle(const Rect& bar, float s) {
    const float sz = bar.h - 6.0f * s;
    return Rect{ bar.x + bar.w - sz - 3.0f * s, bar.y + 3.0f * s, sz, sz };
}

// The height of the bottom panels when they are open. It is the Unit Panel's bar, its tabs and enough room for the
// inventory grid, so the Squad Panel and the Combat Log are as tall as the Unit Panel. Never more than 60% of the window.
inline float unitPanelBodyHeight(int H, float override = 0.0f) {
    if (override > 0.0f) return std::min(override, (float)H);
    const float s = H / 720.0f;
    const float width = 380.0f * s, pad = 14.0f * s, gap = 6.0f * s, headH = 30.0f * s;
    const float innerPad = 10.0f * s, barH = 22.0f * s, tabH = 26.0f * s;
    const float slot = (width - 2.0f * pad - 2.0f * innerPad - (INVENTORY_COLUMNS - 1) * gap) / INVENTORY_COLUMNS;
    const float gridRows = (float)(INVENTORY_SLOTS / INVENTORY_COLUMNS);
    const float gridPanelH = headH + gridRows * slot + (gridRows - 1.0f) * gap + innerPad;
    return std::min(barH + tabH + gridPanelH + 2.0f * pad, H * 0.6f);
}

// Where a bottom panel was dragged. dx and dy move it from its default place, which sits against the bottom edge of the
// window, and w and h give it a dragged width and height. 0 is the default size.
struct PanelPlace { float dx = 0.0f, dy = 0.0f, w = 0.0f, h = 0.0f; };

// The Squad Panel's width for a number of columns: the cells, their gaps, the scroll bar and the margins
inline float squadWidthFor(int cols, int H) {
    const float s = H / 720.0f;
    const float cell = 44.0f * s, gap = 5.0f * s, pad = 10.0f * s, scrollW = 14.0f * s;
    return 2.0f * pad + cols * (cell + gap) + scrollW;
}
inline float squadDefaultWidth(int H) { return squadWidthFor(UNITS_PER_ROW, H); }
inline float squadMinWidth(int H) { return squadWidthFor(2, H); }
inline float squadDefaultX(int W, int H) { return (W - squadDefaultWidth(H)) * 0.5f; }

// The Unit Panel's default width, and its default left edge: it sits against the right edge of the window
inline float sidebarDefaultWidth(int H) { return 380.0f * H / 720.0f; }
inline float sidebarDefaultX(int W, int H) { return W - 8.0f * H / 720.0f - sidebarDefaultWidth(H); }

// A bottom panel's rectangle: its default left edge and bottom edge moved by the place, kept on the screen
inline Rect placedRect(const PanelPlace& p, float defaultX, float w, float h, int W, int H, float margin) {
    const float x = std::clamp(defaultX + p.dx, 0.0f, std::max(0.0f, W - w));
    const float y = std::clamp(H - margin + p.dy - h, 0.0f, std::max(0.0f, (float)H - h));
    return Rect{ x, y, w, h };
}

struct UnitPanelLayout {
    bool collapsed = false;                     // only the title bar shows
    Rect toggle{ 0.0f, 0.0f, 0.0f, 0.0f };      // the [-] or [+] button in the bar
    Rect panel{ 0.0f, 0.0f, 0.0f, 0.0f };
    Rect title{ 0.0f, 0.0f, 0.0f, 0.0f };       // the "Squad Panel" bar across the top
    Rect paceButton{ 0.0f, 0.0f, 0.0f, 0.0f };  // walk and run toggle, across the top of the panel, on the left
    Rect mapButton{ 0.0f, 0.0f, 0.0f, 0.0f };   // View Map, across the top, on the right
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

// Bottom panel: the walk and run toggle and the View Map button across the top, then a 8-per-row grid of portraits with names,
// scrolled vertically.
inline UnitPanelLayout layoutUnitPanel(int W, int H, int unitCount, int scrollRow, bool collapsed = false,
                                       const PanelPlace& place = PanelPlace{}) {
    const float s = H / 720.0f;
    const float cell = 44.0f * s, gap = 5.0f * s, pad = 10.0f * s;
    const float label = unitCellLabelHeight(s), scrollW = 14.0f * s;
    const float toggleH = 22.0f * s;
    const float barH = 22.0f * s;   // the title bar above everything else in the panel
    // Half the height of the Unit Panel by default, or as dragged. As many rows as that has room for.
    const float bodyH = place.h > 0.0f ? std::min(place.h, (float)H) : unitPanelBodyHeight(H) * 0.5f;
    const float chromeH = barH + pad + toggleH + gap + pad;
    const int rowsVisible = std::max(2, (int)std::floor((bodyH - chromeH + gap) / (cell + label + gap)));
    // As many columns as a dragged width has room for. By default, UNITS_PER_ROW.
    const int cols = place.w > 0.0f ? std::max(2, (int)std::floor((place.w - 2.0f * pad - scrollW) / (cell + gap)))
                                    : UNITS_PER_ROW;

    UnitPanelLayout L;
    L.rowsVisible = rowsVisible;
    L.rowCount = (unitCount + cols - 1) / cols;
    const int maxScroll = std::max(0, L.rowCount - rowsVisible);
    L.scrollRow = std::clamp(scrollRow, 0, maxScroll);

    const float cellsW = cols * cell + (cols - 1) * gap;
    const float gridH = rowsVisible * (cell + label) + (rowsVisible - 1) * gap;
    const float panelW = pad + cellsW + gap + scrollW + pad;
    const float panelH = collapsed ? barH : bodyH;
    L.panel = placedRect(place, squadDefaultX(W, H), panelW, panelH, W, H, 8.0f * s);
    L.title = Rect{ L.panel.x, L.panel.y, panelW, barH };
    L.toggle = BarToggle(L.title, s);
    L.collapsed = collapsed;
    const float bodyY = L.panel.y + barH;

    // The top row holds the pace toggle and the View Map button side by side. The grid starts under it.
    const float halfW = (cellsW - gap) * 0.5f;
    L.paceButton = Rect{ L.panel.x + pad, bodyY + pad, halfW, toggleH };
    L.mapButton = Rect{ L.panel.x + pad + halfW + gap, bodyY + pad, halfW, toggleH };
    const float top = bodyY + pad + toggleH + gap;

    for (int r = 0; r < rowsVisible; ++r) {
        for (int c = 0; c < cols; ++c) {
            const int idx = (L.scrollRow + r) * cols + c;
            if (idx >= unitCount) break;
            const float x = L.panel.x + pad + c * (cell + gap);
            const float y = top + r * (cell + label + gap);
            L.cells.push_back(UnitCell{ idx, Rect{ x, y, cell, cell + label } });
        }
    }

    L.track = Rect{ L.panel.x + panelW - pad - scrollW, top, scrollW, gridH };
    if (!L.scrollable()) {
        L.thumb = L.track;
    } else {
        const float th = L.track.h * rowsVisible / L.rowCount;
        const float ty = L.track.y + (L.track.h - th) * L.scrollRow / maxScroll;
        L.thumb = Rect{ L.track.x, ty, L.track.w, th };
    }
    if (collapsed) {
        // Only the bar shows, so nothing in the body can be pressed
        L.cells.clear();
        L.paceButton = L.mapButton = L.track = L.thumb = Rect{};
    }
    return L;
}

// The minimap in the top right corner, the compass strip under it, and the clock under that. They line up
// against right, the window's right edge or the sidebar's left edge while the sidebar shows.
// Everything is in window pixels, scaled from a 720-pixel-high window.
struct MinimapLayout {
    Rect map{ 0.0f, 0.0f, 0.0f, 0.0f };
    Rect compass{ 0.0f, 0.0f, 0.0f, 0.0f };
    float right = 0.0f;       // the edge the block lines up against, its margin not included
    float clockTop = 0.0f;    // where the clock box starts
};

inline MinimapLayout layoutMinimap(int H, float right) {
    const float s = H / 720.0f;
    const float margin = 12.0f * s, size = 180.0f * s, gap = 6.0f * s, compassH = 30.0f * s;
    MinimapLayout L;
    L.right = right;
    L.map = Rect{ right - margin - size, margin, size, size };
    L.compass = Rect{ right - margin - size, margin + size + gap, size, compassH };
    L.clockTop = L.compass.y + compassH + gap;
    return L;
}

// The part of the world map's area that holds the map: a square under a line of text, centred
inline Rect WorldMapBox(const Rect& area, float s) {
    const float top = area.y + 30.0f * s;
    const float side = std::max(0.0f, std::min(area.w, area.h - 30.0f * s));
    return Rect{ area.x + (area.w - side) * 0.5f, top, side, side };
}

// The ground point under a pixel of the minimap, false outside the minimap or outside the level. The minimap
// shows a window of `span` tiles centred on cx, cz, with the largest x at its left and the largest z at its top.
inline bool minimapGround(const Rect& r, float cx, float cz, float span, float levelSize, float px, float py,
                          float& x, float& z) {
    if (!r.contains(px, py) || span <= 0.0f || r.w <= 0.0f || r.h <= 0.0f) return false;
    x = cx + span * 0.5f - (px - r.x) / r.w * span;
    z = cz + span * 0.5f - (py - r.y) / r.h * span;
    return x >= 0.0f && z >= 0.0f && x <= levelSize && z <= levelSize;
}

// The ground point under a pixel of the world map box, which shows the whole level
inline bool worldMapGround(const Rect& box, float levelSize, float px, float py, float& x, float& z) {
    if (!box.contains(px, py) || box.w <= 0.0f || levelSize <= 0.0f) return false;
    x = levelSize - (px - box.x) / box.w * levelSize;
    z = levelSize - (py - box.y) / box.h * levelSize;
    return x >= 0.0f && z >= 0.0f && x <= levelSize && z <= levelSize;
}

// One row of the Behavior list: the on box, the four option cells, then the priority and delete buttons,
// which sit outside the columns
struct BehaviorRowLayout {
    Rect row{ 0.0f, 0.0f, 0.0f, 0.0f };             // the whole row, which the hover text is for
    Rect toggle{ 0.0f, 0.0f, 0.0f, 0.0f };          // on or off
    Rect conditionTarget{ 0.0f, 0.0f, 0.0f, 0.0f }; // Self, Ally or Enemy
    Rect condition{ 0.0f, 0.0f, 0.0f, 0.0f };       // Health or Hunger, the operator and the percent
    Rect action{ 0.0f, 0.0f, 0.0f, 0.0f };          // what the rule does
    Rect actionTarget{ 0.0f, 0.0f, 0.0f, 0.0f };    // who it does it to
    Rect up{ 0.0f, 0.0f, 0.0f, 0.0f };              // [^] moves the rule up the priority list
    Rect down{ 0.0f, 0.0f, 0.0f, 0.0f };            // [v] moves it down
    Rect remove{ 0.0f, 0.0f, 0.0f, 0.0f };          // [x] deletes it
};

// The parts of a behavior row that a click or hover can land on
enum class BehaviorPart { None, Toggle, ConditionTarget, Condition, Action, ActionTarget, Up, Down, Remove };

// Right sidebar for the one unit shown. From the top, the content scrolls as one: a profile card with the
// unit's picture, name, stats and carry bar, then the gear, then the inventory grid of 8 columns of slots,
// then the unit's behavior rules. The sidebar sits on a see-through panel, which does not scroll, and each
// section has a background of its own.
// The Unit Panel's tabs, left to right
enum UnitTab : int { UNIT_TAB_INVENTORY = 0, UNIT_TAB_EQUIPMENT, UNIT_TAB_PROFILE, UNIT_TAB_STATS, UNIT_TAB_BEHAVIOR,
                     UNIT_TAB_COUNT };
// The groups the Stats tab lists its stats under
constexpr int STAT_GROUP_COUNT = 5;

struct SidebarLayout {
    Rect panel{ 0.0f, 0.0f, 0.0f, 0.0f };      // the whole Unit Panel, bottom right
    bool open = false;                          // closed: the bar alone. open: the tabs and the picked tab
    int tab = UNIT_TAB_INVENTORY;               // the tab shown, when open
    Rect title{ 0.0f, 0.0f, 0.0f, 0.0f };      // the "Unit Panel" bar across the top, which does not scroll
    Rect minus{ 0.0f, 0.0f, 0.0f, 0.0f };      // [-], shown while open; empty otherwise
    Rect plus{ 0.0f, 0.0f, 0.0f, 0.0f };       // [+], shown while closed; empty otherwise
    std::array<Rect, UNIT_TAB_COUNT> tabs{};   // the tab buttons under the bar; empty while closed
    Rect body{ 0.0f, 0.0f, 0.0f, 0.0f };       // the area under the tabs, where the picked tab's contents scroll
    Rect profile{ 0.0f, 0.0f, 0.0f, 0.0f };    // profile card: name, health, hunger and carry
    Rect portrait{ 0.0f, 0.0f, 0.0f, 0.0f };   // the unit's picture, inside the card on the left
    Rect gearPanel{ 0.0f, 0.0f, 0.0f, 0.0f };  // background behind the gear slots and their totals
    std::vector<Rect> gear;                    // GEAR_SLOTS rectangles in GearSlot order, four per row
    Rect gridPanel{ 0.0f, 0.0f, 0.0f, 0.0f };  // background behind the inventory grid
    Rect sortButton{ 0.0f, 0.0f, 0.0f, 0.0f }; // sorts the grid by item type, top right of its heading
    std::vector<Rect> slots;                   // INVENTORY_SLOTS rectangles, row by row
    Rect statsArea{ 0.0f, 0.0f, 0.0f, 0.0f };  // the stats list, in the Stats tab
    float statRowH = 0.0f;                     // the height of each row in the stats list
    Rect behaviorPanel{ 0.0f, 0.0f, 0.0f, 0.0f };  // background behind the behavior list
    std::array<Rect, 5> behaviorHeads{};       // column headings: On, Target, Condition, Action, Target
    std::vector<BehaviorRowLayout> behaviorRows;   // one per rule, highest priority first
    Rect addButton{ 0.0f, 0.0f, 0.0f, 0.0f };  // adds a rule, under the last one
    float scroll = 0.0f;                       // the scroll offset used, in pixels, within 0..maxScroll
    float maxScroll = 0.0f;                    // how far the picked tab can scroll
};

// The Unit Panel along the bottom right. Closed, it is its title bar alone, with [+]. Open, it has five tabs under
// the bar and the picked tab's contents, which scroll when they are taller than the panel. The panel is as tall as
// its tallest tab, and never taller than 60% of the window. Everything is in window pixels, scaled from a
// 720-pixel-high window.
inline SidebarLayout layoutInventorySidebar(int W, int H, int ruleCount, float scroll, bool open = false, int tab = 0,
                                            const PanelPlace& place = PanelPlace{}) {
    const float s = H / 720.0f;
    const float pad = 14.0f * s, gap = 6.0f * s, margin = 8.0f * s;
    // Its default width, or as dragged between a minimum and the screen
    const float width = place.w > 0.0f ? std::min(std::max(place.w, 200.0f * s), W - 16.0f * s) : sidebarDefaultWidth(H);
    const float cardH = 200.0f * s, cardInset = 8.0f * s, headH = 30.0f * s, innerPad = 10.0f * s;
    const float barH = 22.0f * s, tabH = 26.0f * s, tabGap = 3.0f * s;
    const float colHeadH = 16.0f * s, rowH = 24.0f * s, rowGap = 2.0f * s, addH = 24.0f * s;
    const float buttonW = 18.0f * s, buttonGap = 2.0f * s;
    const float sortW = 56.0f * s, sortH = 22.0f * s, statRowH = 20.0f * s;
    const int rules = std::clamp(ruleCount, 0, BEHAVIOR_LIMIT);
    tab = std::clamp(tab, 0, UNIT_TAB_COUNT - 1);

    SidebarLayout L;
    L.open = open;
    L.tab = tab;
    if (!open) {
        L.panel = placedRect(place, sidebarDefaultX(W, H), width, barH, W, H, margin);
        L.title = Rect{ L.panel.x, L.panel.y, width, barH };
        L.plus = BarToggle(L.title, s);
        return L;
    }

    const float innerW = width - 2.0f * pad - 2.0f * innerPad;
    // The height each tab's contents need
    const int gearCols = 4, gearRows = GEAR_SLOTS / gearCols;
    const float gearH = 56.0f * s, totalsH = 26.0f * s;
    const float gearPanelH = headH + gearRows * gearH + gearRows * gap + totalsH + innerPad;
    const float gridRows = (float)(INVENTORY_SLOTS / INVENTORY_COLUMNS);
    const float slot = (width - 2.0f * pad - 2.0f * innerPad - (INVENTORY_COLUMNS - 1) * gap) / INVENTORY_COLUMNS;
    const float gridPanelH = headH + gridRows * slot + (gridRows - 1.0f) * gap + innerPad;
    const float behaviorPanelH = headH + colHeadH + rules * (rowH + rowGap) + addH + innerPad;
    const float statsH = (1.0f + STAT_GROUP_COUNT + STAT_COUNT) * statRowH;
    const float contents[UNIT_TAB_COUNT] = { gridPanelH, gearPanelH, cardH, statsH, behaviorPanelH };

    const float bodyVis = unitPanelBodyHeight(H, place.h) - barH - tabH;
    const float maxScroll = std::max(0.0f, contents[tab] + 2.0f * pad - bodyVis);
    const float shift = -std::clamp(scroll, 0.0f, maxScroll);   // the contents move up by this much
    const float panelH = barH + tabH + bodyVis;
    L.scroll = -shift;
    L.maxScroll = maxScroll;
    L.panel = placedRect(place, sidebarDefaultX(W, H), width, panelH, W, H, margin);
    L.title = Rect{ L.panel.x, L.panel.y, width, barH };
    L.minus = BarToggle(L.title, s);

    const float tabY = L.panel.y + barH;
    const float tabW = (width - 2.0f * pad - (UNIT_TAB_COUNT - 1) * tabGap) / UNIT_TAB_COUNT;
    for (int i = 0; i < UNIT_TAB_COUNT; ++i) {
        L.tabs[i] = Rect{ L.panel.x + pad + i * (tabW + tabGap), tabY + 3.0f * s, tabW, tabH - 6.0f * s };
    }
    L.body = Rect{ L.panel.x, tabY + tabH, width, bodyVis };
    const float top = L.body.y + pad + shift;   // the picked tab's contents, which scroll

    if (tab == UNIT_TAB_PROFILE) {
        L.profile = Rect{ L.panel.x + pad, top, width - 2.0f * pad, cardH };
        const float side = cardH - 2.0f * cardInset;
        L.portrait = Rect{ L.profile.x + cardInset, L.profile.y + cardInset, side, side };
    }
    if (tab == UNIT_TAB_EQUIPMENT) {
        // Four gear slots to a row, two rows, then a line of attack and defense totals
        const float gearW = (innerW - (gearCols - 1) * gap) / gearCols;
        L.gearPanel = Rect{ L.panel.x + pad, top, width - 2.0f * pad, gearPanelH };
        for (int i = 0; i < GEAR_SLOTS; ++i) {
            const int c = i % gearCols, r = i / gearCols;
            L.gear.push_back(Rect{ L.gearPanel.x + innerPad + c * (gearW + gap),
                                   top + headH + r * (gearH + gap), gearW, gearH });
        }
    }
    if (tab == UNIT_TAB_INVENTORY) {
        L.gridPanel = Rect{ L.panel.x + pad, top, width - 2.0f * pad, gridPanelH };
        L.sortButton = Rect{ L.gridPanel.x + L.gridPanel.w - innerPad - sortW, top + (headH - sortH) * 0.5f,
                             sortW, sortH };
        for (int i = 0; i < INVENTORY_SLOTS; ++i) {
            const int c = i % INVENTORY_COLUMNS, r = i / INVENTORY_COLUMNS;
            L.slots.push_back(Rect{ L.gridPanel.x + innerPad + c * (slot + gap),
                                    top + headH + r * (slot + gap), slot, slot });
        }
    }
    if (tab == UNIT_TAB_STATS) {
        L.statsArea = Rect{ L.panel.x + pad, top, width - 2.0f * pad, statsH };
        L.statRowH = statRowH;
    }
    if (tab == UNIT_TAB_BEHAVIOR) {
        // Behaviors: the on box has a fixed width, and the four option columns share what the priority buttons
        // leave, in the proportions below. The buttons sit at the right end of each row.
        L.behaviorPanel = Rect{ L.panel.x + pad, top, width - 2.0f * pad, behaviorPanelH };
        const float rowX = L.behaviorPanel.x + innerPad;
        const float rowW = L.behaviorPanel.w - 2.0f * innerPad;
        const float toggleW = 18.0f * s, colGap = 3.0f * s;
        const float buttonsW = 3.0f * buttonW + 2.0f * buttonGap;
        const float colsW = rowW - buttonsW - gap;                 // the toggle and the four option columns
        const float optionsW = colsW - toggleW - 4.0f * colGap;    // the four option columns, with their gaps taken out
        const float share[4] = { 0.19f, 0.31f, 0.33f, 0.17f };     // condition target, condition, action, action target
        std::array<float, 5> colX{}, colWidth{};                   // the toggle, then the four option columns
        colX[0] = rowX;
        colWidth[0] = toggleW;
        float cursor = rowX + toggleW;
        for (int i = 0; i < 4; ++i) {
            cursor += colGap;
            colX[i + 1] = cursor;
            colWidth[i + 1] = optionsW * share[i];
            cursor += colWidth[i + 1];
        }
        const float buttonsX = rowX + colsW + gap;
        const float headY = L.behaviorPanel.y + headH;
        const float rowsY = headY + colHeadH;
        for (int i = 0; i < rules; ++i) {
            BehaviorRowLayout R;
            const float y = rowsY + i * (rowH + rowGap);
            R.row = Rect{ rowX, y, rowW, rowH };
            R.toggle = Rect{ colX[0], y + (rowH - toggleW) * 0.5f, toggleW, toggleW };
            R.conditionTarget = Rect{ colX[1], y, colWidth[1], rowH };
            R.condition = Rect{ colX[2], y, colWidth[2], rowH };
            R.action = Rect{ colX[3], y, colWidth[3], rowH };
            R.actionTarget = Rect{ colX[4], y, colWidth[4], rowH };
            const float by = y + 2.0f * s, bh = rowH - 4.0f * s;
            R.up = Rect{ buttonsX, by, buttonW, bh };
            R.down = Rect{ buttonsX + buttonW + buttonGap, by, buttonW, bh };
            R.remove = Rect{ buttonsX + 2.0f * (buttonW + buttonGap), by, buttonW, bh };
            L.behaviorRows.push_back(R);
        }
        for (size_t i = 0; i < 5; ++i) L.behaviorHeads[i] = Rect{ colX[i], headY, colWidth[i], colHeadH };
        L.addButton = Rect{ rowX, rowsY + rules * (rowH + rowGap), rowW, addH };
    }
    return L;
}

// The behavior row part under a point. The row is -1 when the point is over no row. Only the sidebar's visible
// part counts, so a row scrolled out of view can't be hit.
struct BehaviorHit {
    int row = -1;
    BehaviorPart part = BehaviorPart::None;
};

inline BehaviorHit behaviorHitAt(const SidebarLayout& L, float x, float y) {
    BehaviorHit hit;
    if (!L.panel.contains(x, y)) return hit;
    for (size_t i = 0; i < L.behaviorRows.size(); ++i) {
        const BehaviorRowLayout& R = L.behaviorRows[i];
        if (!R.row.contains(x, y)) continue;
        hit.row = (int)i;
        const struct { Rect r; BehaviorPart part; } parts[] = {
            { R.toggle, BehaviorPart::Toggle },
            { R.conditionTarget, BehaviorPart::ConditionTarget },
            { R.condition, BehaviorPart::Condition },
            { R.action, BehaviorPart::Action },
            { R.actionTarget, BehaviorPart::ActionTarget },
            { R.up, BehaviorPart::Up },
            { R.down, BehaviorPart::Down },
            { R.remove, BehaviorPart::Remove },
        };
        for (const auto& p : parts) {
            if (p.r.contains(x, y)) {
                hit.part = p.part;
                break;
            }
        }
        return hit;
    }
    return hit;
}

// Index of the rectangle under a point, or -1
inline int rectIndexAt(const std::vector<Rect>& rects, float x, float y) {
    for (size_t i = 0; i < rects.size(); ++i) {
        if (rects[i].contains(x, y)) return (int)i;
    }
    return -1;
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

// The stats popout, centerd on the window. Two columns of stats, each row showing the level, the
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

// The condition editor, opened from a behavior's Condition cell. Three rows: the kind (Health or Hunger), the
// six operators, then the percent with its steps: -10 and -1 on the left, +1 and +10 on the right.
struct ConditionEditorLayout {
    Rect box{ 0.0f, 0.0f, 0.0f, 0.0f };
    std::vector<Rect> kinds;   // Health, Hunger, in BehaviorCondition order
    std::vector<Rect> ops;     // one per BehaviorOp, in order
    std::vector<Rect> steps;   // -10, -1, +1, +10
    Rect value{ 0.0f, 0.0f, 0.0f, 0.0f };   // the percent, between the two halves of the steps
};

// The editor with its top-left corner at (x, y), kept inside the window. rowH is the height of one line of text.
inline ConditionEditorLayout layoutConditionEditor(float x, float y, float rowH, int W, int H) {
    const float s = H / 720.0f;
    const float pad = 6.0f * s, gap = 4.0f * s;
    const float btnH = rowH + 8.0f * s;
    const float w = 216.0f * s;
    const float inner = w - 2.0f * pad;
    const float h = pad + btnH + gap + btnH + gap + btnH + pad;
    const float bx = std::clamp(x, 0.0f, std::max(0.0f, W - w));
    const float by = std::clamp(y, 0.0f, std::max(0.0f, H - h));

    ConditionEditorLayout L;
    L.box = Rect{ bx, by, w, h };
    const float kindW = (inner - gap) / 2.0f;
    for (int i = 0; i < 2; ++i) {
        L.kinds.push_back(Rect{ bx + pad + i * (kindW + gap), by + pad, kindW, btnH });
    }
    const float opW = (inner - 5.0f * gap) / 6.0f;
    const float opsY = by + pad + btnH + gap;
    for (int i = 0; i < 6; ++i) {
        L.ops.push_back(Rect{ bx + pad + i * (opW + gap), opsY, opW, btnH });
    }
    const float valueW = 60.0f * s;
    const float stepW = (inner - valueW - 4.0f * gap) / 4.0f;
    const float stepsY = opsY + btnH + gap;
    const float left = bx + pad;
    L.steps.push_back(Rect{ left, stepsY, stepW, btnH });
    L.steps.push_back(Rect{ left + stepW + gap, stepsY, stepW, btnH });
    L.value = Rect{ left + 2.0f * (stepW + gap), stepsY, valueW, btnH };
    const float right = L.value.x + valueW + gap;
    L.steps.push_back(Rect{ right, stepsY, stepW, btnH });
    L.steps.push_back(Rect{ right + stepW + gap, stepsY, stepW, btnH });
    return L;
}
