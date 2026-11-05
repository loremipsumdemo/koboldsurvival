// Renderer: the Faction Panel (squad and reputation), the Information Panel (game log) and the world map.
#include "renderer_internal.h"

using namespace render_detail;

namespace {

// The rows of the Game tab's filter menu, in order. A heading names a group of kinds and is not clickable.
struct FilterEntry {
    const char* label;     // a language key, looked up when the menu is drawn
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
    { "UILABEL_XP_ATHLETICS", LogKind::XpAthletics, false },
    { "UILABEL_XP_ENDURANCE", LogKind::XpEndurance, false },
    { "UILABEL_XP_CARRYING", LogKind::XpCarrying, false },
    { "UILABEL_ITEM_MESSAGES", LogKind::Other, true },
    { "UILABEL_PICKUP", LogKind::Pickup, false },
    { "UILABEL_DROP", LogKind::Drop, false },
    { "UILABEL_LOOT", LogKind::Loot, false },
};

constexpr int kFilterRowCount = (int)(sizeof(kFilterEntries) / sizeof(kFilterEntries[0]));

} // namespace

float Renderer::profileCardHeight(int H) {
    const float s = H / 720.0f;
    const float line = listTextHeight("Ag");
    const float statRow = line + 3.0f * s + 8.0f * s + 9.0f * s;   // label line, gap, bar, gap: as statRow draws it
    // Health, Doom (while Unconscious), Hunger, Rested, Comfort and Carry
    return 10.0f * s + uiTextHeight("Ag") + 6.0f * s + 2.0f * (line + 6.0f * s) + 6.0f * statRow + 4.0f * s;
}

// The world map window. Every shape on the map is drawn in window pixels with drawUiTris.
void Renderer::drawWorldMap(const WorldMapLayout& L, const std::vector<MapDot>& dots, float cx, float cz, float span,
                            const WorldMapMarks& marks) {
    pollOverview();
    const float s = uiH_ / 720.0f;
    const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f };
    const RGBA black{ 0.0f, 0.0f, 0.0f, 1.0f };
    const Rect& w = L.window;
    const Rect& r = L.map;

    // The window: a pink edge, a dark body, and the title bar with its X
    drawRect(Rect{ w.x - 1.5f * s, w.y - 1.5f * s, w.w + 3.0f * s, w.h + 3.0f * s }, theme::alpha(theme::PINK, 0.95f));
    drawRect(w, RGBA{ 0.075f, 0.03f, 0.04f, 0.97f });
    drawTitleBar(L.bar, ICON_NONE, lang("UILABEL_WORLD_MAP"));
    drawCloseButton(L.close);

    drawRect(r, RGBA{ 0.10f, 0.20f, 0.30f, 1.0f });   // sea, where the view is past the edge of the level
    if (span <= 0.0f || r.w <= 0.0f || r.h <= 0.0f) return;

    // The view in tiles. Its left edge is the largest x and its top edge the largest z.
    const float across = mapSpanAcross(r, span);
    const float xHi = cx + across * 0.5f, xLo = cx - across * 0.5f;
    const float zHi = cz + span * 0.5f, zLo = cz - span * 0.5f;
    auto sx = [&](float x) { return r.x + (xHi - x) / across * r.w; };
    auto sy = [&](float z) { return r.y + (zHi - z) / span * r.h; };

    if (overviewTex_ && overviewSize_ > 0.0f) {
        const float size = overviewSize_, ext = overviewExtent_;
        auto u = [&](float x) { return (size - x) / ext; };
        auto v = [&](float z) { return (size - z) / ext; };
        // The part of the view inside the level
        const float ax = std::min(xHi, size), bx = std::max(xLo, 0.0f);
        const float az = std::min(zHi, size), bz = std::max(zLo, 0.0f);
        if (ax > bx && az > bz) drawUiQuad(overviewTex_, sx(ax), sy(az), sx(bx), sy(bz), u(ax), v(az), u(bx), v(bz), white);
    }

    // A thick line from one point to another, its ends pushed out by half its width so corners meet
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
    // A ring of the given radius and width
    auto ring = [&](float x, float y, float radius, float width, RGBA c) {
        const int segs = 32;
        const float rIn = radius - width * 0.5f, rOut = radius + width * 0.5f;
        for (int i = 0; i < segs; ++i) {
            const float a0 = 6.2831853f * i / segs, a1 = 6.2831853f * (i + 1) / segs;
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

    // Every prop, of any kind: a purple square with a black edge, under the units
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

    // The units: a green dot for the player's, the white dot tinted red or gray, with its black ring, for the rest
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

    const float edge = std::max(1.0f, 1.0f * s);   // the black edge around each line
    // The green X where the last move order went, fading out
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

    // The camera's pivot: a hollow ring round the point the camera turns about, and a hollow triangle over it pointing
    // the way the camera looks. On the map the larger x is to the left and the larger z to the top, so the camera's
    // ground direction (-sin yaw, -cos yaw) points along (sin yaw, cos yaw) in window pixels.
    {
        const float px = sx(marks.camX), py = sy(marks.camZ);
        if (inMap(px, py)) {
            const float rad = 7.0f * s, lw = std::max(1.5f, 1.5f * s);
            const float fx = std::sin(marks.camYaw), fy = std::cos(marks.camYaw);   // ahead
            const float qx = -fy, qy = fx;                                            // to one side
            auto at = [&](float ahead, float side, float& ox, float& oy) {
                ox = px + fx * ahead * rad + qx * side * rad;
                oy = py + fy * ahead * rad + qy * side * rad;
            };
            float ax, ay, bx, by, cx2, cy2;
            at(2.1f, 0.0f, ax, ay);      // the tip
            at(-0.45f, 1.8f, bx, by);    // the base corners
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

void Renderer::drawUnitPanel(World& w, const UnitPanelLayout& L, const std::vector<Entity>& units, bool running,
                             int repView) {
    const float s = uiH_ / 720.0f;
    const float bgAlpha = 0.5f;
    drawRect(L.panel, RGBA{ 0.085f, 0.035f, 0.045f, bgAlpha });
    drawTitleBar(L.title, ICON_SQUAD_PANEL, lang("UILABEL_SQUAD_PANEL"));
    drawCollapseButton(L.toggle, L.collapsed);
    if (L.collapsed) return;

    // The tabs: the one shown is lit
    {
        const float th = listTextHeight("Ag");
        auto tab = [&](const Rect& r, const std::string& label, bool on) {
            if (r.w <= 0.0f) return;
            const bool hot = !on && r.contains(mouseX_, mouseY_);
            if (hot) noteHover(r);
            drawRect(r, on ? RGBA{ 0.353f, 0.235f, 0.110f, 0.95f } : RGBA{ 0.16f, 0.07f, 0.085f, hot ? 1.0f : 0.9f });
            if (on) drawRect(Rect{ r.x, r.y + r.h - 2.0f, r.w, 2.0f }, theme::GREEN);
            const std::string t = fitText(label, r.w - 6.0f * s, false);
            drawListText(t, r.x + (r.w - listTextWidth(t)) * 0.5f, r.y + (r.h - th) * 0.5f,
                         on ? RGBA{ 1.0f, 1.0f, 1.0f, 1.0f } : RGBA{ 0.80f, 0.72f, 0.64f, 1.0f });
        };
        tab(L.tabSquad, lang("UILABEL_SQUAD_TAB"), L.tab == FACTION_TAB_SQUAD);
        tab(L.tabReputation, lang("UILABEL_REPUTATION"), L.tab == FACTION_TAB_REPUTATION);
    }
    if (L.tab == FACTION_TAB_REPUTATION) {
        drawReputationList(L, repView);
        return;
    }

    // Walk and run: the lit one is the pace the squad is moving at
    auto mode = [&](const Rect& r, const std::string& text, bool on) {
        if (r.contains(mouseX_, mouseY_)) noteHover(r);
        drawRect(r, on ? RGBA{ 0.74f, 0.54f, 0.24f, 1.0f } : RGBA{ 0.19f, 0.08f, 0.10f, bgAlpha });
        const float tw = listTextWidth(text), th = listTextHeight(text);
        drawListText(text, r.x + (r.w - tw) * 0.5f, r.y + (r.h - th) * 0.5f, RGBA{ 1.0f, 1.0f, 1.0f, 1.0f });
    };
    // One toggle for the pace, lit while the squad runs, and the View Map button beside it
    mode(L.paceButton, running ? lang("UILABEL_PACE_RUN") : lang("UILABEL_PACE_WALK"), running);
    // Optimize All, beside it: opens a dialog to confirm
    {
        const std::string label = lang("UILABEL_OPTIMIZE_ALL");
        const std::string shown = fitText(label, L.optimizeAllButton.w - 6.0f * s, false);
        if (shown != label) noteTip(label, L.optimizeAllButton);
        mode(L.optimizeAllButton, shown, false);
    }

    for (const UnitCell& c : L.cells) {
        const Entity e = units[c.index];
        const bool selected = w.has<Selected>(e);
        const Rect frame{ c.rect.x - 2.0f, c.rect.y - 2.0f, c.rect.w + 4.0f, c.rect.h + 4.0f };
        if (c.rect.contains(mouseX_, mouseY_)) noteHover(c.rect);
        // A selected unit's frame is solid purple, so it stands out from the see-through frames
        drawRect(frame, selected ? RGBA{ 0.74f, 0.54f, 0.24f, 1.0f } : RGBA{ 0.42f, 0.20f, 0.24f, bgAlpha });
        drawRect(Rect{ c.rect.x, c.rect.y, c.rect.w, c.rect.w }, RGBA{ 0.16f, 0.07f, 0.085f, bgAlpha });

        // Portrait fitted inside the square, keeping its shape
        const TextTex& g = unitPortrait(w, e);
        drawPortrait(g, Rect{ c.rect.x, c.rect.y, c.rect.w, c.rect.w }, unitSaturation(w, e));

        const Needs* nd = w.get<Needs>(e);
        // Low health: a red gradient rises from the bottom of the portrait by the share of health lost, as in the sidebar
        if (nd && nd->maxHp > 0 && nd->hp < nd->maxHp) {
            const float lost = std::clamp(1.0f - (float)nd->hp / (float)nd->maxHp, 0.0f, 1.0f);
            const float gh = c.rect.w * lost;
            if (gh > 0.0f) {
                drawRectGradient(Rect{ c.rect.x, c.rect.y + c.rect.w - gh, c.rect.w, gh },
                                 RGBA{ 0.85f, 0.10f, 0.10f, 0.0f }, RGBA{ 0.85f, 0.10f, 0.10f, 0.65f });
            }
        }

        // Health bar under the portrait
        const float frac = (nd && nd->maxHp > 0) ? std::clamp((float)nd->hp / (float)nd->maxHp, 0.0f, 1.0f) : 0.0f;
        const float barY = c.rect.y + c.rect.w + 3.0f * s, barH = 4.0f * s;
        drawRect(Rect{ c.rect.x, barY, c.rect.w, barH }, RGBA{ 0.24f, 0.11f, 0.13f, bgAlpha });
        drawRect(Rect{ c.rect.x, barY, c.rect.w * frac, barH }, RGBA{ 0.80f, 0.26f, 0.26f, 1.0f });

        // Name under the bar, cut at a word with "..." to fit the cell
        const Name* nm = w.get<Name>(e);
        const std::string full = nm ? nm->text : std::string(lang("UILABEL_UNNAMED_UNIT"));
        const std::string label = fitText(full, c.rect.w, false);
        if (label != full) noteTip(full, c.rect);
        drawListText(label, c.rect.x, c.rect.y + c.rect.w + 8.0f * s, RGBA{ 1.0f, 1.0f, 1.0f, 1.0f });
    }

    // The scroll bar: the track and its thumb, with an arrow button at each end that moves the list a row. The arrows
    // dim at the end of the list.
    if (L.track.h > 0.0f || L.scrollUp.h > 0.0f) {
        drawRect(L.track, RGBA{ 0.24f, 0.10f, 0.12f, bgAlpha });
        if (L.scrollable()) drawRect(L.thumb, L.thumb.contains(mouseX_, mouseY_) ? RGBA{ 0.96f, 0.88f, 0.76f, 1.0f }
                                                                                : RGBA{ 0.80f, 0.70f, 0.60f, 1.0f });
        auto arrow = [&](const Rect& r, bool up, bool can) {
            if (r.h <= 0.0f) return;
            const bool hot = can && r.contains(mouseX_, mouseY_);
            if (hot) noteHover(r);
            drawRect(r, hot ? RGBA{ 0.46f, 0.22f, 0.26f, 1.0f } : RGBA{ 0.29f, 0.13f, 0.16f, 0.9f });
            const RGBA c = can ? RGBA{ 1.0f, 1.0f, 1.0f, 1.0f } : RGBA{ 0.56f, 0.49f, 0.45f, 0.6f };
            const float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.5f, hw = r.w * 0.3f, hh = r.h * 0.22f;
            if (up) drawUiTris({ { cx, cy - hh, c }, { cx + hw, cy + hh, c }, { cx - hw, cy + hh, c } });
            else drawUiTris({ { cx - hw, cy - hh, c }, { cx + hw, cy - hh, c }, { cx, cy + hh, c } });
        };
        arrow(L.scrollUp, true, L.scrollRow > 0);
        arrow(L.scrollDown, false, L.scrollRow < L.maxScroll());
    }
}

// The Reputation tab: the faction shown, with its colour, then each faction's reputation with it. The faction shown is
// always at 100 with itself, so its own row reads (...). Each value is red where the two fight on sight, green where
// they are allies, and gray between; its tooltip says which. A row is pressed to show that faction's reputations.
void Renderer::drawReputationList(const UnitPanelLayout& L, int repView) {
    const float s = uiH_ / 720.0f;
    const float th = listTextHeight("Ag");
    const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f };
    const RGBA gray{ 0.80f, 0.72f, 0.64f, 1.0f };
    auto swatch = [&](float x, float cy, float d, int faction) {
        const FactionColor c = factionColor(faction);
        drawRect(Rect{ x - 1.0f, cy - d * 0.5f - 1.0f, d + 2.0f, d + 2.0f }, RGBA{ 0.0f, 0.0f, 0.0f, 0.9f });
        drawRect(Rect{ x, cy - d * 0.5f, d, d }, RGBA{ c.r, c.g, c.b, 1.0f });
    };
    // The heading: the faction shown
    {
        const Rect& h = L.repHeading;
        drawRect(h, RGBA{ 0.0f, 0.0f, 0.0f, 0.25f });
        const float d = 12.0f * s;
        swatch(h.x + 6.0f * s, h.y + h.h * 0.5f, d, repView);
        const std::string name = fitText(factionName(repView), h.w - d - 16.0f * s, false);
        drawListText(name, h.x + d + 12.0f * s, h.y + (h.h - th) * 0.5f, RGBA{ 0.961f, 0.788f, 0.459f, 1.0f });
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
        drawRect(r, self ? RGBA{ 0.353f, 0.235f, 0.110f, 0.55f } : RGBA{ 0.16f, 0.07f, 0.085f, hot ? 0.95f : 0.6f });
        const float d = 10.0f * s;
        swatch(r.x + 6.0f * s, r.y + r.h * 0.5f, d, other);
        const int value = reputation(repView, other);
        const std::string valueText = self ? std::string("(...)") : std::to_string(value);
        const RGBA valueTint = self ? gray
                             : value <= REP_HOSTILE ? RGBA{ 1.0f, 0.42f, 0.42f, 1.0f }
                             : value >= REP_ALLIED ? RGBA{ 0.5f, 0.95f, 0.5f, 1.0f } : gray;
        const float vw = listTextWidth(valueText);
        const float nameX = r.x + d + 12.0f * s;
        const std::string name = fitText(factionName(other), r.x + r.w - 8.0f * s - vw - 8.0f * s - nameX, false);
        drawListText(name, nameX, r.y + (r.h - th) * 0.5f, white);
        drawListText(valueText, r.x + r.w - 8.0f * s - vw, r.y + (r.h - th) * 0.5f, valueTint);
        if (!self) {
            noteTip(lang(value <= REP_HOSTILE ? "UILABEL_REP_HOSTILE" : value >= REP_ALLIED ? "UILABEL_REP_ALLIED" : "UILABEL_REP_NEUTRAL"),
                    Rect{ r.x + r.w - 8.0f * s - vw, r.y, vw + 8.0f * s, r.h });
        }
    }
}

GameLogLayout Renderer::gameLogLayout(int winW, int winH, int tab, bool filterOpen, int offset, int lineCount,
                                          bool collapsed, const PanelPlace& place) {
    const float s = winH / 720.0f;
    const float pad = 8.0f * s, scrollW = 10.0f * s, gap = 4.0f * s;
    const float barH = PANEL_BAR_H * s, tabH = 20.0f * s;
    const float lineH = listTextHeight("Ag") + 2.0f * s;
    // Its open size and place: the top left corner by default. As many lines as its height has room for, under the tabs.
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

    // Its bar runs along its top or its bottom; folded, it is the bar alone. y and h are the body's: the panel less its bar.
    const float w = open.w;
    L.panel = panelShownRect(open, collapsed, place.barBottom, barH);
    L.bar = panelBarRect(open, place.barBottom, barH);
    const Rect bodyR = panelBodyRect(open, place.barBottom, barH);
    L.bodyTop = bodyR.y;
    L.bodyBottom = bodyR.y + bodyR.h;
    const float x = open.x, y = bodyR.y - barH;   // laid out as if the bar were above the body
    const float h = bodyR.h + barH;
    L.toggle = BarToggle(L.bar, s);
    L.lock = BarLock(L.bar, s);
    L.collapsed = collapsed;
    L.textX = x + pad;
    L.textW = w - 2.0f * pad - scrollW - gap;

    // Which lines are visible: the window ends L.offset lines above the newest
    const int end = lineCount - L.offset;
    L.shown = std::min(linesVisible, std::max(0, end));
    L.first = end - L.shown;

    const float trackX = x + w - pad * 0.5f - scrollW * 0.5f;
    const float trackY = y + barH + tabH + pad, trackH = h - barH - tabH - 2.0f * pad;
    L.track = Rect{ trackX, trackY, scrollW * 0.5f, trackH };
    if (L.scrollable()) {
        const float share = (float)linesVisible / (float)lineCount;
        const float thumbH = std::max(16.0f * s, trackH * share);
        // At offset 0 the thumb sits at the bottom, and at the top of its range it sits at the top
        const float frac = L.maxOffset() > 0 ? (float)L.offset / (float)L.maxOffset() : 0.0f;
        L.thumb = Rect{ trackX, trackY + (trackH - thumbH) * (1.0f - frac), scrollW * 0.5f, thumbH };
    }

    // The tabs under the bar. The Filter button sits at the right of the row.
    const float tabY = y + barH;
    // Each tab is as wide as its name needs
    auto tabW = [&](const char* key) { return std::max(48.0f * s, listTextWidth(lang(key)) + 16.0f * s); };
    L.tabMessage = Rect{ x + pad * 0.5f, tabY + 2.0f * s, tabW("UILABEL_MESSAGE"), tabH - 2.0f * s };
    L.tabGame = Rect{ L.tabMessage.x + L.tabMessage.w + 4.0f * s, tabY + 2.0f * s, tabW("UILABEL_GAME"), tabH - 2.0f * s };
    L.tabTime = Rect{ L.tabGame.x + L.tabGame.w + 4.0f * s, tabY + 2.0f * s, tabW("UILABEL_TIME"), tabH - 2.0f * s };
    // The Filter button: at the right of the row, but never over the Time tab
    {
        const float tabsEnd = L.tabTime.x + L.tabTime.w + 4.0f * s;
        const float fw = listTextWidth(lang("UILABEL_FILTER")) + 14.0f * s;
        const float fx = std::max(tabsEnd, x + w - pad - scrollW - fw);
        L.filterBtn = Rect{ fx, tabY + 2.0f * s, std::max(0.0f, std::min(fw, x + w - 2.0f * s - fx)), tabH - 2.0f * s };
    }

    // The filter menu opens downward from the Filter button, or upward where the window has no room below it
    if (L.filterOpen) {
        const float rowH = listTextHeight("Ag") + 6.0f * s;
        // As wide as its longest row needs: the indented name, a gap, then On or Off
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
    const float s = uiH_ / 720.0f;
    const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f };
    const RGBA gray{ 0.74f, 0.66f, 0.59f, 1.0f };
    const RGBA lit{ 0.26f, 0.16f, 0.07f, 0.95f };
    const RGBA dim{ 0.14f, 0.06f, 0.075f, 0.90f };

    drawRect(L.panel, RGBA{ 0.085f, 0.035f, 0.045f, 0.86f });
    drawTitleBar(L.bar, ICON_GAME_LOG, lang("UILABEL_INFORMATION_PANEL"));
    drawCollapseButton(L.toggle, L.collapsed);
    if (L.collapsed) return;

    // The two tabs: the one shown is lit
    auto tab = [&](const Rect& r, const std::string& label, bool selected) {
        if (r.contains(mx, my)) noteHover(r);
        drawRect(r, selected ? lit : dim);
        drawListText(label, r.x + (r.w - listTextWidth(label)) * 0.5f, r.y + (r.h - listTextHeight("Ag")) * 0.5f,
                     selected ? white : gray);
    };
    tab(L.tabMessage, lang("UILABEL_MESSAGE"), L.tab == 0);
    tab(L.tabGame, lang("UILABEL_GAME"), L.tab == 1);
    tab(L.tabTime, lang("UILABEL_TIME"), L.tab == 2);
    if (L.tab == 1) drawButton(L.filterBtn, lang("UILABEL_FILTER"), true, L.filterBtn.contains(mx, my) || L.filterOpen);

    // The rows are centred in the space under the tabs
    const float tabH = 20.0f * s;
    const float bodyTop = L.bodyTop + tabH;

    // The Time tab: the time of day large, then the date written out, the date as numbers and the days passed, each
    // line centred, the block centred in the space under the tabs
    if (L.tab == 2) {
        const RGBA gray{ 0.85f, 0.78f, 0.70f, 1.0f };
        const float bigH = uiTextHeight("Ag"), smallH = listTextHeight("Ag"), gap = 4.0f * s;
        const float blockH = bigH + 3.0f * (gap + smallH);
        const float bodyH = L.bodyBottom - bodyTop;
        float y = bodyTop + std::max(0.0f, (bodyH - blockH) * 0.5f);
        const float midX = L.panel.x + L.panel.w * 0.5f, maxW = L.panel.w - 16.0f * s;
        drawClockText(time.clock, midX - clockTextWidth(time.clock) * 0.5f, y, white);
        y += bigH + gap;
        for (const std::string* line : { &time.date, &time.ymd, &time.days }) {
            const std::string t = fitText(*line, maxW, false);
            drawListText(t, midX - listTextWidth(t) * 0.5f, y, line == &time.date ? white : gray);
            y += smallH + gap;
        }
        return;
    }
    const float top = bodyTop + (L.bodyBottom - bodyTop - L.linesVisible * L.lineH) * 0.5f;
    const int count = (int)rows.size();
    for (int k = 0; k < L.shown && L.first + k < count; ++k) {
        const LogLine& ln = *rows[(size_t)(L.first + k)];
        // Bottom-aligned: the newest line is the last row
        const int row = L.linesVisible - L.shown + k;
        drawListText(fitText(ln.text, L.textW, false), L.textX, top + row * L.lineH, logTint(ln.tint));
    }
    if (L.scrollable()) {
        drawRect(L.track, RGBA{ 0.20f, 0.09f, 0.11f, 0.9f });
        drawRect(L.thumb, RGBA{ 0.910f, 0.376f, 0.431f, 0.9f });
    }

    // The filter menu, over the lines. Each kind reads On or Off.
    if (L.filterOpen) {
        drawRect(L.filterMenu, RGBA{ 0.075f, 0.03f, 0.04f, 0.97f });
        const float th = listTextHeight("Ag");
        for (const GameLogLayout::FilterRow& fr : L.filterRows) {
            if (fr.heading) {
                drawListText(fr.label, fr.r.x + 6.0f * s, fr.r.y + (fr.r.h - th) * 0.5f, RGBA{ 0.961f, 0.788f, 0.459f, 1.0f });
                continue;
            }
            if (fr.r.contains(mx, my)) {
                noteHover(fr.r);
                drawRect(fr.r, RGBA{ 1.0f, 1.0f, 1.0f, 0.10f });
            }
            const bool on = filter.on[(int)fr.kind];
            drawListText(fr.label, fr.r.x + 16.0f * s, fr.r.y + (fr.r.h - th) * 0.5f, white);
            const std::string state = on ? lang("UILABEL_ON") : lang("UILABEL_OFF");
            drawListText(state, fr.r.x + fr.r.w - 10.0f * s - listTextWidth(state), fr.r.y + (fr.r.h - th) * 0.5f,
                         on ? RGBA{ 0.6f, 1.0f, 0.6f, 1.0f } : gray);
        }
    }
}
