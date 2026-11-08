#include "renderer_internal.h"

using namespace render_detail;

namespace {
struct FilterEntry {
    const char* label;
    LogKind kind;
    bool heading;
};

const FilterEntry kFilterEntries[] = {
    { "UILABEL_NPC_MESSAGES", LogKind::Other, true },
    { "UILABEL_SHOW_NPC_INFO", LogKind::Npc, false },
    { "UILABEL_COMBAT_MESSAGES", LogKind::Other, true },
    { "UILABEL_SEEN", LogKind::Seen, false },
    { "UILABEL_HIT", LogKind::Hit, false },
    { "UILABEL_MISS", LogKind::Miss, false },
    { "UILABEL_DAMAGE", LogKind::Damage, false },
    { "UILABEL_EXPERIENCE_MESSAGES", LogKind::Other, true },
    { "UILABEL_EXPERIENCE", LogKind::Xp, false },
    { "UILABEL_ITEM_MESSAGES", LogKind::Other, true },
    { "UILABEL_PICKUP", LogKind::Pickup, false },
    { "UILABEL_DROP", LogKind::Drop, false },
    { "UILABEL_LOOT", LogKind::Loot, false },
};

constexpr int kFilterRowCount = (int)(sizeof(kFilterEntries) / sizeof(kFilterEntries[0]));
}

float Renderer::profileCardHeight(int H) {
    const float s = uiScaleFor(H);
    const float line = listTextHeight("Ag");
    const float statRow = line + 3.0f * s + 8.0f * s + 9.0f * s;
    return 10.0f * s + uiTextHeight("Ag") + 6.0f * s + 2.0f * (line + 6.0f * s) + 7.0f * statRow + 4.0f * s;
}

void Renderer::drawWorldMap(const WorldMapLayout& L, const std::vector<MapDot>& dots, float cx, float cz, float span,
                            const WorldMapMarks& marks) {
    pollOverview();
    const float s = uiScale();
    const RGBA black{ 0.0f, 0.0f, 0.0f, 1.0f };
    const Rect& w = L.window;
    const Rect& r = L.map;

    drawRect(Rect{ w.x - 1.5f * s, w.y - 1.5f * s, w.w + 3.0f * s, w.h + 3.0f * s }, theme::alpha(theme::PINK, 0.95f));
    drawRect(w, theme::alpha(theme::MENU_FILL, 0.97f));
    drawTitleBar(L.bar, ICON_NONE, lang("UILABEL_WORLD_MAP"));
    drawCloseButton(L.close);

    drawRect(r, RGBA{ 0.10f, 0.20f, 0.30f, 1.0f });
    if (span <= 0.0f || r.w <= 0.0f || r.h <= 0.0f) return;

    const float across = mapSpanAcross(r, span);
    const float xHi = cx + across * 0.5f, xLo = cx - across * 0.5f;
    const float zHi = cz + span * 0.5f, zLo = cz - span * 0.5f;
    auto sx = [&](float x) { return r.x + (xHi - x) / across * r.w; };
    auto sy = [&](float z) { return r.y + (zHi - z) / span * r.h; };

    if (overviewTex_ && overviewSize_ > 0.0f) {
        const float size = overviewSize_, ext = overviewExtent_;
        auto u = [&](float x) { return (size - x) / ext; };
        auto v = [&](float z) { return (size - z) / ext; };
        const float ax = std::min(xHi, size), bx = std::max(xLo, 0.0f);
        const float az = std::min(zHi, size), bz = std::max(zLo, 0.0f);
        if (ax > bx && az > bz) drawUiQuad(overviewTex_, sx(ax), sy(az), sx(bx), sy(bz), u(ax), v(az), u(bx), v(bz), WHITE);
    }

    std::vector<UiVert> tris;
    auto line = [&](float x0, float y0, float x1, float y1, float width, RGBA c) {
        const float dx = x1 - x0, dy = y1 - y0, len = std::sqrt(dx * dx + dy * dy);
        if (len <= 0.0f) return;
        const float ux = dx / len, uy = dy / len, h = width * 0.5f;
        const float nx = -uy * h, ny = ux * h;
        x0 -= ux * h; y0 -= uy * h; x1 += ux * h; y1 += uy * h;
        tris.insert(tris.end(), { { x0 + nx, y0 + ny, c }, { x1 + nx, y1 + ny, c }, { x1 - nx, y1 - ny, c },
                                  { x0 + nx, y0 + ny, c }, { x1 - nx, y1 - ny, c }, { x0 - nx, y0 - ny, c } });
    };
    auto ring = [&](float x, float y, float radius, float width, RGBA c) {
        const int segs = 32;
        const float rIn = radius - width * 0.5f, rOut = radius + width * 0.5f;
        for (int i = 0; i < segs; ++i) {
            const float a0 = TWO_PI * i / segs, a1 = TWO_PI * (i + 1) / segs;
            const float c0 = std::cos(a0), s0 = std::sin(a0), c1 = std::cos(a1), s1 = std::sin(a1);
            tris.insert(tris.end(), { { x + c0 * rIn, y + s0 * rIn, c }, { x + c0 * rOut, y + s0 * rOut, c },
                                      { x + c1 * rOut, y + s1 * rOut, c }, { x + c0 * rIn, y + s0 * rIn, c },
                                      { x + c1 * rOut, y + s1 * rOut, c }, { x + c1 * rIn, y + s1 * rIn, c } });
        }
    };
    auto square = [&](float x, float y, float side, RGBA c) {
        const float h = side * 0.5f;
        tris.insert(tris.end(), { { x - h, y - h, c }, { x + h, y - h, c }, { x + h, y + h, c },
                                  { x - h, y - h, c }, { x + h, y + h, c }, { x - h, y + h, c } });
    };
    auto inMap = [&](float px, float py) { return r.contains(px, py); };

    const RGBA purple{ 0.62f, 0.25f, 0.88f, 1.0f };
    const float propSide = std::max(4.0f, 6.0f * s);
    for (const MapDot& dot : dots) {
        if (dot.kind != MapDotKind::Prop) continue;
        const float px = sx(dot.at.x), py = sy(dot.at.z);
        if (!inMap(px, py)) continue;
        square(px, py, propSide + 2.0f * std::max(1.0f, s), black);
        square(px, py, propSide, purple);
    }
    drawUiTris(tris);
    tris.clear();

    const float d = std::max(6.0f, 9.0f * s);
    for (const MapDot& dot : dots) {
        if (dot.kind == MapDotKind::Prop) continue;
        const float px = sx(dot.at.x), py = sy(dot.at.z);
        if (!inMap(px, py)) continue;
        if (dot.kind == MapDotKind::Player) {
            drawDot(px, py, d);
            continue;
        }
        if (!dotWhiteTex_) dotWhiteTex_ = MakeDotTexture(true);
        const RGBA tint = dot.kind == MapDotKind::Hostile ? RGBA{ 1.0f, 0.25f, 0.25f, 1.0f } : RGBA{ 0.6f, 0.6f, 0.6f, 1.0f };
        const float h = d * 0.5f;
        drawUiQuad(dotWhiteTex_, px - h, py - h, px + h, py + h, 0.0f, 0.0f, 1.0f, 1.0f, tint);
    }

    const float edge = std::max(1.0f, 1.0f * s);
    if (marks.moveAlpha > 0.0f) {
        const float px = sx(marks.moveX), py = sy(marks.moveZ);
        if (inMap(px, py)) {
            const float h = 6.0f * s, lw = std::max(2.0f, 2.5f * s);
            const RGBA k{ 0.0f, 0.0f, 0.0f, marks.moveAlpha };
            const RGBA g{ 0.30f, 1.0f, 0.30f, marks.moveAlpha };
            line(px - h, py - h, px + h, py + h, lw + 2.0f * edge, k);
            line(px - h, py + h, px + h, py - h, lw + 2.0f * edge, k);
            line(px - h, py - h, px + h, py + h, lw, g);
            line(px - h, py + h, px + h, py - h, lw, g);
        }
    }

    {
        const float px = sx(marks.camX), py = sy(marks.camZ);
        if (inMap(px, py)) {
            const float rad = 7.0f * s, lw = std::max(1.5f, 1.5f * s);
            const float fx = std::sin(marks.camYaw), fy = std::cos(marks.camYaw);
            const float qx = -fy, qy = fx;
            auto at = [&](float ahead, float side, float& ox, float& oy) {
                ox = px + fx * ahead * rad + qx * side * rad;
                oy = py + fy * ahead * rad + qy * side * rad;
            };
            float ax, ay, bx, by, cx2, cy2;
            at(2.1f, 0.0f, ax, ay);
            at(-0.45f, 1.8f, bx, by);
            at(-0.45f, -1.8f, cx2, cy2);
            const RGBA yellow{ 1.0f, 0.86f, 0.18f, 1.0f };
            for (int pass = 0; pass < 2; ++pass) {
                const RGBA c = pass == 0 ? black : yellow;
                const float wdt = pass == 0 ? lw + 2.0f * edge : lw;
                line(ax, ay, bx, by, wdt, c);
                line(bx, by, cx2, cy2, wdt, c);
                line(cx2, cy2, ax, ay, wdt, c);
                ring(px, py, rad, wdt, c);
            }
        }
    }
    drawUiTris(tris);
}

void Renderer::drawUnitPanel(World& w, const UnitPanelLayout& L, const std::vector<ObjectId>& units, bool running,
                             int repView, const std::string* renameText, int floorLevel, bool floorManual) {
    const float s = uiScale();
    const float bgAlpha = 0.5f;
    drawRect(L.panel, RGBA{ 0.085f, 0.035f, 0.045f, bgAlpha });
    drawTitleBar(L.title, ICON_SQUAD_PANEL, lang("UILABEL_SQUAD_PANEL"));
    drawCollapseButton(L.toggle, L.collapsed);
    if (L.collapsed) return;

    {
        const float th = listTextHeight("Ag");
        auto tab = [&](const Rect& r, const std::string& label, bool on) {
            if (r.w <= 0.0f) return;
            const bool hot = !on && r.contains(mouseX_, mouseY_);
            if (hot) noteHover(r);
            drawRect(r, on ? theme::alpha(theme::GOLD_DEEP, 0.95f) : RGBA{ 0.16f, 0.07f, 0.085f, hot ? 1.0f : 0.9f });
            if (on) drawRect(Rect{ r.x, r.y + r.h - 2.0f, r.w, 2.0f }, theme::GOLD);
            const std::string t = fitText(label, r.w - 6.0f * s, false);
            drawListText(t, r.x + (r.w - listTextWidth(t)) * 0.5f, r.y + (r.h - th) * 0.5f,
                         on ? WHITE : theme::TEXT_DIM);
        };
        tab(L.tabSquad, lang("UILABEL_SQUAD_TAB"), L.tab == FACTION_TAB_SQUAD);
        tab(L.tabFaction, lang("UILABEL_FACTION_TAB"), L.tab == FACTION_TAB_FACTION);
    }
    if (L.tab == FACTION_TAB_FACTION) {
        drawSquadGrid(w, L, renameText);
        if (L.repHeading.w > 0.0f) drawReputationList(w, L, repView);
        return;
    }

    auto mode = [&](const Rect& r, const std::string& text, bool on) {
        if (r.contains(mouseX_, mouseY_)) noteHover(r);
        drawRect(r, on ? RGBA{ 0.74f, 0.54f, 0.24f, 1.0f } : RGBA{ 0.19f, 0.08f, 0.10f, bgAlpha });
        const float tw = listTextWidth(text), th = listTextHeight(text);
        drawListText(text, r.x + (r.w - tw) * 0.5f, r.y + (r.h - th) * 0.5f, WHITE);
    };
    {
        const int current = w.resource<SquadBook>().current;
        const std::string& custom = w.resource<SquadBook>().names[(size_t)current];
        std::string head = langf("UILABEL_SQUAD_HEADING", { std::to_string(current + 1) });
        if (!custom.empty()) head += "  " + custom;
        const Rect& h = L.squadHeading;
        const std::string shown = fitTextTip(head, h.w, h);
        drawListText(shown, h.x, h.y + (h.h - listTextHeight(shown)) * 0.5f, theme::GOLD);
    }
    mode(L.paceButton, fitTextTip(running ? lang("UILABEL_PACE_RUN") : lang("UILABEL_PACE_WALK"), L.paceButton.w - 6.0f * s, L.paceButton),
         running);
    mode(L.selectButton, fitTextTip(lang("UILABEL_SELECT_MENU"), L.selectButton.w - 6.0f * s, L.selectButton), false);
    {
        const std::string label = lang("UILABEL_OPTIMIZE");
        const std::string shown = fitTextTip(label, L.optimizeAllButton.w - 6.0f * s, L.optimizeAllButton);
        mode(L.optimizeAllButton, shown, false);
        noteTip(lang("UILABEL_OPTIMIZE_ALL_TIP"), L.optimizeAllButton);
    }
    {
        const std::string level = floorLevel > 0 ? std::to_string(floorLevel) : std::string(lang("UILABEL_FLOOR_GROUND_SHORT"));
        const std::string label = langf("UILABEL_FLOOR_BUTTON", { level });
        mode(L.floorButton, fitTextTip(label, L.floorButton.w - 6.0f * s, L.floorButton), floorManual);
        noteTip(lang(floorManual ? "UILABEL_FLOOR_MANUAL_TIP" : "UILABEL_FLOOR_AUTO_TIP"), L.floorButton);
    }

    for (const UnitCell& c : L.cells) {
        const ObjectId e = units[c.index];
        const bool selected = w.has<Selected>(e);
        const Rect frame{ c.rect.x - 2.0f, c.rect.y - 2.0f, c.rect.w + 4.0f, c.rect.h + 4.0f };
        if (c.rect.contains(mouseX_, mouseY_)) noteHover(c.rect);
        drawRect(frame, selected ? RGBA{ 0.74f, 0.54f, 0.24f, 1.0f } : RGBA{ 0.42f, 0.20f, 0.24f, bgAlpha });
        drawRect(Rect{ c.rect.x, c.rect.y, c.rect.w, c.rect.w }, RGBA{ 0.16f, 0.07f, 0.085f, bgAlpha });

        const TextTex& g = unitPortrait(w, e);
        drawPortrait(g, Rect{ c.rect.x, c.rect.y, c.rect.w, c.rect.w }, unitSaturation(w, e));

        const Needs* nd = w.get<Needs>(e);
        if (nd && nd->maxHp > 0 && nd->hp < nd->maxHp) {
            const float lost = std::clamp(1.0f - (float)nd->hp / (float)nd->maxHp, 0.0f, 1.0f);
            const float gh = c.rect.w * lost;
            if (gh > 0.0f) {
                drawRectGradient(Rect{ c.rect.x, c.rect.y + c.rect.w - gh, c.rect.w, gh },
                                 theme::alpha(theme::HURT, 0.0f), theme::alpha(theme::HURT, 0.65f));
            }
        }

        const float frac = (nd && nd->maxHp > 0) ? std::clamp((float)nd->hp / (float)nd->maxHp, 0.0f, 1.0f) : 0.0f;
        const float barY = c.rect.y + c.rect.w + 3.0f * s, barH = 4.0f * s;
        drawRect(Rect{ c.rect.x, barY, c.rect.w, barH }, RGBA{ 0.24f, 0.11f, 0.13f, bgAlpha });
        drawRect(Rect{ c.rect.x, barY, c.rect.w * frac, barH }, theme::HEALTH_BAR);

        const Name* nm = w.get<Name>(e);
        const std::string full = nm ? nm->text : std::string(lang("UILABEL_UNNAMED_UNIT"));
        const std::string label = fitTextTip(full, c.rect.w, c.rect);
        drawListText(label, c.rect.x, c.rect.y + c.rect.w + 8.0f * s, WHITE);
    }

    if (L.track.h > 0.0f || L.scrollUp.h > 0.0f) {
        drawRect(L.track, RGBA{ 0.24f, 0.10f, 0.12f, bgAlpha });
        if (L.scrollable()) drawRect(L.thumb, L.thumb.contains(mouseX_, mouseY_) ? RGBA{ 0.96f, 0.88f, 0.76f, 1.0f }
                                                                                : RGBA{ 0.80f, 0.70f, 0.60f, 1.0f });
        auto arrow = [&](const Rect& r, bool up, bool can) {
            if (r.h <= 0.0f) return;
            const bool hot = can && r.contains(mouseX_, mouseY_);
            if (hot) noteHover(r);
            drawRect(r, hot ? RGBA{ 0.46f, 0.22f, 0.26f, 1.0f } : RGBA{ 0.29f, 0.13f, 0.16f, 0.9f });
            const RGBA c = can ? WHITE : RGBA{ 0.56f, 0.49f, 0.45f, 0.6f };
            const float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.5f, hw = r.w * 0.3f, hh = r.h * 0.22f;
            if (up) drawUiTris({ { cx, cy - hh, c }, { cx + hw, cy + hh, c }, { cx - hw, cy + hh, c } });
            else drawUiTris({ { cx - hw, cy - hh, c }, { cx + hw, cy - hh, c }, { cx, cy + hh, c } });
        };
        arrow(L.scrollUp, true, L.scrollRow > 0);
        arrow(L.scrollDown, false, L.scrollRow < L.maxScroll());
    }
}

void Renderer::drawSquadGrid(World& w, const UnitPanelLayout& L, const std::string* renameText) {
    const float s = uiScale();
    const float th = listTextHeight("Ag");
    const SquadBook& book = w.resource<SquadBook>();
    const std::array<int, SQUAD_COUNT> sizes = squadSizes(w);
    for (int k = 0; k < SQUAD_COUNT; ++k) {
        const Rect& r = L.squadCells[(size_t)k];
        if (r.w <= 0.0f) continue;
        const bool current = k == book.current;
        const bool hot = r.contains(mouseX_, mouseY_);
        if (hot) noteHover(r);
        drawRect(r, current ? theme::alpha(theme::GOLD_DEEP, 0.95f) : RGBA{ 0.16f, 0.07f, 0.085f, hot ? 1.0f : 0.75f });
        if (current) drawRect(Rect{ r.x, r.y + r.h - 2.0f, r.w, 2.0f }, theme::GOLD);
        const float inset = 4.0f * s;
        const std::string number = std::to_string(k + 1);
        drawListText(number, r.x + inset, r.y + 2.0f * s, theme::GOLD);
        const float nameX = r.x + inset + listTextWidth(number) + 4.0f * s;
        std::string name;
        if (current && renameText) name = *renameText + ((SDL_GetTicks() / 500) % 2 ? "|" : " ");
        else name = book.names[(size_t)k];
        if (!name.empty()) {
            const std::string fitted = fitText(name, r.x + r.w - inset - nameX, false);
            drawListText(fitted, nameX, r.y + 2.0f * s, WHITE);
            if (!renameText && fitted != name) noteTip(name, r);
        }
        const std::string count = std::to_string(sizes[(size_t)k]) + "/" + std::to_string(SQUAD_SIZE);
        const RGBA countTint = sizes[(size_t)k] >= SQUAD_SIZE ? RGBA{ 1.0f, 0.62f, 0.35f, 1.0f }
                             : sizes[(size_t)k] == 0 ? theme::TEXT_DIM : WHITE;
        drawListText(count, r.x + r.w - inset - listTextWidth(count), r.y + r.h - th - 3.0f * s, countTint);
    }
    if (L.renameButton.w > 0.0f) {
        const Rect& b = L.renameButton;
        const bool hot = b.contains(mouseX_, mouseY_);
        if (hot) noteHover(b);
        drawRect(b, renameText ? RGBA{ 0.74f, 0.54f, 0.24f, 1.0f } : RGBA{ 0.19f, 0.08f, 0.10f, hot ? 0.95f : 0.5f });
        const std::string label = renameText ? lang("UILABEL_SQUAD_RENAME_DONE")
                                             : langf("UILABEL_SQUAD_RENAME", { std::to_string(book.current + 1) });
        const std::string shown = fitText(label, b.w - 6.0f * s, false);
        drawListText(shown, b.x + (b.w - listTextWidth(shown)) * 0.5f, b.y + (b.h - th) * 0.5f, WHITE);
    }
}

void Renderer::drawReputationList(World& w, const UnitPanelLayout& L, int repView) {
    const float s = uiScale();
    const float th = listTextHeight("Ag");
    auto swatch = [&](float x, float cy, float d, int faction) {
        const FactionColor c = factionColor(faction);
        drawRect(Rect{ x - 1.0f, cy - d * 0.5f - 1.0f, d + 2.0f, d + 2.0f }, RGBA{ 0.0f, 0.0f, 0.0f, 0.9f });
        drawRect(Rect{ x, cy - d * 0.5f, d, d }, RGBA{ c.r, c.g, c.b, 1.0f });
    };
    {
        const Rect& h = L.repHeading;
        drawRect(h, RGBA{ 0.0f, 0.0f, 0.0f, 0.25f });
        const float d = 12.0f * s;
        swatch(h.x + 6.0f * s, h.y + h.h * 0.5f, d, repView);
        const std::string name = fitText(factionName(repView), h.w - d - 16.0f * s, false);
        drawListText(name, h.x + d + 12.0f * s, h.y + (h.h - th) * 0.5f, theme::GOLD);
    }
    const std::vector<FactionDef>& list = reputationFactions();
    for (size_t i = 0; i < L.repRows.size(); ++i) {
        const size_t idx = (size_t)L.repFirst + i;
        if (idx >= list.size()) break;
        const Rect& r = L.repRows[i];
        const int other = list[idx].id;
        const bool self = other == repView;
        const bool hot = r.contains(mouseX_, mouseY_);
        if (hot && !self) noteHover(r);
        drawRect(r, self ? theme::alpha(theme::GOLD_DEEP, 0.55f) : RGBA{ 0.16f, 0.07f, 0.085f, hot ? 0.95f : 0.6f });
        const float d = 10.0f * s;
        swatch(r.x + 6.0f * s, r.y + r.h * 0.5f, d, other);
        const int value = reputation(w, repView, other);
        const std::string valueText = self ? std::string("(...)") : std::to_string(value);
        const RGBA valueTint = self ? theme::TEXT_DIM
                             : value <= REP_HOSTILE ? RGBA{ 1.0f, 0.42f, 0.42f, 1.0f }
                             : value >= REP_ALLIED ? RGBA{ 0.5f, 0.95f, 0.5f, 1.0f } : theme::TEXT_DIM;
        const float vw = listTextWidth(valueText);
        const float nameX = r.x + d + 12.0f * s;
        const std::string name = fitText(factionName(other), r.x + r.w - 8.0f * s - vw - 8.0f * s - nameX, false);
        drawListText(name, nameX, r.y + (r.h - th) * 0.5f, WHITE);
        drawListText(valueText, r.x + r.w - 8.0f * s - vw, r.y + (r.h - th) * 0.5f, valueTint);
        if (!self) {
            noteTip(lang(value <= REP_HOSTILE ? "UILABEL_REP_HOSTILE" : value >= REP_ALLIED ? "UILABEL_REP_ALLIED" : "UILABEL_REP_NEUTRAL"),
                    Rect{ r.x + r.w - 8.0f * s - vw, r.y, vw + 8.0f * s, r.h });
        }
    }
}

GameLogLayout Renderer::gameLogLayout(int winW, int winH, int tab, bool filterOpen, int offset, int lineCount,
                                          bool collapsed, const PanelPlace& place) {
    const float s = uiScaleFor(winH);
    const float pad = 8.0f * s, scrollW = 10.0f * s, gap = 4.0f * s;
    const float barH = PANEL_BAR_H * s, tabH = 20.0f * s;
    const float lineH = listTextHeight("Ag") + 2.0f * s;
    const Rect open = panelRect(PanelId::Info, place, winW, winH);
    const float bodyH = open.h;
    const int linesVisible = std::max(1, (int)std::floor((bodyH - barH - tabH - 2.0f * pad) / lineH));

    GameLogLayout L;
    L.linesVisible = linesVisible;
    L.lineCount = lineCount;
    L.lineH = lineH;
    L.offset = std::clamp(offset, 0, L.maxOffset());
    L.tab = std::clamp(tab, 0, 2);
    L.filterOpen = filterOpen && L.tab == 1 && !collapsed;

    const float w = open.w;
    L.panel = panelShownRect(open, collapsed, place.barBottom, barH);
    L.bar = panelBarRect(open, place.barBottom, barH);
    const Rect bodyR = panelBodyRect(open, place.barBottom, barH);
    L.bodyTop = bodyR.y;
    L.bodyBottom = bodyR.y + bodyR.h;
    const float x = open.x, y = bodyR.y - barH;
    const float h = bodyR.h + barH;
    L.toggle = BarToggle(L.bar, s);
    L.lock = BarLock(L.bar, s);
    L.collapsed = collapsed;
    L.textX = x + pad;
    L.textW = w - 2.0f * pad - scrollW - gap;

    const int end = lineCount - L.offset;
    L.shown = std::min(linesVisible, std::max(0, end));
    L.first = end - L.shown;

    const float trackX = x + w - pad * 0.5f - scrollW * 0.5f;
    const float trackY = y + barH + tabH + pad, trackH = h - barH - tabH - 2.0f * pad;
    L.track = Rect{ trackX, trackY, scrollW * 0.5f, trackH };
    if (L.scrollable()) {
        const float frac = L.maxOffset() > 0 ? (float)L.offset / (float)L.maxOffset() : 0.0f;
        L.thumb = scrollThumb(L.track, (float)linesVisible / (float)lineCount, 16.0f * s, 1.0f - frac);
    }

    const float tabY = y + barH;
    auto tabW = [&](const char* key) { return std::max(48.0f * s, listTextWidth(lang(key)) + 16.0f * s); };
    L.tabMessage = Rect{ x + pad * 0.5f, tabY + 2.0f * s, tabW("UILABEL_MESSAGE"), tabH - 2.0f * s };
    L.tabGame = Rect{ L.tabMessage.x + L.tabMessage.w + 4.0f * s, tabY + 2.0f * s, tabW("UILABEL_GAME"), tabH - 2.0f * s };
    L.tabTime = Rect{ L.tabGame.x + L.tabGame.w + 4.0f * s, tabY + 2.0f * s, tabW("UILABEL_TIME"), tabH - 2.0f * s };
    {
        const float tabsEnd = L.tabTime.x + L.tabTime.w + 4.0f * s;
        const float fw = listTextWidth(lang("UILABEL_FILTER")) + 14.0f * s;
        const float fx = std::max(tabsEnd, x + w - pad - scrollW - fw);
        L.filterBtn = Rect{ fx, tabY + 2.0f * s, std::max(0.0f, std::min(fw, x + w - 2.0f * s - fx)), tabH - 2.0f * s };
    }

    if (L.filterOpen) {
        const float rowH = listTextHeight("Ag") + 6.0f * s;
        float longest = 0.0f;
        for (const FilterEntry& fe : kFilterEntries) {
            longest = std::max(longest, listTextWidth(lang(fe.label)) + (fe.heading ? 6.0f : 16.0f) * s);
        }
        const float stateW = std::max(listTextWidth(lang("UILABEL_ON")), listTextWidth(lang("UILABEL_OFF")));
        const float menuW = std::min((float)winW, std::max({ 160.0f * s, L.filterBtn.w, longest + 20.0f * s + stateW + 10.0f * s }));
        const float menuH = kFilterRowCount * rowH + 8.0f * s;
        const float below = tabY + tabH;
        const float menuY = below + menuH <= winH ? below : std::max(0.0f, tabY - menuH);
        const float menuX = std::clamp(L.filterBtn.x + L.filterBtn.w - menuW, 0.0f, std::max(0.0f, winW - menuW));
        L.filterMenu = Rect{ menuX, menuY, menuW, menuH };
        for (int i = 0; i < kFilterRowCount; ++i) {
            const Rect r{ L.filterMenu.x, L.filterMenu.y + 4.0f * s + i * rowH, menuW, rowH };
            L.filterRows.push_back(GameLogLayout::FilterRow{ r, kFilterEntries[i].kind, kFilterEntries[i].heading,
                                                               lang(kFilterEntries[i].label) });
        }
    }
    if (collapsed) {
        L.track = L.thumb = Rect{};
        L.tabMessage = L.tabGame = L.tabTime = L.filterBtn = Rect{};
    }
    return L;
}

void Renderer::drawGameLog(const GameLogLayout& L, const std::vector<const LogLine*>& rows, const LogFilter& filter,
                             float mx, float my, const InfoTime& time) {
    const float s = uiScale();
    const RGBA gray = theme::TEXT_QUIET;
    const RGBA lit = theme::alpha(theme::SLOT_HOT, 0.95f);
    const RGBA dim = theme::alpha(theme::SLOT, 0.90f);

    drawRect(L.panel, theme::alpha(theme::PANEL_FILL, 0.86f));
    drawTitleBar(L.bar, ICON_GAME_LOG, lang("UILABEL_INFORMATION_PANEL"));
    drawCollapseButton(L.toggle, L.collapsed);
    if (L.collapsed) return;

    auto tab = [&](const Rect& r, const std::string& label, bool selected) {
        if (r.contains(mx, my)) noteHover(r);
        drawRect(r, selected ? lit : dim);
        drawListText(label, r.x + (r.w - listTextWidth(label)) * 0.5f, r.y + (r.h - listTextHeight("Ag")) * 0.5f,
                     selected ? WHITE : gray);
    };
    tab(L.tabMessage, lang("UILABEL_MESSAGE"), L.tab == 0);
    tab(L.tabGame, lang("UILABEL_GAME"), L.tab == 1);
    tab(L.tabTime, lang("UILABEL_TIME"), L.tab == 2);
    if (L.tab == 1) drawButton(L.filterBtn, lang("UILABEL_FILTER"), true, L.filterBtn.contains(mx, my) || L.filterOpen);

    const float tabH = 20.0f * s;
    const float bodyTop = L.bodyTop + tabH;

    if (L.tab == 2) {
        const RGBA logGray = theme::LOG_TEXT;
        const float bigH = uiTextHeight("Ag"), smallH = listTextHeight("Ag"), gap = 4.0f * s;
        const float blockH = bigH + 3.0f * (gap + smallH);
        const float bodyH = L.bodyBottom - bodyTop;
        float y = bodyTop + std::max(0.0f, (bodyH - blockH) * 0.5f);
        const float midX = L.panel.x + L.panel.w * 0.5f, maxW = L.panel.w - 16.0f * s;
        drawClockText(time.clock, midX - clockTextWidth(time.clock) * 0.5f, y, WHITE);
        y += bigH + gap;
        for (const std::string* line : { &time.date, &time.ymd, &time.days }) {
            const std::string t = fitText(*line, maxW, false);
            drawListText(t, midX - listTextWidth(t) * 0.5f, y, line == &time.date ? WHITE : logGray);
            y += smallH + gap;
        }
        return;
    }
    const float top = bodyTop + (L.bodyBottom - bodyTop - L.linesVisible * L.lineH) * 0.5f;
    const int count = (int)rows.size();
    for (int k = 0; k < L.shown && L.first + k < count; ++k) {
        const LogLine& ln = *rows[(size_t)(L.first + k)];
        const int row = L.linesVisible - L.shown + k;
        drawListText(fitText(ln.text, L.textW, false), L.textX, top + row * L.lineH, logTint(ln.tint));
    }
    if (L.scrollable()) drawScrollBar(L.track, L.thumb);

    if (L.filterOpen) {
        drawRect(L.filterMenu, theme::alpha(theme::MENU_FILL, 0.97f));
        const float th = listTextHeight("Ag");
        for (const GameLogLayout::FilterRow& fr : L.filterRows) {
            if (fr.heading) {
                drawListText(fr.label, fr.r.x + 6.0f * s, fr.r.y + (fr.r.h - th) * 0.5f, theme::GOLD);
                continue;
            }
            if (fr.r.contains(mx, my)) {
                noteHover(fr.r);
                drawRect(fr.r, RGBA{ 1.0f, 1.0f, 1.0f, 0.10f });
            }
            const bool on = filter.on[(int)fr.kind];
            drawListText(fr.label, fr.r.x + 16.0f * s, fr.r.y + (fr.r.h - th) * 0.5f, WHITE);
            const std::string state = on ? lang("UILABEL_ON") : lang("UILABEL_OFF");
            drawListText(state, fr.r.x + fr.r.w - 10.0f * s - listTextWidth(state), fr.r.y + (fr.r.h - th) * 0.5f,
                         on ? theme::OK_TEXT : gray);
        }
    }
}
