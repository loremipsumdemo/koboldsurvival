#include "renderer_internal.h"
#include "prop_info.h"
#include "airship.h"
#include "building.h"

using namespace render_detail;

namespace {
const RGBA LABEL_FILL{ 0.36f, 0.12f, 0.08f, 0.5f };

const RGBA LABEL_EDGE{ 0.62f, 0.30f, 0.20f, 0.5f };

const RGBA LABEL_TEXT{ 1.0f, 0.93f, 0.86f, 1.0f };

const RGBA LABEL_HOT_FILL{ 0.12f, 0.04f, 0.02f, 0.85f };

const RGBA LABEL_HOT_TEXT{ 1.0f, 0.80f, 0.25f, 1.0f };
}

void Renderer::drawPropProgress(World& w, const Camera& cam, int winW, int winH) {
    const float s = uiScale();
    const Mat4 vp = cam.viewProj((float)winW / (float)winH);
    const float outer = 11.0f * s, inner = 4.3f * s;
    const RGBA back{ 0.31f, 0.24f, 0.17f, 1.0f };
    constexpr float HOP_SECONDS = 0.22f, HOP_HEIGHT = 9.0f;
    const float now = (float)SDL_GetTicks() / 1000.0f;

    std::map<ObjectId, PropHop> seen;
    for (ObjectId e : w.view<PropWork, Prop, Transform>()) {
        const PropWork* pw = w.get<PropWork>(e);
        PropHop hop;
        if (auto it = propHops_.find(e); it != propHops_.end() && it->second.made <= pw->made) hop = it->second;
        else hop.made = pw->made;
        if (pw->made != hop.made) {
            hop.made = pw->made;
            hop.start = now;
        }
        seen[e] = hop;
        const float t = hop.start >= 0.0f ? (now - hop.start) / HOP_SECONDS : 1.0f;
        const bool hopping = t >= 0.0f && t < 1.0f;
        if (pw->workers <= 0 && !hopping) continue;

        const Transform* tr = w.get<Transform>(e);
        if (!withinDetail(cam, tr->x, tr->y, tr->z)) continue;
        const PropKind kind = w.get<Prop>(e)->kind;
        const float top = propTop(kind);
        float sx = 0.0f, sy = 0.0f;
        if (!worldToScreen(vp, tr->x, propBaseY(w, e) + top + 0.25f, tr->z, winW, winH, sx, sy)) continue;
        const float cy = sy - outer - (hopping ? std::sin(t * PI) * HOP_HEIGHT * s : 0.0f);
        const float frac = hopping ? 1.0f : (float)std::clamp(pw->progress, 0.0, 1.0);
        drawRing(sx, cy, outer, inner, 0.0f, 1.0f, back);
        drawRing(sx, cy, outer, inner, 0.0f, frac, theme::AMBER);
    }
    propHops_.swap(seen);

    for (ObjectId e : w.view<Blueprint, Prop, Transform>()) {
        if (cutAway(w, e)) continue;
        const Blueprint* bp = w.get<Blueprint>(e);
        const int need = materialsNeeded(w, e);
        if (bp->supplied <= 0 && bp->progress <= 0.0) continue;
        const Transform* tr = w.get<Transform>(e);
        if (!withinDetail(cam, tr->x, tr->y, tr->z)) continue;
        float sx = 0.0f, sy = 0.0f;
        const float top = propInfo(w.get<Prop>(e)->kind).foundation ? 0.6f : FLOOR_HEIGHT + 0.4f;
        if (!worldToScreen(vp, tr->x, tr->y + top, tr->z, winW, winH, sx, sy)) continue;
        const bool supplied = need <= 0 || bp->supplied >= need;
        const float frac = supplied ? (float)std::clamp(bp->progress, 0.0, 1.0) : (float)bp->supplied / (float)std::max(1, need);
        drawRing(sx, sy - outer, outer, inner, 0.0f, 1.0f, back);
        drawRing(sx, sy - outer, outer, inner, 0.0f, frac, supplied ? RGBA{ 0.35f, 0.9f, 0.45f, 1.0f } : theme::AMBER);
    }

    // Units getting on or off an Airship: how far along they are
    for (ObjectId u : w.view<Boarding, Transform>()) {
        const Boarding* b = w.get<Boarding>(u);
        const Transform* tr = w.get<Transform>(u);
        if (!withinDetail(cam, tr->x, tr->y, tr->z)) continue;
        float sx = 0.0f, sy = 0.0f;
        if (!worldToScreen(vp, tr->x, tr->y + unitTop(w, u) + 0.3f, tr->z, winW, winH, sx, sy)) continue;
        const float whole = b->leaving ? AIRSHIP_LEAVE_SECONDS : AIRSHIP_BOARD_SECONDS;
        const float frac = std::clamp(1.0f - b->left / whole, 0.0f, 1.0f);
        drawRing(sx, sy - outer, outer, inner, 0.0f, 1.0f, back);
        drawRing(sx, sy - outer, outer, inner, 0.0f, frac, theme::AMBER);
    }
}

void Renderer::drawGroundItemName(World& w, const Camera& cam, int winW, int winH, ObjectId item) {
    const GroundItem* gi = w.get<GroundItem>(item);
    const Transform* tr = w.get<Transform>(item);
    if (!gi || !tr) return;
    const float s = uiScaleFor(winH);
    const Mat4 vp = cam.viewProj((float)winW / (float)winH);
    ItemOnScreen os;
    if (!itemOnScreen(vp, winW, winH, *tr, 1.0f, os)) return;
    const std::string name = gi->count > 1 ? std::to_string(gi->count) + "x " + itemNameOf(gi->item) : itemNameOf(gi->item);
    const float padX = 7.0f * s, padY = 3.0f * s, lineH = listTextHeight("Ag");
    const float boxW = listTextWidth(name) + 2.0f * padX, boxH = lineH + 2.0f * padY;
    const Rect box{ std::floor(os.cx - boxW * 0.5f), std::floor(os.top - 4.0f * s - boxH), boxW, boxH };
    drawRect(Rect{ box.x - 1.0f, box.y - 1.0f, box.w + 2.0f, box.h + 2.0f }, LABEL_EDGE);
    drawRect(box, LABEL_HOT_FILL);
    drawListText(name, box.x + padX, box.y + padY, LABEL_HOT_TEXT);
}

void Renderer::drawEmotes(World& w, const Camera& cam, int winW, int winH, bool playerNames, bool otherNames) {
    const float s = uiScaleFor(winH);
    const Mat4 vp = cam.viewProj((float)winW / (float)winH);
    const float size = 28.0f * s;
    const unsigned long long ticks = (unsigned long long)SDL_GetTicks();
    const unsigned long long turn = ticks / EMOTE_CYCLE_MS;
    const unsigned long long sinceHop = ticks % EMOTE_HOP_EVERY_MS;
    const float hop = sinceHop < EMOTE_HOP_MS
        ? std::sin(PI * (float)sinceHop / (float)EMOTE_HOP_MS) * EMOTE_HOP_HEIGHT * s : 0.0f;
    for (ObjectId u : w.view<Entity, Transform>()) {
        const Transform* tr = w.get<Transform>(u);
        if (!withinDetail(cam, tr->x, tr->y, tr->z) || cutAway(w, u)) continue;
        float sx = 0.0f, sy = 0.0f;
        if (!worldToScreen(vp, tr->x, tr->y + unitTop(w, u) + 0.1f, tr->z, winW, winH, sx, sy)) continue;
        float bottom = sy - 2.0f * s;
        const bool named = w.has<Name>(u) && (w.has<Commandable>(u) ? playerNames : otherNames);
        if (named) bottom -= listTextHeight("Ag") + 2.0f * s;
        if (sx < -size || sx > winW + size || bottom < 0.0f || bottom - size > winH) continue;
        const std::vector<EmoteId> emotes = emotesOf(w, u);
        if (emotes.empty()) continue;
        const TextTex* pic = emoteTexture(emotes[(size_t)(turn % emotes.size())]);
        if (!pic) continue;
        drawFitted(*pic, Rect{ sx - size * 0.5f, bottom - size - hop, size, size }, WHITE);
    }
}

void Renderer::drawPropEmotes(World& w, const Camera& cam, int winW, int winH) {
    const float s = uiScaleFor(winH);
    const Mat4 vp = cam.viewProj((float)winW / (float)winH);
    const float size = 28.0f * s;
    const unsigned long long sinceHop = (unsigned long long)SDL_GetTicks() % EMOTE_HOP_EVERY_MS;
    const float hop = sinceHop < EMOTE_HOP_MS
        ? std::sin(PI * (float)sinceHop / (float)EMOTE_HOP_MS) * EMOTE_HOP_HEIGHT * s : 0.0f;
    for (ObjectId e : w.view<Prop, Transform>()) {
        const PropKind kind = w.get<Prop>(e)->kind;
        if (!propInfo(kind).claimsLand) continue;
        const Transform* tr = w.get<Transform>(e);
        if (!withinDetail(cam, tr->x, tr->y, tr->z)) continue;
        EmoteId emote = EmoteId::None;
        if (!altarHasUpkeep(w, e)) emote = EmoteId::NoUpkeep;
        if (propDecaying(w, e)) emote = EmoteId::Decaying;
        if (emote == EmoteId::None) continue;
        float sx = 0.0f, sy = 0.0f;
        if (!worldToScreen(vp, tr->x, propBaseY(w, e) + propTop(kind) + 0.1f, tr->z, winW, winH, sx, sy)) continue;
        const float bottom = sy - 2.0f * s;
        if (sx < -size || sx > winW + size || bottom < 0.0f || bottom - size > winH) continue;
        if (const TextTex* pic = emoteTexture(emote)) {
            drawFitted(*pic, Rect{ sx - size * 0.5f, bottom - size - hop, size, size }, WHITE);
        }
    }
}

void Renderer::drawHoverCue(World& w, const Camera& cam, int winW, int winH, ObjectId u, EmoteId emote, bool namesShown) {
    const Transform* tr = w.get<Transform>(u);
    if (!tr || !withinDetail(cam, tr->x, tr->y, tr->z)) return;
    const float s = uiScaleFor(winH);
    const float size = 28.0f * s;
    const Mat4 vp = cam.viewProj((float)winW / (float)winH);
    float sx = 0.0f, sy = 0.0f;
    if (!worldToScreen(vp, tr->x, tr->y + unitTop(w, u) + 0.1f, tr->z, winW, winH, sx, sy)) return;
    float bottom = sy - 2.0f * s;
    if (namesShown && w.has<Name>(u)) bottom -= listTextHeight("Ag") + 2.0f * s;
    if (!emotesOf(w, u).empty()) bottom -= size + 4.0f * s;
    const TextTex* pic = emoteTexture(emote);
    if (pic) drawFitted(*pic, Rect{ sx - size * 0.5f, bottom - size, size, size }, WHITE);
}

void Renderer::drawGroundLabels(World& w, const Terrain& t, const Camera& cam, int winW, int winH, float mx, float my) {
    const float s = uiScaleFor(winH);
    const std::vector<GroundStack> stacks = groundStacks(w, t, cam, winW, winH);
    size_t hotStack = 0, hotRow = 0;
    const bool hasHot = topLabelRow(stacks, mx, my, hotStack, hotRow);
    for (size_t k = 0; k < stacks.size(); ++k) {
        const GroundStack& st = stacks[k];
        drawRect(Rect{ st.box.x - 1.0f, st.box.y - 1.0f, st.box.w + 2.0f, st.box.h + 2.0f }, LABEL_EDGE);
        drawRect(st.box, LABEL_FILL);
        for (size_t i = 0; i < st.names.size(); ++i) {
            const bool hot = hasHot && k == hotStack && i == hotRow;
            if (hot) drawRect(st.rows[i], LABEL_HOT_FILL);
            drawListText(st.names[i], st.box.x + 7.0f * s, st.rows[i].y, hot ? LABEL_HOT_TEXT : LABEL_TEXT);
        }
    }
}

void Renderer::drawUnitNames(World& w, const Camera& cam, int winW, int winH, bool players, bool others) {
    const float s = uiScaleFor(winH);
    const Mat4 vp = cam.viewProj((float)winW / (float)winH);
    const RGBA hostile{ 1.0f, 0.55f, 0.50f, 1.0f };
    const RGBA neutral{ 0.6f, 0.6f, 0.6f, 1.0f };
    const RGBA shadow{ 0.0f, 0.0f, 0.0f, 0.85f };
    for (ObjectId u : w.view<Entity, Transform, Name>()) {
        const Transform* tr = w.get<Transform>(u);
        const Name* nm = w.get<Name>(u);
        if (!(w.has<Commandable>(u) ? players : others)) continue;
        if (!withinDetail(cam, tr->x, tr->y, tr->z)) continue;
        float sx = 0.0f, sy = 0.0f;
        if (!worldToScreen(vp, tr->x, tr->y + unitTop(w, u) + 0.1f, tr->z, winW, winH, sx, sy)) continue;

        const std::string tag = "[" + nm->text + "]";
        const float tw = listTextWidth(tag), th = listTextHeight(tag);
        if (sx < -tw || sx > winW + tw || sy < -th || sy > winH + th) continue;
        const float x = sx - tw * 0.5f, y = sy - th;
        drawListText(tag, x + 1.5f * s, y + 1.5f * s, shadow);
        drawListText(tag, x, y, w.has<Hostile>(u) ? hostile : w.has<Neutral>(u) ? neutral : WHITE);
    }
}

void Renderer::drawFloatingNumbers(const std::vector<FloatingNumber>& floats, const Camera& cam, int winW, int winH) {
    const float s = uiScaleFor(winH);
    const Mat4 vp = cam.viewProj((float)winW / (float)winH);
    for (const FloatingNumber& f : floats) {
        if (!withinDetail(cam, f.x, f.y, f.z)) continue;
        float sx = 0.0f, sy = 0.0f;
        if (!worldToScreen(vp, f.x, f.y + GLYPH_H, f.z, winW, winH, sx, sy)) continue;

        const float t = std::clamp(f.age / FLOAT_LIFE, 0.0f, 1.0f);
        const float rise = (200.0f * t - 260.0f * t * t) * s;
        const float drift = 40.0f * t * s;
        const float alpha = t < 0.6f ? 1.0f : (1.0f - t) / 0.4f;

        const std::string text = "-" + std::to_string(f.amount);
        const float x = sx + drift - uiTextWidth(text) * 0.5f;
        const float y = sy - 4.0f * s - uiTextHeight(text) - rise;
        RGBA c = logTint(f.tint);
        c.a *= alpha;
        drawUiText(text, x + 1.5f * s, y + 1.5f * s, RGBA{ 0.0f, 0.0f, 0.0f, 0.85f * alpha });
        drawUiText(text, x, y, c);
    }
}

// A puff of smoke: grey clouds swelling out from the spot and fading
void Renderer::drawPuffs(const std::vector<Puff>& puffs, const Camera& cam, int winW, int winH) {
    const float s = uiScaleFor(winH);
    const Mat4 vp = cam.viewProj((float)winW / (float)winH);
    for (const Puff& p : puffs) {
        if (!withinDetail(cam, p.x, p.y, p.z)) continue;
        const float t = std::clamp(p.age / PUFF_LIFE, 0.0f, 1.0f);
        const float alpha = (t < 0.25f ? t / 0.25f : (1.0f - t) / 0.75f) * 0.8f;
        for (int k = 0; k < 9; ++k) {
            const float a = (float)k * 0.6981317f + t * 0.8f;
            const float spread = (k == 0 ? 0.0f : 0.45f + 0.9f * t);
            float sx = 0.0f, sy = 0.0f;
            if (!worldToScreen(vp, p.x + std::cos(a) * spread, p.y + 0.8f + 1.2f * t + (k % 3) * 0.25f, p.z + std::sin(a) * spread, winW, winH, sx, sy)) continue;
            const float r = (18.0f + 26.0f * t + (k % 3) * 6.0f) * s;
            const float g = 0.78f + 0.06f * (float)(k % 3);
            drawRoundedRect(Rect{ sx - r, sy - r, 2.0f * r, 2.0f * r }, r, RGBA{ g, g, g, alpha });
        }
    }
}

void Renderer::drawTradeLabels(const std::vector<TradeLabel>& labels, World& w, const Camera& cam, int winW, int winH) {
    const float s = uiScaleFor(winH);
    const Mat4 vp = cam.viewProj((float)winW / (float)winH);
    std::map<ObjectId, int> stacked;
    for (const TradeLabel& l : labels) {
        const Transform* tr = w.get<Transform>(l.unit);
        if (!tr || !withinDetail(cam, tr->x, tr->y, tr->z)) continue;
        float sx = 0.0f, sy = 0.0f;
        // Over a prop (a store, a blueprint), above its model rather than at a unit's head height
        float lift = GLYPH_H * 2.2f;
        if (const Prop* p = w.get<Prop>(l.unit)) lift = std::max(lift, propInfo(p->kind).size * 1.1f);
        if (!worldToScreen(vp, tr->x, tr->y + lift, tr->z, winW, winH, sx, sy)) continue;
        const int row = stacked[l.unit]++;
        const float t = std::clamp(l.age / TRADE_LABEL_LIFE, 0.0f, 1.0f);
        const float alpha = t < 0.7f ? 1.0f : (1.0f - t) / 0.3f;
        const float lineH = uiTextHeight("Ag") + 2.0f * s;
        const float x = sx - uiTextWidth(l.text) * 0.5f, y = sy - uiTextHeight(l.text) - row * lineH;
        drawUiText(l.text, x + 1.5f * s, y + 1.5f * s, RGBA{ 0.0f, 0.0f, 0.0f, 0.85f * alpha });
        drawUiText(l.text, x, y, l.red ? RGBA{ 1.0f, 0.35f, 0.32f, alpha } : RGBA{ 1.0f, 0.90f, 0.45f, alpha });
    }
}
