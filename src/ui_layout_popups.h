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
#include "ui_layout_panels.h"

// Layout for the popups in play: talk, trade, storage, loot, the prompts and dialogs, the stats popout and item menu.
// Pure functions of the window size and the state,
// so the drawing code and the mouse handling use the same rectangles.

// The talk box: centred. It shows the last lines said, one row each, then one button per option, then End Dialogue at
// the bottom. Its height follows how many lines and options there are.
struct DialogueLayout {
    Rect box;
    std::vector<Rect> lines;                 // one row per line shown, oldest first
    std::vector<Rect> options;               // one button per option
    Rect end;      // End Dialogue
};
inline DialogueLayout layoutDialogue(int W, int H, int lineCount, int optionCount) {
    const float s = uiScaleFor(H);
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
    Rect interaction;
    Rect interBar;
    Rect npcGrid;
    std::vector<Rect> npcSlots;                   // one cell per inventory slot of the NPC
    Rect tradePanel;
    Rect tradeBar;
    Rect theirName;    // the other unit's name, over its side
    Rect yourName;     // your unit's name, over your side
    Rect theirBox;     // the NPC's side
    Rect yourBox;      // your side
    std::vector<Rect> theirCells, yourCells;
    Rect theirTotal;
    Rect yourTotal;
    Rect valueBar;     // how close the offer is to the NPC's price
    Rect statusLine;   // why the trade can't be confirmed, or that it can
    Rect cancelBtn;
    Rect tradeBtn;
};
// top: how far down the window something else is already drawn across the top (the pause bar, which shows during a
// trade); the panels start under it
inline TradeLayout layoutTrade(int W, int H, float top = 0.0f) {
    const float s = uiScaleFor(H);
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

// The storage panel, for a storage prop (a barrel) or a crafting prop (a campfire): an Interaction Panel with the
// prop's name, its inventory, a hint and a Close button, in the top middle of the window, clear of the bottom panels
struct StorageLayout {
    Rect panel;
    Rect bar;
    Rect name;      // the prop's name
    Rect heading;   // "Inventory", or "Input" over a forge's input
    Rect grid;
    std::vector<Rect> slots;                  // STORAGE_SLOTS cells, row by row
    // A passive crafting prop's output (a forge's): its heading, grid and STORAGE_SLOTS cells. Empty for other props.
    bool output = false;
    Rect outHeading;
    Rect outGrid;
    std::vector<Rect> outSlots;
    Rect status;    // a forge's fuel line; zero height for other props
    Rect hint;      // how to take, drop and store items, wrapped
    Rect closeBtn;
    // The owner's lock toggle, bottom left, beside Close: shown for a prop your faction owns, when lockToggle is set
    bool lockToggle = false;
    Rect lockBtn;
};
// The panel for looting an Unconscious unit or a corpse: its gear in one row, then its inventory, 8 cells across, each
// cell ITEM_CELL_PX. Then a hint, and Take All and Close.
struct LootLayout {
    Rect panel;
    Rect bar;
    Rect name;
    Rect gearHeading;
    Rect gearGrid;
    std::vector<Rect> gear;    // GEAR_SLOTS cells
    Rect invHeading;
    Rect invGrid;
    std::vector<Rect> slots;   // INVENTORY_SLOTS cells
    Rect hint;
    Rect takeAllBtn;
    Rect closeBtn;
};
inline LootLayout layoutLoot(int W, int H) {
    const float s = uiScaleFor(H);
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
    const float s = uiScaleFor(H);
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
    Rect box;
    Rect title;
    Rect countBox;
    Rect minus100, minus10, minus1;
    Rect plus1, plus10, plus100;
    Rect single, max;
    Rect confirm, cancel;
};

// The Optimize All dialog: a title, a line saying what it will do, the Attack and Defense slider, three toggles, and
// Confirm and Cancel
struct OptimizeAllLayout {
    Rect box;
    Rect title;
    Rect text;          // wrapped, up to five lines
    Rect sliderLabel;   // "Attack 60% / Defense 40%"
    Rect track;         // the slider: left is all defense, right all attack
    std::array<Rect, 3> toggles{};                // accessories, two-handed weapons, value on ties: the box and its label
    Rect confirm, cancel;
};
inline OptimizeAllLayout layoutOptimizeAll(int W, int H) {
    const float s = uiScaleFor(H);
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
    Rect box;
    Rect title;
    Rect portrait;
    Rect info;   // beside the portrait: the name, then DISBAND_INFO_LINES lines
    Rect text;   // what disbanding does, wrapped, up to three lines
    Rect hint;
    Rect disband, cancel;
};
constexpr int DISBAND_INFO_LINES = 3;
inline DisbandLayout layoutDisband(int W, int H) {
    const float s = uiScaleFor(H);
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
    const float s = uiScaleFor(H);
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

// The prompt the # button opens, in the middle of the window: what it asks, the box the percent is typed in, the
// range it takes, and OK and Cancel. It is modal: nothing else takes clicks or keys while it is up.
struct PercentPromptLayout {
    Rect box;
    Rect input;
    Rect ok;
    Rect cancel;
    float titleY = 0.0f, rangeY = 0.0f, errorY = 0.0f;   // the text lines' tops
};
inline PercentPromptLayout layoutPercentPrompt(int W, int H) {
    const float s = uiScaleFor(H);
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

// The right-click menu for an inventory item: one text button per action, top to bottom. It opens
// at the mouse and is kept inside the window.
struct ItemMenuLayout {
    Rect box;
    std::vector<Rect> buttons;
};

inline ItemMenuLayout layoutItemMenu(float x, float y, int buttonCount, float rowH, int W, int H) {
    const float s = uiScaleFor(H);
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
