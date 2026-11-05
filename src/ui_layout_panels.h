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

// Layout for the panels in play: the three that can be moved and resized, the Squad Panel and the Unit Panel's sidebar.
// Pure functions of the window size and the state,
// so the drawing code and the mouse handling use the same rectangles.

constexpr int UNITS_PER_ROW = 5;        // the Squad Panel's columns at its default width
constexpr int SQUAD_DEFAULT_ROWS = 3;   // and its rows at its default height

// One visible unit in the bottom panel: its index in the player's unit list, and its rectangle
struct UnitCell {
    int index = 0;
    Rect rect;
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
    const float s = uiScaleFor(H);
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
    const float s = uiScaleFor(H);
    const UnitPanelMetrics m(s);
    return std::min(m.barH + m.tabH + m.inventoryTabH() + 2.0f * m.pad, H - 2.0f * PANEL_MARGIN * s);
}

inline float infoDefaultWidth(int H) { return 340.0f * H / 720.0f; }
// 220 pixels at 720p, but never so tall that it reaches the Unit Panel under it in the same corner: in a short window
// it takes what room is left above the Unit Panel, down to its smallest
inline float infoDefaultHeight(int H) {
    const float s = uiScaleFor(H);
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
    const float s = uiScaleFor(H);
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
    const float s = uiScaleFor(H);
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
    const float s = uiScaleFor(H);
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
    Rect toggle;      // the [-] or [+] button in the bar
    Rect lock;        // the lock beside it
    Rect panel;
    Rect title;       // the "Squad Panel" bar across the top
    Rect paceButton;  // walk and run toggle, across the top of the panel, on the left
    Rect optimizeAllButton;   // Optimize All, beside the pace toggle
    // The tabs under the bar: Squad (FACTION_TAB_SQUAD, the units) and Reputation (FACTION_TAB_REPUTATION)
    int tab = 0;
    Rect tabSquad;
    Rect tabReputation;
    // The Reputation tab: a heading naming the faction shown, then one row per faction, from repFirst on
    Rect repHeading;
    std::vector<Rect> repRows;
    int repFirst = 0;                       // the first faction shown, clamped to the list
    int repVisible = 0;                     // how many rows have room
    Rect scrollUp;    // the button at the top of the scroll bar: up a row
    Rect scrollDown;  // the button at the bottom of the scroll bar: down a row
    Rect track;   // scroll bar track, between the two buttons
    Rect thumb;   // scroll bar thumb, which can be dragged
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
    const float s = uiScaleFor(H);
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
        L.thumb = scrollThumb(L.track, (float)rowsVisible / (float)L.rowCount, std::min(8.0f * s, L.track.h),
                              (float)L.scrollRow / (float)L.maxScroll());
    }
    if (barH <= 0.0f) L.track = L.thumb = L.scrollUp = L.scrollDown = Rect{};   // no room for a scroll bar at all
    return L;
}

// The row the Squad Panel's thumb shows at when its top is at thumbTop (dragging the thumb)
inline int squadRowAtThumb(const UnitPanelLayout& L, float thumbTop) {
    if (!L.scrollable()) return 0;
    return (int)std::lround(scrollFracAt(L.track, L.thumb, thumbTop) * L.maxScroll());
}

// One row of the Behavior list: the on box, the behavior's name, then its # button (Eat, Heal and Rest only) and the
// priority and delete buttons
struct BehaviorRowLayout {
    Rect row;             // the whole row, which the hover text is for
    Rect toggle;          // on or off
    Rect label;           // what it does, such as Eat when hunger < 50%
    Rect number;          // [#] asks for the percent Eat, Heal or Rest acts below
    Rect up;              // [^] moves the behavior up the priority list
    Rect down;            // [v] moves it down
    Rect remove;          // [x] deletes it (Add Behavior puts it back)
};

// The parts of a behavior row that a click or hover can land on. Number is the # button, which asks for the percent.
enum class BehaviorPart { None, Toggle, Number, Up, Down, Remove };

// What is open in the Behavior tab besides the rows: the Add Behavior list
struct BehaviorUi {
    int options = -1;        // the entries in the open Add Behavior list, or -1 while it is shut
};

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
    Rect panel;      // the whole Unit Panel, bottom right
    bool open = false;                          // closed: the bar alone. open: the tabs and the picked tab
    UnitTab tab = UnitTab::Inventory;           // the tab shown, when open
    Rect title;      // the "Unit Panel" bar across the top, which does not scroll
    Rect minus;      // [-], shown while open; empty otherwise
    Rect plus;       // [+], shown while closed; empty otherwise
    Rect lock;       // the lock beside the [-] or [+]
    std::array<Rect, UNIT_TAB_COUNT> tabs{};   // the tab buttons under the bar; empty while closed
    Rect body;       // the area under the tabs, where the picked tab's contents scroll
    Rect profile;    // profile card: name, health, hunger and carry
    Rect portrait;   // the unit's picture, inside the card on the left
    Rect gearPanel;  // background behind the gear slots and their totals
    std::vector<Rect> gear;                    // GEAR_SLOTS cells in GearSlot order, in one row like the inventory's
    Rect optimizeButton;   // the Gear tab: wears the best gear on hand (optimizeGear)
    Rect removeAllButton;  // the Gear tab: takes all gear off (removeAllGear)
    Rect gridPanel;  // background behind the inventory grid
    Rect sortButton; // sorts the grid by item type, top right of its heading
    std::vector<Rect> slots;                   // INVENTORY_SLOTS rectangles, row by row
    Rect statsArea;  // the stats list, in the Stats tab
    float statRowH = 0.0f;                     // the height of each row in the stats list
    Rect behaviorPanel;  // background behind the behavior list
    // The modes over the behaviors: Passive, Block, Sneak, then Ranged (shown, not pressed)
    std::array<Rect, 4> modes{};
    std::array<Rect, 2> behaviorHeads{};       // column headings: On, Behavior
    std::vector<BehaviorRowLayout> behaviorRows;   // one per behavior, highest priority first
    Rect behaviorAdd;     // + Add Behavior, under the rows; it opens and shuts the list
    std::vector<Rect> behaviorOptions;             // the Add Behavior list's entries, while it is open
    std::vector<BehaviorOption> behaviorOptionInfo;   // filled by the caller, for drawing: what each entry adds
    Rect buildHeading;   // the Build tab's line saying what is picked
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
    const float s = uiScaleFor(H);
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
    if (!L.body.contains(x, y)) return hit;   // scrolled out of view, under the bar or the tabs
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

// Whether a point is over the in-game panels: the unit list along the bottom, or the inventory
// sidebar when it is showing. Clicks and wheel turns there belong to the panels, never to the
// ground behind them.
inline bool overGamePanels(const UnitPanelLayout& unitPanel, const SidebarLayout& sidebar,
                           bool sidebarShown, float x, float y) {
    return unitPanel.panel.contains(x, y) || (sidebarShown && sidebar.panel.contains(x, y));
}

// Index of the gear slot under a point (in GearSlot order), or -1 if it is over no gear slot or one scrolled out of view
inline int gearSlotAt(const SidebarLayout& L, float x, float y) {
    if (!L.body.contains(x, y)) return -1;
    for (size_t i = 0; i < L.gear.size(); ++i) {
        if (L.gear[i].contains(x, y)) return (int)i;
    }
    return -1;
}

// Index of the inventory slot under a point, or -1 if it is over no slot or one scrolled out of view
inline int sidebarSlotAt(const SidebarLayout& L, float x, float y) {
    if (!L.body.contains(x, y)) return -1;
    for (size_t i = 0; i < L.slots.size(); ++i) {
        if (L.slots[i].contains(x, y)) return (int)i;
    }
    return -1;
}
