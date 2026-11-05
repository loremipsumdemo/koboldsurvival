#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>
#include <vector>
#include "components.h"
#include "items.h"
#include "props.h"
#include "ui_types.h"

// Layout for the in-game panels. Pure functions of the window size and the state,
// so the drawing code and the mouse handling use the same rectangles.

constexpr int UNITS_PER_ROW = 5;        // the Squad Panel's columns at its default width
constexpr int SQUAD_DEFAULT_ROWS = 3;   // and its rows at its default height

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
// The lock button, just left of the [-] or [+] button. Locked, the panel cannot be moved or resized.
inline Rect BarLock(const Rect& bar, float s) {
    const Rect t = BarToggle(bar, s);
    return Rect{ t.x - t.w - 3.0f * s, t.y, t.w, t.h };
}

// ---- The three panels that can be moved and resized ----
// Each sits in its own corner by default: the Information Panel in the top right, the Squad Panel in the bottom left and
// the Unit Panel in the bottom right, under the Information Panel. Where a panel's title bar is depends on where the panel is: a panel mostly in the
// top half of the window has its bar along its top, and one mostly in the bottom half has it along its bottom. Folding
// leaves the bar where it is and takes the rest away, so a panel at the top folds up and one at the bottom folds down;
// opening grows it back away from the bar, downward from a bar in the top half and upward from one in the bottom half.
// Their sizes never change by themselves: only the player resizes them, from any edge, while the panel's lock is open.
// Only the title bar is kept inside the window; the rest of a panel may run past an edge.
enum class PanelId : int { Squad = 0, Unit = 1, Info = 2, Count = 3 };
constexpr int PANEL_COUNT = idx(PanelId::Count);
constexpr PanelId ALL_PANELS[PANEL_COUNT] = { PanelId::Squad, PanelId::Unit, PanelId::Info };
// What dragging a panel does: the edges it pulls (a corner pulls two), or moving it by its title bar
constexpr int GRAB_TOP = 1, GRAB_RIGHT = 2, GRAB_BOTTOM = 4, GRAB_LEFT = 8, GRAB_MOVE = 16;

// PanelPlace, where a panel was put, is in ui_types.h

constexpr float PANEL_MARGIN = 8.0f;     // between a panel in its default place and the window's edge, at 720p
constexpr float PANEL_BAR_H = 22.0f;     // every panel's title bar, at 720p

// The Squad Panel: its cells, gaps and padding
struct SquadMetrics {
    float cell, gap, pad, label, scrollW, toggleH, barH, tabH;
    explicit SquadMetrics(float s)
        : cell(44.0f * s), gap(5.0f * s), pad(10.0f * s), label(34.0f * s), scrollW(14.0f * s), toggleH(22.0f * s),
          barH(PANEL_BAR_H * s), tabH(24.0f * s) {}
    // Everything but the rows: the bar, the tabs, the pace row and the padding
    float chromeH() const { return barH + tabH + pad + toggleH + gap + pad; }
    float rowsH(int rows) const { return rows * (cell + label) + (rows - 1) * gap; }
};
// Each cell: the portrait square, then a thin health bar, then the name
inline float unitCellLabelHeight(float s) { return 34.0f * s; }

// The Squad Panel's width for a number of columns: the cells, their gaps, the scroll bar and the margins
inline float squadWidthFor(int cols, int H) {
    const SquadMetrics m(H / 720.0f);
    return 2.0f * m.pad + cols * (m.cell + m.gap) + m.scrollW;
}
inline float squadDefaultWidth(int H) { return squadWidthFor(UNITS_PER_ROW, H); }
inline float squadMinWidth(int H) { return squadWidthFor(2, H); }
inline float squadDefaultHeight(int H) {
    const SquadMetrics m(H / 720.0f);
    return m.chromeH() + m.rowsH(SQUAD_DEFAULT_ROWS);
}

// The narrowest the Unit Panel can be: eight inventory cells of ITEM_CELL_PX in a row, with its gaps and padding
inline float sidebarMinWidth(int H) {
    const float s = H / 720.0f;
    return INVENTORY_COLUMNS * ITEM_CELL_PX + (INVENTORY_COLUMNS - 1) * 6.0f * s + 2.0f * 14.0f * s + 2.0f * 10.0f * s;
}
inline float sidebarDefaultWidth(int H) { return std::max(380.0f * H / 720.0f, sidebarMinWidth(H)); }

// The Unit Panel's Inventory tab: the gear row and its totals, then the inventory grid. The cells are ITEM_CELL_PX
// whatever the window's size, the rest scales with the window.
struct UnitPanelMetrics {
    float pad, gap, headH, innerPad, barH, tabH, totalsH;
    explicit UnitPanelMetrics(float s)
        : pad(14.0f * s), gap(6.0f * s), headH(30.0f * s), innerPad(10.0f * s), barH(PANEL_BAR_H * s), tabH(26.0f * s),
          totalsH(26.0f * s) {}
    float gearPanelH() const { return headH + ITEM_CELL_PX + gap + totalsH + innerPad; }
    float gridPanelH() const {
        const float rows = (float)(INVENTORY_SLOTS / INVENTORY_COLUMNS);
        return headH + rows * ITEM_CELL_PX + (rows - 1.0f) * gap + innerPad;
    }
    float inventoryTabH() const { return gearPanelH() + gap + gridPanelH(); }
};
// The Unit Panel's default height: tall enough for the gear and the whole inventory without scrolling, if the window has
// the room
inline float unitPanelDefaultHeight(int H) {
    const float s = H / 720.0f;
    const UnitPanelMetrics m(s);
    return std::min(m.barH + m.tabH + m.inventoryTabH() + 2.0f * m.pad, H - 2.0f * PANEL_MARGIN * s);
}

inline float infoDefaultWidth(int H) { return 340.0f * H / 720.0f; }
// 220 pixels at 720p, but never so tall that it reaches the Unit Panel under it in the same corner: in a short window
// it takes what room is left above the Unit Panel, down to its smallest
inline float infoDefaultHeight(int H) {
    const float s = H / 720.0f;
    const float room = H - 2.0f * PANEL_MARGIN * s - unitPanelDefaultHeight(H) - PANEL_MARGIN * s;
    const float smallest = (PANEL_BAR_H + 20.0f + 40.0f) * s;
    return std::max(smallest, std::min(220.0f * s, room));
}

inline void panelDefaultSize(PanelId which, int H, float& w, float& h) {
    if (which == PanelId::Squad) { w = squadDefaultWidth(H); h = squadDefaultHeight(H); }
    else if (which == PanelId::Unit) { w = sidebarDefaultWidth(H); h = unitPanelDefaultHeight(H); }
    else { w = infoDefaultWidth(H); h = infoDefaultHeight(H); }
}
// The smallest a panel can be made: room for its bar and a little of what is under it
inline void panelMinSize(PanelId which, int H, float& w, float& h) {
    const float s = H / 720.0f;
    if (which == PanelId::Squad) {
        const SquadMetrics m(s);
        w = squadMinWidth(H);
        h = m.chromeH() + m.rowsH(1);
    } else if (which == PanelId::Unit) {
        w = sidebarMinWidth(H);
        h = (PANEL_BAR_H + 26.0f + 60.0f) * s;
    } else {
        w = 150.0f * s;
        h = (PANEL_BAR_H + 20.0f + 40.0f) * s;
    }
}

// The panel's default top-left corner for an open size of w by h
inline float panelAnchorX(PanelId which, float w, int W, int H) {
    const float m = PANEL_MARGIN * H / 720.0f;
    return which == PanelId::Squad ? m : W - m - w;   // the Squad Panel on the left, the other two on the right
}
inline float panelAnchorY(PanelId which, float h, int H) {
    const float m = PANEL_MARGIN * H / 720.0f;
    return which == PanelId::Info ? m : H - m - h;
}

// A panel's open rectangle in window pixels: its size and place. Only its title bar is kept inside the window.
inline Rect panelRect(PanelId which, const PanelPlace& p, int W, int H) {
    const float s = H / 720.0f;
    const float barH = PANEL_BAR_H * s;
    float dw = 0.0f, dh = 0.0f, mw = 0.0f, mh = 0.0f;
    panelDefaultSize(which, H, dw, dh);
    panelMinSize(which, H, mw, mh);
    float w = p.w > 0.0f ? p.w * s : dw;
    float h = p.h > 0.0f ? p.h * s : dh;
    w = std::clamp(w, std::min(mw, (float)W), (float)W);
    h = std::clamp(h, std::min(mh, (float)H), (float)H);
    const float x = std::clamp(panelAnchorX(which, w, W, H) + p.dx * s, 0.0f, std::max(0.0f, W - w));
    float y = panelAnchorY(which, h, H) + p.dy * s;
    const float barOffset = p.barBottom ? h - barH : 0.0f;   // where the bar sits in the rectangle
    y = std::clamp(y + barOffset, 0.0f, std::max(0.0f, H - barH)) - barOffset;
    return Rect{ x, y, w, h };
}

// The place that puts a panel's open rectangle at r, with its bar on the side given
inline PanelPlace placeForRect(PanelId which, const Rect& r, int W, int H, bool barBottom = false) {
    const float s = H / 720.0f;
    PanelPlace p;
    p.barBottom = barBottom;
    p.w = r.w / s;
    p.h = r.h / s;
    p.dx = (r.x - panelAnchorX(which, r.w, W, H)) / s;
    p.dy = (r.y - panelAnchorY(which, r.h, H)) / s;
    return p;
}

// A panel's title bar: along the top or the bottom of its open rectangle
inline Rect panelBarRect(const Rect& open, bool barBottom, float barH) {
    return Rect{ open.x, barBottom ? open.y + open.h - barH : open.y, open.w, barH };
}
// What a panel shows: its open rectangle, or its title bar alone while folded
inline Rect panelShownRect(const Rect& open, bool collapsed, bool barBottom, float barH) {
    return collapsed ? panelBarRect(open, barBottom, barH) : open;
}
// The part of an open panel that is not its title bar
inline Rect panelBodyRect(const Rect& open, bool barBottom, float barH) {
    return Rect{ open.x, barBottom ? open.y : open.y + barH, open.w, std::max(0.0f, open.h - barH) };
}

// Puts a panel's bar on the side its place calls for, and returns the place, changed if the side was wrong. An open
// panel mostly in the bottom half of the window has its bar along its bottom, otherwise along its top; the panel stays
// where it is and its bar moves. A folded panel's bar stays where it is, and the panel it opens into goes the right way
// from it: upward from a bar in the bottom half, downward from one in the top half.
inline PanelPlace settlePanel(PanelId which, const PanelPlace& p, int W, int H, bool folded) {
    const float barH = PANEL_BAR_H * H / 720.0f;
    const Rect open = panelRect(which, p, W, H);
    if (!folded) {
        const bool bottom = open.y + open.h * 0.5f > H * 0.5f;
        if (bottom == p.barBottom) return p;
        PanelPlace q = placeForRect(which, open, W, H, bottom);
        return placeForRect(which, panelRect(which, q, W, H), W, H, bottom);   // the moved bar kept on the screen
    }
    const Rect bar = panelBarRect(open, p.barBottom, barH);
    const bool bottom = bar.y + barH * 0.5f > H * 0.5f;
    if (bottom == p.barBottom) return p;
    const Rect moved{ open.x, bottom ? bar.y + barH - open.h : bar.y, open.w, open.h };
    return placeForRect(which, moved, W, H, bottom);
}
// A panel in its default place, with its bar on the right side: the Squad Panel's and the Unit Panel's at their bottom
inline PanelPlace defaultPanelPlace(PanelId which) {
    PanelPlace p;
    p.barBottom = which != PanelId::Info;
    return p;
}

struct UnitPanelLayout {
    bool collapsed = false;                     // only the title bar shows
    Rect toggle{ 0.0f, 0.0f, 0.0f, 0.0f };      // the [-] or [+] button in the bar
    Rect lock{ 0.0f, 0.0f, 0.0f, 0.0f };        // the lock beside it
    Rect panel{ 0.0f, 0.0f, 0.0f, 0.0f };
    Rect title{ 0.0f, 0.0f, 0.0f, 0.0f };       // the "Squad Panel" bar across the top
    Rect paceButton{ 0.0f, 0.0f, 0.0f, 0.0f };  // walk and run toggle, across the top of the panel, on the left
    Rect optimizeAllButton{ 0.0f, 0.0f, 0.0f, 0.0f };   // Optimize All, beside the pace toggle
    // The tabs under the bar: Squad (FACTION_TAB_SQUAD, the units) and Reputation (FACTION_TAB_REPUTATION)
    int tab = 0;
    Rect tabSquad{ 0.0f, 0.0f, 0.0f, 0.0f };
    Rect tabReputation{ 0.0f, 0.0f, 0.0f, 0.0f };
    // The Reputation tab: a heading naming the faction shown, then one row per faction, from repFirst on
    Rect repHeading{ 0.0f, 0.0f, 0.0f, 0.0f };
    std::vector<Rect> repRows;
    int repFirst = 0;                       // the first faction shown, clamped to the list
    int repVisible = 0;                     // how many rows have room
    Rect scrollUp{ 0.0f, 0.0f, 0.0f, 0.0f };    // the button at the top of the scroll bar: up a row
    Rect scrollDown{ 0.0f, 0.0f, 0.0f, 0.0f };  // the button at the bottom of the scroll bar: down a row
    Rect track{ 0.0f, 0.0f, 0.0f, 0.0f };   // scroll bar track, between the two buttons
    Rect thumb{ 0.0f, 0.0f, 0.0f, 0.0f };   // scroll bar thumb, which can be dragged
    std::vector<UnitCell> cells;            // visible units only
    int rowCount = 0;                       // total rows of units
    int rowsVisible = 0;
    int cols = UNITS_PER_ROW;               // units to a row
    int scrollRow = 0;                      // first visible row, clamped to the list

    bool scrollable() const { return rowCount > rowsVisible; }
    int maxScroll() const { return std::max(0, rowCount - rowsVisible); }
};

// The squad panel scrolls very slowly: this many wheel notches move it one row
constexpr float SQUAD_WHEEL_NOTCHES_PER_ROW = 4.0f;
// The Faction Panel's tabs
constexpr int FACTION_TAB_SQUAD = 0;
constexpr int FACTION_TAB_REPUTATION = 1;

// The Faction Panel (the panel once called the Squad Panel): two tabs under its bar. The Squad tab has the walk and run
// toggle and Optimize All across the top, then a grid of portraits with names, scrolled a row at a time; its scroll
// bar has a button at each end that moves it a row, and a thumb that can be dragged. The Reputation tab has a heading
// for the faction shown, then a row for each of factionCount factions, from repScroll on.
inline UnitPanelLayout layoutUnitPanel(int W, int H, int unitCount, int scrollRow, bool collapsed = false,
                                       const PanelPlace& place = PanelPlace{}, int tab = FACTION_TAB_SQUAD,
                                       int factionCount = 0, int repScroll = 0) {
    const float s = H / 720.0f;
    const SquadMetrics m(s);
    const Rect open = panelRect(PanelId::Squad, place, W, H);
    // As many rows as the height has room for, and as many columns as the width; at least one of each
    // (a hair over, so a size made for exactly n rows or columns is never read as n - 1)
    const int rowsVisible = std::max(1, (int)std::floor((open.h - m.chromeH() + m.gap) / (m.cell + m.label + m.gap) + 0.001f));
    const int cols = std::max(1, (int)std::floor((open.w - 2.0f * m.pad - m.scrollW + m.gap) / (m.cell + m.gap) + 0.001f));

    UnitPanelLayout L;
    L.rowsVisible = rowsVisible;
    L.cols = cols;
    L.rowCount = (unitCount + cols - 1) / cols;
    L.scrollRow = std::clamp(scrollRow, 0, L.maxScroll());

    L.panel = panelShownRect(open, collapsed, place.barBottom, m.barH);
    L.title = panelBarRect(open, place.barBottom, m.barH);
    L.toggle = BarToggle(L.title, s);
    L.lock = BarLock(L.title, s);
    L.collapsed = collapsed;
    if (collapsed) return L;   // only the bar shows, so nothing in the body can be pressed

    // The body is the panel less its bar, which runs along its top or its bottom. The tabs run across its top.
    const Rect body = panelBodyRect(open, place.barBottom, m.barH);
    L.tab = tab == FACTION_TAB_REPUTATION ? FACTION_TAB_REPUTATION : FACTION_TAB_SQUAD;
    {
        const float tw = (open.w - 2.0f * m.pad - m.gap) * 0.5f;
        L.tabSquad = Rect{ open.x + m.pad, body.y + 2.0f * s, tw, m.tabH - 4.0f * s };
        L.tabReputation = Rect{ L.tabSquad.x + tw + m.gap, body.y + 2.0f * s, tw, m.tabH - 4.0f * s };
    }
    const float bodyY = body.y + m.tabH;
    if (L.tab == FACTION_TAB_REPUTATION) {
        // A heading, then a row per faction; the rows that have no room are scrolled to with the wheel
        const float rowH = 24.0f * s, rowGap = 2.0f * s;
        L.repHeading = Rect{ open.x + m.pad, bodyY + m.pad * 0.5f, open.w - 2.0f * m.pad, rowH };
        const float listTop = L.repHeading.y + rowH + rowGap;
        const float room = body.y + body.h - m.pad * 0.5f - listTop;
        L.repVisible = std::max(1, (int)std::floor((room + rowGap) / (rowH + rowGap) + 0.001f));
        L.repFirst = std::clamp(repScroll, 0, std::max(0, factionCount - L.repVisible));
        for (int i = L.repFirst; i < factionCount && i < L.repFirst + L.repVisible; ++i) {
            L.repRows.push_back(Rect{ open.x + m.pad, listTop + (i - L.repFirst) * (rowH + rowGap), open.w - 2.0f * m.pad, rowH });
        }
        return L;   // the units' scroll stays as it was, for when the Squad tab is back
    }
    // The top row holds the pace toggle and Optimize All, half each. The grid starts under it.
    const float rowW = open.w - 2.0f * m.pad;
    const float halfW = (rowW - m.gap) * 0.5f;
    L.paceButton = Rect{ open.x + m.pad, bodyY + m.pad, halfW, m.toggleH };
    L.optimizeAllButton = Rect{ L.paceButton.x + halfW + m.gap, bodyY + m.pad, halfW, m.toggleH };
    const float top = bodyY + m.pad + m.toggleH + m.gap;

    for (int r = 0; r < rowsVisible; ++r) {
        for (int c = 0; c < cols; ++c) {
            const int idx = (L.scrollRow + r) * cols + c;
            if (idx >= unitCount) break;
            const float x = open.x + m.pad + c * (m.cell + m.gap);
            const float y = top + r * (m.cell + m.label + m.gap);
            L.cells.push_back(UnitCell{ idx, Rect{ x, y, m.cell, m.cell + m.label } });
        }
    }

    // The scroll bar runs down the right side beside the grid, never past the bottom of the panel: a button at each
    // end, and the track with its thumb between them
    const float barX = open.x + open.w - m.pad - m.scrollW;
    const float barH = std::max(0.0f, std::min(m.rowsH(rowsVisible), body.y + body.h - m.pad - top));
    const float btn = std::min(m.scrollW, barH * 0.5f);
    L.scrollUp = Rect{ barX, top, m.scrollW, btn };
    L.scrollDown = Rect{ barX, top + barH - btn, m.scrollW, btn };
    L.track = Rect{ barX, top + btn, m.scrollW, std::max(0.0f, barH - 2.0f * btn) };
    if (!L.scrollable()) {
        L.thumb = L.track;
    } else {
        const float th = std::max(std::min(8.0f * s, L.track.h), L.track.h * rowsVisible / L.rowCount);
        const float ty = L.track.y + (L.track.h - th) * L.scrollRow / L.maxScroll();
        L.thumb = Rect{ L.track.x, ty, L.track.w, th };
    }
    if (barH <= 0.0f) L.track = L.thumb = L.scrollUp = L.scrollDown = Rect{};   // no room for a scroll bar at all
    return L;
}

// The row the Squad Panel's thumb shows at when its top is at thumbTop (dragging the thumb)
inline int squadRowAtThumb(const UnitPanelLayout& L, float thumbTop) {
    const float room = L.track.h - L.thumb.h;
    if (!L.scrollable() || room <= 0.0f) return 0;
    const float f = std::clamp((thumbTop - L.track.y) / room, 0.0f, 1.0f);
    return (int)std::lround(f * L.maxScroll());
}

// The talk box: centred. It shows the last lines said, one row each, then one button per option, then End Dialogue at
// the bottom. Its height follows how many lines and options there are.
struct DialogueLayout {
    Rect box{ 0.0f, 0.0f, 0.0f, 0.0f };
    std::vector<Rect> lines;                 // one row per line shown, oldest first
    std::vector<Rect> options;               // one button per option
    Rect end{ 0.0f, 0.0f, 0.0f, 0.0f };      // End Dialogue
};
inline DialogueLayout layoutDialogue(int W, int H, int lineCount, int optionCount) {
    const float s = H / 720.0f;
    const float w = 520.0f * s, pad = 12.0f * s, rowH = 66.0f * s, gap = 6.0f * s, btnH = 30.0f * s;
    const float h = pad + lineCount * (rowH + gap) + optionCount * (btnH + gap) + btnH + pad;
    DialogueLayout L;
    L.box = Rect{ (W - w) * 0.5f, (H - h) * 0.5f, w, h };
    float y = L.box.y + pad;
    for (int i = 0; i < lineCount; ++i) {
        L.lines.push_back(Rect{ L.box.x + pad, y, w - 2.0f * pad, rowH });
        y += rowH + gap;
    }
    for (int i = 0; i < optionCount; ++i) {
        L.options.push_back(Rect{ L.box.x + pad, y, w - 2.0f * pad, btnH });
        y += btnH + gap;
    }
    L.end = Rect{ L.box.x + pad, y, w - 2.0f * pad, btnH };
    return L;
}

// The trade popout: the Interaction Panel with the NPC's inventory, and the Trade Panel beside it. Each side of the
// trade has a box of TRADE_SLOTS cells, its total under it, a value bar, a status line, and Cancel and Trade.
constexpr int TRADE_SLOTS = 12;
struct TradeLayout {
    Rect interaction{ 0.0f, 0.0f, 0.0f, 0.0f };
    Rect interBar{ 0.0f, 0.0f, 0.0f, 0.0f };
    Rect npcGrid{ 0.0f, 0.0f, 0.0f, 0.0f };
    std::vector<Rect> npcSlots;                   // one cell per inventory slot of the NPC
    Rect tradePanel{ 0.0f, 0.0f, 0.0f, 0.0f };
    Rect tradeBar{ 0.0f, 0.0f, 0.0f, 0.0f };
    Rect theirName{ 0.0f, 0.0f, 0.0f, 0.0f };    // the other unit's name, over its side
    Rect yourName{ 0.0f, 0.0f, 0.0f, 0.0f };     // your unit's name, over your side
    Rect theirBox{ 0.0f, 0.0f, 0.0f, 0.0f };     // the NPC's side
    Rect yourBox{ 0.0f, 0.0f, 0.0f, 0.0f };      // your side
    std::vector<Rect> theirCells, yourCells;
    Rect theirTotal{ 0.0f, 0.0f, 0.0f, 0.0f };
    Rect yourTotal{ 0.0f, 0.0f, 0.0f, 0.0f };
    Rect valueBar{ 0.0f, 0.0f, 0.0f, 0.0f };     // how close the offer is to the NPC's price
    Rect statusLine{ 0.0f, 0.0f, 0.0f, 0.0f };   // why the trade can't be confirmed, or that it can
    Rect cancelBtn{ 0.0f, 0.0f, 0.0f, 0.0f };
    Rect tradeBtn{ 0.0f, 0.0f, 0.0f, 0.0f };
};
// top: how far down the window something else is already drawn across the top (the pause bar, which shows during a
// trade); the panels start under it
inline TradeLayout layoutTrade(int W, int H, float top = 0.0f) {
    const float s = H / 720.0f;
    const float pad = 10.0f * s, bar = 22.0f * s, gap = 12.0f * s, line = 20.0f * s, btnH = 28.0f * s, btnW = 110.0f * s;
    const float barH = 12.0f * s;
    const float cell = ITEM_CELL_PX, cellGap = 2.0f * s;
    const int cols = INVENTORY_COLUMNS, rows = INVENTORY_SLOTS / cols;
    const float gridW = cols * cell + (cols - 1) * cellGap;
    const float gridH = rows * cell + (rows - 1) * cellGap;
    const float interW = gridW + 2.0f * pad;
    const float interH = bar + pad + 2.0f * line + gridH + 8.0f * s + pad;

    const float cellT = ITEM_CELL_PX, cellTGap = 4.0f * s;
    const int tcols = 4, trows = TRADE_SLOTS / tcols;
    const float boxW = tcols * cellT + (tcols - 1) * cellTGap + 2.0f * pad;
    const float boxH = trows * cellT + (trows - 1) * cellTGap + 2.0f * pad;
    const float tradeW = 2.0f * boxW + 3.0f * pad;
    const float tradeH = bar + pad + line + boxH + 2.0f * s + line + 4.0f * s + barH + 4.0f * s + line + gap + btnH + pad;

    // The two panels side by side in the top left corner, under the pause bar (pulled left past the margin if the window
    // is too narrow, and up if it is too short)
    const float totalW = interW + gap + tradeW;
    const float margin = PANEL_MARGIN * s;
    const float x0 = std::max(0.0f, std::min(margin, W - totalW));
    const float y0 = std::max(0.0f, std::min(top + margin, H - std::max(interH, tradeH)));

    TradeLayout L;
    L.interaction = Rect{ x0, y0, interW, interH };
    L.interBar = Rect{ x0, y0, interW, bar };
    L.npcGrid = Rect{ x0 + pad, y0 + bar + pad + 2.0f * line, gridW, gridH + 8.0f * s };
    const float npcTop = L.npcGrid.y + 4.0f * s;
    for (int i = 0; i < INVENTORY_SLOTS; ++i) {
        const int col = i % cols, row = i / cols;
        L.npcSlots.push_back(Rect{ L.npcGrid.x + col * (cell + cellGap), npcTop + row * (cell + cellGap), cell, cell });
    }

    const float tx = x0 + interW + gap;
    L.tradePanel = Rect{ tx, y0, tradeW, tradeH };
    L.tradeBar = Rect{ tx, y0, tradeW, bar };
    // Each side's unit name over its box
    L.theirName = Rect{ tx + pad, y0 + bar + pad, boxW, line };
    L.yourName = Rect{ tx + 2.0f * pad + boxW, y0 + bar + pad, boxW, line };
    const float boxY = y0 + bar + pad + line;
    L.theirBox = Rect{ tx + pad, boxY, boxW, boxH };
    L.yourBox = Rect{ tx + 2.0f * pad + boxW, boxY, boxW, boxH };
    for (int i = 0; i < TRADE_SLOTS; ++i) {
        const int col = i % tcols, row = i / tcols;
        const float dx = col * (cellT + cellTGap), dy = row * (cellT + cellTGap);
        L.theirCells.push_back(Rect{ L.theirBox.x + pad + dx, L.theirBox.y + pad + dy, cellT, cellT });
        L.yourCells.push_back(Rect{ L.yourBox.x + pad + dx, L.yourBox.y + pad + dy, cellT, cellT });
    }
    const float totalY = boxY + boxH + 2.0f * s;
    L.theirTotal = Rect{ L.theirBox.x, totalY, boxW, line };
    L.yourTotal = Rect{ L.yourBox.x, totalY, boxW, line };
    const float barY = totalY + line + 4.0f * s;
    L.valueBar = Rect{ tx + pad, barY, tradeW - 2.0f * pad, barH };
    L.statusLine = Rect{ tx + pad, barY + barH + 4.0f * s, tradeW - 2.0f * pad, line };
    const float btnY = y0 + tradeH - pad - btnH;
    L.cancelBtn = Rect{ tx + pad, btnY, btnW, btnH };
    L.tradeBtn = Rect{ tx + tradeW - pad - btnW, btnY, btnW, btnH };
    return L;
}

// The scenario page, between New Game and the party menu: the picked scenario's name and description at the top, and
// a list of the scenarios at the bottom, which scrolls when it has more than fit, with Back and Continue under it
struct ScenarioLayout {
    Rect box{ 0.0f, 0.0f, 0.0f, 0.0f };
    Rect heading{ 0.0f, 0.0f, 0.0f, 0.0f };   // "Scenario", over everything
    Rect name{ 0.0f, 0.0f, 0.0f, 0.0f };      // the picked scenario's name
    Rect desc{ 0.0f, 0.0f, 0.0f, 0.0f };      // and its description, wrapped
    Rect list{ 0.0f, 0.0f, 0.0f, 0.0f };      // where the rows are
    std::vector<Rect> rows;                   // the rows shown, for scenarios first .. first + rows.size() - 1
    int first = 0;                            // the scroll, kept in range
    int visible = 0;                          // how many rows fit
    Rect track{ 0.0f, 0.0f, 0.0f, 0.0f };     // the scroll bar, when the list scrolls
    Rect thumb{ 0.0f, 0.0f, 0.0f, 0.0f };
    Rect back{ 0.0f, 0.0f, 0.0f, 0.0f };
    Rect next{ 0.0f, 0.0f, 0.0f, 0.0f };
};
inline ScenarioLayout layoutScenario(int W, int H, int count, int scroll) {
    const float s = H / 720.0f;
    const float pad = 20.0f * s, rowH = 38.0f * s, rowGap = 6.0f * s, btnH = 36.0f * s, btnW = 170.0f * s;
    const float scrollW = 8.0f * s;
    const float boxW = std::min((float)W - 48.0f * s, 760.0f * s), boxH = (float)H - 64.0f * s;
    const float x0 = (W - boxW) * 0.5f, y0 = 32.0f * s;
    ScenarioLayout L;
    L.box = Rect{ x0, y0, boxW, boxH };
    L.heading = Rect{ x0 + pad, y0 + pad, boxW - 2.0f * pad, 26.0f * s };
    L.name = Rect{ x0 + pad, L.heading.y + L.heading.h + 10.0f * s, boxW - 2.0f * pad, 34.0f * s };
    L.desc = Rect{ x0 + pad, L.name.y + L.name.h + 8.0f * s, boxW - 2.0f * pad, 130.0f * s };
    L.back = Rect{ x0 + pad, y0 + boxH - pad - btnH, btnW, btnH };
    L.next = Rect{ x0 + boxW - pad - btnW, y0 + boxH - pad - btnH, btnW, btnH };
    const float listTop = L.desc.y + L.desc.h + 14.0f * s;
    const float listBottom = L.back.y - 16.0f * s;
    L.list = Rect{ x0 + pad, listTop, boxW - 2.0f * pad, std::max(0.0f, listBottom - listTop) };
    L.visible = std::max(1, (int)std::floor((L.list.h + rowGap) / (rowH + rowGap)));
    L.first = std::clamp(scroll, 0, std::max(0, count - L.visible));
    const bool scrolls = count > L.visible;
    const float rowW = L.list.w - (scrolls ? scrollW + 8.0f * s : 0.0f);
    for (int i = L.first; i < count && i < L.first + L.visible; ++i) {
        L.rows.push_back(Rect{ L.list.x, listTop + (i - L.first) * (rowH + rowGap), rowW, rowH });
    }
    if (scrolls) {
        const float shownH = L.visible * (rowH + rowGap) - rowGap;
        L.track = Rect{ L.list.x + L.list.w - scrollW, listTop, scrollW, shownH };
        const float th = std::max(16.0f * s, shownH * L.visible / count);
        const int maxFirst = count - L.visible;
        L.thumb = Rect{ L.track.x, L.track.y + (shownH - th) * L.first / std::max(1, maxFirst), scrollW, th };
    }
    return L;
}

// The storage panel, for a storage prop (a barrel) or a crafting prop (a campfire): an Interaction Panel with the
// prop's name, its inventory, a hint and a Close button, in the top middle of the window, clear of the bottom panels
struct StorageLayout {
    Rect panel{ 0.0f, 0.0f, 0.0f, 0.0f };
    Rect bar{ 0.0f, 0.0f, 0.0f, 0.0f };
    Rect name{ 0.0f, 0.0f, 0.0f, 0.0f };      // the prop's name
    Rect heading{ 0.0f, 0.0f, 0.0f, 0.0f };   // "Inventory", or "Input" over a forge's input
    Rect grid{ 0.0f, 0.0f, 0.0f, 0.0f };
    std::vector<Rect> slots;                  // STORAGE_SLOTS cells, row by row
    // A passive crafting prop's output (a forge's): its heading, grid and STORAGE_SLOTS cells. Empty for other props.
    bool output = false;
    Rect outHeading{ 0.0f, 0.0f, 0.0f, 0.0f };
    Rect outGrid{ 0.0f, 0.0f, 0.0f, 0.0f };
    std::vector<Rect> outSlots;
    Rect status{ 0.0f, 0.0f, 0.0f, 0.0f };    // a forge's fuel line; zero height for other props
    Rect hint{ 0.0f, 0.0f, 0.0f, 0.0f };      // how to take, drop and store items, wrapped
    Rect closeBtn{ 0.0f, 0.0f, 0.0f, 0.0f };
    // The owner's lock toggle, bottom left, beside Close: shown for a prop your faction owns, when lockToggle is set
    bool lockToggle = false;
    Rect lockBtn{ 0.0f, 0.0f, 0.0f, 0.0f };
};
// The panel for looting an Unconscious unit or a corpse: its gear in one row, then its inventory, 8 cells across, each
// cell ITEM_CELL_PX. Then a hint, and Take All and Close.
struct LootLayout {
    Rect panel{ 0.0f, 0.0f, 0.0f, 0.0f };
    Rect bar{ 0.0f, 0.0f, 0.0f, 0.0f };
    Rect name{ 0.0f, 0.0f, 0.0f, 0.0f };
    Rect gearHeading{ 0.0f, 0.0f, 0.0f, 0.0f };
    Rect gearGrid{ 0.0f, 0.0f, 0.0f, 0.0f };
    std::vector<Rect> gear;    // GEAR_SLOTS cells
    Rect invHeading{ 0.0f, 0.0f, 0.0f, 0.0f };
    Rect invGrid{ 0.0f, 0.0f, 0.0f, 0.0f };
    std::vector<Rect> slots;   // INVENTORY_SLOTS cells
    Rect hint{ 0.0f, 0.0f, 0.0f, 0.0f };
    Rect takeAllBtn{ 0.0f, 0.0f, 0.0f, 0.0f };
    Rect closeBtn{ 0.0f, 0.0f, 0.0f, 0.0f };
};
inline LootLayout layoutLoot(int W, int H) {
    const float s = H / 720.0f;
    const float pad = 10.0f * s, bar = 22.0f * s, line = 20.0f * s, gap = 8.0f * s;
    const float btnH = 28.0f * s, btnW = 110.0f * s;
    const float cell = ITEM_CELL_PX, cellGap = 4.0f * s, inset = 4.0f * s;
    const int cols = INVENTORY_COLUMNS;
    const float gridW = cols * cell + (cols - 1) * cellGap + 2.0f * inset;
    const float gearH = cell + 2.0f * inset;
    const int invRows = INVENTORY_SLOTS / cols;
    const float invH = invRows * cell + (invRows - 1) * cellGap + 2.0f * inset;
    const float panelW = gridW + 2.0f * pad;
    const float hintH = 2.0f * line;
    const float panelH = bar + pad + line + line + gearH + gap + line + invH + gap + hintH + gap + btnH + pad;
    const float x0 = std::floor((W - panelW) * 0.5f);
    const float y0 = std::floor(std::max(8.0f * s, (H - panelH) * 0.5f));
    LootLayout L;
    L.panel = Rect{ x0, y0, panelW, panelH };
    L.bar = Rect{ x0, y0, panelW, bar };
    L.name = Rect{ x0 + pad, y0 + bar + pad, panelW - 2.0f * pad, line };
    L.gearHeading = Rect{ x0 + pad, L.name.y + line, panelW - 2.0f * pad, line };
    L.gearGrid = Rect{ x0 + pad, L.gearHeading.y + line, gridW, gearH };
    for (int i = 0; i < GEAR_SLOTS; ++i) {
        L.gear.push_back(Rect{ L.gearGrid.x + inset + i * (cell + cellGap), L.gearGrid.y + inset, cell, cell });
    }
    L.invHeading = Rect{ x0 + pad, L.gearGrid.y + gearH + gap, panelW - 2.0f * pad, line };
    L.invGrid = Rect{ x0 + pad, L.invHeading.y + line, gridW, invH };
    for (int i = 0; i < INVENTORY_SLOTS; ++i) {
        const int c = i % cols, r = i / cols;
        L.slots.push_back(Rect{ L.invGrid.x + inset + c * (cell + cellGap), L.invGrid.y + inset + r * (cell + cellGap), cell, cell });
    }
    L.hint = Rect{ x0 + pad, L.invGrid.y + invH + gap, panelW - 2.0f * pad, hintH };
    const float btnY = y0 + panelH - pad - btnH;
    L.takeAllBtn = Rect{ x0 + pad, btnY, btnW * 1.3f, btnH };
    L.closeBtn = Rect{ x0 + panelW - pad - btnW, btnY, btnW, btnH };
    return L;
}

// The storage panel. With output set (a passive crafting prop) it has a second grid, for the output, and a status line.
inline StorageLayout layoutStorage(int W, int H, bool output = false, bool lockToggle = false) {
    const float s = H / 720.0f;
    const float pad = 10.0f * s, bar = 22.0f * s, line = 20.0f * s, gap = 8.0f * s;
    const float btnH = 28.0f * s, btnW = 110.0f * s;
    const float cell = ITEM_CELL_PX, cellGap = 4.0f * s, inset = 4.0f * s;
    const int cols = STORAGE_COLUMNS, rows = (STORAGE_SLOTS + STORAGE_COLUMNS - 1) / STORAGE_COLUMNS;
    const float gridW = cols * cell + (cols - 1) * cellGap + 2.0f * inset;
    const float gridH = rows * cell + (rows - 1) * cellGap + 2.0f * inset;
    const float panelW = std::max(gridW, 260.0f * s) + 2.0f * pad;
    const float hintH = 6.0f * line;   // room for the longest hint: a filtered storage's or a forge's
    const float outH = output ? line + gridH + line : 0.0f;   // output heading, grid and status line
    const float panelH = bar + pad + 2.0f * line + gridH + outH + gap + hintH + gap + btnH + pad;
    const float x0 = (W - panelW) * 0.5f;
    const float y0 = std::max(8.0f * s, std::min(H * 0.14f, H - panelH - 8.0f * s));

    StorageLayout L;
    L.panel = Rect{ x0, y0, panelW, panelH };
    L.bar = Rect{ x0, y0, panelW, bar };
    L.name = Rect{ x0 + pad, y0 + bar + pad, panelW - 2.0f * pad, line };
    L.heading = Rect{ x0 + pad, L.name.y + line, panelW - 2.0f * pad, line };
    L.grid = Rect{ x0 + (panelW - gridW) * 0.5f, L.heading.y + line, gridW, gridH };
    auto cells = [&](const Rect& grid, std::vector<Rect>& out) {
        for (int i = 0; i < STORAGE_SLOTS; ++i) {
            const int col = i % cols, row = i / cols;
            out.push_back(Rect{ grid.x + inset + col * (cell + cellGap), grid.y + inset + row * (cell + cellGap), cell, cell });
        }
    };
    cells(L.grid, L.slots);
    float below = L.grid.y + gridH;
    L.output = output;
    if (output) {
        L.outHeading = Rect{ x0 + pad, below, panelW - 2.0f * pad, line };
        L.outGrid = Rect{ L.grid.x, below + line, gridW, gridH };
        cells(L.outGrid, L.outSlots);
        L.status = Rect{ x0 + pad, L.outGrid.y + gridH, panelW - 2.0f * pad, line };
        below = L.status.y + line;
    }
    L.hint = Rect{ x0 + pad, below + gap, panelW - 2.0f * pad, hintH };
    L.closeBtn = Rect{ x0 + (panelW - btnW) * 0.5f, y0 + panelH - pad - btnH, btnW, btnH };
    L.lockToggle = lockToggle;
    if (lockToggle) {
        // With the toggle, Close moves to the right so the two sit side by side at the bottom
        L.lockBtn = Rect{ x0 + pad, y0 + panelH - pad - btnH, btnW, btnH };
        L.closeBtn.x = x0 + panelW - pad - btnW;
    }
    return L;
}

// Whether a trade can be confirmed: why not, if it can't, and how close the offer is to the price (0 to 1)
struct TradeStatus {
    bool ok = false;
    std::string text;
    float progress = 0.0f;
};

// The trade fee for a Charisma level, in percent: 100 at level 1, 1 at level 99, a straight line between
inline float tradeFeePercent(int charisma) {
    const float t = (float)(std::clamp(charisma, 1, MAX_LEVEL) - 1) / (float)(MAX_LEVEL - 1);
    return 100.0f - 99.0f * t;
}

// What an NPC asks for its items: their value plus the trade fee, rounded up to a whole number
inline int requiredOffer(int npcValue, int charisma) {
    const double fee = tradeFeePercent(charisma);
    return (int)std::ceil(npcValue * (100.0 + fee) / 100.0 - 1e-9);
}

// The sell value of every item in one side of a trade, summed
inline int tradeValueOf(const std::vector<TradeGhost>& ghosts) {
    int total = 0;
    for (const TradeGhost& g : ghosts) {
        const ItemDef* d = findItem(g.stack.item);
        if (d) total += d->sellValue * g.stack.count;
    }
    return total;
}

// The quantity prompt for a trade: how many of a stack's item go in the trade. It opens over the trade popout.
struct QuantityLayout {
    Rect box{ 0.0f, 0.0f, 0.0f, 0.0f };
    Rect title{ 0.0f, 0.0f, 0.0f, 0.0f };
    Rect countBox{ 0.0f, 0.0f, 0.0f, 0.0f };
    Rect minus100{ 0.0f, 0.0f, 0.0f, 0.0f }, minus10{ 0.0f, 0.0f, 0.0f, 0.0f }, minus1{ 0.0f, 0.0f, 0.0f, 0.0f };
    Rect plus1{ 0.0f, 0.0f, 0.0f, 0.0f }, plus10{ 0.0f, 0.0f, 0.0f, 0.0f }, plus100{ 0.0f, 0.0f, 0.0f, 0.0f };
    Rect single{ 0.0f, 0.0f, 0.0f, 0.0f }, max{ 0.0f, 0.0f, 0.0f, 0.0f };
    Rect confirm{ 0.0f, 0.0f, 0.0f, 0.0f }, cancel{ 0.0f, 0.0f, 0.0f, 0.0f };
};
// The title screen, top left: the game's logo, a pink rule, and the notice board of update notes. Along the bottom:
// the build stamp on the left and the keys on the right.
struct TitleLayout {
    float side = 0.0f;                          // the screen's left and right margin
    Rect name{ 0.0f, 0.0f, 0.0f, 0.0f };        // the name's line: its height is the size the name is drawn at
    Rect rule{ 0.0f, 0.0f, 0.0f, 0.0f };
    Rect news{ 0.0f, 0.0f, 0.0f, 0.0f };        // the notice board, frame included
    Rect newsText{ 0.0f, 0.0f, 0.0f, 0.0f };    // inside the board's padding
    Rect footer{ 0.0f, 0.0f, 0.0f, 0.0f };      // the line along the bottom
};
inline float titleClamp(float lo, float v, float hi) { return std::max(lo, std::min(v, hi)); }
inline TitleLayout layoutTitle(int W, int H) {
    const float s = H / 720.0f;
    TitleLayout L;
    L.side = titleClamp(24.0f * s, W * 0.05f, 90.0f * s);
    const float top = titleClamp(26.0f * s, H * 0.07f, 80.0f * s);
    const float gap = titleClamp(18.0f * s, H * 0.034f, 38.0f * s);
    // Tall enough for the logo (data/illustration/title.png, 1280x600), which is drawn as large as this line allows
    const float nameH = titleClamp(120.0f * s, std::min(H * 0.36f, (W * 0.56f - L.side) * (600.0f / 1280.0f)), 300.0f * s);
    const float boardW = titleClamp(280.0f * s, W * 0.34f, 470.0f * s);
    const float footerH = 18.0f * s, footerPad = titleClamp(16.0f * s, H * 0.03f, 30.0f * s);
    L.footer = Rect{ L.side, H - footerPad - footerH, W - 2.0f * L.side, footerH };
    L.name = Rect{ L.side, top, W * 0.56f - L.side, nameH };
    L.rule = Rect{ L.side, L.name.y + nameH + gap * 0.45f, boardW, std::max(1.0f, 2.0f * s) };
    const float boardY = L.rule.y + L.rule.h + gap;
    const float boardH = std::max(160.0f * s, L.footer.y - 28.0f * s - boardY);
    L.news = Rect{ L.side, boardY, boardW, boardH };
    const float padX = titleClamp(16.0f * s, W * 0.02f, 26.0f * s), padY = titleClamp(14.0f * s, W * 0.017f, 22.0f * s);
    L.newsText = Rect{ L.news.x + 3.0f * s + padX, L.news.y + 3.0f * s + padY, L.news.w - 6.0f * s - 2.0f * padX,
                       L.news.h - 6.0f * s - 2.0f * padY };
    return L;
}

// The Optimize All dialog: a title, a line saying what it will do, the Attack and Defense slider, three toggles, and
// Confirm and Cancel
struct OptimizeAllLayout {
    Rect box{ 0.0f, 0.0f, 0.0f, 0.0f };
    Rect title{ 0.0f, 0.0f, 0.0f, 0.0f };
    Rect text{ 0.0f, 0.0f, 0.0f, 0.0f };          // wrapped, up to five lines
    Rect sliderLabel{ 0.0f, 0.0f, 0.0f, 0.0f };   // "Attack 60% / Defense 40%"
    Rect track{ 0.0f, 0.0f, 0.0f, 0.0f };         // the slider: left is all defense, right all attack
    std::array<Rect, 3> toggles{};                // accessories, two-handed weapons, value on ties: the box and its label
    Rect confirm{ 0.0f, 0.0f, 0.0f, 0.0f }, cancel{ 0.0f, 0.0f, 0.0f, 0.0f };
};
inline OptimizeAllLayout layoutOptimizeAll(int W, int H) {
    const float s = H / 720.0f;
    // line: one line of the list font at its usual size (20 points), with a little room
    const float pad = 16.0f * s, gap = 8.0f * s, line = 28.0f * s, btnH = 32.0f * s, btnW = 160.0f * s;
    const float boxW = 560.0f * s;
    const float boxH = pad + line + gap + 5.0f * line + gap + line + 16.0f * s + gap + 3.0f * (line + 4.0f * s) + gap + btnH + pad;
    OptimizeAllLayout L;
    L.box = Rect{ (W - boxW) * 0.5f, (H - boxH) * 0.5f, boxW, boxH };
    const float x = L.box.x + pad, w = boxW - 2.0f * pad;
    float y = L.box.y + pad;
    L.title = Rect{ x, y, w, line };
    y += line + gap;
    L.text = Rect{ x, y, w, 5.0f * line };
    y += 5.0f * line + gap;
    L.sliderLabel = Rect{ x, y, w, line };
    y += line;
    L.track = Rect{ x + 6.0f * s, y + 4.0f * s, w - 12.0f * s, 8.0f * s };
    y += 16.0f * s + gap;
    for (Rect& t : L.toggles) {
        t = Rect{ x, y, w, line };
        y += line + 4.0f * s;
    }
    y += gap;
    L.confirm = Rect{ L.box.x + boxW * 0.5f - gap * 0.5f - btnW, y, btnW, btnH };
    L.cancel = Rect{ L.box.x + boxW * 0.5f + gap * 0.5f, y, btnW, btnH };
    return L;
}

// The Disband popout, from Disband on a squad unit's right-click menu: the title, the unit's portrait with its name and
// a few lines about it beside it, what disbanding does, the hint to hold, and Disband and Cancel. Disband must be held
// for DISBAND_HOLD_MS to confirm; its bar fills as it is held, and letting go starts it over.
constexpr unsigned long long DISBAND_HOLD_MS = 5000;
struct DisbandLayout {
    Rect box{ 0.0f, 0.0f, 0.0f, 0.0f };
    Rect title{ 0.0f, 0.0f, 0.0f, 0.0f };
    Rect portrait{ 0.0f, 0.0f, 0.0f, 0.0f };
    Rect info{ 0.0f, 0.0f, 0.0f, 0.0f };   // beside the portrait: the name, then DISBAND_INFO_LINES lines
    Rect text{ 0.0f, 0.0f, 0.0f, 0.0f };   // what disbanding does, wrapped, up to three lines
    Rect hint{ 0.0f, 0.0f, 0.0f, 0.0f };
    Rect disband{ 0.0f, 0.0f, 0.0f, 0.0f }, cancel{ 0.0f, 0.0f, 0.0f, 0.0f };
};
constexpr int DISBAND_INFO_LINES = 3;
inline DisbandLayout layoutDisband(int W, int H) {
    const float s = H / 720.0f;
    const float pad = 16.0f * s, gap = 8.0f * s, line = 28.0f * s, btnH = 32.0f * s, btnW = 160.0f * s;
    const float boxW = 500.0f * s, pic = (1.0f + DISBAND_INFO_LINES) * line;
    const float boxH = pad + line + gap + pic + gap + 3.0f * line + gap + line + gap + btnH + pad;
    DisbandLayout L;
    L.box = Rect{ (W - boxW) * 0.5f, (H - boxH) * 0.5f, boxW, boxH };
    const float x = L.box.x + pad, w = boxW - 2.0f * pad;
    float y = L.box.y + pad;
    L.title = Rect{ x, y, w, line };
    y += line + gap;
    L.portrait = Rect{ x, y, pic, pic };
    L.info = Rect{ x + pic + 12.0f * s, y, w - pic - 12.0f * s, pic };
    y += pic + gap;
    L.text = Rect{ x, y, w, 3.0f * line };
    y += 3.0f * line + gap;
    L.hint = Rect{ x, y, w, line };
    y += line + gap;
    L.disband = Rect{ L.box.x + boxW * 0.5f - gap * 0.5f - btnW, y, btnW, btnH };
    L.cancel = Rect{ L.box.x + boxW * 0.5f + gap * 0.5f, y, btnW, btnH };
    return L;
}

inline QuantityLayout layoutQuantity(int W, int H) {
    const float s = H / 720.0f;
    const float pad = 12.0f * s, gap = 6.0f * s, line = 22.0f * s, btnH = 28.0f * s;
    const float smallW = 58.0f * s, countW = 96.0f * s, wideW = 110.0f * s;
    const float rowW = 6.0f * smallW + countW + 6.0f * gap;
    const float boxW = rowW + 2.0f * pad;
    const float boxH = pad + line + gap + btnH + gap + btnH + gap + btnH + pad;
    QuantityLayout L;
    L.box = Rect{ (W - boxW) * 0.5f, (H - boxH) * 0.5f, boxW, boxH };
    float y = L.box.y + pad;
    L.title = Rect{ L.box.x + pad, y, boxW - 2.0f * pad, line };
    y += line + gap;
    float x = L.box.x + pad;
    auto place = [&](Rect& r, float w) { r = Rect{ x, y, w, btnH }; x += w + gap; };
    place(L.minus100, smallW);
    place(L.minus10, smallW);
    place(L.minus1, smallW);
    L.countBox = Rect{ x, y, countW, btnH };
    x += countW + gap;
    place(L.plus1, smallW);
    place(L.plus10, smallW);
    place(L.plus100, smallW);
    y += btnH + gap;
    const float row2W = 2.0f * wideW + gap;
    const float x2 = L.box.x + (boxW - row2W) * 0.5f;
    L.single = Rect{ x2, y, wideW, btnH };
    L.max = Rect{ x2 + wideW + gap, y, wideW, btnH };
    y += btnH + gap;
    L.confirm = Rect{ x2, y, wideW, btnH };
    L.cancel = Rect{ x2 + wideW + gap, y, wideW, btnH };
    return L;
}

// The world map window: 800 by 600 pixels, in the middle of the screen until it is dragged by its title bar (less in a
// smaller window, and always kept inside it). The title bar runs across its top with the X that closes it at the right
// end, and the map fills the rest.
constexpr float WORLD_MAP_W = 800.0f;
constexpr float WORLD_MAP_H = 600.0f;
struct WorldMapLayout {
    Rect window{ 0.0f, 0.0f, 0.0f, 0.0f };
    Rect bar{ 0.0f, 0.0f, 0.0f, 0.0f };     // the title bar
    Rect close{ 0.0f, 0.0f, 0.0f, 0.0f };   // the X at the right end of the bar
    Rect map{ 0.0f, 0.0f, 0.0f, 0.0f };     // the map itself
};

// dx, dy: how far the window has been dragged from the middle of the screen
inline WorldMapLayout layoutWorldMap(int W, int H, float dx = 0.0f, float dy = 0.0f) {
    const float s = H / 720.0f;
    const float barH = 22.0f * s, pad = 4.0f * s;
    const float w = std::max(0.0f, std::min(WORLD_MAP_W, (float)W));
    const float h = std::max(0.0f, std::min(WORLD_MAP_H, (float)H));
    WorldMapLayout L;
    const float x = std::clamp(std::floor((W - w) * 0.5f + dx), 0.0f, std::max(0.0f, W - w));
    const float y = std::clamp(std::floor((H - h) * 0.5f + dy), 0.0f, std::max(0.0f, H - h));
    L.window = Rect{ x, y, w, h };
    L.bar = Rect{ x, y, w, barH };
    L.close = BarToggle(L.bar, s);
    const float mapTop = L.bar.y + barH + pad;
    L.map = Rect{ x + pad, mapTop, std::max(0.0f, w - 2.0f * pad), std::max(0.0f, y + h - pad - mapTop) };
    return L;
}

// The world map shows `span` tiles from its top to its bottom, and as many more across as its shape gives, so a tile is
// square. Its centre is the ground point cx, cz; the largest x is at its left and the largest z at its top.
inline float mapTilesPerPixel(const Rect& r, float span) { return r.h > 0.0f ? span / r.h : 0.0f; }
// The tiles across the map from left to right
inline float mapSpanAcross(const Rect& r, float span) { return r.h > 0.0f ? span * r.w / r.h : 0.0f; }

// The ground point under a pixel of the map, false off the map or off the level
inline bool mapGround(const Rect& r, float cx, float cz, float span, float levelSize, float px, float py,
                      float& x, float& z) {
    if (!r.contains(px, py) || span <= 0.0f || r.w <= 0.0f || r.h <= 0.0f) return false;
    const float k = mapTilesPerPixel(r, span);
    x = cx + (r.x + r.w * 0.5f - px) * k;
    z = cz + (r.y + r.h * 0.5f - py) * k;
    return x >= 0.0f && z >= 0.0f && x <= levelSize && z <= levelSize;
}

// The pixel on the map over a ground point
inline std::pair<float, float> mapPixel(const Rect& r, float cx, float cz, float span, float x, float z) {
    const float k = mapTilesPerPixel(r, span);
    if (k <= 0.0f) return { r.x, r.y };
    return { r.x + r.w * 0.5f - (x - cx) / k, r.y + r.h * 0.5f - (z - cz) / k };
}

// The widest view of the map: the whole level fits both ways
inline float mapMaxSpan(const Rect& r, float levelSize) {
    if (r.w <= 0.0f || r.h <= 0.0f) return levelSize;
    return levelSize * std::max(1.0f, r.h / r.w);
}

// Keeps the map's view on the level: no closer in than one chunk, no further out than the whole level, and the centre
// placed so the view does not run off an edge (or centred on the level where the view is wider than it)
inline void clampMapView(const Rect& r, float levelSize, float& cx, float& cz, float& span) {
    span = std::clamp(span, std::min(64.0f, levelSize), mapMaxSpan(r, levelSize));
    auto keep = [&](float& c, float across) {
        if (across >= levelSize) c = levelSize * 0.5f;
        else c = std::clamp(c, across * 0.5f, levelSize - across * 0.5f);
    };
    keep(cx, mapSpanAcross(r, span));
    keep(cz, span);
}

// One row of the Behavior list: the on box, the behavior's name, then its # button (Eat, Heal and Rest only) and the
// priority and delete buttons
struct BehaviorRowLayout {
    Rect row{ 0.0f, 0.0f, 0.0f, 0.0f };             // the whole row, which the hover text is for
    Rect toggle{ 0.0f, 0.0f, 0.0f, 0.0f };          // on or off
    Rect label{ 0.0f, 0.0f, 0.0f, 0.0f };           // what it does, such as Eat when hunger < 50%
    Rect number{ 0.0f, 0.0f, 0.0f, 0.0f };          // [#] asks for the percent Eat, Heal or Rest acts below
    Rect up{ 0.0f, 0.0f, 0.0f, 0.0f };              // [^] moves the behavior up the priority list
    Rect down{ 0.0f, 0.0f, 0.0f, 0.0f };            // [v] moves it down
    Rect remove{ 0.0f, 0.0f, 0.0f, 0.0f };          // [x] deletes it (Add Behavior puts it back)
};

// The parts of a behavior row that a click or hover can land on. Number is the # button, which asks for the percent.
enum class BehaviorPart { None, Toggle, Number, Up, Down, Remove };

// What is open in the Behavior tab besides the rows: the Add Behavior list
struct BehaviorUi {
    int options = -1;        // the entries in the open Add Behavior list, or -1 while it is shut
};

// The prompt the # button opens, in the middle of the window: what it asks, the box the percent is typed in, the
// range it takes, and OK and Cancel. It is modal: nothing else takes clicks or keys while it is up.
struct PercentPromptLayout {
    Rect box{ 0.0f, 0.0f, 0.0f, 0.0f };
    Rect input{ 0.0f, 0.0f, 0.0f, 0.0f };
    Rect ok{ 0.0f, 0.0f, 0.0f, 0.0f };
    Rect cancel{ 0.0f, 0.0f, 0.0f, 0.0f };
    float titleY = 0.0f, rangeY = 0.0f, errorY = 0.0f;   // the text lines' tops
};
inline PercentPromptLayout layoutPercentPrompt(int W, int H) {
    const float s = H / 720.0f;
    PercentPromptLayout P;
    const float w = 340.0f * s, h = 160.0f * s, pad = 14.0f * s, line = 20.0f * s;
    P.box = Rect{ std::floor((W - w) * 0.5f), std::floor((H - h) * 0.5f), w, h };
    P.titleY = P.box.y + pad;
    const float inputW = 90.0f * s, inputH = 28.0f * s;
    P.input = Rect{ P.box.x + (w - inputW) * 0.5f, P.titleY + line + 4.0f * s, inputW, inputH };
    P.rangeY = P.input.y + inputH + 4.0f * s;
    P.errorY = P.rangeY + line - 4.0f * s;
    const float bw = 96.0f * s, bh = 24.0f * s, bgap = 12.0f * s;
    const float by = P.box.y + h - pad - bh;
    P.ok = Rect{ P.box.x + w * 0.5f - bgap * 0.5f - bw, by, bw, bh };
    P.cancel = Rect{ P.box.x + w * 0.5f + bgap * 0.5f, by, bw, bh };
    return P;
}

// Right sidebar for the one unit shown. From the top, the content scrolls as one: a profile card with the
// unit's picture, name, stats and carry bar, then the gear, then the inventory grid of 8 columns of slots,
// then the unit's behavior rules. The sidebar sits on a see-through panel, which does not scroll, and each
// section has a background of its own.
// The Unit Panel's tabs, left to right
// Inventory holds the gear at the top and the inventory grid under it. A unit that is not in your squad (an NPC, or a
// corpse) shows only its Profile: the other tabs are off.
// Build is the fifth tab: the props to pick and place. It is not about a unit, so it is on whatever unit is shown.
enum class UnitTab : int { Inventory = 0, Profile, Stats, Behavior, Build, Count };
constexpr int UNIT_TAB_COUNT = idx(UnitTab::Count);
// Whether a tab of the Unit Panel can be picked for a unit: every tab for one of your squad, only Profile for any other
// unit (an NPC, or a corpse)
inline bool unitTabEnabled(World& w, Entity unit, UnitTab tab) {
    return tab == UnitTab::Profile || tab == UnitTab::Build || unit == NULL_ENTITY || w.has<Commandable>(unit);
}
// The groups the Stats tab lists its stats under
constexpr int STAT_GROUP_COUNT = 5;

struct SidebarLayout {
    Rect panel{ 0.0f, 0.0f, 0.0f, 0.0f };      // the whole Unit Panel, bottom right
    bool open = false;                          // closed: the bar alone. open: the tabs and the picked tab
    UnitTab tab = UnitTab::Inventory;           // the tab shown, when open
    Rect title{ 0.0f, 0.0f, 0.0f, 0.0f };      // the "Unit Panel" bar across the top, which does not scroll
    Rect minus{ 0.0f, 0.0f, 0.0f, 0.0f };      // [-], shown while open; empty otherwise
    Rect plus{ 0.0f, 0.0f, 0.0f, 0.0f };       // [+], shown while closed; empty otherwise
    Rect lock{ 0.0f, 0.0f, 0.0f, 0.0f };       // the lock beside the [-] or [+]
    std::array<Rect, UNIT_TAB_COUNT> tabs{};   // the tab buttons under the bar; empty while closed
    Rect body{ 0.0f, 0.0f, 0.0f, 0.0f };       // the area under the tabs, where the picked tab's contents scroll
    Rect profile{ 0.0f, 0.0f, 0.0f, 0.0f };    // profile card: name, health, hunger and carry
    Rect portrait{ 0.0f, 0.0f, 0.0f, 0.0f };   // the unit's picture, inside the card on the left
    Rect gearPanel{ 0.0f, 0.0f, 0.0f, 0.0f };  // background behind the gear slots and their totals
    std::vector<Rect> gear;                    // GEAR_SLOTS cells in GearSlot order, in one row like the inventory's
    Rect optimizeButton{ 0.0f, 0.0f, 0.0f, 0.0f };   // the Gear tab: wears the best gear on hand (optimizeGear)
    Rect removeAllButton{ 0.0f, 0.0f, 0.0f, 0.0f };  // the Gear tab: takes all gear off (removeAllGear)
    Rect gridPanel{ 0.0f, 0.0f, 0.0f, 0.0f };  // background behind the inventory grid
    Rect sortButton{ 0.0f, 0.0f, 0.0f, 0.0f }; // sorts the grid by item type, top right of its heading
    std::vector<Rect> slots;                   // INVENTORY_SLOTS rectangles, row by row
    Rect statsArea{ 0.0f, 0.0f, 0.0f, 0.0f };  // the stats list, in the Stats tab
    float statRowH = 0.0f;                     // the height of each row in the stats list
    Rect behaviorPanel{ 0.0f, 0.0f, 0.0f, 0.0f };  // background behind the behavior list
    // The modes over the behaviors: Passive, Block, Sneak, then Ranged (shown, not pressed)
    std::array<Rect, 4> modes{};
    std::array<Rect, 2> behaviorHeads{};       // column headings: On, Behavior
    std::vector<BehaviorRowLayout> behaviorRows;   // one per behavior, highest priority first
    Rect behaviorAdd{ 0.0f, 0.0f, 0.0f, 0.0f };     // + Add Behavior, under the rows; it opens and shuts the list
    std::vector<Rect> behaviorOptions;             // the Add Behavior list's entries, while it is open
    std::vector<BehaviorOption> behaviorOptionInfo;   // filled by the caller, for drawing: what each entry adds
    Rect buildHeading{ 0.0f, 0.0f, 0.0f, 0.0f };   // the Build tab's line saying what is picked
    std::vector<Rect> buildRows;               // the Build tab: one row per PropKind, in order. Picking one picks it.
    float scroll = 0.0f;                       // the scroll offset used, in pixels, within 0..maxScroll
    float maxScroll = 0.0f;                    // how far the picked tab can scroll
};

// The Unit Panel, in the bottom right by default. Closed, it is its title bar alone, with [+], at the bottom of where
// the open panel would be. Open, it has its tabs under the bar and the picked tab's contents, which scroll when they are
// taller than the panel. Everything is in window pixels, scaled from a 720-pixel-high window, except the item cells,
// which are always ITEM_CELL_PX.
// profileH: the height the profile card's text needs, from the renderer (profileCardHeight). Under the text is the
// unit's picture, PROFILE_PICTURE_PX on a side whatever the UI scale (smaller only when the panel is too narrow).
inline SidebarLayout layoutInventorySidebar(int W, int H, int ruleCount, float scroll, bool open = false, UnitTab tab = UnitTab::Inventory,
                                            const PanelPlace& place = PanelPlace{}, float profileH = 0.0f,
                                            const BehaviorUi& bui = BehaviorUi{}) {
    const float s = H / 720.0f;
    const UnitPanelMetrics um(s);
    const float pad = um.pad, gap = um.gap;
    const Rect openRect = panelRect(PanelId::Unit, place, W, H);
    const float width = openRect.w;
    const float cardInset = 8.0f * s;
    constexpr float PROFILE_PICTURE_PX = 256.0f;   // the profile pictures' own size, in screen pixels
    const float pictureSide = std::max(0.0f, std::min(PROFILE_PICTURE_PX, width - 2.0f * pad - 2.0f * cardInset));
    const float cardH = profileH + pictureSide + cardInset, headH = um.headH, innerPad = um.innerPad;
    const float barH = um.barH, tabH = um.tabH, tabGap = 3.0f * s;
    const float colHeadH = 16.0f * s, rowH = 24.0f * s, rowGap = 2.0f * s;
    const float buttonW = 18.0f * s, buttonGap = 2.0f * s;
    const float sortW = 56.0f * s, sortH = 22.0f * s, statRowH = 20.0f * s;
    const int rules = std::clamp(ruleCount, 0, BEHAVIOR_LIMIT);
    tab = (UnitTab)std::clamp(idx(tab), 0, UNIT_TAB_COUNT - 1);

    SidebarLayout L;
    L.open = open;
    L.tab = tab;
    if (!open) {
        L.panel = panelShownRect(openRect, true, place.barBottom, barH);
        L.title = L.panel;
        L.plus = BarToggle(L.title, s);
        L.lock = BarLock(L.title, s);
        return L;
    }

    // The height each tab's contents need
    const float gearPanelH = um.gearPanelH();
    const float slot = ITEM_CELL_PX;   // fixed: a wider panel leaves room beside the grid instead
    const float gridPanelH = um.gridPanelH();
    const float modeRowH = 30.0f * s;   // the row of fighting modes under the heading, with a gap under it
    // Under the rows: + Add Behavior and the open list's entries
    const float addH = 24.0f * s, optionH = 22.0f * s;
    const int optionCount = std::max(0, bui.options);
    const float behaviorPanelH = headH + modeRowH + colHeadH + rules * (rowH + rowGap) + 4.0f * s + addH +
                                 optionCount * (optionH + rowGap) + innerPad;
    const float statsH = (1.0f + STAT_GROUP_COUNT + STAT_COUNT) * statRowH;
    // The Build tab: the heading line, then a row for Destroy and each prop kind, then a little space under the last
    const float buildRowH = 22.0f * s, buildHeadH = 22.0f * s;
    const float buildH = buildHeadH + buildRowCount() * (buildRowH + rowGap) + innerPad;
    const float contents[UNIT_TAB_COUNT] = { um.inventoryTabH(), cardH, statsH, behaviorPanelH, buildH };

    const float bodyVis = std::max(0.0f, openRect.h - barH - tabH);
    const float maxScroll = std::max(0.0f, contents[idx(tab)] + 2.0f * pad - bodyVis);
    const float shift = -std::clamp(scroll, 0.0f, maxScroll);   // the contents move up by this much
    L.scroll = -shift;
    L.maxScroll = maxScroll;
    L.panel = openRect;
    L.title = panelBarRect(openRect, place.barBottom, barH);
    L.minus = BarToggle(L.title, s);
    L.lock = BarLock(L.title, s);

    // The tabs at the top of the body, which is the panel less its bar (along its top or its bottom)
    const float tabY = panelBodyRect(openRect, place.barBottom, barH).y;
    const float tabW = (width - 2.0f * pad - (UNIT_TAB_COUNT - 1) * tabGap) / UNIT_TAB_COUNT;
    for (int i = 0; i < UNIT_TAB_COUNT; ++i) {
        L.tabs[i] = Rect{ L.panel.x + pad + i * (tabW + tabGap), tabY + 3.0f * s, tabW, tabH - 6.0f * s };
    }
    L.body = Rect{ L.panel.x, tabY + tabH, width, bodyVis };
    const float top = L.body.y + pad + shift;   // the picked tab's contents, which scroll

    if (tab == UnitTab::Profile) {
        L.profile = Rect{ L.panel.x + pad, top, width - 2.0f * pad, cardH };
        // The picture at the bottom, under all the text, centred
        L.portrait = Rect{ std::floor(L.profile.x + (L.profile.w - pictureSide) * 0.5f), std::floor(L.profile.y + profileH),
                           pictureSide, pictureSide };
    }
    if (tab == UnitTab::Inventory) {
        // The gear first: the eight gear slots in one row of item cells, lined up with the inventory grid's columns,
        // then a line of attack and defense totals
        L.gearPanel = Rect{ L.panel.x + pad, top, width - 2.0f * pad, gearPanelH };
        // Optimize and Remove All, side by side at the right of the heading
        const float btnW = 84.0f * s, btnGap = 6.0f * s;
        L.removeAllButton = Rect{ L.gearPanel.x + L.gearPanel.w - innerPad - btnW, top + (headH - sortH) * 0.5f, btnW, sortH };
        L.optimizeButton = Rect{ L.removeAllButton.x - btnGap - btnW, L.removeAllButton.y, btnW, sortH };
        for (int i = 0; i < GEAR_SLOTS; ++i) {
            L.gear.push_back(Rect{ L.gearPanel.x + innerPad + i * (slot + gap), top + headH, slot, slot });
        }
    }
    if (tab == UnitTab::Inventory) {
        // Then the inventory grid, under the gear
        const float gridTop = top + gearPanelH + gap;
        L.gridPanel = Rect{ L.panel.x + pad, gridTop, width - 2.0f * pad, gridPanelH };
        L.sortButton = Rect{ L.gridPanel.x + L.gridPanel.w - innerPad - sortW, gridTop + (headH - sortH) * 0.5f,
                             sortW, sortH };
        for (int i = 0; i < INVENTORY_SLOTS; ++i) {
            const int c = i % INVENTORY_COLUMNS, r = i / INVENTORY_COLUMNS;
            L.slots.push_back(Rect{ L.gridPanel.x + innerPad + c * (slot + gap),
                                    gridTop + headH + r * (slot + gap), slot, slot });
        }
    }
    if (tab == UnitTab::Stats) {
        L.statsArea = Rect{ L.panel.x + pad, top, width - 2.0f * pad, statsH };
        L.statRowH = statRowH;
    }
    if (tab == UnitTab::Build) {
        L.buildHeading = Rect{ L.panel.x + pad, top, width - 2.0f * pad, buildHeadH };
        for (int i = 0; i < buildRowCount(); ++i) {
            L.buildRows.push_back(Rect{ L.panel.x + pad, top + buildHeadH + i * (buildRowH + rowGap),
                                        width - 2.0f * pad, buildRowH });
        }
    }
    if (tab == UnitTab::Behavior) {
        // Behaviors: the on box has a fixed width, and the name takes what the buttons leave. The # button, then the
        // priority and delete buttons, sit at the right end of each row.
        L.behaviorPanel = Rect{ L.panel.x + pad, top, width - 2.0f * pad, behaviorPanelH };
        const float rowX = L.behaviorPanel.x + innerPad;
        const float rowW = L.behaviorPanel.w - 2.0f * innerPad;
        const float toggleW = 18.0f * s, colGap = 3.0f * s;
        const float buttonsW = 4.0f * buttonW + 3.0f * buttonGap;
        const float colsW = rowW - buttonsW - gap;                 // the toggle and the name
        const std::array<float, 2> colX{ rowX, rowX + toggleW + colGap };
        const std::array<float, 2> colWidth{ toggleW, colsW - toggleW - colGap };
        const float buttonsX = rowX + colsW + gap;
        // The modes: four equal buttons across the panel, under its heading
        {
            const float mGap = 4.0f * s, mW = (rowW - 3.0f * mGap) / 4.0f;
            for (int i = 0; i < 4; ++i) {
                L.modes[(size_t)i] = Rect{ rowX + i * (mW + mGap), L.behaviorPanel.y + headH, mW, modeRowH - 8.0f * s };
            }
        }
        const float headY = L.behaviorPanel.y + headH + modeRowH;
        const float rowsY = headY + colHeadH;
        float y = rowsY;
        for (int i = 0; i < rules; ++i) {
            BehaviorRowLayout R;
            R.row = Rect{ rowX, y, rowW, rowH };
            R.toggle = Rect{ colX[0], y + (rowH - toggleW) * 0.5f, toggleW, toggleW };
            R.label = Rect{ colX[1], y, colWidth[1], rowH };
            const float by = y + 2.0f * s, bh = rowH - 4.0f * s;
            R.number = Rect{ buttonsX, by, buttonW, bh };
            R.up = Rect{ buttonsX + (buttonW + buttonGap), by, buttonW, bh };
            R.down = Rect{ buttonsX + 2.0f * (buttonW + buttonGap), by, buttonW, bh };
            R.remove = Rect{ buttonsX + 3.0f * (buttonW + buttonGap), by, buttonW, bh };
            L.behaviorRows.push_back(R);
            y += rowH + rowGap;
        }
        // + Add Behavior across the panel, and the list it opens under it
        y += 4.0f * s;
        L.behaviorAdd = Rect{ rowX, y, rowW, addH };
        y += addH + rowGap;
        for (int i = 0; i < optionCount; ++i) {
            L.behaviorOptions.push_back(Rect{ rowX + toggleW + colGap, y, rowW - toggleW - colGap, optionH });
            y += optionH + rowGap;
        }
        for (size_t i = 0; i < 2; ++i) L.behaviorHeads[i] = Rect{ colX[i], headY, colWidth[i], colHeadH };
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
            { R.number, BehaviorPart::Number },
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

// The part of the Behavior tab's Add Behavior button and list under a point
struct BehaviorExtraHit {
    bool add = false;    // + Add Behavior
    int option = -1;     // an entry of the Add Behavior list
};
inline BehaviorExtraHit behaviorExtraHitAt(const SidebarLayout& L, float x, float y) {
    BehaviorExtraHit hit;
    if (!L.body.contains(x, y)) return hit;   // scrolled out of view
    hit.add = L.behaviorAdd.contains(x, y);
    for (size_t i = 0; i < L.behaviorOptions.size(); ++i) {
        if (L.behaviorOptions[i].contains(x, y)) hit.option = (int)i;
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
