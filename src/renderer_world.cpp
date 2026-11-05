// Renderer: the 3D frame (sky, terrain, props, units, selection), the hover list, ground items and the build preview.
#include "renderer_internal.h"

using namespace render_detail;

namespace {

// Whether the chunk under (x, z) is loaded. Items on unloaded ground are not drawn.
bool groundLoaded(const Terrain& t, float x, float z) {
    const int cx = (int)std::floor(x / CHUNK_TILES), cz = (int)std::floor(z / CHUNK_TILES);
    return t.chunkAt(cx, cz) != nullptr;
}

// Thin square outline on the ground
void outlineSquare(Batch& b, float cx, float y, float cz, float r, float t, RGBA c) {
    b.quad({ cx - r, y, cz - r }, { cx + r, y, cz - r }, { cx + r, y, cz - r + t }, { cx - r, y, cz - r + t }, c);
    b.quad({ cx - r, y, cz + r - t }, { cx + r, y, cz + r - t }, { cx + r, y, cz + r }, { cx - r, y, cz + r }, c);
    b.quad({ cx - r, y, cz - r }, { cx - r + t, y, cz - r }, { cx - r + t, y, cz + r }, { cx - r, y, cz + r }, c);
    b.quad({ cx + r - t, y, cz - r }, { cx + r, y, cz - r }, { cx + r, y, cz + r }, { cx + r - t, y, cz + r }, c);
}

} // namespace

// The ground items whose sprite covers the pixel, the same test groundItemAtPixel uses for sprites
std::vector<Entity> Renderer::groundItemsAtPixel(World& w, const Terrain& t, const Camera& cam, float mx, float my,
                                                 int winW, int winH) {
    std::vector<Entity> out;
    const Mat4 vp = cam.viewProj((float)winW / (float)winH);
    for (Entity e : w.view<GroundItem, Transform>()) {
        const GroundItem* gi = w.get<GroundItem>(e);
        const Transform* tr = w.get<Transform>(e);
        if (!groundLoaded(t, tr->x, tr->z) || !withinDetail(cam, tr->x, tr->y, tr->z)) continue;
        ItemOnScreen os;
        if (!itemOnScreen(vp, winW, winH, *tr, 1.0f, os)) continue;
        if (!(mx >= os.cx - os.halfW && mx <= os.cx + os.halfW && my >= os.top && my <= os.bottom)) continue;
        if (os.bottom - os.top < 1.0f) continue;
        if (itemPixelSolid(gi->item, (mx - (os.cx - os.halfW)) / (2.0f * os.halfW), (my - os.top) / (os.bottom - os.top))) out.push_back(e);
    }
    std::sort(out.begin(), out.end());
    return out;
}

void Renderer::drawHud(const std::vector<HudEntry>& lines, int winW, int winH) {
    if (!hudFont_) return;

    // Rebuild only the lines whose text changed. The text is made white and tinted to its colour when drawn.
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i == hudLines_.size()) hudLines_.emplace_back();
        HudLine& hl = hudLines_[i];
        const std::string& text = lines[i].text;
        if (hl.tex.tex && hl.text == text) continue;
        if (hl.tex.tex) glDeleteTextures(1, &hl.tex.tex);

        int tw = 0, th = 0;
        TTF_GetStringSize(hudFont_, text.c_str(), text.size(), &tw, &th);
        hl.text = text;
        hl.tex = makeGlyphTexture(hudFont_, text.c_str(), SDL_Color{ 255, 255, 255, 255 });
        hl.w = (float)tw;
        hl.h = (float)th;
    }
    // Drop lines that are no longer shown
    while (hudLines_.size() > lines.size()) {
        if (hudLines_.back().tex.tex) glDeleteTextures(1, &hudLines_.back().tex.tex);
        hudLines_.pop_back();
    }
    if (lines.empty()) return;

    Mat4 pixels = ortho(0.0f, (float)winW, (float)winH, 0.0f, -1.0f, 1.0f);
    const float margin = 12.0f, pad = 6.0f, gap = 2.0f;

    glDisable(GL_DEPTH_TEST);

    // Dark panels behind the text
    Batch panels;
    float y = hudTop_;
    for (const HudLine& hl : hudLines_) {
        float x0 = margin, x1 = margin + hl.w + pad * 2.0f;
        float y0 = y, y1 = y + hl.h + pad * 2.0f;
        panels.quad({ x0, y0, 0 }, { x1, y0, 0 }, { x1, y1, 0 }, { x0, y1, 0 },
                    RGBA{ 0.0f, 0.0f, 0.0f, 0.5f });
        y = y1 + gap;
    }
    glUseProgram(colorProgram_);
    glUniformMatrix4fv(uMVPColor_, 1, GL_FALSE, pixels.m);
    glBindVertexArray(dynVao_);
    glBindBuffer(GL_ARRAY_BUFFER, dynVbo_);
    glBufferData(GL_ARRAY_BUFFER, panels.v.size() * sizeof(Vertex), panels.v.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)panels.v.size());

    // Text on top of each panel
    glUseProgram(glyphProgram_);
    glUniformMatrix4fv(uMVPGlyph_, 1, GL_FALSE, pixels.m);
    y = hudTop_;   // the same top as the panels
    for (size_t i = 0; i < hudLines_.size(); ++i) {
        const HudLine& hl = hudLines_[i];
        float x0 = margin + pad, x1 = x0 + hl.w;
        float y0 = y + pad, y1 = y0 + hl.h;
        float v[4 * 5] = {
            x0, y1, 0.0f, 0.0f, 1.0f,
            x1, y1, 0.0f, 1.0f, 1.0f,
            x1, y0, 0.0f, 1.0f, 0.0f,
            x0, y0, 0.0f, 0.0f, 0.0f
        };
        drawQuadVerts(hl.tex.tex, v, lines[i].color);
        y = y1 + pad + gap;
    }

    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::drawPivotRing(const Camera& cam, const Terrain& t, float alpha, int winW, int winH) {
    if (alpha <= 0.0f) return;

    // Sits on the ground at the exact point the camera orbits around
    const float cx = cam.focusX, cz = cam.focusZ;
    const float cy = t.heightAt(cx, cz) + 0.03f;
    const float outer = 0.5f, inner = 0.44f;     // about one tile across
    const int segments = 48;
    const float twoPi = 6.2831853f;

    Batch ring;
    RGBA col{ 0.95f, 0.95f, 1.0f, 0.9f * alpha };
    for (int i = 0; i < segments; ++i) {
        float a0 = twoPi * i / segments, a1 = twoPi * (i + 1) / segments;
        V3 o0{ cx + outer * std::cos(a0), cy, cz + outer * std::sin(a0) };
        V3 o1{ cx + outer * std::cos(a1), cy, cz + outer * std::sin(a1) };
        V3 i0{ cx + inner * std::cos(a0), cy, cz + inner * std::sin(a0) };
        V3 i1{ cx + inner * std::cos(a1), cy, cz + inner * std::sin(a1) };
        ring.quad(o0, o1, i1, i0, col);
    }

    glUseProgram(colorProgram_);
    glUniformMatrix4fv(uMVPColor_, 1, GL_FALSE, cam.viewProj((float)winW / (float)winH).m);
    glBindVertexArray(dynVao_);
    glBindBuffer(GL_ARRAY_BUFFER, dynVbo_);
    glBufferData(GL_ARRAY_BUFFER, ring.v.size() * sizeof(Vertex), ring.v.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)ring.v.size());
}

void Renderer::drawActionCrystal(const Camera& cam, const Terrain& t, float x, float z, float alpha,
                                 int winW, int winH) {
    if (alpha <= 0.0f) return;

    // An eight-faced crystal: a point at the top and bottom, with a ring of four corners between.
    // It floats a little above the ground, so it reads as a marker rather than part of the terrain.
    const float gy = t.heightAt(x, z);
    const float cy = gy + 0.55f, halfH = 0.45f, r = 0.22f;
    const V3 top{ x, cy + halfH, z };
    const V3 bottom{ x, cy - halfH, z };
    const V3 corner[4] = { V3{ x + r, cy, z }, V3{ x, cy, z + r }, V3{ x - r, cy, z }, V3{ x, cy, z - r } };

    Batch crystal;
    for (int i = 0; i < 4; ++i) {
        const V3& a = corner[i];
        const V3& b = corner[(i + 1) % 4];
        // Alternate brightness between faces so the facets read as separate planes
        const float shade = (i % 2 == 0) ? 1.0f : 0.8f;
        RGBA upper{ 0.6f * shade, 0.95f * shade, 1.0f, 0.95f * alpha };
        RGBA lower{ 0.3f * shade, 0.65f * shade, 0.95f, 0.95f * alpha };
        crystal.tri(top, a, b, upper);
        crystal.tri(bottom, b, a, lower);
    }

    glUseProgram(colorProgram_);
    glUniformMatrix4fv(uMVPColor_, 1, GL_FALSE, cam.viewProj((float)winW / (float)winH).m);
    glBindVertexArray(dynVao_);
    glBindBuffer(GL_ARRAY_BUFFER, dynVbo_);
    glBufferData(GL_ARRAY_BUFFER, crystal.v.size() * sizeof(Vertex), crystal.v.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)crystal.v.size());
}

void Renderer::drawFrame(World& w, const Terrain& t, const Camera& cam, const Selection& sel,
                         const std::vector<HudEntry>& hud, int winW, int winH) {
    glViewport(0, 0, winW, winH);
    glClearColor(0.55f, 0.7f, 0.9f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    viewProj_ = cam.viewProj((float)winW / (float)winH);
    float ex, ey, ez;
    cam.eye(ex, ey, ez);
    // Sprites turn to face a point 200 units behind the camera, along its view axis
    constexpr float BILLBOARD_DISTANCE = 200.0f;
    const float cp = std::cos(cam.pitch);
    billX_ = ex + BILLBOARD_DISTANCE * std::sin(cam.yaw) * cp;
    billZ_ = ez + BILLBOARD_DISTANCE * std::cos(cam.yaw) * cp;

    // 0. Sky: a gradient with a sun, sampled by view direction so it stays fixed while the camera moves
    {
        // View matrix with the eye at the origin, so the sky only follows rotation
        Mat4 proj = cam.projection((float)winW / (float)winH);
        Mat4 viewRot = lookAt(0.0f, 0.0f, 0.0f,
                              cam.focusX - ex, cam.focusY - ey, cam.focusZ - ez,
                              0.0f, 1.0f, 0.0f);
        float invSky[16];
        invert4x4(mul(proj, viewRot).m, invSky);

        // Sun, moon, stars and sky colors come from the sky settings and the time of day.
        // The sun rises at 06:00, peaks at noon and sets at 18:00. The moon sits opposite the sun.
        const float ang = (dayMinutes_ / 1440.0f - 0.25f) * 2.0f * 3.14159265f;
        const float elev = std::sin(ang);
        float sunDir[3] = { std::cos(ang) * 0.4f, elev, std::cos(ang) * 0.9f };
        const float sunLen = std::sqrt(sunDir[0] * sunDir[0] + sunDir[1] * sunDir[1] + sunDir[2] * sunDir[2]);
        for (float& c : sunDir) c /= sunLen;
        const float moonDir[3] = { -sunDir[0], -sunDir[1], -sunDir[2] };
        skySun_[0] = sunDir[0];
        skySun_[1] = sunDir[1];
        skySun_[2] = sunDir[2];

        auto smooth = [](float e0, float e1, float x) {
            float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        };
        auto lerp3 = [](const float* p, const float* q, float t, float* out) {
            for (int i = 0; i < 3; ++i) out[i] = p[i] + (q[i] - p[i]) * t;
        };
        const float day = smooth(-0.1f, 0.2f, elev);
        const float dusk = 1.0f - smooth(0.0f, 0.4f, std::fabs(elev));
        const float sunAlpha = smooth(-0.05f, 0.05f, elev);
        const float moonAlpha = sky_.moon ? (1.0f - sunAlpha) : 0.0f;
        const float starAlpha = sky_.stars ? (1.0f - day) * sky_.starBrightness : 0.0f;

        const auto& C = sky_.colors;
        float zen[3], hor[3], horDusk[3], sunC[3];
        lerp3(C[idx(ColorTarget::NightZenith)].data(), C[idx(ColorTarget::DayZenith)].data(), day, zen);
        lerp3(C[idx(ColorTarget::NightHorizon)].data(), C[idx(ColorTarget::DayHorizon)].data(), day, hor);
        lerp3(hor, C[idx(ColorTarget::DuskHorizon)].data(), dusk * 0.8f, horDusk);
        lerp3(C[idx(ColorTarget::DuskSun)].data(), C[idx(ColorTarget::Sun)].data(), day, sunC);
        for (float& c : sunC) c *= sky_.sunBrightness;

        glUseProgram(skyProgram_);
        glUniformMatrix4fv(uSkyInvVP_, 1, GL_FALSE, invSky);
        glUniform3fv(uSunDir_, 1, sunDir);
        glUniform1f(uSunAlpha_, sunAlpha);
        glUniform3fv(uSunCol_, 1, sunC);
        glUniform3fv(uMoonDir_, 1, moonDir);
        glUniform1f(uMoonAlpha_, moonAlpha);
        glUniform3fv(uMoonCol_, 1, C[idx(ColorTarget::Moon)].data());
        glUniform1f(uStarAlpha_, starAlpha);
        glUniform3fv(uZenith_, 1, zen);
        glUniform3fv(uHorizon_, 1, horDusk);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glBindVertexArray(skyVao_);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        glBindVertexArray(0);
        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
    }

    // 1. Terrain
    glUseProgram(colorProgram_);
    glUniformMatrix4fv(uMVPColor_, 1, GL_FALSE, viewProj_.m);
    drawTerrain(t, cam);

    // 2. Sprites and models, each standing on the terrain at its entity's height
    glUseProgram(glyphProgram_);
    glUniformMatrix4fv(uMVPGlyph_, 1, GL_FALSE, viewProj_.m);

    for (Entity e : w.view<Prop, Transform>()) {
        const Transform* tr = w.get<Transform>(e);
        if (!withinDetail(cam, tr->x, tr->y, tr->z)) continue;
        const PropKind kind = w.get<Prop>(e)->kind;
        if (idx(kind) < 0 || idx(kind) >= PROP_KIND_COUNT) continue;
        const float baseY = propBaseY(w, e);
        if (propModel(kind).vao) {
            drawModel(propModel(kind), tr->x, baseY, tr->z, propInfo(kind).size, w.get<Prop>(e)->yaw);
        } else {
            // No model: a pink square as tall as the prop's size, standing on its spot and turned to the camera
            drawGlyph(white_, tr->x, baseY, tr->z, missingPropSide(kind), MISSING_PROP_PINK);
        }
    }

    // Standing units upright. An Unconscious unit lies on its side, half gray (a corpse all gray), on the ground or, if
    // it is being carried, over its carrier's shoulders and in front of it.
    for (Entity e : w.view<Unit, Transform>()) {
        const Transform* tr = w.get<Transform>(e);
        if (!withinDetail(cam, tr->x, tr->y, tr->z)) continue;
        // A unit with a model is drawn by it, turned the way it walks; a mob, or a unit whose model is missing, by its sprite
        if (const Model* m = unitModel(w, e)) {
            const float yaw = unitYaw(e, tr->x, tr->z, unitIdle(w, e));
            // In a bed or bedroll it lies tipped the way a carried unit is, up on the bed
            const float bed = bedLift(w, e);
            const bool inBed = bed >= 0.0f;
            const bool lying = w.has<Unconscious>(e) || w.has<Carried>(e) || w.has<PlayingDead>(e) || inBed;
            const bool carried = w.has<Carried>(e);
            if (lying) setSaturation(unitSaturation(w, e));
            const RGBA tint = unitFactionTint(w, e);
            glUniform4f(uFaction_, tint.r, tint.g, tint.b, tint.a);
            // A carried model lies over its carrier's shoulders, higher than a sprite does since the model has depth
            const float lift = carried ? CARRIED_MODEL_LIFT : inBed ? bed : unitHover(w, e);
            drawUnitModel(*m, tr->x, tr->y + lift, tr->z, yaw, lying, carried ? 0.15f : 0.0f, carried || inBed,
                          unitHeight(w, e));
            glUniform4f(uFaction_, 0.0f, 0.0f, 0.0f, 0.0f);
            if (lying) setSaturation(1.0f);
            continue;
        }
        const TextTex& g = unitGlyph(w, e);
        const RGBA tint = WHITE;   // every unit in its own colours: a hostile one is not tinted
        const float bed = bedLift(w, e);
        if (w.has<Unconscious>(e) || w.has<Carried>(e) || w.has<PlayingDead>(e) || bed >= 0.0f) {
            setSaturation(unitSaturation(w, e));
            const bool carried = w.has<Carried>(e);
            const float lift = carried ? CARRIED_LIFT : std::max(0.0f, bed);
            drawLyingGlyph(g, tr->x, tr->y + lift, tr->z, unitHeight(w, e), tint,
                           carried ? 0.15f : 0.0f);
            setSaturation(1.0f);
            continue;
        }
        drawGlyph(g, tr->x, tr->y + unitHover(w, e), tr->z, unitHeight(w, e), tint);
    }

    drawGroundItems(w, t, cam);
    drawBuildPreview(t);

    // 3. Color overlays: selection rings and the drag box
    auto flush = [&](const Batch& batch, const Mat4& m) {
        glUseProgram(colorProgram_);
        glUniformMatrix4fv(uMVPColor_, 1, GL_FALSE, m.m);
        glBindVertexArray(dynVao_);
        glBindBuffer(GL_ARRAY_BUFFER, dynVbo_);
        glBufferData(GL_ARRAY_BUFFER, batch.v.size() * sizeof(Vertex), batch.v.data(), GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)batch.v.size());
    };

    Batch rings;
    for (Entity e : w.view<Unit, Transform, Selected>()) {
        const Transform* tr = w.get<Transform>(e);
        outlineSquare(rings, tr->x, tr->y + 0.03f, tr->z, 0.6f, 0.07f, RGBA{ 0.3f, 1.0f, 0.3f, 1.0f });
    }
    flush(rings, viewProj_);

    if (sel.dragging) {
        Batch box;
        float x0 = std::min(sel.x0, sel.x1), x1 = std::max(sel.x0, sel.x1);
        float y0 = std::min(sel.y0, sel.y1), y1 = std::max(sel.y0, sel.y1);
        RGBA fill{ 0.3f, 1.0f, 0.3f, 0.30f };
        RGBA line{ 0.3f, 1.0f, 0.3f, 1.0f };
        float th = 2.0f;

        box.quad({ x0, y0, 0 }, { x1, y0, 0 }, { x1, y1, 0 }, { x0, y1, 0 }, fill);
        box.quad({ x0, y0, 0 }, { x1, y0, 0 }, { x1, y0 + th, 0 }, { x0, y0 + th, 0 }, line);
        box.quad({ x0, y1 - th, 0 }, { x1, y1 - th, 0 }, { x1, y1, 0 }, { x0, y1, 0 }, line);
        box.quad({ x0, y0, 0 }, { x0 + th, y0, 0 }, { x0 + th, y1, 0 }, { x0, y1, 0 }, line);
        box.quad({ x1 - th, y0, 0 }, { x1, y0, 0 }, { x1, y1, 0 }, { x1 - th, y1, 0 }, line);

        // Blending must be on for the box's fill to be 30% see-through; the world pass leaves it off
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        flush(box, ortho(0.0f, (float)winW, (float)winH, 0.0f, -1.0f, 1.0f));
        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
    }

    drawHud(hud, winW, winH);
    glBindVertexArray(0);
}

// The placement preview: the footprint of the picked prop, as a grid of quarter-tile squares on the ground, lined in
// red, and filled green where it can be placed or red where it can't. Only squares whose centre lies in the shape are
// drawn, so the grid follows the shape, turned as the prop will be.
void Renderer::drawBuildPreview(const Terrain& t) {
    if (!buildPreview_.on && buildPreview_.ringR <= 0.0f) return;
    const float lift = 0.04f;      // off the ground, so the grid does not flicker into it
    const float lineW = 0.03f;     // a grid line, in tiles
    auto hgt = [&](float x, float z) { return t.heightAt(x, z) + lift; };
    Batch b;

    if (buildPreview_.on) {
        const Footprint& f = propInfo(buildPreview_.kind).foot;
        const float x0 = buildPreview_.x, z0 = buildPreview_.z, yaw = buildPreview_.yaw;
        const float reach = footprintReach(f);
        const float cell = 0.25f;      // grid squares are a quarter of a tile
        const RGBA fill = buildPreview_.valid ? RGBA{ 0.25f, 1.0f, 0.35f, 0.25f } : RGBA{ 1.0f, 0.2f, 0.2f, 0.25f };
        const RGBA line{ 1.0f, 0.15f, 0.15f, 0.9f };
        auto strip = [&](float ax, float az, float bx, float bz) {
            const float dx = bx - ax, dz = bz - az;
            const float len = std::hypot(dx, dz);
            if (len <= 0.0f) return;
            const float px = -dz / len * lineW * 0.5f, pz = dx / len * lineW * 0.5f;
            b.quad(V3{ ax + px, hgt(ax + px, az + pz), az + pz }, V3{ bx + px, hgt(bx + px, bz + pz), bz + pz },
                   V3{ bx - px, hgt(bx - px, bz - pz), bz - pz }, V3{ ax - px, hgt(ax - px, az - pz), az - pz }, line);
        };
        const float startX = std::floor((x0 - reach) / cell) * cell, endX = x0 + reach;
        const float startZ = std::floor((z0 - reach) / cell) * cell, endZ = z0 + reach;
        for (float gx = startX; gx < endX; gx += cell) {
            for (float gz = startZ; gz < endZ; gz += cell) {
                const float cx = gx + cell * 0.5f, cz = gz + cell * 0.5f;
                if (!insideFootprint(f, cx - x0, cz - z0, yaw)) continue;
                const V3 a{ gx, hgt(gx, gz), gz }, bb{ gx + cell, hgt(gx + cell, gz), gz };
                const V3 c{ gx + cell, hgt(gx + cell, gz + cell), gz + cell }, d{ gx, hgt(gx, gz + cell), gz + cell };
                b.quad(a, bb, c, d, fill);
                strip(gx, gz, gx + cell, gz);
                strip(gx + cell, gz, gx + cell, gz + cell);
                strip(gx + cell, gz + cell, gx, gz + cell);
                strip(gx, gz + cell, gx, gz);
            }
        }
    }

    // The claim circle: a band a third of a tile wide, on the ground around the flag's spot. While it shows, every
    // other flag's circle is drawn too, in white.
    auto ring = [&](float cx, float cz, float r, const RGBA& tone) {
        const float ro = r, ri = r - 0.3f;
        const int segments = 96;
        const float twoPi = 6.2831853f;
        for (int i = 0; i < segments; ++i) {
            const float a0 = twoPi * i / segments, a1 = twoPi * (i + 1) / segments;
            const float o0x = cx + ro * std::cos(a0), o0z = cz + ro * std::sin(a0);
            const float o1x = cx + ro * std::cos(a1), o1z = cz + ro * std::sin(a1);
            const float i0x = cx + ri * std::cos(a0), i0z = cz + ri * std::sin(a0);
            const float i1x = cx + ri * std::cos(a1), i1z = cz + ri * std::sin(a1);
            b.quad(V3{ o0x, hgt(o0x, o0z), o0z }, V3{ o1x, hgt(o1x, o1z), o1z },
                   V3{ i1x, hgt(i1x, i1z), i1z }, V3{ i0x, hgt(i0x, i0z), i0z }, tone);
        }
    };
    if (buildPreview_.ringR > 0.0f && buildPreview_.ringTone > 0) {
        for (const auto& o : buildPreview_.otherRings) ring(o.first, o.second, buildPreview_.ringR, RGBA{ 0.95f, 0.95f, 1.0f, 0.85f });
        const RGBA tone = buildPreview_.ringTone == 1 ? RGBA{ 0.25f, 1.0f, 0.35f, 0.85f }
                        : buildPreview_.ringTone == 2 ? RGBA{ 1.0f, 0.2f, 0.2f, 0.85f }
                                                      : RGBA{ 0.95f, 0.95f, 1.0f, 0.85f };
        ring(buildPreview_.x, buildPreview_.z, buildPreview_.ringR, tone);
    }
    if (b.v.empty()) return;

    glUseProgram(colorProgram_);
    glUniformMatrix4fv(uMVPColor_, 1, GL_FALSE, viewProj_.m);
    glBindVertexArray(dynVao_);
    glBindBuffer(GL_ARRAY_BUFFER, dynVbo_);
    glBufferData(GL_ARRAY_BUFFER, b.v.size() * sizeof(Vertex), b.v.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)b.v.size());
}

void Renderer::drawGroundItems(World& w, const Terrain& t, const Camera& cam) {
    // Each item stands on its ground point, turned to the camera, half as tall as a unit
    for (Entity e : w.view<GroundItem, Transform>()) {
        const Transform* tr = w.get<Transform>(e);
        if (!groundLoaded(t, tr->x, tr->z) || !withinDetail(cam, tr->x, tr->y, tr->z)) continue;
        const GroundItem* gi = w.get<GroundItem>(e);
        const TextTex* pic = itemTexture(gi->item);
        TextTex square = *pic;
        square.aspect = 1.0f;   // item pictures are square, whatever size the file is
        drawGlyph(square, tr->x, tr->y, tr->z, ITEM_GLYPH_H, WHITE);
    }
}

std::vector<Renderer::GroundStack> Renderer::groundStacks(World& w, const Terrain& t, const Camera& cam,
                                                          int winW, int winH) {
    const float s = winH / 720.0f;
    const Mat4 vp = cam.viewProj((float)winW / (float)winH);
    const float padX = 7.0f * s, padY = 3.0f * s, gapY = 4.0f * s, lineGap = 2.0f * s;
    const float lineH = listTextHeight("Ag");
    float ex = 0.0f, ey = 0.0f, ez = 0.0f;
    cam.eye(ex, ey, ez);

    // The items on each tile, with their sprite positions, names and squared distance from the camera
    struct Seen { Entity e; ItemOnScreen os; std::string name; float dist2; };
    std::map<std::pair<int, int>, std::vector<Seen>> tiles;
    for (Entity e : w.view<GroundItem, Transform>()) {
        const GroundItem* gi = w.get<GroundItem>(e);
        const Transform* tr = w.get<Transform>(e);
        if (!groundLoaded(t, tr->x, tr->z)) continue;
        Seen seen{ e, ItemOnScreen{}, itemNameOf(gi->item), 0.0f };
        const float dx = tr->x - ex, dy = tr->y - ey, dz = tr->z - ez;
        seen.dist2 = dx * dx + dy * dy + dz * dz;
        if (seen.dist2 > LOD_MID_TILES * LOD_MID_TILES || !withinDetail(cam, tr->x, tr->y, tr->z)) continue;
        if (!itemOnScreen(vp, winW, winH, *tr, 1.0f, seen.os)) continue;
        tiles[{ (int)std::floor(tr->x), (int)std::floor(tr->z) }].push_back(seen);
    }

    std::vector<GroundStack> out;
    for (auto& entry : tiles) {
        std::vector<Seen>& seen = entry.second;
        // Entity order, so the names keep the same places from frame to frame
        std::sort(seen.begin(), seen.end(), [](const Seen& a, const Seen& b) { return a.e < b.e; });

        // One box above the middle of the tile's items, one line per item, the widest name setting its width
        const float n = (float)seen.size();
        float cx = 0.0f, top = 0.0f, maxW = 0.0f, nearest = seen[0].dist2;
        for (const Seen& sn : seen) {
            cx += sn.os.cx;
            top += sn.os.top;
            maxW = std::max(maxW, listTextWidth(sn.name));
            nearest = std::min(nearest, sn.dist2);
        }
        cx /= n;
        top /= n;
        const float boxW = maxW + 2.0f * padX;
        const float boxH = n * lineH + (n - 1.0f) * lineGap + 2.0f * padY;

        GroundStack st;
        st.dist2 = nearest;
        st.box = Rect{ cx - boxW * 0.5f, top - gapY - boxH, boxW, boxH };
        if (st.box.x + st.box.w < 0.0f || st.box.x > winW || st.box.y + st.box.h < 0.0f || st.box.y > winH) continue;
        for (size_t i = 0; i < seen.size(); ++i) {
            st.items.push_back(seen[i].e);
            st.names.push_back(seen[i].name);
            st.rows.push_back(Rect{ st.box.x, st.box.y + padY + (float)i * (lineH + lineGap), boxW, lineH });
        }
        out.push_back(std::move(st));
    }
    // Farthest first, so the nearest box is drawn last, on top
    std::stable_sort(out.begin(), out.end(),
                     [](const GroundStack& a, const GroundStack& b) { return a.dist2 > b.dist2; });
    return out;
}

bool Renderer::topLabelRow(const std::vector<GroundStack>& stacks, float mx, float my, size_t& stack, size_t& row) {
    // The last stack is drawn on top, so search from the end. A box that covers the point without a
    // name line under it hides the boxes behind it.
    for (size_t k = stacks.size(); k-- > 0;) {
        const GroundStack& st = stacks[k];
        for (size_t j = 0; j < st.rows.size(); ++j) {
            if (st.rows[j].contains(mx, my)) {
                stack = k;
                row = j;
                return true;
            }
        }
        if (st.box.contains(mx, my)) return false;
    }
    return false;
}

Entity Renderer::groundItemAtPixel(World& w, const Terrain& t, const Camera& cam, float mx, float my, int winW, int winH,
                                   bool labels) {
    // A name line in a label box picks its item. The box on top wins, so the nearer item is picked.
    if (labels) {
        const std::vector<GroundStack> stacks = groundStacks(w, t, cam, winW, winH);
        size_t k = 0, j = 0;
        if (topLabelRow(stacks, mx, my, k, j)) return stacks[k].items[j];
    }

    // Otherwise the sprite under the mouse
    const Mat4 vp = cam.viewProj((float)winW / (float)winH);
    Entity best = NULL_ENTITY;
    float bestD2 = 0.0f;
    for (Entity e : w.view<GroundItem, Transform>()) {
        const GroundItem* gi = w.get<GroundItem>(e);
        const Transform* tr = w.get<Transform>(e);
        if (!groundLoaded(t, tr->x, tr->z) || !withinDetail(cam, tr->x, tr->y, tr->z)) continue;

        ItemOnScreen os;
        if (!itemOnScreen(vp, winW, winH, *tr, 1.0f, os)) continue;
        const bool onSprite = mx >= os.cx - os.halfW && mx <= os.cx + os.halfW && my >= os.top && my <= os.bottom;
        if (!onSprite || os.bottom - os.top < 1.0f) continue;
        // Only the visible part of its picture counts, not the clear pixels around it
        if (!itemPixelSolid(gi->item, (mx - (os.cx - os.halfW)) / (2.0f * os.halfW), (my - os.top) / (os.bottom - os.top))) continue;

        // The item nearest the mouse wins when several overlap
        const float cy = (os.top + os.bottom) * 0.5f;
        const float d2 = (mx - os.cx) * (mx - os.cx) + (my - cy) * (my - cy);
        if (best == NULL_ENTITY || d2 < bestD2) {
            best = e;
            bestD2 = d2;
        }
    }
    return best;
}
