#include "ui_layout_popups.h"
#include <algorithm>
#include <cmath>

DialogueLayout layoutDialogue(int W, int H, int lineCount, int optionCount) {
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

TradeLayout layoutTrade(int W, int H, float top) {
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

LootLayout layoutLoot(int W, int H) {
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

StorageLayout layoutStorage(int W, int H, bool output, bool lockToggle, int slotCount) {
    const float s = uiScaleFor(H);
    const float pad = 10.0f * s, bar = 22.0f * s, line = 20.0f * s, gap = 8.0f * s;
    const float btnH = 28.0f * s, btnW = 110.0f * s;
    const float cell = ITEM_CELL_PX, cellGap = 4.0f * s, inset = 4.0f * s;
    const int shown = visibleStorageSlots(std::max(1, slotCount));
    const int cols = STORAGE_COLUMNS, rows = (shown + STORAGE_COLUMNS - 1) / STORAGE_COLUMNS;
    const float gridW = cols * cell + (cols - 1) * cellGap + 2.0f * inset;
    const float gridH = rows * cell + (rows - 1) * cellGap + 2.0f * inset;
    const float panelW = std::max(gridW, 260.0f * s) + 2.0f * pad;
    const float hintH = 6.0f * line;
    const float outH = output ? line + gridH + line : 0.0f;
    const float panelH = bar + pad + 2.0f * line + gridH + outH + gap + hintH + gap + btnH + pad;
    const float x0 = (W - panelW) * 0.5f;
    const float y0 = std::max(8.0f * s, std::min(H * 0.14f, H - panelH - 8.0f * s));

    StorageLayout L;
    L.panel = Rect{ x0, y0, panelW, panelH };
    L.bar = Rect{ x0, y0, panelW, bar };
    L.name = Rect{ x0 + pad, y0 + bar + pad, panelW - 2.0f * pad, line };
    L.heading = Rect{ x0 + pad, L.name.y + line, panelW - 2.0f * pad, line };
    L.grid = Rect{ x0 + (panelW - gridW) * 0.5f, L.heading.y + line, gridW, gridH };
    auto cells = [&](const Rect& grid, std::vector<Rect>& out, int count) {
        for (int i = 0; i < count; ++i) {
            const int col = i % cols, row = i / cols;
            out.push_back(Rect{ grid.x + inset + col * (cell + cellGap), grid.y + inset + row * (cell + cellGap), cell, cell });
        }
    };
    cells(L.grid, L.slots, shown);
    float below = L.grid.y + gridH;
    L.output = output;
    if (output) {
        L.outHeading = Rect{ x0 + pad, below, panelW - 2.0f * pad, line };
        const float outGridH = 2 * cell + cellGap + 2.0f * inset;
        L.outGrid = Rect{ L.grid.x, below + line, gridW, outGridH };
        cells(L.outGrid, L.outSlots, STORAGE_SLOTS);
        L.status = Rect{ x0 + pad, L.outGrid.y + outGridH, panelW - 2.0f * pad, line };
        below = L.status.y + line;
    }
    L.hint = Rect{ x0 + pad, below + gap, panelW - 2.0f * pad, hintH };
    L.closeBtn = Rect{ x0 + (panelW - btnW) * 0.5f, y0 + panelH - pad - btnH, btnW, btnH };
    L.lockToggle = lockToggle;
    if (lockToggle) {
        L.lockBtn = Rect{ x0 + pad, y0 + panelH - pad - btnH, btnW, btnH };
        L.closeBtn.x = x0 + panelW - pad - btnW;
    }
    return L;
}

float tradeFeePercent(int charisma) {
    const float t = (float)(std::clamp(charisma, 1, MAX_LEVEL) - 1) / (float)(MAX_LEVEL - 1);
    return 100.0f - 99.0f * t;
}

int requiredOffer(int npcValue, int charisma) {
    const double fee = tradeFeePercent(charisma);
    return (int)std::ceil(npcValue * (100.0 + fee) / 100.0 - 1e-9);
}

int tradeValueOf(const std::vector<TradeGhost>& ghosts) {
    int total = 0;
    for (const TradeGhost& g : ghosts) {
        const ItemDef* d = findItem(g.stack.item);
        if (d) total += d->sellValue * g.stack.count;
    }
    return total;
}

OptimizeAllLayout layoutOptimizeAll(int W, int H) {
    const float s = uiScaleFor(H);
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

DisbandLayout layoutDisband(int W, int H) {
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

QuantityLayout layoutQuantity(int W, int H) {
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

PercentPromptLayout layoutPercentPrompt(int W, int H) {
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

ItemMenuLayout layoutItemMenu(float x, float y, int buttonCount, float rowH, int W, int H) {
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

int itemMenuButtonAt(const ItemMenuLayout& L, float x, float y) {
    for (size_t i = 0; i < L.buttons.size(); ++i) {
        if (L.buttons[i].contains(x, y)) return (int)i;
    }
    return -1;
}
