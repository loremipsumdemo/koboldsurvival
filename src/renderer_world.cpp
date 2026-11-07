#include "renderer_internal.h"
#include "frustum.h"

using namespace render_detail;

namespace {
bool groundLoaded(const Terrain& t, float x, float z) {
    const int cx = (int)std::floor(x / CHUNK_TILES), cz = (int)std::floor(z / CHUNK_TILES);
    return t.chunkAt(cx, cz) != nullptr;
}

void outlineSquare(Batch& b, float cx, float y, float cz, float r, float t, RGBA c) {
    b.quad({ cx - r, y, cz - r }, { cx + r, y, cz - r }, { cx + r, y, cz - r + t }, { cx - r, y, cz - r + t }, c);
    b.quad({ cx - r, y, cz + r - t }, { cx + r, y, cz + r - t }, { cx + r, y, cz + r }, { cx - r, y, cz + r }, c);
    b.quad({ cx - r, y, cz - r }, { cx - r + t, y, cz - r }, { cx - r + t, y, cz + r }, { cx - r, y, cz + r }, c);
    b.quad({ cx + r - t, y, cz - r }, { cx + r, y, cz - r }, { cx + r, y, cz + r }, { cx + r - t, y, cz + r }, c);
}
}

std::vector<ObjectId> Renderer::groundItemsAtPixel(World& w, const Terrain& t, const Camera& cam, float mx, float my,
                                                 int winW, int winH) {
    std::vector<ObjectId> out;
    const Mat4 vp = cam.viewProj((float)winW / (float)winH);
    const unsigned long long ticks = SDL_GetTicks();
    for (ObjectId e : w.view<GroundItem, Transform>()) {
        const GroundItem* gi = w.get<GroundItem>(e);
        const Transform* tr = w.get<Transform>(e);
        if (!groundLoaded(t, tr->x, tr->z) || !withinDetail(cam, tr->x, tr->y, tr->z)) continue;
        ItemOnScreen os;
        // Where it is drawn right now, so a click on the picture finds it
        const Transform drawn{ tr->x, tr->y + itemBobLift(e, ticks), tr->z };
        if (!itemOnScreen(vp, winW, winH, drawn, 1.0f, os)) continue;
        if (!(mx >= os.cx - os.halfW && mx <= os.cx + os.halfW && my >= os.top && my <= os.bottom)) continue;
        if (os.bottom - os.top < 1.0f) continue;
        if (itemPixelSolid(gi->item, (mx - (os.cx - os.halfW)) / (2.0f * os.halfW), (my - os.top) / (os.bottom - os.top))) out.push_back(e);
    }
    std::sort(out.begin(), out.end());
    return out;
}

void Renderer::drawHud(const std::vector<HudEntry>& lines, int winW, int winH) {
    if (!text_.hud) return;

    std::vector<std::string> texts;
    texts.reserve(lines.size());
    for (const HudEntry& e : lines) texts.push_back(e.text);
    const std::vector<TextLine>& hudLines = text_.hudLines(texts);
    if (lines.empty()) return;

    Mat4 pixels = ortho(0.0f, (float)winW, (float)winH, 0.0f, -1.0f, 1.0f);
    const float margin = 12.0f, pad = 6.0f, gap = 2.0f;

    glDisable(GL_DEPTH_TEST);

    Batch panels;
    float y = hudTop_;
    for (const TextLine& hl : hudLines) {
        float x0 = margin, x1 = margin + hl.w + pad * 2.0f;
        float y0 = y, y1 = y + hl.h + pad * 2.0f;
        panels.quad({ x0, y0, 0 }, { x1, y0, 0 }, { x1, y1, 0 }, { x0, y1, 0 },
                    RGBA{ 0.0f, 0.0f, 0.0f, 0.5f });
        y = y1 + gap;
    }
    drawColorTris(panels, pixels);

    glUseProgram(glyphProgram_);
    glUniformMatrix4fv(uMVPGlyph_, 1, GL_FALSE, pixels.m);
    y = hudTop_;
    for (size_t i = 0; i < hudLines.size(); ++i) {
        const TextLine& hl = hudLines[i];
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

// A cave or galaxy sky takes too long to work out for every pixel of every frame, and it doesn't move, so it is drawn once
// into a cube of six pictures whenever the theme changes, and looked up from there
void Renderer::bakeSky(int skyKind, const float* zenith, const float* horizon, float stars) {
    constexpr int SIDE = 1024;
    skyBakeWanted_ = false;
    if (!skyCube_) {
        glGenTextures(1, &skyCube_);
        glBindTexture(GL_TEXTURE_CUBE_MAP, skyCube_);
        for (int face = 0; face < 6; ++face) {
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_RGBA8, SIDE, SIDE, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        }
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
    }
    GLint viewport[4], fbo = 0;
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
    const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    glDisable(GL_SCISSOR_TEST);

    // For each face: the directions that a picture's across, up and straight-on stand for, as OpenGL lays out a cube
    static const float FACES[6][9] = {
        { 0, 0, -1,  0, -1, 0,  1, 0, 0 },
        { 0, 0, 1,  0, -1, 0,  -1, 0, 0 },
        { 1, 0, 0,  0, 0, 1,  0, 1, 0 },
        { 1, 0, 0,  0, 0, -1,  0, -1, 0 },
        { 1, 0, 0,  0, -1, 0,  0, 0, 1 },
        { -1, 0, 0,  0, -1, 0,  0, 0, -1 },
    };
    glUseProgram(skyProgram_);
    glUniform1i(uSkyKind_, skyKind);
    glUniform1i(uSkyUseBaked_, 0);
    glUniform1f(uSunAlpha_, 0.0f);
    glUniform1f(uMoonAlpha_, 0.0f);
    glUniform1f(uStarAlpha_, stars);
    glUniform3fv(uZenith_, 1, zenith);
    glUniform3fv(uHorizon_, 1, horizon);
    glBindFramebuffer(GL_FRAMEBUFFER, skyBakeFbo_);
    glViewport(0, 0, SIDE, SIDE);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glBindVertexArray(skyVao_);
    for (int face = 0; face < 6; ++face) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, skyCube_, 0);
        const float* f = FACES[face];
        const float m[16] = { f[0], f[1], f[2], 0.0f, f[3], f[4], f[5], 0.0f, f[6], f[7], f[8], 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };
        glUniformMatrix4fv(uSkyInvVP_, 1, GL_FALSE, m);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    }
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)fbo);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    if (scissor) glEnable(GL_SCISSOR_TEST);
}

void Renderer::drawPivotRing(const Camera& cam, const Terrain& t, float alpha, int winW, int winH) {
    if (alpha <= 0.0f) return;

    const float cx = cam.focusX, cz = cam.focusZ;
    const float cy = t.heightAt(cx, cz) + 0.03f;
    const float outer = 0.5f, inner = 0.44f;
    const int segments = 48;

    Batch ring;
    RGBA col{ 0.95f, 0.95f, 1.0f, 0.9f * alpha };
    for (int i = 0; i < segments; ++i) {
        float a0 = TWO_PI * i / segments, a1 = TWO_PI * (i + 1) / segments;
        V3 o0{ cx + outer * std::cos(a0), cy, cz + outer * std::sin(a0) };
        V3 o1{ cx + outer * std::cos(a1), cy, cz + outer * std::sin(a1) };
        V3 i0{ cx + inner * std::cos(a0), cy, cz + inner * std::sin(a0) };
        V3 i1{ cx + inner * std::cos(a1), cy, cz + inner * std::sin(a1) };
        ring.quad(o0, o1, i1, i0, col);
    }

    drawColorTris(ring, cam.viewProj((float)winW / (float)winH));
}

void Renderer::drawActionCrystal(const Camera& cam, const Terrain& t, float x, float z, float alpha,
                                 int winW, int winH) {
    if (alpha <= 0.0f) return;

    const float gy = t.heightAt(x, z);
    const float cy = gy + 0.55f, halfH = 0.45f, r = 0.22f;
    const V3 top{ x, cy + halfH, z };
    const V3 bottom{ x, cy - halfH, z };
    const V3 corner[4] = { V3{ x + r, cy, z }, V3{ x, cy, z + r }, V3{ x - r, cy, z }, V3{ x, cy, z - r } };

    Batch crystal;
    for (int i = 0; i < 4; ++i) {
        const V3& a = corner[i];
        const V3& b = corner[(i + 1) % 4];
        const float shade = (i % 2 == 0) ? 1.0f : 0.8f;
        RGBA upper{ 0.6f * shade, 0.95f * shade, 1.0f, 0.95f * alpha };
        RGBA lower{ 0.3f * shade, 0.65f * shade, 0.95f, 0.95f * alpha };
        crystal.tri(top, a, b, upper);
        crystal.tri(bottom, b, a, lower);
    }

    drawColorTris(crystal, cam.viewProj((float)winW / (float)winH));
}

void Renderer::drawFrame(World& w, const Terrain& t, const Camera& cam, const Selection& sel,
                         const std::vector<HudEntry>& hud, int winW, int winH) {
    glViewport(0, 0, winW, winH);
    glClearColor(0.55f, 0.7f, 0.9f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Destroyed units leave their facing behind, so sweep once enough of them have piled up
    constexpr size_t FACING_SLACK = 64;
    if (unitFacing_.size() > w.count<Entity>() + FACING_SLACK) {
        for (auto it = unitFacing_.begin(); it != unitFacing_.end();) {
            if (w.has<Entity>(it->first)) ++it;
            else it = unitFacing_.erase(it);
        }
    }

    viewProj_ = cam.viewProj((float)winW / (float)winH);
    float ex, ey, ez;
    cam.eye(ex, ey, ez);
    constexpr float BILLBOARD_DISTANCE = 200.0f;
    const float cp = std::cos(cam.pitch);
    billX_ = ex + BILLBOARD_DISTANCE * std::sin(cam.yaw) * cp;
    billZ_ = ez + BILLBOARD_DISTANCE * std::cos(cam.yaw) * cp;

    {
        Mat4 proj = cam.projection((float)winW / (float)winH);
        Mat4 viewRot = lookAt(0.0f, 0.0f, 0.0f,
                              cam.focusX - ex, cam.focusY - ey, cam.focusZ - ez,
                              0.0f, 1.0f, 0.0f);
        float invSky[16];
        invert4x4(mul(proj, viewRot).m, invSky);

        const float minutes = hasTheme_ && !theme_.dayCycle ? (float)theme_.fixedHour * 60.0f : dayMinutes_;
        const float ang = (minutes / 1440.0f - 0.25f) * 2.0f * PI;
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
        const float sunUp = smooth(-0.05f, 0.05f, elev);
        const float sunAlpha = hasTheme_ && !theme_.sunShown ? 0.0f : sunUp;
        const float moonAlpha = hasTheme_ && !theme_.moonShown ? 0.0f : 1.0f - sunUp;
        // Space has its stars all day
        float starAlpha = 1.0f - day;
        if (hasTheme_) starAlpha = (theme_.skyKind == (int)SkyKind::Galaxy ? 1.0f : starAlpha) * (float)theme_.stars / 100.0f;

        const auto& C = hasTheme_ ? themeSky_.colors : sky_.colors;
        float zen[3], hor[3], horDusk[3], sunC[3];
        lerp3(C[idx(ColorTarget::NightZenith)].data(), C[idx(ColorTarget::DayZenith)].data(), day, zen);
        lerp3(C[idx(ColorTarget::NightHorizon)].data(), C[idx(ColorTarget::DayHorizon)].data(), day, hor);
        lerp3(hor, C[idx(ColorTarget::DuskHorizon)].data(), dusk * 0.8f, horDusk);
        lerp3(C[idx(ColorTarget::DuskSun)].data(), C[idx(ColorTarget::Sun)].data(), day, sunC);
        const int skyKind = hasTheme_ ? theme_.skyKind : (int)SkyKind::Gradient;
        if (skyKind == (int)SkyKind::Solid) {
            for (int i = 0; i < 3; ++i) zen[i] = horDusk[i] = C[idx(ColorTarget::DayZenith)][(size_t)i];
        } else if (skyKind == (int)SkyKind::Cave || skyKind == (int)SkyKind::Galaxy) {
            for (int i = 0; i < 3; ++i) {
                zen[i] = C[idx(ColorTarget::DayZenith)][(size_t)i];
                horDusk[i] = C[idx(ColorTarget::DayHorizon)][(size_t)i];
            }
        }

        if (skyBakeWanted_ && (skyKind == (int)SkyKind::Cave || skyKind == (int)SkyKind::Galaxy)) bakeSky(skyKind, zen, horDusk, starAlpha);
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
        glUniform1i(uSkyKind_, skyKind);
        const bool baked = skyKind == (int)SkyKind::Cave || skyKind == (int)SkyKind::Galaxy;
        glUniform1i(uSkyUseBaked_, baked ? 1 : 0);
        glUniform1i(uSkyBaked_, 0);
        if (baked) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_CUBE_MAP, skyCube_);
        }
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glBindVertexArray(skyVao_);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        glBindVertexArray(0);
        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
    }

    glUseProgram(colorProgram_);
    glUniformMatrix4fv(uMVPColor_, 1, GL_FALSE, viewProj_.m);
    drawTerrain(t, cam);

    glUseProgram(glyphProgram_);
    glUniformMatrix4fv(uMVPGlyph_, 1, GL_FALSE, viewProj_.m);

    const DetailRange detail(cam);
    const FrustumPlanes frustum = framePlanes(viewProj_);
    for (ObjectId e : w.view<Prop, Transform>()) {
        const Transform* tr = w.get<Transform>(e);
        if (!detail.within(tr->x, tr->y, tr->z)) continue;
        const PropKind kind = w.get<Prop>(e)->kind;
        if (idx(kind) < 0 || idx(kind) >= propKindCount()) continue;
        const float baseY = propBaseY(w, e);
        const bool modelled = propModel(kind).vao != 0;
        const float side = modelled ? propInfo(kind).size : missingPropSide(kind);
        if (!sphereVisible(frustum, tr->x, baseY + 0.5f * side, tr->z, side)) continue;
        if (modelled) {
            queuePropModel(kind, tr->x, baseY, tr->z, propInfo(kind).size, w.get<Prop>(e)->yaw);
        } else {
            drawGlyph(white_, tr->x, baseY, tr->z, missingPropSide(kind), MISSING_PROP_PINK);
        }
    }
    drawPropInstances();

    for (ObjectId e : w.view<Entity, Transform>()) {
        const Transform* tr = w.get<Transform>(e);
        if (!detail.within(tr->x, tr->y, tr->z)) continue;
        const float tall = std::max(1.0f, unitHeight(w, e));
        if (!sphereVisible(frustum, tr->x, tr->y + 0.5f * tall, tr->z, 1.5f * tall + 1.0f)) continue;
        const UnitPose pose = unitPose(w, e);
        if (pose.lying) setSaturation(unitSaturation(w, e));
        if (const Model* m = unitModel(w, e)) {
            const float yaw = unitYaw(e, tr->x, tr->z, unitIdle(w, e));
            const RGBA tint = unitFactionTint(w, e);
            glUniform4f(uFaction_, tint.r, tint.g, tint.b, tint.a);
            drawUnitModel(*m, unitWorldMatrix(w, e, *m, yaw));
            glUniform4f(uFaction_, 0.0f, 0.0f, 0.0f, 0.0f);
        } else if (pose.lying) {
            drawLyingGlyph(unitGlyph(w, e), tr->x, tr->y + pose.lift, tr->z, unitHeight(w, e), WHITE, pose.carried ? 0.15f : 0.0f);
        } else {
            drawGlyph(unitGlyph(w, e), tr->x, tr->y + pose.lift, tr->z, unitHeight(w, e), WHITE);
        }
        if (pose.lying) setSaturation(1.0f);
    }

    drawGroundItems(w, t, cam);
    drawBuildPreview(t);

    Batch rings;
    for (ObjectId e : w.view<Entity, Transform, Selected>()) {
        const Transform* tr = w.get<Transform>(e);
        outlineSquare(rings, tr->x, tr->y + 0.03f, tr->z, 0.6f, 0.07f, RGBA{ 0.3f, 1.0f, 0.3f, 1.0f });
    }
    drawColorTris(rings, viewProj_);

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
        drawColorTris(box, ortho(0.0f, (float)winW, (float)winH, 0.0f, -1.0f, 1.0f));
        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
    }

    drawHud(hud, winW, winH);
    glBindVertexArray(0);
}

void Renderer::drawBuildPreview(const Terrain& t) {
    if (!buildPreview_.on && buildPreview_.ringR <= 0.0f) return;
    const float lift = 0.04f;
    const float lineW = 0.03f;
    auto hgt = [&](float x, float z) { return t.heightAt(x, z) + lift; };
    Batch b;

    if (buildPreview_.on) {
        const Footprint& f = propInfo(buildPreview_.kind).foot;
        const float x0 = buildPreview_.x, z0 = buildPreview_.z;
        const float reach = footprintReach(f);
        const float cell = 0.25f;
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
                if (!insideFootprint(f, cx - x0, cz - z0, 0.0f)) continue;
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

    auto ring = [&](float cx, float cz, float r, const RGBA& tone) {
        const float ro = r, ri = r - 0.3f;
        const int segments = 96;
        for (int i = 0; i < segments; ++i) {
            const float a0 = TWO_PI * i / segments, a1 = TWO_PI * (i + 1) / segments;
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

    drawColorTris(b, viewProj_);
}

void Renderer::drawGroundItems(World& w, const Terrain& t, const Camera& cam) {
    const unsigned long long ticks = SDL_GetTicks();
    std::vector<ObjectId> items;
    Batch shadows;
    for (ObjectId e : w.view<GroundItem, Transform>()) {
        const Transform* tr = w.get<Transform>(e);
        if (!groundLoaded(t, tr->x, tr->z) || !withinDetail(cam, tr->x, tr->y, tr->z)) continue;
        items.push_back(e);

        // Dark in the middle and fading out at the edge, and a little smaller while the item is high
        constexpr int SEGMENTS = 16;
        constexpr float SHADOW_LIFT = 0.03f;
        const float radius = ITEM_SHADOW_RADIUS * (1.0f - 0.15f * itemBobShare(e, ticks));
        const float shadowAlpha = hasTheme_ ? std::min(1.0f, ITEM_SHADOW_ALPHA * (float)theme_.shadows / 50.0f) : ITEM_SHADOW_ALPHA;
        const RGBA middle{ 0.0f, 0.0f, 0.0f, shadowAlpha }, edge{ 0.0f, 0.0f, 0.0f, 0.0f };
        const V3 center{ tr->x, t.heightAt(tr->x, tr->z) + SHADOW_LIFT, tr->z };
        V3 last{};
        for (int i = 0; i <= SEGMENTS; ++i) {
            const float a = TWO_PI * (float)i / (float)SEGMENTS;
            const float x = tr->x + radius * std::cos(a), z = tr->z + radius * std::sin(a);
            const V3 rim{ x, t.heightAt(x, z) + SHADOW_LIFT, z };
            if (i > 0) {
                shadows.v.push_back({ center, middle });
                shadows.v.push_back({ last, edge });
                shadows.v.push_back({ rim, edge });
            }
            last = rim;
        }
    }
    if (items.empty()) return;

    const GLboolean wasBlending = glIsEnabled(GL_BLEND);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    drawColorTris(shadows, viewProj_);
    glDepthMask(GL_TRUE);
    if (!wasBlending) glDisable(GL_BLEND);
    glUseProgram(glyphProgram_);

    for (ObjectId e : items) {
        const Transform* tr = w.get<Transform>(e);
        const GroundItem* gi = w.get<GroundItem>(e);
        const TextTex* pic = itemTexture(gi->item);
        TextTex square = *pic;
        square.aspect = 1.0f;
        glUniform1f(uShine_, itemShineShare(e, ticks));
        drawGlyph(square, tr->x, tr->y + itemBobLift(e, ticks), tr->z, ITEM_GLYPH_H, WHITE);
    }
    glUniform1f(uShine_, 0.0f);
}

std::vector<Renderer::GroundStack> Renderer::groundStacks(World& w, const Terrain& t, const Camera& cam,
                                                          int winW, int winH) {
    const float s = uiScaleFor(winH);
    const Mat4 vp = cam.viewProj((float)winW / (float)winH);
    const float padX = 7.0f * s, padY = 3.0f * s, gapY = 4.0f * s, lineGap = 2.0f * s;
    const float lineH = listTextHeight("Ag");
    float ex = 0.0f, ey = 0.0f, ez = 0.0f;
    cam.eye(ex, ey, ez);

    struct Seen { std::pair<int, int> tile; ObjectId e; ItemOnScreen os; std::string name; float dist2; };
    std::vector<Seen> all;
    const DetailRange detail(cam);
    for (ObjectId e : w.view<GroundItem, Transform>()) {
        const Transform* tr = w.get<Transform>(e);
        if (!groundLoaded(t, tr->x, tr->z)) continue;
        const float dx = tr->x - ex, dy = tr->y - ey, dz = tr->z - ez;
        const float dist2 = dx * dx + dy * dy + dz * dz;
        if (dist2 > LOD_MID_TILES * LOD_MID_TILES || !detail.within(tr->x, tr->y, tr->z)) continue;
        ItemOnScreen os{};
        if (!itemOnScreen(vp, winW, winH, *tr, 1.0f, os)) continue;
        all.push_back(Seen{ { (int)std::floor(tr->x), (int)std::floor(tr->z) }, e, os,
                            itemNameOf(w.get<GroundItem>(e)->item), dist2 });
    }
    std::sort(all.begin(), all.end(), [](const Seen& a, const Seen& b) {
        return a.tile != b.tile ? a.tile < b.tile : a.e < b.e;
    });

    std::vector<GroundStack> out;
    for (size_t first = 0; first < all.size();) {
        size_t end = first + 1;
        while (end < all.size() && all[end].tile == all[first].tile) ++end;
        const Seen* seen = &all[first];
        const size_t count = end - first;
        first = end;

        const float n = (float)count;
        float cx = 0.0f, top = 0.0f, maxW = 0.0f, nearest = seen[0].dist2;
        for (size_t k = 0; k < count; ++k) {
            const Seen& sn = seen[k];
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
        for (size_t i = 0; i < count; ++i) {
            st.items.push_back(seen[i].e);
            st.names.push_back(seen[i].name);
            st.rows.push_back(Rect{ st.box.x, st.box.y + padY + (float)i * (lineH + lineGap), boxW, lineH });
        }
        out.push_back(std::move(st));
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const GroundStack& a, const GroundStack& b) { return a.dist2 > b.dist2; });
    return out;
}

bool Renderer::topLabelRow(const std::vector<GroundStack>& stacks, float mx, float my, size_t& stack, size_t& row) {
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

ObjectId Renderer::groundItemAtPixel(World& w, const Terrain& t, const Camera& cam, float mx, float my, int winW, int winH,
                                   bool labels) {
    if (labels) {
        const std::vector<GroundStack> stacks = groundStacks(w, t, cam, winW, winH);
        size_t k = 0, j = 0;
        if (topLabelRow(stacks, mx, my, k, j)) return stacks[k].items[j];
    }

    return nearestGroundItem(groundItemsAtPixel(w, t, cam, mx, my, winW, winH), w, cam, mx, my, winW, winH);
}

ObjectId Renderer::nearestGroundItem(const std::vector<ObjectId>& items, World& w, const Camera& cam, float mx, float my,
                                   int winW, int winH) {
    const Mat4 vp = cam.viewProj((float)winW / (float)winH);
    ObjectId best = NO_OBJECT;
    float bestD2 = 0.0f;
    for (ObjectId e : items) {
        const Transform* tr = w.get<Transform>(e);
        ItemOnScreen os;
        if (!tr || !itemOnScreen(vp, winW, winH, *tr, 1.0f, os)) continue;
        const float cy = (os.top + os.bottom) * 0.5f;
        const float d2 = (mx - os.cx) * (mx - os.cx) + (my - cy) * (my - cy);
        if (best == NO_OBJECT || d2 < bestD2) {
            best = e;
            bestD2 = d2;
        }
    }
    return best;
}
