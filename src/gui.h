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

struct UnitPanelLayout {
    Rect panel{ 0.0f, 0.0f, 0.0f, 0.0f };
    Rect walkButton{ 0.0f, 0.0f, 0.0f, 0.0f };  // walk and run toggle, across the top of the panel
    Rect runButton{ 0.0f, 0.0f, 0.0f, 0.0f };
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

// Bottom panel: a walk and run toggle across the top, then a 8-per-row grid of portraits with names,
// scrolled vertically.
inline UnitPanelLayout layoutUnitPanel(int W, int H, int unitCount, int scrollRow) {
    const float s = H / 720.0f;
    const float cell = 64.0f * s, gap = 6.0f * s, pad = 10.0f * s;
    const float label = unitCellLabelHeight(s), scrollW = 14.0f * s;
    const float toggleH = 22.0f * s;
    const int rowsVisible = 2;

    UnitPanelLayout L;
    L.rowsVisible = rowsVisible;
    L.rowCount = (unitCount + UNITS_PER_ROW - 1) / UNITS_PER_ROW;
    const int maxScroll = std::max(0, L.rowCount - rowsVisible);
    L.scrollRow = std::clamp(scrollRow, 0, maxScroll);

    const float cellsW = UNITS_PER_ROW * cell + (UNITS_PER_ROW - 1) * gap;
    const float gridH = rowsVisible * (cell + label) + (rowsVisible - 1) * gap;
    const float panelW = pad + cellsW + gap + scrollW + pad;
    const float panelH = pad + toggleH + gap + gridH + pad;
    L.panel = Rect{ (W - panelW) * 0.5f, H - panelH - 8.0f * s, panelW, panelH };

    // The toggle spans the cells, with Walk and Run side by side. The grid starts under it.
    const float halfW = (cellsW - gap) * 0.5f;
    L.walkButton = Rect{ L.panel.x + pad, L.panel.y + pad, halfW, toggleH };
    L.runButton = Rect{ L.panel.x + pad + halfW + gap, L.panel.y + pad, halfW, toggleH };
    const float top = L.panel.y + pad + toggleH + gap;

    for (int r = 0; r < rowsVisible; ++r) {
        for (int c = 0; c < UNITS_PER_ROW; ++c) {
            const int idx = (L.scrollRow + r) * UNITS_PER_ROW + c;
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
    return L;
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
struct SidebarLayout {
    Rect panel{ 0.0f, 0.0f, 0.0f, 0.0f };      // the whole sidebar, drawn see-through
    Rect profile{ 0.0f, 0.0f, 0.0f, 0.0f };    // profile card at the top
    Rect portrait{ 0.0f, 0.0f, 0.0f, 0.0f };   // the unit's picture, inside the card on the left
    Rect statsButton{ 0.0f, 0.0f, 0.0f, 0.0f };// opens the stats popout, top right of the card
    Rect gearPanel{ 0.0f, 0.0f, 0.0f, 0.0f };  // background behind the gear slots and their totals
    std::vector<Rect> gear;                    // GEAR_SLOTS rectangles in GearSlot order, four per row
    Rect gridPanel{ 0.0f, 0.0f, 0.0f, 0.0f };  // background behind the inventory grid
    Rect sortButton{ 0.0f, 0.0f, 0.0f, 0.0f }; // sorts the grid by item type, top right of its heading
    std::vector<Rect> slots;                   // INVENTORY_SLOTS rectangles, row by row
    Rect behaviorPanel{ 0.0f, 0.0f, 0.0f, 0.0f };  // background behind the behavior list
    std::array<Rect, 5> behaviorHeads{};       // column headings: On, Target, Condition, Action, Target
    std::vector<BehaviorRowLayout> behaviorRows;   // one per rule, highest priority first
    Rect addButton{ 0.0f, 0.0f, 0.0f, 0.0f };  // adds a rule, under the last one
    float scroll = 0.0f;                       // the scroll offset used, in pixels, within 0..maxScroll
    float maxScroll = 0.0f;                    // how far the sidebar can scroll
};

// The sidebar for a window of size W by H, showing a unit with ruleCount behavior rules, scrolled by scroll
// pixels. The scroll is kept within range, and the result says what it was clamped to.
inline SidebarLayout layoutInventorySidebar(int W, int H, int ruleCount, float scroll) {
    const float s = H / 720.0f;
    const float width = 400.0f * s, pad = 14.0f * s, gap = 6.0f * s;
    const float cardH = 200.0f * s, cardInset = 8.0f * s, headH = 30.0f * s, innerPad = 10.0f * s;
    const float sectionGap = 12.0f * s;
    const float innerW = width - 2.0f * pad - 2.0f * innerPad;
    const float colHeadH = 16.0f * s, rowH = 24.0f * s, rowGap = 2.0f * s, addH = 24.0f * s;
    const float buttonW = 18.0f * s, buttonGap = 2.0f * s;
    const float sortW = 56.0f * s, sortH = 22.0f * s;
    const int rules = std::clamp(ruleCount, 0, BEHAVIOR_LIMIT);

    // The heights of the sections, which give the height of the whole sidebar and so how far it scrolls
    const int gearCols = 4, gearRows = GEAR_SLOTS / gearCols;
    const float gearH = 56.0f * s, totalsH = 26.0f * s;
    const float gearPanelH = headH + gearRows * gearH + gearRows * gap + totalsH + innerPad;
    const float gridRows = (float)(INVENTORY_SLOTS / INVENTORY_COLUMNS);
    const float slot = (width - 2.0f * pad - 2.0f * innerPad - (INVENTORY_COLUMNS - 1) * gap) / INVENTORY_COLUMNS;
    const float gridPanelH = headH + gridRows * slot + (gridRows - 1.0f) * gap + innerPad;
    const float behaviorPanelH = headH + colHeadH + rules * (rowH + rowGap) + addH + innerPad;
    const float contentH = pad + cardH + sectionGap + gearPanelH + sectionGap + gridPanelH + sectionGap
                         + behaviorPanelH + pad;
    const float maxScroll = std::max(0.0f, contentH - H);
    const float shift = -std::clamp(scroll, 0.0f, maxScroll);   // the content moves up by this much

    SidebarLayout L;
    L.scroll = -shift;
    L.maxScroll = maxScroll;
    L.panel = Rect{ W - width, 0.0f, width, (float)H };

    L.profile = Rect{ L.panel.x + pad, pad + shift, width - 2.0f * pad, cardH };
    const float side = cardH - 2.0f * cardInset;
    L.portrait = Rect{ L.profile.x + cardInset, L.profile.y + cardInset, side, side };
    const float btnW = 66.0f * s, btnH = 24.0f * s;
    L.statsButton = Rect{ L.profile.x + L.profile.w - cardInset - btnW, L.profile.y + cardInset, btnW, btnH };

    // Gear: four slots to a row, two rows, then a line of attack and defense totals
    const float gearTop = L.profile.y + cardH + sectionGap;
    const float gearW = (innerW - (gearCols - 1) * gap) / gearCols;
    L.gearPanel = Rect{ L.panel.x + pad, gearTop, width - 2.0f * pad, gearPanelH };
    for (int i = 0; i < GEAR_SLOTS; ++i) {
        const int c = i % gearCols, r = i / gearCols;
        L.gear.push_back(Rect{ L.gearPanel.x + innerPad + c * (gearW + gap),
                               gearTop + headH + r * (gearH + gap),
                               gearW, gearH });
    }

    // Grid: columns fill the width inside the grid background, rows follow. The sort button is in the heading.
    const float gridTop = L.gearPanel.y + L.gearPanel.h + sectionGap;
    L.gridPanel = Rect{ L.panel.x + pad, gridTop, width - 2.0f * pad, gridPanelH };
    L.sortButton = Rect{ L.gridPanel.x + L.gridPanel.w - innerPad - sortW, gridTop + (headH - sortH) * 0.5f,
                         sortW, sortH };
    for (int i = 0; i < INVENTORY_SLOTS; ++i) {
        const int c = i % INVENTORY_COLUMNS, r = i / INVENTORY_COLUMNS;
        L.slots.push_back(Rect{ L.gridPanel.x + innerPad + c * (slot + gap),
                                gridTop + headH + r * (slot + gap),
                                slot, slot });
    }

    // Behaviors: the on box has a fixed width, and the four option columns share what the priority buttons
    // leave, in the proportions below. The buttons sit at the right end of each row.
    const float behaviorTop = L.gridPanel.y + L.gridPanel.h + sectionGap;
    L.behaviorPanel = Rect{ L.panel.x + pad, behaviorTop, width - 2.0f * pad, behaviorPanelH };
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
