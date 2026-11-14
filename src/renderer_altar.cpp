#include "renderer_internal.h"
#include "altar_ui.h"
#include "squads.h"
#include "ghosts.h"

using namespace render_detail;

namespace {
// A unit's combat levels added together, and all its other levels added together
void levelTotals(World& w, ObjectId u, int& combat, int& other) {
    combat = other = 0;
    const Stats* st = w.get<Stats>(u);
    if (!st) return;
    for (int i = 0; i < STAT_COUNT; ++i) {
        const int lv = levelOf(*st, (StatId)i);
        if (statInfo((StatId)i).group == StatGroup::Combat) combat += lv;
        else other += lv;
    }
}

std::string unitNameOf(World& w, ObjectId u) {
    const Name* nm = w.get<Name>(u);
    return nm && !nm->text.empty() ? nm->text : std::string(lang("UILABEL_UNNAMED_UNIT"));
}
}

void Renderer::drawAltarRoster(World& w, const AltarLayout& L, const std::array<std::vector<ObjectId>, 3>& cols,
                               const std::map<ObjectId, int>& moves, const AltarTotals& totals, ObjectId pinned,
                               float mx, float my) {
    const float s = uiScale();
    const float th = listTextHeight("Ag");
    drawScrim();
    drawThemedPanel(L.box);
    drawListText(fitText(lang("UILABEL_ALTAR_ROSTER"), L.title.w, false), L.title.x, L.title.y + (L.title.h - th) * 0.5f, WHITE);
    if (L.close.contains(mx, my)) noteHover(L.close);
    drawCloseButton(L.close);

    {
        const bool overSoft = totals.citizens > totals.softCap;
        const std::string text = langf("UILABEL_ALTAR_TOTALS", { std::to_string(totals.military), std::to_string(MILITARY_MAX),
                                                                 std::to_string(totals.citizens), std::to_string(totals.softCap),
                                                                 std::to_string(totals.hardCap), std::to_string(totals.fortified) });
        const std::string shown = fitTextTip(text, L.totals.w, L.totals);
        drawListText(shown, L.totals.x, L.totals.y + (L.totals.h - th) * 0.5f,
                     overSoft ? RGBA{ 1.0f, 0.62f, 0.35f, 1.0f } : theme::TEXT_DIM);
        if (overSoft) noteTip(lang("UILABEL_ALTAR_OVER_SOFT_CAP"), L.totals);
    }

    const char* heads[3] = { "UILABEL_ALTAR_MILITARY", "UILABEL_ALTAR_CITIZENS", "UILABEL_ALTAR_FORTIFIED" };
    const char* tips[3] = { "UILABEL_ALTAR_MILITARY_TIP", "UILABEL_ALTAR_CITIZENS_TIP", "UILABEL_ALTAR_FORTIFIED_TIP" };
    for (int c = 0; c < 3; ++c) {
        const Rect& h = L.heads[(size_t)c];
        drawRect(h, theme::alpha(theme::GOLD_DEEP, 0.85f));
        const std::string countText = std::to_string(cols[(size_t)c].size());
        drawListText(fitText(lang(heads[c]), h.w - listTextWidth(countText) - 16.0f * s, false), h.x + 6.0f * s,
                     h.y + (h.h - th) * 0.5f, theme::GOLD);
        drawListText(countText, h.x + h.w - 6.0f * s - listTextWidth(countText), h.y + (h.h - th) * 0.5f, WHITE);
        noteTip(lang(tips[c]), h);
        drawRect(L.lists[(size_t)c], RGBA{ 0.0f, 0.0f, 0.0f, 0.22f });

        for (const AltarRow& r : L.rows[(size_t)c]) {
            if (r.index < 0 || r.index >= (int)cols[(size_t)c].size()) continue;
            const ObjectId u = cols[(size_t)c][(size_t)r.index];
            const bool ghost = moves.count(u) > 0;
            std::string tag;
            if (ghost) tag = lang(c != ALTAR_MILITARY ? "UILABEL_ALTAR_TO_CITIZENS" : "UILABEL_ALTAR_TO_MILITARY");
            else if (c == ALTAR_MILITARY && squadOf(w, u) >= 0) tag = langf("UILABEL_ALTAR_SQUAD_TAG", { std::to_string(squadOf(w, u) + 1) });
            // The columns sit in a row, Military, Citizens, Fortified: a unit's arrow moves it toward the citizens (or on to the
            // Fortified, from them), and a citizen has a second one, to the left of its group, back to the Military
            const bool right = c != ALTAR_FORTIFIED;
            drawRosterRow(w, r, u, ghost, tag, right,
                          ghost ? "UILABEL_ALTAR_UNDO_TIP" : c == ALTAR_MILITARY ? "UILABEL_ALTAR_TO_CITIZENS_TIP"
                          : c == ALTAR_CITIZEN ? "UILABEL_ALTAR_TO_FORTIFIED_TIP" : "UILABEL_ALTAR_TO_CITIZENS_TIP",
                          pinned, mx, my);
        }
        if (cols[(size_t)c].empty()) {
            const std::string none = lang("UILABEL_ALTAR_EMPTY");
            const Rect& list = L.lists[(size_t)c];
            drawListText(none, list.x + (list.w - listTextWidth(none)) * 0.5f, list.y + 12.0f * s, theme::TEXT_DIM);
        }
        const int count = (int)cols[(size_t)c].size();
        if (count > L.visible[(size_t)c] && L.visible[(size_t)c] > 0) {
            const Rect& list = L.lists[(size_t)c];
            const float barW = 4.0f * s;
            const Rect track{ list.x + list.w - barW, list.y, barW, list.h };
            const float share = (float)L.visible[(size_t)c] / (float)count;
            const float at = (float)L.first[(size_t)c] / (float)std::max(1, count - L.visible[(size_t)c]);
            const float thumbH = std::max(8.0f * s, track.h * share);
            drawRect(track, RGBA{ 0.24f, 0.10f, 0.12f, 0.8f });
            drawRect(Rect{ track.x, track.y + (track.h - thumbH) * at, barW, thumbH }, RGBA{ 0.80f, 0.70f, 0.60f, 1.0f });
        }
    }

    const bool anyMoves = !moves.empty();
    drawButton(L.apply, langf("UILABEL_ALTAR_APPLY", { std::to_string(moves.size()) }), anyMoves, anyMoves && L.apply.contains(mx, my));
    drawButton(L.cancel, lang("UILABEL_CLOSE"), true, L.cancel.contains(mx, my));
}

void Renderer::drawRosterRow(World& w, const AltarRow& r, ObjectId u, bool ghost, const std::string& tag, bool pointRight,
                             const char* moveTipKey, ObjectId pinned, float mx, float my) {
    const float s = uiScale();
    const float th = listTextHeight("Ag");
    const bool hot = r.row.contains(mx, my);
    if (hot) noteHover(r.row);
    RGBA bg = hot ? RGBA{ 0.26f, 0.12f, 0.14f, 0.95f } : RGBA{ 0.16f, 0.07f, 0.085f, 0.85f };
    if (u == pinned) bg = theme::alpha(theme::GOLD_DEEP, 0.9f);
    drawRect(r.row, bg);
    if (ghost) {
        // A ghost: dashed gold edges and faded contents, until Apply
        const float dash = 6.0f * s, t = std::max(1.0f, 1.5f * s);
        for (float x = r.row.x; x < r.row.x + r.row.w; x += 2.0f * dash) {
            const float dw = std::min(dash, r.row.x + r.row.w - x);
            drawRect(Rect{ x, r.row.y, dw, t }, theme::GOLD);
            drawRect(Rect{ x, r.row.y + r.row.h - t, dw, t }, theme::GOLD);
        }
    }
    const float fade = ghost ? 0.55f : 1.0f;
    drawRect(r.portrait, theme::TAB_OFF);
    drawPortrait(unitPortrait(w, u), r.portrait, unitSaturation(w, u) * fade);
    if (ghost) drawRect(r.portrait, RGBA{ 0.0f, 0.0f, 0.0f, 0.4f });
    noteTip(lang("UILABEL_ALTAR_RENAME_TIP"), r.portrait);

    int combat = 0, other = 0;
    levelTotals(w, u, combat, other);
    const float textX = r.portrait.x + r.portrait.w + 8.0f * s;
    const float tagW = tag.empty() ? 0.0f : listTextWidth(tag) + 8.0f * s;
    const float rightEdge = r.back.w > 0.0f && !ghost ? r.back.x : r.group.w > 0.0f && !ghost ? r.group.x : r.move.x;
    const float textW = rightEdge - 6.0f * s - tagW - textX;
    const RGBA nameTint = ghost ? RGBA{ 1.0f, 1.0f, 1.0f, 0.6f } : WHITE;
    const RGBA dimTint = ghost ? theme::alpha(theme::TEXT_DIM, 0.6f) : theme::TEXT_DIM;
    const float top = r.row.y + r.row.h * 0.5f - th - 1.0f * s;
    drawListText(fitText(unitNameOf(w, u), textW, false), textX, top, nameTint);
    const std::string levels = langf("UILABEL_ALTAR_LEVELS", { std::to_string(combat), std::to_string(other) });
    drawListText(fitText(levels, textW, false), textX, top + th + 2.0f * s, dimTint);
    if (!tag.empty()) {
        drawListText(tag, rightEdge - 6.0f * s - listTextWidth(tag), r.row.y + (r.row.h - th) * 0.5f, ghost ? theme::GOLD : theme::TEXT_DIM);
    }
    // A citizen of yours: the behavior group it follows
    if (const Citizen* cz = w.get<Citizen>(u); cz && !ghost && r.group.w > 0.0f) {
        const bool groupHot = r.group.contains(mx, my);
        if (groupHot) noteHover(r.group);
        drawButton(r.group, langf("UILABEL_ALTAR_GROUP_TAG", { std::to_string(cz->group + 1) }), true, groupHot);
        noteTip(lang("UILABEL_ALTAR_GROUP_TIP"), r.group);
    }

    // The move button points to the other column
    const bool moveHot = r.move.contains(mx, my);
    if (moveHot) noteHover(r.move);
    drawRect(r.move, moveHot ? RGBA{ 0.46f, 0.22f, 0.26f, 1.0f } : RGBA{ 0.29f, 0.13f, 0.16f, 0.95f });
    const float cx = r.move.x + r.move.w * 0.5f, cy = r.move.y + r.move.h * 0.5f;
    const float hw = r.move.w * 0.22f, hh = r.move.h * 0.3f;
    const RGBA ac = ghost ? theme::GOLD : WHITE;
    if (pointRight) drawUiTris({ { cx - hw, cy - hh, ac }, { cx + hw, cy, ac }, { cx - hw, cy + hh, ac } });
    else drawUiTris({ { cx + hw, cy - hh, ac }, { cx - hw, cy, ac }, { cx + hw, cy + hh, ac } });
    noteTip(lang(moveTipKey), r.move);

    // A citizen's way back to the military
    if (r.back.w > 0.0f && !ghost) {
        const bool backHot = r.back.contains(mx, my);
        if (backHot) noteHover(r.back);
        drawRect(r.back, backHot ? RGBA{ 0.46f, 0.22f, 0.26f, 1.0f } : RGBA{ 0.29f, 0.13f, 0.16f, 0.95f });
        const float bx = r.back.x + r.back.w * 0.5f, by = r.back.y + r.back.h * 0.5f;
        drawUiTris({ { bx + hw, by - hh, WHITE }, { bx - hw, by, WHITE }, { bx + hw, by + hh, WHITE } });
        noteTip(lang("UILABEL_ALTAR_TO_MILITARY_TIP"), r.back);
    }
}

void Renderer::drawSquadsRoster(World& w, const SquadsLayout& SL, const std::array<std::vector<ObjectId>, 2>& cols,
                                const std::map<ObjectId, int>& moves, const SquadsRosterText& text, ObjectId pinned,
                                float mx, float my) {
    const AltarLayout& L = SL.roster;
    const float s = uiScale();
    const float th = listTextHeight("Ag");
    drawScrim();
    drawThemedPanel(L.box);
    drawListText(fitText(lang("UILABEL_MANAGE_SQUADS"), L.title.w, false), L.title.x, L.title.y + (L.title.h - th) * 0.5f, WHITE);
    if (L.close.contains(mx, my)) noteHover(L.close);
    drawCloseButton(L.close);
    {
        const std::string shown = fitTextTip(text.totals, L.totals.w, L.totals);
        drawListText(shown, L.totals.x, L.totals.y + (L.totals.h - th) * 0.5f, text.warn ? RGBA{ 1.0f, 0.62f, 0.35f, 1.0f } : theme::TEXT_DIM);
    }
    for (int c = 0; c < 2; ++c) {
        const Rect& h = L.heads[(size_t)c];
        drawRect(h, theme::alpha(theme::GOLD_DEEP, 0.85f));
        const std::string& countText = text.counts[(size_t)c];
        const float countX = SL.prev[(size_t)c].x - 6.0f * s - listTextWidth(countText);
        drawListText(fitText(text.heads[(size_t)c], countX - h.x - 12.0f * s, false), h.x + 6.0f * s, h.y + (h.h - th) * 0.5f, theme::GOLD);
        drawListText(countText, countX, h.y + (h.h - th) * 0.5f, text.full[(size_t)c] ? RGBA{ 1.0f, 0.62f, 0.35f, 1.0f } : WHITE);
        // The arrows that pick the squad this column shows
        for (int side = 0; side < 2; ++side) {
            const Rect& b = side == 0 ? SL.prev[(size_t)c] : SL.next[(size_t)c];
            const bool hot = b.contains(mx, my);
            if (hot) noteHover(b);
            drawRect(b, hot ? RGBA{ 0.46f, 0.22f, 0.26f, 1.0f } : RGBA{ 0.29f, 0.13f, 0.16f, 0.95f });
            const float cx = b.x + b.w * 0.5f, cy = b.y + b.h * 0.5f, hw = b.w * 0.2f, hh = b.h * 0.28f;
            if (side == 1) drawUiTris({ { cx - hw, cy - hh, WHITE }, { cx + hw, cy, WHITE }, { cx - hw, cy + hh, WHITE } });
            else drawUiTris({ { cx + hw, cy - hh, WHITE }, { cx - hw, cy, WHITE }, { cx + hw, cy + hh, WHITE } });
            noteTip(lang("UILABEL_SQUADS_PICK_TIP"), b);
        }
        drawRect(L.lists[(size_t)c], RGBA{ 0.0f, 0.0f, 0.0f, 0.22f });
        for (const AltarRow& r : L.rows[(size_t)c]) {
            if (r.index < 0 || r.index >= (int)cols[(size_t)c].size()) continue;
            const ObjectId u = cols[(size_t)c][(size_t)r.index];
            const bool ghost = moves.count(u) > 0;
            std::string tag;
            if (ghost) {
                const int from = squadOf(w, u);
                tag = from >= 0 ? langf("UILABEL_SQUADS_FROM", { std::to_string(from + 1) }) : std::string();
            }
            drawRosterRow(w, r, u, ghost, tag, c == 0, "UILABEL_SQUADS_MOVE_TIP", pinned, mx, my);
        }
        if (cols[(size_t)c].empty()) {
            const std::string none = lang("UILABEL_ALTAR_EMPTY");
            const Rect& list = L.lists[(size_t)c];
            drawListText(none, list.x + (list.w - listTextWidth(none)) * 0.5f, list.y + 12.0f * s, theme::TEXT_DIM);
        }
        const int count = (int)cols[(size_t)c].size();
        if (count > L.visible[(size_t)c] && L.visible[(size_t)c] > 0) {
            const Rect& list = L.lists[(size_t)c];
            const float barW = 4.0f * s;
            const Rect track{ list.x + list.w - barW, list.y, barW, list.h };
            const float share = (float)L.visible[(size_t)c] / (float)count;
            const float at = (float)L.first[(size_t)c] / (float)std::max(1, count - L.visible[(size_t)c]);
            const float thumbH = std::max(8.0f * s, track.h * share);
            drawRect(track, RGBA{ 0.24f, 0.10f, 0.12f, 0.8f });
            drawRect(Rect{ track.x, track.y + (track.h - thumbH) * at, barW, thumbH }, RGBA{ 0.80f, 0.70f, 0.60f, 1.0f });
        }
    }
    drawButton(L.apply, langf("UILABEL_ALTAR_APPLY", { std::to_string(text.moveCount) }), text.canApply, text.canApply && L.apply.contains(mx, my));
    drawButton(L.cancel, lang("UILABEL_CLOSE"), true, L.cancel.contains(mx, my));
}

void Renderer::drawGhostsWindow(World& w, const AltarLayout& L, const std::vector<ItemStack>& bones, const std::vector<size_t>& ghosts,
                                int bonesTotal, float mx, float my) {
    const float s = uiScale();
    const float th = listTextHeight("Ag");
    const Graveyard& yard = w.resource<Graveyard>();
    drawScrim();
    drawThemedPanel(L.box);
    drawListText(fitText(lang("UILABEL_APPEASE_GHOSTS"), L.title.w, false), L.title.x, L.title.y + (L.title.h - th) * 0.5f, WHITE);
    if (L.close.contains(mx, my)) noteHover(L.close);
    drawCloseButton(L.close);

    int waiting = 0;
    for (const Ghost& g : yard.ghosts) waiting += g.appeased ? 0 : 1;
    {
        std::string text = langf("UILABEL_GHOSTS_TOTALS", { std::to_string(bonesTotal), std::to_string(waiting) });
        if (yard.haunted) text += "   " + lang("UILABEL_GHOSTS_HAUNTED");
        const std::string shown = fitTextTip(text, L.totals.w, L.totals);
        drawListText(shown, L.totals.x, L.totals.y + (L.totals.h - th) * 0.5f, yard.haunted ? RGBA{ 1.0f, 0.45f, 0.4f, 1.0f } : theme::TEXT_DIM);
        noteTip(lang("UILABEL_GHOSTS_HOW"), L.totals);
    }

    const char* heads[2] = { "UILABEL_GHOSTS_BONES", "UILABEL_GHOSTS_GRAVEYARD" };
    for (int c = 0; c < 2; ++c) {
        const Rect& h = L.heads[(size_t)c];
        drawRect(h, theme::alpha(theme::GOLD_DEEP, 0.85f));
        drawListText(fitText(lang(heads[c]), h.w - 12.0f * s, false), h.x + 6.0f * s, h.y + (h.h - th) * 0.5f, theme::GOLD);
        drawRect(L.lists[(size_t)c], RGBA{ 0.0f, 0.0f, 0.0f, 0.22f });
    }

    for (const AltarRow& r : L.rows[0]) {
        if (r.index >= (int)bones.size()) continue;
        const ItemStack& st = bones[(size_t)r.index];
        drawRect(r.row, RGBA{ 0.16f, 0.07f, 0.085f, 0.85f });
        drawRect(r.portrait, theme::TAB_OFF);
        drawTexture(itemTexture(st.item)->tex, r.portrait.x + 1.0f, r.portrait.y + 1.0f, r.portrait.w - 2.0f, r.portrait.h - 2.0f, WHITE);
        const float textX = r.portrait.x + r.portrait.w + 8.0f * s;
        const std::string count = "x" + std::to_string(st.count);
        drawListText(count, r.row.x + r.row.w - 8.0f * s - listTextWidth(count), r.row.y + (r.row.h - th) * 0.5f, WHITE);
        drawListText(fitText(itemNameOf(st.item), r.row.x + r.row.w - 16.0f * s - listTextWidth(count) - textX, false), textX,
                     r.row.y + (r.row.h - th) * 0.5f, WHITE);
    }
    if (bones.empty()) {
        const Rect& list = L.lists[0];
        float y = list.y + 12.0f * s;
        for (const std::string& line : wrapListText(lang("UILABEL_GHOSTS_NO_BONES"), list.w - 16.0f * s)) {
            drawListText(line, list.x + 8.0f * s, y, theme::TEXT_DIM);
            y += th;
        }
    }

    for (const AltarRow& r : L.rows[1]) {
        if (r.index >= (int)ghosts.size()) continue;
        const Ghost& g = yard.ghosts[ghosts[(size_t)r.index]];
        const bool haunting = !g.appeased && g.hours >= GHOST_GRACE_HOURS;
        drawRect(r.row, haunting ? RGBA{ 0.30f, 0.06f, 0.08f, 0.9f } : RGBA{ 0.16f, 0.07f, 0.085f, 0.85f });
        drawRect(r.portrait, theme::TAB_OFF);
        drawPortrait(unitPicture(g.sprite), r.portrait, g.appeased ? 1.0f : 0.0f);
        if (!g.appeased) drawRect(r.portrait, RGBA{ 0.55f, 0.75f, 1.0f, 0.25f });
        const Rect btn = ghostButton(r, uiH_);
        const float textX = r.portrait.x + r.portrait.w + 8.0f * s, textW = btn.x - 6.0f * s - textX;
        const float top = r.row.y + r.row.h * 0.5f - th - 1.0f * s;
        drawListText(fitText(g.name, textW, false), textX, top, WHITE);
        std::string status;
        RGBA tint = theme::TEXT_DIM;
        if (g.appeased) {
            status = lang("UILABEL_GHOST_APPEASED");
            tint = RGBA{ 0.5f, 0.95f, 0.5f, 1.0f };
        } else if (haunting) {
            status = lang("UILABEL_GHOST_HAUNTING");
            tint = RGBA{ 1.0f, 0.45f, 0.4f, 1.0f };
        } else {
            status = langf("UILABEL_GHOST_HOURS_LEFT", { std::to_string((int)std::ceil(GHOST_GRACE_HOURS - g.hours)) });
            tint = RGBA{ 1.0f, 0.78f, 0.35f, 1.0f };
        }
        drawListText(fitText(status, textW, false), textX, top + th + 2.0f * s, tint);
        if (!g.appeased) drawButton(btn, lang("UILABEL_APPEASE"), bonesTotal > 0, bonesTotal > 0 && btn.contains(mx, my));
    }
    if (ghosts.empty()) {
        const Rect& list = L.lists[1];
        const std::string none = lang("UILABEL_GHOSTS_NONE");
        drawListText(none, list.x + (list.w - listTextWidth(none)) * 0.5f, list.y + 12.0f * s, theme::TEXT_DIM);
    }
    const bool can = bonesTotal > 0 && waiting > 0;
    drawButton(L.apply, langf("UILABEL_APPEASE_ALL", { std::to_string(std::min(bonesTotal, waiting)) }), can, can && L.apply.contains(mx, my));
    drawButton(L.cancel, lang("UILABEL_CLOSE"), true, L.cancel.contains(mx, my));
}

void Renderer::drawUnitCard(World& w, ObjectId u, const UnitCardLayout& C) {
    if (!w.has<Entity>(u)) return;
    const float s = uiScale();
    const float th = listTextHeight("Ag");
    drawThemedPanel(C.box);
    drawRect(C.portrait, theme::TAB_OFF);
    drawPortrait(unitPortrait(w, u), C.portrait, unitSaturation(w, u));

    {
        std::vector<std::pair<std::string, RGBA>> lines;
        std::string where;
        if (w.has<Citizen>(u)) where = lang("UILABEL_ALTAR_CITIZEN");
        else if (squadOf(w, u) >= 0) where = squadLabel(w, squadOf(w, u));
        lines.push_back({ where, theme::GOLD });
        int combat = 0, other = 0;
        levelTotals(w, u, combat, other);
        lines.push_back({ langf("UILABEL_ALTAR_LEVELS", { std::to_string(combat), std::to_string(other) }), WHITE });
        if (const Needs* n = w.get<Needs>(u)) {
            std::string needs = lang("UILABEL_HEALTH") + ": " + std::to_string(n->hp) + " / " + std::to_string(n->maxHp);
            if (!w.has<Mob>(u)) needs += "   " + lang("UILABEL_HUNGER") + ": " + std::to_string(n->hunger) + " / " + std::to_string(n->maxHunger);
            lines.push_back({ needs, theme::TEXT_DIM });
        }
        const float nameH = uiTextHeight("Ag");
        drawUiText(fitText(unitNameOf(w, u), C.head.w, true), C.head.x, C.head.y, WHITE);
        float y = C.head.y + nameH + 1.0f * s;
        for (const auto& [text, tint] : lines) {
            if (y + th > C.head.y + C.head.h + 4.0f * s) break;
            drawListText(fitText(text, C.head.w, false), C.head.x, y, tint);
            y += th + 1.0f * s;
        }
    }

    // Stats in columns: each group's title, then its stats with their levels
    if (const Stats* st = w.get<Stats>(u)) {
        struct Entry { std::string text, level; bool title; };
        std::vector<Entry> entries;
        const StatGroup groups[] = { StatGroup::Attributes, StatGroup::Combat, StatGroup::Gathering, StatGroup::Crafting,
                                     StatGroup::Utility };
        for (StatGroup group : groups) {
            entries.push_back({ groupTitle(group), std::string(), true });
            for (int i = 0; i < STAT_COUNT; ++i) {
                if (statInfo((StatId)i).group != group) continue;
                entries.push_back({ statName((StatId)i), std::to_string(levelOf(*st, (StatId)i)), false });
            }
        }
        const int rows = std::max(1, (int)std::ceil((float)entries.size() / (float)C.statColumns));
        const float colGap = 10.0f * s;
        const float colW = (C.stats.w - (C.statColumns - 1) * colGap) / (float)C.statColumns;
        for (size_t i = 0; i < entries.size(); ++i) {
            const int col = (int)i / rows, row = (int)i % rows;
            const float x = C.stats.x + col * (colW + colGap), y = C.stats.y + row * C.line;
            const Entry& e = entries[i];
            if (e.title) {
                drawListText(fitText(e.text, colW, false), x, y, theme::GOLD);
                continue;
            }
            const float lw = listTextWidth(e.level);
            drawListText(e.level, x + colW - lw, y, WHITE);
            drawListText(fitText(e.text, colW - lw - 6.0f * s, false), x + 4.0f * s, y, theme::TEXT_DIM);
        }
    } else {
        drawListText(lang("UILABEL_ALTAR_NO_STATS"), C.stats.x, C.stats.y, theme::TEXT_DIM);
    }

    drawListText(lang("UILABEL_GEAR"), C.gearHead.x, C.gearHead.y, theme::GOLD);
    const Equipment* worn = w.get<Equipment>(u);
    for (size_t g = 0; g < C.gear.size(); ++g) {
        const Rect& r = C.gear[g];
        drawRect(r, RGBA{ 0.16f, 0.07f, 0.085f, 0.9f });
        const int item = worn ? worn->items[g] : 0;
        if (item != 0) drawTexture(itemTexture(item)->tex, r.x + 1.0f, r.y + 1.0f, r.w - 2.0f, r.h - 2.0f, WHITE);
    }
    drawListText(lang("UILABEL_INVENTORY"), C.items.x, C.items.y, theme::GOLD);
    const Inventory* inv = w.get<Inventory>(u);
    for (size_t i = 0; i < C.slots.size(); ++i) {
        const Rect& r = C.slots[i];
        drawRect(r, RGBA{ 0.16f, 0.07f, 0.085f, 0.9f });
        if (!inv || i >= inv->slots.size() || inv->slots[i].item == 0) continue;
        const ItemStack& st = inv->slots[i];
        drawTexture(itemTexture(st.item)->tex, r.x + 1.0f, r.y + 1.0f, r.w - 2.0f, r.h - 2.0f, WHITE);
        if (st.count > 1) {
            const std::string n = std::to_string(st.count);
            drawListText(n, r.x + r.w - listTextWidth(n) - 2.0f * s, r.y + r.h - th, WHITE);
        }
    }
}

void Renderer::drawSelectArrows(World& w, const Camera& cam, const std::vector<ObjectId>& units, int winW, int winH) {
    if (units.empty()) return;
    const float s = uiScale();
    const Mat4 vp = cam.viewProj((float)winW / (float)winH);
    const RGBA green{ 0.3f, 1.0f, 0.3f, 1.0f }, edge{ 0.0f, 0.0f, 0.0f, 0.9f };
    const float hw = 7.0f * s, hh = 9.0f * s, lift = 4.0f * s, border = std::max(1.0f, 1.5f * s);
    std::vector<UiVert> tris;
    for (ObjectId u : units) {
        const Transform* tr = w.get<Transform>(u);
        if (!tr) continue;
        float sx = 0.0f, sy = 0.0f;
        if (!worldToScreen(vp, tr->x, tr->y + std::max(1.0f, unitHeight(w, u)) + 0.25f, tr->z, winW, winH, sx, sy)) continue;
        const float tip = sy - lift;
        // A downward arrow, with a dark outline so it shows on any ground
        tris.push_back({ sx - hw - border, tip - hh - border, edge });
        tris.push_back({ sx + hw + border, tip - hh - border, edge });
        tris.push_back({ sx, tip + border * 1.5f, edge });
        tris.push_back({ sx - hw, tip - hh, green });
        tris.push_back({ sx + hw, tip - hh, green });
        tris.push_back({ sx, tip, green });
    }
    drawUiTris(tris);
}
