#include "ui_layout_panels.h"
#include <algorithm>
#include <cmath>

Rect BarToggle(const Rect& bar, float s) {
    const float sz = bar.h - 6.0f * s;
    return Rect{ bar.x + bar.w - sz - 3.0f * s, bar.y + 3.0f * s, sz, sz };
}

Rect BarLock(const Rect& bar, float s) {
    const Rect t = BarToggle(bar, s);
    return Rect{ t.x - t.w - 3.0f * s, t.y, t.w, t.h };
}

float squadWidthFor(int cols, int H) {
    const SquadMetrics m(H / 720.0f);
    return 2.0f * m.pad + cols * (m.cell + m.gap) + m.scrollW;
}

float squadDefaultHeight(int H) {
    const SquadMetrics m(H / 720.0f);
    return m.chromeH() + m.rowsH(SQUAD_DEFAULT_ROWS);
}

float sidebarMinWidth(int H) {
    const float s = uiScaleFor(H);
    return INVENTORY_COLUMNS * ITEM_CELL_PX + (INVENTORY_COLUMNS - 1) * 6.0f * s + 2.0f * 14.0f * s + 2.0f * 10.0f * s;
}

float unitPanelDefaultHeight(int H) {
    const float s = uiScaleFor(H);
    const UnitPanelMetrics m(s);
    return std::min(m.barH + m.tabH + m.inventoryTabH() + 2.0f * m.pad, H - 2.0f * PANEL_MARGIN * s);
}

float infoDefaultHeight(int H) {
    const float s = uiScaleFor(H);
    const float room = H - 2.0f * PANEL_MARGIN * s - unitPanelDefaultHeight(H) - PANEL_MARGIN * s;
    const float smallest = (PANEL_BAR_H + 20.0f + 40.0f) * s;
    return std::max(smallest, std::min(220.0f * s, room));
}

void panelDefaultSize(PanelId which, int H, float& w, float& h) {
    if (which == PanelId::Squad) { w = squadDefaultWidth(H); h = squadDefaultHeight(H); }
    else if (which == PanelId::Unit) { w = sidebarDefaultWidth(H); h = unitPanelDefaultHeight(H); }
    else { w = infoDefaultWidth(H); h = infoDefaultHeight(H); }
}

void panelMinSize(PanelId which, int H, float& w, float& h) {
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

float panelAnchorX(PanelId which, float w, int W, int H) {
    const float m = PANEL_MARGIN * H / 720.0f;
    return which == PanelId::Squad ? m : W - m - w;
}

float panelAnchorY(PanelId which, float h, int H) {
    const float m = PANEL_MARGIN * H / 720.0f;
    return which == PanelId::Info ? m : H - m - h;
}

Rect panelRect(PanelId which, const PanelPlace& p, int W, int H) {
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
    const float barOffset = p.barBottom ? h - barH : 0.0f;
    y = std::clamp(y + barOffset, 0.0f, std::max(0.0f, H - barH)) - barOffset;
    return Rect{ x, y, w, h };
}

PanelPlace placeForRect(PanelId which, const Rect& r, int W, int H, bool barBottom) {
    const float s = uiScaleFor(H);
    PanelPlace p;
    p.barBottom = barBottom;
    p.w = r.w / s;
    p.h = r.h / s;
    p.dx = (r.x - panelAnchorX(which, r.w, W, H)) / s;
    p.dy = (r.y - panelAnchorY(which, r.h, H)) / s;
    return p;
}

Rect panelBarRect(const Rect& open, bool barBottom, float barH) {
    return Rect{ open.x, barBottom ? open.y + open.h - barH : open.y, open.w, barH };
}

Rect panelShownRect(const Rect& open, bool collapsed, bool barBottom, float barH) {
    return collapsed ? panelBarRect(open, barBottom, barH) : open;
}

Rect panelBodyRect(const Rect& open, bool barBottom, float barH) {
    return Rect{ open.x, barBottom ? open.y : open.y + barH, open.w, std::max(0.0f, open.h - barH) };
}

PanelPlace settlePanel(PanelId which, const PanelPlace& p, int W, int H, bool folded) {
    const float barH = PANEL_BAR_H * H / 720.0f;
    const Rect open = panelRect(which, p, W, H);
    if (!folded) {
        const bool bottom = open.y + open.h * 0.5f > H * 0.5f;
        if (bottom == p.barBottom) return p;
        PanelPlace q = placeForRect(which, open, W, H, bottom);
        return placeForRect(which, panelRect(which, q, W, H), W, H, bottom);
    }
    const Rect bar = panelBarRect(open, p.barBottom, barH);
    const bool bottom = bar.y + barH * 0.5f > H * 0.5f;
    if (bottom == p.barBottom) return p;
    const Rect moved{ open.x, bottom ? bar.y + barH - open.h : bar.y, open.w, open.h };
    return placeForRect(which, moved, W, H, bottom);
}

PanelPlace defaultPanelPlace(PanelId which) {
    PanelPlace p;
    p.barBottom = which != PanelId::Info;
    return p;
}

UnitPanelLayout layoutUnitPanel(int W, int H, int unitCount, int scrollRow, bool collapsed,
                                       const PanelPlace& place, int tab,
                                       int factionCount, int repScroll) {
    const float s = uiScaleFor(H);
    const SquadMetrics m(s);
    const Rect open = panelRect(PanelId::Squad, place, W, H);
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
    if (collapsed) return L;

    const Rect body = panelBodyRect(open, place.barBottom, m.barH);
    L.tab = tab == FACTION_TAB_FACTION ? FACTION_TAB_FACTION : FACTION_TAB_SQUAD;
    {
        const float tw = (open.w - 2.0f * m.pad - m.gap) * 0.5f;
        L.tabSquad = Rect{ open.x + m.pad, body.y + 2.0f * s, tw, m.tabH - 4.0f * s };
        L.tabFaction = Rect{ L.tabSquad.x + tw + m.gap, body.y + 2.0f * s, tw, m.tabH - 4.0f * s };
    }
    const float bodyY = body.y + m.tabH;
    if (L.tab == FACTION_TAB_FACTION) {
        // The squads first, as a numpad: squad 1 at the bottom left and 9 at the top right
        const float gridGap = 4.0f * s, cellH = 36.0f * s;
        const float cellW = (open.w - 2.0f * m.pad - 2.0f * gridGap) / 3.0f;
        const float gridTop = bodyY + m.pad * 0.5f;
        for (int k = 0; k < SQUAD_COUNT; ++k) {
            const int row = 2 - k / 3, col = k % 3;
            L.squadCells[(size_t)k] = Rect{ open.x + m.pad + col * (cellW + gridGap), gridTop + row * (cellH + gridGap), cellW, cellH };
        }
        L.renameButton = Rect{ open.x + m.pad, gridTop + 3.0f * cellH + 2.0f * gridGap + m.gap, open.w - 2.0f * m.pad, m.toggleH };

        const float rowH = 24.0f * s, rowGap = 2.0f * s;
        L.repHeading = Rect{ open.x + m.pad, L.renameButton.y + L.renameButton.h + m.gap, open.w - 2.0f * m.pad, rowH };
        const float listTop = L.repHeading.y + rowH + rowGap;
        const float room = body.y + body.h - m.pad * 0.5f - listTop;
        L.repVisible = std::max(0, (int)std::floor((room + rowGap) / (rowH + rowGap) + 0.001f));
        L.repFirst = std::clamp(repScroll, 0, std::max(0, factionCount - L.repVisible));
        for (int i = L.repFirst; i < factionCount && i < L.repFirst + L.repVisible; ++i) {
            L.repRows.push_back(Rect{ open.x + m.pad, listTop + (i - L.repFirst) * (rowH + rowGap), open.w - 2.0f * m.pad, rowH });
        }
        if (listTop > body.y + body.h) L.repHeading = Rect{};
        return L;
    }
    const float rowW = open.w - 2.0f * m.pad;
    L.squadHeading = Rect{ open.x + m.pad, bodyY + m.pad, rowW, m.headH };
    const float buttonsY = bodyY + m.pad + m.headH + m.gap;
    const float quarterW = (rowW - 3.0f * m.gap) / 4.0f;
    L.paceButton = Rect{ open.x + m.pad, buttonsY, quarterW, m.toggleH };
    L.selectButton = Rect{ L.paceButton.x + quarterW + m.gap, buttonsY, quarterW, m.toggleH };
    L.optimizeAllButton = Rect{ L.selectButton.x + quarterW + m.gap, buttonsY, quarterW, m.toggleH };
    L.floorButton = Rect{ L.optimizeAllButton.x + quarterW + m.gap, buttonsY, quarterW, m.toggleH };
    const float top = buttonsY + m.toggleH + m.gap;

    for (int r = 0; r < rowsVisible; ++r) {
        for (int c = 0; c < cols; ++c) {
            const int idx = (L.scrollRow + r) * cols + c;
            if (idx >= unitCount) break;
            const float x = open.x + m.pad + c * (m.cell + m.gap);
            const float y = top + r * (m.cell + m.label + m.gap);
            L.cells.push_back(UnitCell{ idx, Rect{ x, y, m.cell, m.cell + m.label } });
        }
    }

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
    if (barH <= 0.0f) L.track = L.thumb = L.scrollUp = L.scrollDown = Rect{};
    return L;
}

int squadRowAtThumb(const UnitPanelLayout& L, float thumbTop) {
    if (!L.scrollable()) return 0;
    return (int)std::lround(scrollFracAt(L.track, L.thumb, thumbTop) * L.maxScroll());
}

bool unitTabEnabled(World& w, ObjectId unit, UnitTab tab) {
    return tab == UnitTab::Profile || tab == UnitTab::Build || unit == NO_OBJECT || w.has<Commandable>(unit);
}

SidebarLayout layoutInventorySidebar(int W, int H, int ruleCount, float scroll, bool open, UnitTab tab,
                                            const PanelPlace& place, float profileH,
                                            const BehaviorUi& bui) {
    const float s = uiScaleFor(H);
    const UnitPanelMetrics um(s);
    const float pad = um.pad, gap = um.gap;
    const Rect openRect = panelRect(PanelId::Unit, place, W, H);
    const float width = openRect.w;
    const float cardInset = 8.0f * s;
    constexpr float PROFILE_PICTURE_PX = 256.0f;
    const float pictureSide = std::max(0.0f, std::min(PROFILE_PICTURE_PX, width - 2.0f * pad - 2.0f * cardInset));
    const float cardH = profileH + pictureSide + cardInset, headH = um.headH, innerPad = um.innerPad;
    const float barH = um.barH, tabH = um.tabH, tabGap = 3.0f * s;
    const float colHeadH = 16.0f * s, rowH = 24.0f * s, rowGap = 2.0f * s;
    const float buttonW = 18.0f * s, buttonGap = 2.0f * s;
    const float sortW = 56.0f * s, sortH = 22.0f * s, statRowH = 20.0f * s;
    const int rules = std::clamp(ruleCount, 0, BEHAVIOR_TASKS_MAX);
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

    const float gearPanelH = um.gearPanelH();
    const float slot = ITEM_CELL_PX;
    const float gridPanelH = um.gridPanelH();
    const float modeRowH = 30.0f * s;
    const float addH = 24.0f * s, optionH = 22.0f * s;
    const int optionCount = std::max(0, bui.options);
    const float behaviorPanelH = headH + modeRowH + colHeadH + rules * (rowH + rowGap) + 4.0f * s + addH +
                                 optionCount * (optionH + rowGap) + innerPad;
    const float statsH = (1.0f + STAT_GROUP_COUNT + STAT_COUNT) * statRowH;
    const float buildRowH = 22.0f * s, buildHeadH = 22.0f * s;
    const float buildH = buildHeadH + buildRowCount() * (buildRowH + rowGap) + innerPad;
    const float contents[UNIT_TAB_COUNT] = { um.inventoryTabH(), cardH, statsH, behaviorPanelH, buildH };

    const float bodyVis = std::max(0.0f, openRect.h - barH - tabH);
    const float maxScroll = std::max(0.0f, contents[idx(tab)] + 2.0f * pad - bodyVis);
    const float shift = -std::clamp(scroll, 0.0f, maxScroll);
    L.scroll = -shift;
    L.maxScroll = maxScroll;
    L.panel = openRect;
    L.title = panelBarRect(openRect, place.barBottom, barH);
    L.minus = BarToggle(L.title, s);
    L.lock = BarLock(L.title, s);

    const float tabY = panelBodyRect(openRect, place.barBottom, barH).y;
    const float tabW = (width - 2.0f * pad - (UNIT_TAB_COUNT - 1) * tabGap) / UNIT_TAB_COUNT;
    for (int i = 0; i < UNIT_TAB_COUNT; ++i) {
        L.tabs[i] = Rect{ L.panel.x + pad + i * (tabW + tabGap), tabY + 3.0f * s, tabW, tabH - 6.0f * s };
    }
    L.body = Rect{ L.panel.x, tabY + tabH, width, bodyVis };
    const float top = L.body.y + pad + shift;

    if (tab == UnitTab::Profile) {
        L.profile = Rect{ L.panel.x + pad, top, width - 2.0f * pad, cardH };
        L.portrait = Rect{ std::floor(L.profile.x + (L.profile.w - pictureSide) * 0.5f), std::floor(L.profile.y + profileH),
                           pictureSide, pictureSide };
    }
    if (tab == UnitTab::Inventory) {
        L.gearPanel = Rect{ L.panel.x + pad, top, width - 2.0f * pad, gearPanelH };
        const float btnW = 84.0f * s, btnGap = 6.0f * s;
        L.removeAllButton = Rect{ L.gearPanel.x + L.gearPanel.w - innerPad - btnW, top + (headH - sortH) * 0.5f, btnW, sortH };
        L.optimizeButton = Rect{ L.removeAllButton.x - btnGap - btnW, L.removeAllButton.y, btnW, sortH };
        for (int i = 0; i < GEAR_SLOTS; ++i) {
            L.gear.push_back(Rect{ L.gearPanel.x + innerPad + i * (slot + gap), top + headH, slot, slot });
        }
    }
    if (tab == UnitTab::Inventory) {
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
        L.behaviorPanel = Rect{ L.panel.x + pad, top, width - 2.0f * pad, behaviorPanelH };
        const float rowX = L.behaviorPanel.x + innerPad;
        const float rowW = L.behaviorPanel.w - 2.0f * innerPad;
        const float toggleW = 18.0f * s, colGap = 3.0f * s;
        const float buttonsW = 4.0f * buttonW + 3.0f * buttonGap;
        const float colsW = rowW - buttonsW - gap;
        const std::array<float, 2> colX{ rowX, rowX + toggleW + colGap };
        const std::array<float, 2> colWidth{ toggleW, colsW - toggleW - colGap };
        const float buttonsX = rowX + colsW + gap;
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

BehaviorHit behaviorHitAt(const SidebarLayout& L, float x, float y) {
    BehaviorHit hit;
    if (!L.body.contains(x, y)) return hit;
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

BehaviorExtraHit behaviorExtraHitAt(const SidebarLayout& L, float x, float y) {
    BehaviorExtraHit hit;
    if (!L.body.contains(x, y)) return hit;
    hit.add = L.behaviorAdd.contains(x, y);
    for (size_t i = 0; i < L.behaviorOptions.size(); ++i) {
        if (L.behaviorOptions[i].contains(x, y)) hit.option = (int)i;
    }
    return hit;
}

bool overGamePanels(const UnitPanelLayout& unitPanel, const SidebarLayout& sidebar,
                           bool sidebarShown, float x, float y) {
    return unitPanel.panel.contains(x, y) || (sidebarShown && sidebar.panel.contains(x, y));
}

int gearSlotAt(const SidebarLayout& L, float x, float y) {
    if (!L.body.contains(x, y)) return -1;
    for (size_t i = 0; i < L.gear.size(); ++i) {
        if (L.gear[i].contains(x, y)) return (int)i;
    }
    return -1;
}

int sidebarSlotAt(const SidebarLayout& L, float x, float y) {
    if (!L.body.contains(x, y)) return -1;
    for (size_t i = 0; i < L.slots.size(); ++i) {
        if (L.slots[i].contains(x, y)) return (int)i;
    }
    return -1;
}
