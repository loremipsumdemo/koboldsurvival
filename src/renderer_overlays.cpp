// Renderer: what floats over the world: name tags, emotes, ground item labels, damage numbers and work rings.
#include "renderer_internal.h"

using namespace render_detail;

namespace {

// Name box over an item on the ground: dark red-brown, as in Kenshi
const RGBA LABEL_FILL{ 0.36f, 0.12f, 0.08f, 0.5f };

const RGBA LABEL_EDGE{ 0.62f, 0.30f, 0.20f, 0.5f };

const RGBA LABEL_TEXT{ 1.0f, 0.93f, 0.86f, 1.0f };

// The name under the mouse: a darker cell with gold text
const RGBA LABEL_HOT_FILL{ 0.12f, 0.04f, 0.02f, 0.85f };

const RGBA LABEL_HOT_TEXT{ 1.0f, 0.80f, 0.25f, 1.0f };

} // namespace

// The ring sits over the top of the prop's model, in window pixels, and fills as the next item nears. When an item is
// finished, the ring hops: up and back down once, quickly. It stays up for the hop even if the work stopped with that
// item, so the last one shows too.
void Renderer::drawPropProgress(World& w, const Camera& cam, int winW, int winH, bool names) {
    const float s = uiScale();
    const Mat4 vp = cam.viewProj((float)winW / (float)winH);
    const float outer = 11.0f * s, inner = 4.3f * s;          // the hole is about two fifths of the ring
    const RGBA back{ 0.31f, 0.24f, 0.17f, 1.0f };             // dark brown
    constexpr float HOP_SECONDS = 0.22f, HOP_HEIGHT = 9.0f;
    const float now = (float)SDL_GetTicks() / 1000.0f;

    std::map<Entity, PropHop> seen;
    for (Entity e : w.view<PropWork, Prop, Transform>()) {
        const PropWork* pw = w.get<PropWork>(e);
        PropHop hop;
        if (auto it = propHops_.find(e); it != propHops_.end() && it->second.made <= pw->made) hop = it->second;
        else hop.made = pw->made;   // first seen (or a new world): no hop for items made before
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
        // Above the prop's name tag when names are shown
        // Above the prop's block of name lines when names are shown (the block's top, from drawPropNames this frame)
        float above = sy - (names ? listTextHeight("Ag") + 2.0f * s : 0.0f);
        if (names) {
            if (const auto it = propLabelTop_.find(e); it != propLabelTop_.end()) above = std::min(above, it->second - 2.0f * s);
        }
        const float cy = above - outer - (hopping ? std::sin(t * 3.14159265f) * HOP_HEIGHT * s : 0.0f);
        // Just finished: the ring shows full for the hop, then the next item's progress
        const float frac = hopping ? 1.0f : (float)std::clamp(pw->progress, 0.0, 1.0);
        drawRing(sx, cy, outer, inner, 0.0f, 1.0f, back);
        drawRing(sx, cy, outer, inner, 0.0f, frac, theme::AMBER);
    }
    propHops_.swap(seen);   // props no longer worked, or gone, are forgotten
}

void Renderer::drawGroundItemName(World& w, const Camera& cam, int winW, int winH, Entity item) {
    const GroundItem* gi = w.get<GroundItem>(item);
    const Transform* tr = w.get<Transform>(item);
    if (!gi || !tr) return;
    const float s = winH / 720.0f;
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
    const float s = winH / 720.0f;
    const Mat4 vp = cam.viewProj((float)winW / (float)winH);
    const float size = 28.0f * s;
    const unsigned long long ticks = (unsigned long long)SDL_GetTicks();
    const unsigned long long turn = ticks / EMOTE_CYCLE_MS;
    // The hop: up and back down once, at the start of every EMOTE_HOP_EVERY_MS. Every emote hops together.
    const unsigned long long sinceHop = ticks % EMOTE_HOP_EVERY_MS;
    const float hop = sinceHop < EMOTE_HOP_MS
        ? std::sin(3.14159265f * (float)sinceHop / (float)EMOTE_HOP_MS) * EMOTE_HOP_HEIGHT * s : 0.0f;
    for (Entity u : w.view<Unit, Transform>()) {
        const std::vector<EmoteId> emotes = emotesOf(w, u);
        if (emotes.empty()) continue;
        const Transform* tr = w.get<Transform>(u);
        if (!withinDetail(cam, tr->x, tr->y, tr->z)) continue;
        // Above the head, where the name tag is, and above the tag when names are shown
        float sx = 0.0f, sy = 0.0f;
        if (!worldToScreen(vp, tr->x, tr->y + unitTop(w, u) + 0.1f, tr->z, winW, winH, sx, sy)) continue;
        float bottom = sy - 2.0f * s;
        const bool named = w.has<Name>(u) && (w.has<Commandable>(u) ? playerNames : otherNames);
        if (named) bottom -= listTextHeight("Ag") + 2.0f * s;
        if (sx < -size || sx > winW + size || bottom < 0.0f || bottom - size > winH) continue;
        const TextTex* pic = emoteTexture(emotes[(size_t)(turn % emotes.size())]);
        if (!pic) continue;
        drawFitted(*pic, Rect{ sx - size * 0.5f, bottom - size - hop, size, size }, WHITE);
    }
}

void Renderer::drawPropEmotes(World& w, const Camera& cam, int winW, int winH, bool propNames) {
    const float s = winH / 720.0f;
    const Mat4 vp = cam.viewProj((float)winW / (float)winH);
    const float size = 28.0f * s;
    const unsigned long long sinceHop = (unsigned long long)SDL_GetTicks() % EMOTE_HOP_EVERY_MS;
    const float hop = sinceHop < EMOTE_HOP_MS
        ? std::sin(3.14159265f * (float)sinceHop / (float)EMOTE_HOP_MS) * EMOTE_HOP_HEIGHT * s : 0.0f;
    std::vector<Claim> claims;
    bool haveClaims = false;
    for (Entity e : w.view<Prop, Transform>()) {
        const PropKind kind = w.get<Prop>(e)->kind;
        if (kind != PropKind::Vault && kind != PropKind::Flag) continue;
        const Transform* tr = w.get<Transform>(e);
        if (!withinDetail(cam, tr->x, tr->y, tr->z)) continue;
        EmoteId emote = EmoteId::None;
        if (kind == PropKind::Vault && !vaultHasUpkeep(w, e)) emote = EmoteId::NoUpkeep;
        if (kind == PropKind::Flag) {
            if (!haveClaims) {
                claims = claimsOf(w);
                haveClaims = true;
            }
            if (propDecaying(w, e, claims)) emote = EmoteId::Decaying;
        }
        if (emote == EmoteId::None) continue;
        float sx = 0.0f, sy = 0.0f;
        if (!worldToScreen(vp, tr->x, propBaseY(w, e) + propTop(kind) + 0.1f, tr->z, winW, winH, sx, sy)) continue;
        float bottom = sy - 2.0f * s;
        if (propNames) bottom -= listTextHeight("Ag") + 2.0f * s;
        if (sx < -size || sx > winW + size || bottom < 0.0f || bottom - size > winH) continue;
        if (const TextTex* pic = emoteTexture(emote)) {
            drawFitted(*pic, Rect{ sx - size * 0.5f, bottom - size - hop, size, size }, WHITE);
        }
    }
}

void Renderer::drawHoverCue(World& w, const Camera& cam, int winW, int winH, Entity u, EmoteId emote, bool namesShown) {
    const Transform* tr = w.get<Transform>(u);
    if (!tr || !withinDetail(cam, tr->x, tr->y, tr->z)) return;
    const float s = winH / 720.0f;
    const float size = 28.0f * s;
    const Mat4 vp = cam.viewProj((float)winW / (float)winH);
    float sx = 0.0f, sy = 0.0f;
    if (!worldToScreen(vp, tr->x, tr->y + unitTop(w, u) + 0.1f, tr->z, winW, winH, sx, sy)) return;
    // Above its name, and above its own emote if it shows one, so both can be seen
    float bottom = sy - 2.0f * s;
    if (namesShown && w.has<Name>(u)) bottom -= listTextHeight("Ag") + 2.0f * s;
    if (!emotesOf(w, u).empty()) bottom -= size + 4.0f * s;
    const TextTex* pic = emoteTexture(emote);
    if (pic) drawFitted(*pic, Rect{ sx - size * 0.5f, bottom - size, size, size }, WHITE);
}

void Renderer::drawGroundLabels(World& w, const Terrain& t, const Camera& cam, int winW, int winH, float mx, float my) {
    const float s = winH / 720.0f;
    // Items on one tile share a box, with one line each, top line first and the last one nearest the item.
    // Boxes are drawn farthest first, so a nearer box covers the ones behind it. The one line on top under
    // (mx, my) gets a darker cell and gold text, so it is clear which item a click picks.
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
    const float s = winH / 720.0f;
    const Mat4 vp = cam.viewProj((float)winW / (float)winH);
    const RGBA hostile{ 1.0f, 0.55f, 0.50f, 1.0f };
    const RGBA neutral{ 0.6f, 0.6f, 0.6f, 1.0f };
    const RGBA shadow{ 0.0f, 0.0f, 0.0f, 0.85f };
    for (Entity u : w.view<Unit, Transform, Name>()) {
        const Transform* tr = w.get<Transform>(u);
        const Name* nm = w.get<Name>(u);
        if (!(w.has<Commandable>(u) ? players : others)) continue;
        if (!withinDetail(cam, tr->x, tr->y, tr->z)) continue;
        // The tag sits just above the top of the unit's picture
        float sx = 0.0f, sy = 0.0f;
        if (!worldToScreen(vp, tr->x, tr->y + unitTop(w, u) + 0.1f, tr->z, winW, winH, sx, sy)) continue;

        const std::string tag = "[" + nm->text + "]";
        const float tw = listTextWidth(tag), th = listTextHeight(tag);
        if (sx < -tw || sx > winW + tw || sy < -th || sy > winH + th) continue;
        const float x = sx - tw * 0.5f, y = sy - th;
        // The drop shadow is the same text in black, a little down and to the right
        drawListText(tag, x + 1.5f * s, y + 1.5f * s, shadow);
        drawListText(tag, x, y, w.has<Hostile>(u) ? hostile : w.has<Neutral>(u) ? neutral : WHITE);
    }
}

void Renderer::drawPropNames(World& w, const Camera& cam, int winW, int winH, bool showInfo) {
    // Stacked as the Alt item names are, without their box: the props that share a spot (a foundation and what stands
    // on it, or props on one tile) share one block of lines over them, each prop's name in purple with its info lines
    // (Show Prop Info) under it in a paler purple, the lines left-aligned, the last prop nearest the props. Blocks are
    // drawn farthest first, so a nearer one is drawn over the ones behind it.
    const float s = winH / 720.0f;
    const Mat4 vp = cam.viewProj((float)winW / (float)winH);
    const RGBA purple{ 0.80f, 0.56f, 1.0f, 1.0f };
    const RGBA pale{ 0.88f, 0.82f, 0.96f, 1.0f };
    const RGBA shadow{ 0.0f, 0.0f, 0.0f, 0.85f };
    const float lineGap = 2.0f * s;
    const float lineH = listTextHeight("Ag");
    float ex = 0.0f, ey = 0.0f, ez = 0.0f;
    cam.eye(ex, ey, ez);
    propLabelTop_.clear();

    struct Seen { Entity e; float sx, sy, dist2; bool base; };
    // The group a prop joins: the foundation it stands on (or is), otherwise its tile
    std::map<std::pair<int, long long>, std::vector<Seen>> groups;
    for (Entity e : w.view<Prop, Transform>()) {
        const Transform* tr = w.get<Transform>(e);
        if (!withinDetail(cam, tr->x, tr->y, tr->z)) continue;
        const PropKind kind = w.get<Prop>(e)->kind;
        if (idx(kind) < 0 || idx(kind) >= PROP_KIND_COUNT) continue;
        // Just above the top of the prop, as a unit's tag is above its picture
        float sx = 0.0f, sy = 0.0f;
        if (!worldToScreen(vp, tr->x, propBaseY(w, e) + propTop(kind) + 0.1f, tr->z, winW, winH, sx, sy)) continue;
        const float dx = tr->x - ex, dy = tr->y - ey, dz = tr->z - ez;
        const bool base = propInfo(kind).foot.foundation;
        std::pair<int, long long> key{ 1, ((long long)std::floor(tr->x) << 32) ^ (long long)(unsigned)(int)std::floor(tr->z) };
        if (base) key = { 0, (long long)e };
        else if (const OnBase* ob = w.get<OnBase>(e); ob && w.has<Prop>(ob->foundation)) key = { 0, (long long)ob->foundation };
        groups[key].push_back(Seen{ e, sx, sy, dx * dx + dy * dy + dz * dz, base });
    }

    struct Block {
        float x = 0.0f, y = 0.0f, dist2 = 0.0f;
        std::vector<std::pair<std::string, RGBA>> lines;
    };
    std::vector<Block> blocks;
    for (auto& entry : groups) {
        std::vector<Seen>& seen = entry.second;
        // Entity order, so the names keep their places from frame to frame; the foundation last, nearest the props
        std::sort(seen.begin(), seen.end(), [](const Seen& a, const Seen& b) {
            if (a.base != b.base) return !a.base;
            return a.e < b.e;
        });
        Block b;
        float cx = 0.0f, top = 0.0f, maxW = 0.0f;
        b.dist2 = seen[0].dist2;
        for (size_t i = 0; i < seen.size(); ++i) {
            const Seen& sn = seen[i];
            cx += sn.sx;
            top = i == 0 ? sn.sy : std::min(top, sn.sy);
            b.dist2 = std::min(b.dist2, sn.dist2);
            b.lines.push_back({ "[" + lang(propInfo(w.get<Prop>(sn.e)->kind).nameKey) + "]", purple });
            if (showInfo) {
                for (const std::string& line : propInfoLines(w, sn.e)) b.lines.push_back({ line, pale });
            }
        }
        for (const auto& line : b.lines) maxW = std::max(maxW, listTextWidth(line.first));
        cx /= (float)seen.size();
        const float n = (float)b.lines.size();
        const float blockH = n * lineH + (n - 1.0f) * lineGap;
        b.x = cx - maxW * 0.5f;
        b.y = top - blockH;
        if (b.x + maxW < 0.0f || b.x > winW || b.y + blockH < 0.0f || b.y > winH) continue;
        for (const Seen& sn : seen) propLabelTop_[sn.e] = b.y;
        blocks.push_back(std::move(b));
    }
    std::stable_sort(blocks.begin(), blocks.end(), [](const Block& a, const Block& b) { return a.dist2 > b.dist2; });
    for (const Block& b : blocks) {
        for (size_t i = 0; i < b.lines.size(); ++i) {
            const float y = b.y + (float)i * (lineH + lineGap);
            drawListText(b.lines[i].first, b.x + 1.5f * s, y + 1.5f * s, shadow);
            drawListText(b.lines[i].first, b.x, y, b.lines[i].second);
        }
    }
}

void Renderer::drawFloatingNumbers(const std::vector<FloatingNumber>& floats, const Camera& cam, int winW, int winH) {
    const float s = winH / 720.0f;
    const Mat4 vp = cam.viewProj((float)winW / (float)winH);
    for (const FloatingNumber& f : floats) {
        if (!withinDetail(cam, f.x, f.y, f.z)) continue;
        // Anchored just above the unit's head, like its name
        float sx = 0.0f, sy = 0.0f;
        if (!worldToScreen(vp, f.x, f.y + GLYPH_H, f.z, winW, winH, sx, sy)) continue;

        // Arc: up and to the right, then down, fading out over the last 40% of its life. The rise peaks
        // about two fifths of the way through (about 38 pixels up at 720 lines), then falls 60 pixels below its start.
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

// Trade values over their units: a second's life, fading out over its last 30%, above the name tag
void Renderer::drawTradeLabels(const std::vector<TradeLabel>& labels, World& w, const Camera& cam, int winW, int winH) {
    const float s = winH / 720.0f;
    const Mat4 vp = cam.viewProj((float)winW / (float)winH);
    // A unit with several labels at once (an item's Health and Hunger) shows them stacked, the newest on top
    std::map<Entity, int> stacked;
    for (const TradeLabel& l : labels) {
        const Transform* tr = w.get<Transform>(l.unit);
        if (!tr || !withinDetail(cam, tr->x, tr->y, tr->z)) continue;
        float sx = 0.0f, sy = 0.0f;
        if (!worldToScreen(vp, tr->x, tr->y + GLYPH_H * 2.2f, tr->z, winW, winH, sx, sy)) continue;
        const int row = stacked[l.unit]++;
        const float t = std::clamp(l.age / TRADE_LABEL_LIFE, 0.0f, 1.0f);
        const float alpha = t < 0.7f ? 1.0f : (1.0f - t) / 0.3f;
        const float lineH = uiTextHeight("Ag") + 2.0f * s;
        const float x = sx - uiTextWidth(l.text) * 0.5f, y = sy - uiTextHeight(l.text) - row * lineH;
        drawUiText(l.text, x + 1.5f * s, y + 1.5f * s, RGBA{ 0.0f, 0.0f, 0.0f, 0.85f * alpha });
        drawUiText(l.text, x, y, l.red ? RGBA{ 1.0f, 0.35f, 0.32f, alpha } : RGBA{ 1.0f, 0.90f, 0.45f, alpha });
    }
}
