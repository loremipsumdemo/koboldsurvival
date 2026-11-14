#include "altar_ui.h"
#include <algorithm>
#include <cmath>
#include "components.h"

AltarLayout layoutAltar(int W, int H, const std::array<int, 3>& counts, const std::array<int, 3>& scroll, int columns) {
    const float s = uiScaleFor((float)H);
    const float margin = 16.0f * s, pad = 14.0f * s, gap = 8.0f * s, line = 28.0f * s, btnH = 32.0f * s, btnW = 150.0f * s;
    const float rowH = 40.0f * s, rowGap = 3.0f * s;
    AltarLayout L;
    const float boxW = std::min((float)W - 2.0f * margin, (columns > 2 ? 1060.0f : 820.0f) * s);
    const float boxH = std::min((float)H - 2.0f * margin, 600.0f * s);
    L.box = Rect{ std::floor((W - boxW) * 0.5f), std::floor((H - boxH) * 0.5f), boxW, boxH };
    const float x = L.box.x + pad, w = boxW - 2.0f * pad;
    float y = L.box.y + pad;
    L.title = Rect{ x, y, w - line, line };
    L.close = Rect{ x + w - line, y, line, line };
    y += line + 2.0f * s;
    L.totals = Rect{ x, y, w, line };
    y += line + gap;
    const float colW = (w - gap * (float)(columns - 1)) / (float)columns;
    const float listBottom = L.box.y + boxH - pad - btnH - gap;
    for (int c = 0; c < columns; ++c) {
        const float cx = x + c * (colW + gap);
        L.heads[(size_t)c] = Rect{ cx, y, colW, line };
        L.lists[(size_t)c] = Rect{ cx, y + line + 2.0f * s, colW, std::max(0.0f, listBottom - (y + line + 2.0f * s)) };
        const Rect& list = L.lists[(size_t)c];
        const int visible = std::max(0, (int)std::floor((list.h + rowGap) / (rowH + rowGap) + 0.001f));
        L.visible[(size_t)c] = visible;
        L.first[(size_t)c] = std::clamp(scroll[(size_t)c], 0, std::max(0, counts[(size_t)c] - visible));
        for (int i = L.first[(size_t)c]; i < counts[(size_t)c] && i < L.first[(size_t)c] + visible; ++i) {
            AltarRow r;
            r.index = i;
            r.row = Rect{ list.x, list.y + (i - L.first[(size_t)c]) * (rowH + rowGap), list.w, rowH };
            r.portrait = Rect{ r.row.x + 3.0f * s, r.row.y + 3.0f * s, rowH - 6.0f * s, rowH - 6.0f * s };
            const float mw = 34.0f * s;
            r.move = Rect{ r.row.x + r.row.w - mw - 4.0f * s, r.row.y + 6.0f * s, mw, rowH - 12.0f * s };
            if (c == ALTAR_CITIZEN) {
                const float gw = 74.0f * s;
                r.group = Rect{ r.move.x - gw - 6.0f * s, r.move.y, gw, r.move.h };
                if (columns > 2) r.back = Rect{ r.group.x - mw - 4.0f * s, r.move.y, mw, r.move.h };
            }
            L.rows[(size_t)c].push_back(r);
        }
    }
    const float by = L.box.y + boxH - pad - btnH;
    L.apply = Rect{ L.box.x + boxW * 0.5f - gap * 0.5f - btnW, by, btnW, btnH };
    L.cancel = Rect{ L.box.x + boxW * 0.5f + gap * 0.5f, by, btnW, btnH };
    return L;
}

Rect ghostButton(const AltarRow& r, int H) {
    const float s = uiScaleFor((float)H);
    const float w = 96.0f * s;
    return Rect{ r.row.x + r.row.w - w - 4.0f * s, r.move.y, w, r.move.h };
}

PerimeterLayout layoutPerimeter(int W, int H, float px, float py) {
    const float s = uiScaleFor((float)H);
    const float pad = 10.0f * s, gap = 6.0f * s, line = 26.0f * s, btnH = 30.0f * s, boxW = 220.0f * s;
    PerimeterLayout L;
    const float boxH = pad + line + gap + 5.0f * (btnH + gap) + pad - gap;
    const float ox = std::clamp(px < 0.0f ? (W - boxW) * 0.5f : px, 0.0f, std::max(0.0f, (float)W - boxW));
    const float oy = std::clamp(py < 0.0f ? 64.0f * s : py, 0.0f, std::max(0.0f, (float)H - boxH));
    L.panel = Rect{ std::floor(ox), std::floor(oy), boxW, boxH };
    const float x = L.panel.x + pad, w = boxW - 2.0f * pad;
    float y = L.panel.y + pad;
    L.title = Rect{ x, y, w, line };
    y += line + gap;
    L.draw = Rect{ x, y, w, btnH };
    y += btnH + gap;
    L.erase = Rect{ x, y, w, btnH };
    y += btnH + gap;
    L.reset = Rect{ x, y, w, btnH };
    y += btnH + gap;
    L.confirm = Rect{ x, y, w, btnH };
    y += btnH + gap;
    L.cancel = Rect{ x, y, w, btnH };
    return L;
}

NoUnitsLayout layoutNoUnits(int W, int H, float px, float py) {
    const float s = uiScaleFor((float)H);
    const float pad = 10.0f * s, gap = 6.0f * s, line = 26.0f * s, btnH = 30.0f * s, boxW = std::min((float)W - 16.0f * s, 340.0f * s);
    NoUnitsLayout L;
    const float boxH = pad + line + gap + line + gap + 3.0f * (btnH + gap) + pad - gap;
    const float ox = std::clamp(px < 0.0f ? (W - boxW) * 0.5f : px, 0.0f, std::max(0.0f, (float)W - boxW));
    const float oy = std::clamp(py < 0.0f ? 64.0f * s : py, 0.0f, std::max(0.0f, (float)H - boxH));
    L.panel = Rect{ std::floor(ox), std::floor(oy), boxW, boxH };
    const float x = L.panel.x + pad, w = boxW - 2.0f * pad;
    float y = L.panel.y + pad;
    L.title = Rect{ x, y, w, line };
    y += line + gap;
    L.message = Rect{ x, y, w, line };
    y += line + gap;
    L.altar = Rect{ x, y, w, btnH };
    y += btnH + gap;
    L.unit = Rect{ x, y, w, btnH };
    y += btnH + gap;
    L.claim = Rect{ x, y, w, btnH };
    return L;
}

RenameLayout layoutRename(int W, int H) {
    const float s = uiScaleFor((float)H);
    const float pad = 16.0f * s, gap = 8.0f * s, line = 28.0f * s, btnH = 32.0f * s, btnW = 140.0f * s;
    const float boxW = std::min((float)W - 32.0f * s, 420.0f * s), boxH = pad + line + gap + 34.0f * s + gap + btnH + pad;
    RenameLayout L;
    L.box = Rect{ std::floor((W - boxW) * 0.5f), std::floor((H - boxH) * 0.5f), boxW, boxH };
    float y = L.box.y + pad;
    L.title = Rect{ L.box.x + pad, y, boxW - 2.0f * pad, line };
    y += line + gap;
    L.field = Rect{ L.box.x + pad, y, boxW - 2.0f * pad, 34.0f * s };
    y += 34.0f * s + gap;
    L.ok = Rect{ L.box.x + boxW * 0.5f - gap * 0.5f - btnW, y, btnW, btnH };
    L.cancel = Rect{ L.box.x + boxW * 0.5f + gap * 0.5f, y, btnW, btnH };
    return L;
}

int altarMaxScroll(const AltarLayout& L, int column, int count) {
    return std::max(0, count - L.visible[(size_t)column]);
}

SquadsLayout layoutSquads(int W, int H, const std::array<int, 3>& counts, const std::array<int, 3>& scroll) {
    const float s = uiScaleFor((float)H);
    SquadsLayout L;
    L.roster = layoutAltar(W, H, counts, scroll);
    for (int c = 0; c < 2; ++c) {
        for (AltarRow& r : L.roster.rows[(size_t)c]) r.group = Rect{};   // no behavior group buttons here
        const Rect& h = L.roster.heads[(size_t)c];
        const float bw = 28.0f * s, inset = 2.0f * s;
        L.next[(size_t)c] = Rect{ h.x + h.w - bw - inset, h.y + inset, bw, h.h - 2.0f * inset };
        L.prev[(size_t)c] = Rect{ L.next[(size_t)c].x - bw - inset, h.y + inset, bw, h.h - 2.0f * inset };
    }
    return L;
}

UnitCardLayout layoutUnitCard(const Rect& beside, int W, int H, int statLines) {
    const float s = uiScaleFor((float)H);
    const float pad = 10.0f * s, gap = 8.0f * s, margin = 8.0f * s;
    const float cell = 34.0f * s, cellGap = 3.0f * s, headLine = 18.0f * s;
    UnitCardLayout L;
    L.line = 16.0f * s;
    L.statColumns = 3;
    const float pic = 56.0f * s;
    const float boxW = std::min((float)W - 2.0f * margin, 470.0f * s);
    const int statRows = (statLines + L.statColumns - 1) / L.statColumns;
    const float itemRows = (float)(INVENTORY_SLOTS / INVENTORY_COLUMNS);
    const float boxH = pad + pic + gap + statRows * L.line + gap + headLine + cell + gap + headLine +
                       itemRows * cell + (itemRows - 1.0f) * cellGap + pad;

    // To the right of the row if it fits, or else to its left, and moved up or down to stay in the window
    float x = beside.x + beside.w + gap;
    if (x + boxW > W - margin) x = beside.x - gap - boxW;
    x = std::clamp(x, margin, std::max(margin, (float)W - margin - boxW));
    float y = beside.y + beside.h * 0.5f - boxH * 0.5f;
    y = std::clamp(y, margin, std::max(margin, (float)H - margin - boxH));
    L.box = Rect{ std::floor(x), std::floor(y), boxW, std::min(boxH, (float)H - 2.0f * margin) };

    const float ix = L.box.x + pad, iw = boxW - 2.0f * pad;
    float cy = L.box.y + pad;
    L.portrait = Rect{ ix, cy, pic, pic };
    L.head = Rect{ ix + pic + gap, cy, iw - pic - gap, pic };
    cy += pic + gap;
    L.stats = Rect{ ix, cy, iw, statRows * L.line };
    cy += statRows * L.line + gap;
    L.gearHead = Rect{ ix, cy, iw, headLine };
    cy += headLine;
    for (int g = 0; g < GEAR_SLOTS; ++g) L.gear.push_back(Rect{ ix + g * (cell + cellGap), cy, cell, cell });
    cy += cell + gap;
    L.items = Rect{ ix, cy, iw, headLine };
    cy += headLine;
    for (int i = 0; i < INVENTORY_SLOTS; ++i) {
        const int c = i % INVENTORY_COLUMNS, r = i / INVENTORY_COLUMNS;
        L.slots.push_back(Rect{ ix + c * (cell + cellGap), cy + r * (cell + cellGap), cell, cell });
    }
    return L;
}
