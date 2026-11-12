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
#include "squads.h"
#include "ui_types.h"

constexpr int UNITS_PER_ROW = 5;
constexpr int SQUAD_DEFAULT_ROWS = 3;

struct UnitCell {
    int index = 0;
    Rect rect;
};

Rect BarToggle(const Rect& bar, float s);
Rect BarLock(const Rect& bar, float s);

enum class PanelId : int { Squad = 0, Unit = 1, Info = 2, Count = 3 };
constexpr int PANEL_COUNT = idx(PanelId::Count);
constexpr PanelId ALL_PANELS[PANEL_COUNT] = { PanelId::Squad, PanelId::Unit, PanelId::Info };
constexpr int GRAB_TOP = 1, GRAB_RIGHT = 2, GRAB_BOTTOM = 4, GRAB_LEFT = 8, GRAB_MOVE = 16;

constexpr float PANEL_MARGIN = 8.0f;
constexpr float PANEL_BAR_H = 22.0f;

struct SquadMetrics {
    float cell, gap, pad, label, scrollW, toggleH, barH, tabH, headH;
    explicit SquadMetrics(float s)
        : cell(44.0f * s), gap(5.0f * s), pad(10.0f * s), label(34.0f * s), scrollW(14.0f * s), toggleH(22.0f * s),
          barH(PANEL_BAR_H * s), tabH(24.0f * s), headH(20.0f * s) {}
    float chromeH() const { return barH + tabH + pad + headH + gap + toggleH + gap + pad; }
    float rowsH(int rows) const { return rows * (cell + label) + (rows - 1) * gap; }
};

float squadWidthFor(int cols, int H);
inline float squadDefaultWidth(int H) { return squadWidthFor(UNITS_PER_ROW, H); }
inline float squadMinWidth(int H) { return squadWidthFor(2, H); }
float squadDefaultHeight(int H);

float sidebarMinWidth(int H);
inline float sidebarDefaultWidth(int H) { return std::max(380.0f * H / 720.0f, sidebarMinWidth(H)); }

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
float unitPanelDefaultHeight(int H);

inline float infoDefaultWidth(int H) { return 340.0f * H / 720.0f; }
float infoDefaultHeight(int H);

void panelDefaultSize(PanelId which, int H, float& w, float& h);
void panelMinSize(PanelId which, int H, float& w, float& h);

float panelAnchorX(PanelId which, float w, int W, int H);
float panelAnchorY(PanelId which, float h, int H);

Rect panelRect(PanelId which, const PanelPlace& p, int W, int H);

PanelPlace placeForRect(PanelId which, const Rect& r, int W, int H, bool barBottom = false);

Rect panelBarRect(const Rect& open, bool barBottom, float barH);
Rect panelShownRect(const Rect& open, bool collapsed, bool barBottom, float barH);
Rect panelBodyRect(const Rect& open, bool barBottom, float barH);

PanelPlace settlePanel(PanelId which, const PanelPlace& p, int W, int H, bool folded);
PanelPlace defaultPanelPlace(PanelId which);

struct UnitPanelLayout {
    bool collapsed = false;
    Rect toggle;
    Rect lock;
    Rect panel;
    Rect title;
    Rect squadHeading;   // "Squad: 1" at the top of the Squad tab
    Rect paceButton;
    Rect formButton;     // opens the formation list
    Rect selectButton;
    Rect optimizeAllButton;
    Rect floorButton;
    int tab = 0;
    Rect tabSquad;
    Rect tabFaction;
    // The Faction tab has three tabs of its own, each scrolling: Squads, Reputation and Settings. What shows is laid out from
    // view's top, moved up by subScroll (in pixels); what is outside view is cut off when drawn, and a click there is no click.
    std::array<Rect, 3> subTabs{};
    int sub = 0;
    Rect view;
    Rect subTrack, subThumb;   // the scrollbar of the tab, when its contents don't fit
    float subScroll = 0.0f, subScrollMax = 0.0f, subContentH = 0.0f;
    std::array<Rect, SQUAD_COUNT> squadCells{};   // Squads: the squads, laid out like a numpad: 7 8 9 on top
    Rect manageSquadsButton;   // Squads: below the squads, opens Manage Squads
    Rect renameButton;         // Squads: below manageSquadsButton
    Rect factionRulesButton;   // Settings: opens Faction Rules
    Rect repHeading;           // Reputation: whose view it is, kept at the top while the list scrolls
    std::vector<Rect> repRows;   // the rows that show, from repFirst
    int repFirst = 0;
    int repVisible = 0;
    bool subScrollable() const { return subScrollMax > 0.0f; }
    Rect scrollUp;
    Rect scrollDown;
    Rect track;
    Rect thumb;
    std::vector<UnitCell> cells;
    int rowCount = 0;
    int rowsVisible = 0;
    int cols = UNITS_PER_ROW;
    int scrollRow = 0;

    bool scrollable() const { return rowCount > rowsVisible; }
    int maxScroll() const { return std::max(0, rowCount - rowsVisible); }
};

constexpr float SQUAD_WHEEL_NOTCHES_PER_ROW = 4.0f;
constexpr int FACTION_TAB_SQUAD = 0;
constexpr int FACTION_TAB_FACTION = 1;
constexpr int FACTION_SUB_SQUADS = 0;
constexpr int FACTION_SUB_REPUTATION = 1;
constexpr int FACTION_SUB_SETTINGS = 2;

UnitPanelLayout layoutUnitPanel(int W, int H, int unitCount, int scrollRow, bool collapsed = false,
                                       const PanelPlace& place = PanelPlace{}, int tab = FACTION_TAB_SQUAD,
                                       int factionCount = 0, float subScroll = 0.0f, int sub = FACTION_SUB_SQUADS);

int squadRowAtThumb(const UnitPanelLayout& L, float thumbTop);

struct BehaviorRowLayout {
    Rect row;
    Rect toggle;
    Rect label;
    Rect number;
    Rect up;
    Rect down;
    Rect remove;
};

enum class BehaviorPart { None, Toggle, Number, Up, Down, Remove };

struct BehaviorUi {
    int options = -1;
};

enum class UnitTab : int { Inventory = 0, Profile, Stats, Behavior, Build, Count };
constexpr int UNIT_TAB_COUNT = idx(UnitTab::Count);
bool unitTabEnabled(World& w, ObjectId unit, UnitTab tab);
constexpr int STAT_GROUP_COUNT = 5;

struct SidebarLayout {
    Rect panel;
    bool open = false;
    UnitTab tab = UnitTab::Inventory;
    Rect title;
    Rect minus;
    Rect plus;
    Rect lock;
    std::array<Rect, UNIT_TAB_COUNT> tabs{};
    Rect body;
    Rect profile;
    Rect portrait;
    Rect gearPanel;
    std::vector<Rect> gear;
    Rect optimizeButton;
    Rect removeAllButton;
    Rect gridPanel;
    Rect sortButton;
    std::vector<Rect> slots;
    Rect statsArea;
    float statRowH = 0.0f;
    Rect behaviorPanel;
    std::array<Rect, 4> modes{};
    std::array<Rect, 2> behaviorHeads{};
    std::vector<BehaviorRowLayout> behaviorRows;
    Rect behaviorAdd;
    std::vector<Rect> behaviorOptions;
    std::vector<BehaviorOption> behaviorOptionInfo;
    Rect buildHeading;
    std::vector<Rect> buildRows;
    float scroll = 0.0f;
    float maxScroll = 0.0f;
};

SidebarLayout layoutInventorySidebar(int W, int H, int ruleCount, float scroll, bool open = false, UnitTab tab = UnitTab::Inventory,
                                            const PanelPlace& place = PanelPlace{}, float profileH = 0.0f,
                                            const BehaviorUi& bui = BehaviorUi{});

struct BehaviorHit {
    int row = -1;
    BehaviorPart part = BehaviorPart::None;
};

BehaviorHit behaviorHitAt(const SidebarLayout& L, float x, float y);

struct BehaviorExtraHit {
    bool add = false;
    int option = -1;
};
BehaviorExtraHit behaviorExtraHitAt(const SidebarLayout& L, float x, float y);

bool overGamePanels(const UnitPanelLayout& unitPanel, const SidebarLayout& sidebar,
                           bool sidebarShown, float x, float y);

int gearSlotAt(const SidebarLayout& L, float x, float y);

int sidebarSlotAt(const SidebarLayout& L, float x, float y);
