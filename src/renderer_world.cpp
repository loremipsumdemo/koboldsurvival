#include "motivation.h"
#include "prop_visual.h"
#include "renderer_internal.h"
#include <unordered_set>
#include "airship.h"
#include "building.h"
#include "wheelbarrow.h"
#include "frustum.h"
#include <unordered_map>

using namespace render_detail;

namespace {
// Whether to draw what stands at a spot: all of it is, out to the detail distance, loaded chunk or not (the far ground is drawn under it)
bool groundLoaded(const Terrain&, float, float) { return true; }

void outlineSquare(Batch& b, float cx, float y, float cz, float r, float t, RGBA c) {
    b.quad({ cx - r, y, cz - r }, { cx + r, y, cz - r }, { cx + r, y, cz - r + t }, { cx - r, y, cz - r + t }, c);
    b.quad({ cx - r, y, cz + r - t }, { cx + r, y, cz + r - t }, { cx + r, y, cz + r }, { cx - r, y, cz + r }, c);
    b.quad({ cx - r, y, cz - r }, { cx - r + t, y, cz - r }, { cx - r + t, y, cz + r }, { cx - r, y, cz + r }, c);
    b.quad({ cx + r - t, y, cz - r }, { cx + r, y, cz - r }, { cx + r, y, cz + r }, { cx + r - t, y, cz + r }, c);
}

// Items on one tile make a pile. The pile floats as one, on the loop of its oldest item, and its items keep a fixed
// order front to back, each one a little further from the camera than the one before, so they never trade places.
// The most valuable item is in front, then the higher item number, then the older item.
constexpr float PILE_STEP = 0.02f;

struct PileSpot {
    ObjectId bob = NO_OBJECT;   // whose float loop the pile follows
    int depth = 0;              // 0 is the front
};

std::unordered_map<ObjectId, PileSpot> pileSpots(World& w, const std::vector<ObjectId>& items) {
    struct Entry { std::pair<int, int> tile; int value; int item; ObjectId e; };
    std::vector<Entry> all;
    all.reserve(items.size());
    for (ObjectId e : items) {
        const Transform* tr = w.get<Transform>(e);
        const GroundItem* gi = w.get<GroundItem>(e);
        if (!tr || !gi) continue;
        const ItemDef* d = findItem(gi->item);
        all.push_back(Entry{ { (int)std::floor(tr->x), (int)std::floor(tr->z) }, d ? d->buyValue : 0, gi->item, e });
    }
    std::sort(all.begin(), all.end(), [](const Entry& a, const Entry& b) {
        if (a.tile != b.tile) return a.tile < b.tile;
        if (a.value != b.value) return a.value > b.value;
        if (a.item != b.item) return a.item > b.item;
        return a.e < b.e;
    });
    std::unordered_map<ObjectId, PileSpot> out;
    out.reserve(all.size());
    for (size_t first = 0; first < all.size();) {
        size_t end = first + 1;
        ObjectId oldest = all[first].e;
        while (end < all.size() && all[end].tile == all[first].tile) oldest = std::min(oldest, all[end++].e);
        for (size_t i = first; i < end; ++i) out[all[i].e] = PileSpot{ oldest, (int)(i - first) };
        first = end;
    }
    return out;
}

// Where an item in a pile is drawn right now: floating with its pile, and pushed back from the camera by its depth
Transform pileDrawn(const Transform& tr, const PileSpot& spot, unsigned long long ticks, float ex, float ey, float ez) {
    Transform at{ tr.x, tr.y + itemBobLift(spot.bob, ticks), tr.z };
    const float dx = ex - at.x, dy = ey - at.y, dz = ez - at.z;
    const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (len > 1e-4f && spot.depth > 0) {
        const float back = PILE_STEP * (float)spot.depth / len;
        at.x -= dx * back;
        at.y -= dy * back;
        at.z -= dz * back;
    }
    return at;
}
}

std::vector<ObjectId> Renderer::groundItemsAtPixel(World& w, const Terrain& t, const Camera& cam, float mx, float my,
                                                 int winW, int winH) {
    std::vector<ObjectId> out;
    const Mat4 vp = cam.viewProj((float)winW / (float)winH);
    const unsigned long long ticks = SDL_GetTicks();
    float ex = 0.0f, ey = 0.0f, ez = 0.0f;
    cam.eye(ex, ey, ez);
    std::vector<ObjectId> shown;
    for (ObjectId e : w.view<GroundItem, Transform>()) {
        const Transform* tr = w.get<Transform>(e);
        if (groundLoaded(t, tr->x, tr->z) && withinDetail(cam, tr->x, tr->y, tr->z) && !pickHidden(w, e)) shown.push_back(e);
    }
    const std::unordered_map<ObjectId, PileSpot> spots = pileSpots(w, shown);
    for (ObjectId e : shown) {
        const GroundItem* gi = w.get<GroundItem>(e);
        const Transform* tr = w.get<Transform>(e);
        ItemOnScreen os;
        // Where it is drawn right now, so a click on the picture finds it
        const Transform drawn = pileDrawn(*tr, spots.at(e), ticks, ex, ey, ez);
        if (!itemOnScreen(vp, winW, winH, drawn, 1.0f, os)) continue;
        if (!(mx >= os.cx - os.halfW && mx <= os.cx + os.halfW && my >= os.top && my <= os.bottom)) continue;
        if (os.bottom - os.top < 1.0f) continue;
        if (itemPixelSolid(gi->item, (mx - (os.cx - os.halfW)) / (2.0f * os.halfW), (my - os.top) / (os.bottom - os.top))) out.push_back(e);
    }
    // The front of each pile first, so whatever takes the first hit takes the item that shows
    std::sort(out.begin(), out.end(), [&](ObjectId a, ObjectId b) {
        const int da = spots.at(a).depth, db = spots.at(b).depth;
        return da != db ? da < db : a < b;
    });
    return out;
}

bool Renderer::cutAway(World& w, ObjectId e) const {
    if (!cutaway_.on) return false;
    const Transform* tr = w.get<Transform>(e);
    if (!tr || storeyOf(w, e) <= cutaway_.level) return false;
    return cutaway_.tiles.count(tileKey((int)std::floor(tr->x), (int)std::floor(tr->z))) > 0;
}

bool Renderer::pickHidden(World& w, ObjectId e) const {
    if (!cutaway_.on) return false;
    const Transform* tr = w.get<Transform>(e);
    if (!tr || storeyOf(w, e) == cutaway_.level) return false;
    // A ladder or stairs from the floor below comes up to this one: it is clicked to climb down
    if (const Prop* p = w.get<Prop>(e); p && propInfo(p->kind).ladder && storeyOf(w, e) == cutaway_.level - 1) return false;
    return cutaway_.tiles.count(tileKey((int)std::floor(tr->x), (int)std::floor(tr->z))) > 0;
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
    const float cy = t.heightAt(cx, cz) + cam.floorLift + 0.03f;
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

void Renderer::drawFormationPreview(const Camera& cam, const Terrain& t, const std::vector<Vec2>& spots, int winW, int winH) {
    const float outer = 0.45f, inner = 0.34f;
    constexpr int SEGMENTS = 24;
    Batch rings;
    const RGBA col{ 0.45f, 0.90f, 1.0f, 0.9f };
    for (const Vec2& at : spots) {
        const float y = t.heightAt(at.x, at.z) + 0.05f;
        for (int i = 0; i < SEGMENTS; ++i) {
            const float a0 = TWO_PI * i / SEGMENTS, a1 = TWO_PI * (i + 1) / SEGMENTS;
            const V3 o0{ at.x + outer * std::cos(a0), y, at.z + outer * std::sin(a0) };
            const V3 o1{ at.x + outer * std::cos(a1), y, at.z + outer * std::sin(a1) };
            const V3 i0{ at.x + inner * std::cos(a0), y, at.z + inner * std::sin(a0) };
            const V3 i1{ at.x + inner * std::cos(a1), y, at.z + inner * std::sin(a1) };
            rings.quad(o0, o1, i1, i0, col);
        }
    }
    drawColorTris(rings, cam.viewProj((float)winW / (float)winH));
}

void Renderer::drawActionCrystal(const Camera& cam, const Terrain& t, float x, float z, float alpha, int storey,
                                 int winW, int winH) {
    if (alpha <= 0.0f) return;

    const float gy = t.heightAt(x, z) + storeyBaseHeight(storey);
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
    beginScene(winW, winH);
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

        if (skyBakeWanted_ && SDL_GetTicks() >= skyBakeNotBefore_ && (skyKind == (int)SkyKind::Cave || skyKind == (int)SkyKind::Galaxy)) bakeSky(skyKind, zen, horDusk, starAlpha);
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
        // The sky is a quad at the far plane. Where there is water it waits until the ground is down and fills only what is left
        // (depth still at its far value), so the card does not work out sky pixels that terrain covers. Over a void the ground's edge
        // blends into the sky behind it, so there it is drawn first.
        auto paintSky = [this, baked](bool afterGround) {
            glUseProgram(skyProgram_);
            if (baked) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_CUBE_MAP, skyCube_);
            }
            if (afterGround) glDepthFunc(GL_LEQUAL);
            else glDisable(GL_DEPTH_TEST);
            glDepthMask(GL_FALSE);
            glBindVertexArray(skyVao_);
            glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
            glBindVertexArray(0);
            glDepthMask(GL_TRUE);
            glDepthFunc(GL_LESS);
            glEnable(GL_DEPTH_TEST);
        };
        if (!hasTheme_ || theme_.waterShown) skyLater_ = [paintSky]() { paintSky(true); };
        else paintSky(false);
    }

    glUseProgram(colorProgram_);
    glUniformMatrix4fv(uMVPColor_, 1, GL_FALSE, viewProj_.m);
    drawTerrain(t, cam);
    if (skyLater_) {   // drawTerrain paints it between the ground and the water; this is only if it did not get that far
        skyLater_();
        skyLater_ = nullptr;
    }

    glUseProgram(glyphProgram_);
    glUniformMatrix4fv(uMVPGlyph_, 1, GL_FALSE, viewProj_.m);

    const DetailRange detail(cam);
    const FrustumPlanes frustum = framePlanes(viewProj_);
    // Buildings: blueprints are see-through boxes, a foundation is a slab on each of its tiles, and a wall takes the shape
    // that fits the walls and doors beside it
    // The walls' and doors' tiles only change when one is added or removed, or moves between floors, so they are kept
    // and worked out again only then. Walls and doors don't move, and a foundation doesn't change a wall's shape.
    BuildingTiles& bt = buildingTiles_;
    if (!bt.valid || bt.propRevision != w.revision<Prop>() || bt.storeyRevision != w.revision<Storey>()) {
        bt.walls.clear();
        bt.doors.clear();
        bt.perim.clear();
        bt.floors.clear();
        for (ObjectId e : w.view<Prop, Transform>()) {
            const PropInfo& info = propInfo(w.get<Prop>(e)->kind);
            if (info.perimeter) {
                const Transform* ptr = w.get<Transform>(e);
                int px0 = 0, pz0 = 0, px1 = 0, pz1 = 0;
                propTileRect(info, ptr->x, ptr->z, w.get<Prop>(e)->yaw, px0, pz0, px1, pz1);
                for (int z = pz0; z <= pz1; ++z) {
                    for (int x = px0; x <= px1; ++x) bt.perim.insert(tileKey(x, z));
                }
                continue;
            }
            if (info.upperFloor) {
                const Transform* ftr = w.get<Transform>(e);
                bt.floors.insert(levelKey((int)std::floor(ftr->x), (int)std::floor(ftr->z), storeyOf(w, e)));
                continue;
            }
            if (!info.wallRule && !info.door) continue;
            const Transform* tr = w.get<Transform>(e);
            const long long k = levelKey((int)std::floor(tr->x), (int)std::floor(tr->z), storeyOf(w, e));
            bt.walls.insert(k);
            if (info.door) bt.doors.insert(k);
        }
        bt.propRevision = w.revision<Prop>();
        bt.storeyRevision = w.revision<Storey>();
        bt.valid = true;
    }
    const std::unordered_set<long long>& wallTiles = bt.walls;   // walls join the walls on their own floor
    // A door stands open while anyone is in its doorway. Units move every frame, so only the door tiles are looked up.
    std::unordered_set<long long> standing;
    if (!bt.doors.empty()) {
        for (ObjectId u : w.view<Entity, Transform>()) {
            if (outCold(w, u) || w.has<Carried>(u)) continue;
            const Transform* ut = w.get<Transform>(u);
            const long long k = levelKey((int)std::floor(ut->x), (int)std::floor(ut->z), storeyOf(w, u));
            if (bt.doors.count(k)) standing.insert(k);
        }
    }
    // Blueprints are see-through, so they are drawn after everything solid (units included), farthest first
    struct PlannedBox {
        float dist2;
        float x0, y0, z0, x1, y1, z1;
        bool ghost;   // laid ahead of what it stands on: drawn fainter
    };
    std::vector<PlannedBox> plannedBoxes;
    bool drawingGhost = false;
    fires_.clear();
    Batch stairSteps;   // stairs, drawn as plain steps, and the mooring ropes of Airships nobody is aboard
    // A thin square rope from a to b
    auto rope = [&](V3 a, V3 b, float r, RGBA col) {
        stairSteps.quad(V3{ a.x - r, a.y, a.z - r }, V3{ a.x + r, a.y, a.z - r }, V3{ b.x + r, b.y, b.z - r }, V3{ b.x - r, b.y, b.z - r }, col);
        stairSteps.quad(V3{ a.x - r, a.y, a.z + r }, V3{ a.x + r, a.y, a.z + r }, V3{ b.x + r, b.y, b.z + r }, V3{ b.x - r, b.y, b.z + r }, col);
        stairSteps.quad(V3{ a.x - r, a.y, a.z - r }, V3{ a.x - r, a.y, a.z + r }, V3{ b.x - r, b.y, b.z + r }, V3{ b.x - r, b.y, b.z - r }, col);
        stairSteps.quad(V3{ a.x + r, a.y, a.z - r }, V3{ a.x + r, a.y, a.z + r }, V3{ b.x + r, b.y, b.z + r }, V3{ b.x + r, b.y, b.z - r }, col);
    };
    // Who pushes each Wheelbarrow: it is drawn in front of them, facing their way
    std::unordered_map<ObjectId, ObjectId> pushers;
    for (ObjectId u : w.view<Pushing, Transform>()) pushers[w.get<Pushing>(u)->barrow] = u;
    const RGBA planned{ 0.35f, 0.7f, 1.0f, 0.22f }, plannedEdge{ 0.55f, 0.85f, 1.0f, 0.45f };
    float camX = 0.0f, camY = 0.0f, camZ = 0.0f;
    cam.eye(camX, camY, camZ);
    auto box = [&](float x0, float y0, float z0, float x1, float y1, float z1, RGBA, RGBA) {
        if (!boxVisible(frustum, x0, y0, z0, x1, y1, z1)) return;
        const float cx = (x0 + x1) * 0.5f - camX, cy = (y0 + y1) * 0.5f - camY, cz = (z0 + z1) * 0.5f - camZ;
        plannedBoxes.push_back(PlannedBox{ cx * cx + cy * cy + cz * cz, x0, y0, z0, x1, y1, z1, drawingGhost });
    };
    const RGBA ghostFill{ 0.75f, 0.82f, 0.95f, 0.07f }, ghostEdge{ 0.75f, 0.82f, 0.95f, 0.2f };
    const RGBA condemnedTint{ 1.0f, 0.12f, 0.08f, 0.5f };   // a building being pulled down: half red
    // One prop: queued to be drawn (models are drawn together, a model at a time, once every prop and unit is queued),
    // or added to the stairs' and blueprints' batches
    auto drawProp = [&](ObjectId e, const Transform* tr, PropKind kind, const PropInfo& info) {
        const Blueprint* planBp = w.get<Blueprint>(e);
        drawingGhost = planBp && planBp->ghost;
        propTint_ = w.has<Condemned>(e) ? condemnedTint : RGBA{ 0.0f, 0.0f, 0.0f, 0.0f };
        // The model for this piece's material and shape (0 plain), or nullptr for the plain wood one
        const BuildMaterial mat = materialOf(w, e);
        auto materialModel = [&](int shape) -> const Model* {
            if (mat == BuildMaterial::Wood || (size_t)idx(kind) >= materialModels_.size()) return nullptr;
            const Model& m = materialModels_[(size_t)idx(kind)][(size_t)mat - 1][(size_t)shape];
            return m.vao ? &m : nullptr;
        };
        if (const Foundation* f = w.get<Foundation>(e)) {
            // The whole slab first (its ground may rise and fall, so the box is tall), then each tile
            if (!boxVisible(frustum, (float)f->x0, tr->y - 64.0f, (float)f->z0, (float)(f->x0 + f->w), tr->y + 64.0f,
                            (float)(f->z0 + f->h))) {
                return;
            }
            const bool plannedOnly = w.has<Blueprint>(e);
            const Model* made = materialModel(0);
            for (int z = f->z0; z < f->z0 + f->h; ++z) {
                for (int x = f->x0; x < f->x0 + f->w; ++x) {
                    const float cx = x + 0.5f, cz = z + 0.5f, y = t.heightAt(cx, cz);
                    if (plannedOnly) box((float)x + 0.02f, y, (float)z + 0.02f, x + 0.98f, y + 0.2f, z + 0.98f, planned, plannedEdge);
                    else if (!sphereVisible(frustum, cx, y + 0.1f, cz, 0.8f)) continue;
                    else if (made) queueModel(*made, cx, y, cz, info.size, 0.0f);
                    else queuePropModel(kind, cx, y, cz, info.size, 0.0f);
                }
            }
            return;
        }
        if (info.stairs && !info.slope) {
            // Three steps up toward the way they face, each a third of the way to the floor above
            const int d = stairsDir(w, e), lv = storeyOf(w, e);
            const float rise = storeyBaseHeight(lv + 1) - storeyBaseHeight(lv);
            const int mx = (int)std::floor(tr->x), mz = (int)std::floor(tr->z);
            const bool plannedOnly = w.has<Blueprint>(e);
            for (int i = 0; i < 3; ++i) {
                const float x0 = (float)(mx + (i - 1) * STAIRS_DX[d]), z0 = (float)(mz + (i - 1) * STAIRS_DZ[d]);
                const float top = tr->y + rise * (float)(i + 1) / 3.0f;
                if (plannedOnly) {
                    box(x0 + 0.02f, tr->y, z0 + 0.02f, x0 + 0.98f, top, z0 + 0.98f, planned, plannedEdge);
                    continue;
                }
                const float x1 = x0 + 1.0f, z1 = z0 + 1.0f, y0 = tr->y;
                if (!boxVisible(frustum, x0, y0, z0, x1, top, z1)) continue;
                const Model& made = stairSteps_[(size_t)mat];
                const Model& s = made.vao ? made : stairSteps_[0];
                if (s.vao && s.maxX > s.minX && s.maxY > s.minY && s.maxZ > s.minZ) {
                    // The step's model stretched over the step: a tile across and up to its top, wood grain and all
                    const float sx = 1.0f / (s.maxX - s.minX), sy = (top - y0) / (s.maxY - s.minY), sz = 1.0f / (s.maxZ - s.minZ);
                    Mat4 place{};
                    place.m[0] = sx;
                    place.m[5] = sy;
                    place.m[10] = sz;
                    place.m[12] = x0 - s.minX * sx;
                    place.m[13] = y0 - s.minY * sy;
                    place.m[14] = z0 - s.minZ * sz;
                    place.m[15] = 1.0f;
                    queueModelMatrix(s, place, propTint_, 1.0f);
                    continue;
                }
                const RGBA side{ 0.45f, 0.30f, 0.17f, 1.0f }, face{ 0.62f, 0.44f, 0.26f, 1.0f };
                stairSteps.quad(V3{ x0, top, z0 }, V3{ x1, top, z0 }, V3{ x1, top, z1 }, V3{ x0, top, z1 }, face);
                stairSteps.quad(V3{ x0, y0, z0 }, V3{ x1, y0, z0 }, V3{ x1, top, z0 }, V3{ x0, top, z0 }, side);
                stairSteps.quad(V3{ x0, y0, z1 }, V3{ x1, y0, z1 }, V3{ x1, top, z1 }, V3{ x0, top, z1 }, side);
                stairSteps.quad(V3{ x0, y0, z0 }, V3{ x0, y0, z1 }, V3{ x0, top, z1 }, V3{ x0, top, z0 }, side);
                stairSteps.quad(V3{ x1, y0, z0 }, V3{ x1, y0, z1 }, V3{ x1, top, z1 }, V3{ x1, top, z0 }, side);
            }
            return;
        }
        if (w.has<Blueprint>(e) && w.has<FreeBuild>(e)) {
            // A free blueprint: the see-through blue of the prop's own size and shape, not a tile
            const Model& m = propModel(kind);
            float hx = 0.4f, hz = 0.4f, height = info.size;
            if (m.vao && m.maxX > m.minX && m.maxY > m.minY && m.maxZ > m.minZ) {
                const float extent = std::max({ m.maxX - m.minX, m.maxY - m.minY, m.maxZ - m.minZ });
                const float k = info.size / extent;
                hx = 0.5f * k * (m.maxX - m.minX);
                hz = 0.5f * k * (m.maxZ - m.minZ);
                height = k * (m.maxY - m.minY);
                if (std::fabs(std::sin(w.get<Prop>(e)->yaw)) > 0.7f) std::swap(hx, hz);
            }
            box(tr->x - hx, tr->y, tr->z - hz, tr->x + hx, tr->y + height, tr->z + hz, planned, plannedEdge);
            return;
        }
        if (w.has<Blueprint>(e)) {
            const float x = std::floor(tr->x), z = std::floor(tr->z), y = tr->y;
            if (info.upperFloor) box(x, y, z, x + 1.0f, y + 0.14f, z + 1.0f, planned, plannedEdge);
            else if (info.ladder && !info.slope) box(tr->x - 0.3f, y, tr->z - 0.08f, tr->x + 0.3f, y + FLOOR_HEIGHT + 0.3f, tr->z + 0.08f, planned, plannedEdge);
            else if (info.door && std::fabs(w.get<Prop>(e)->yaw) > 0.1f) box(tr->x - 0.15f, y, z, tr->x + 0.15f, y + FLOOR_HEIGHT, z + 1.0f, planned, plannedEdge);
            else if (info.door) box(x, y, tr->z - 0.15f, x + 1.0f, y + FLOOR_HEIGHT, tr->z + 0.15f, planned, plannedEdge);
            else if (info.perimeter || info.tower || info.slope) {
                int px0 = 0, pz0 = 0, px1 = 0, pz1 = 0;
                propTileRect(info, tr->x, tr->z, w.get<Prop>(e)->yaw, px0, pz0, px1, pz1);
                box((float)px0 + 0.05f, y, (float)pz0 + 0.05f, (float)px1 + 0.95f, y + info.size, (float)pz1 + 0.95f, planned, plannedEdge);
            }
            else box(x + 0.05f, y, z + 0.05f, x + 0.95f, y + (info.wallRule ? FLOOR_HEIGHT : info.size), z + 0.95f, planned, plannedEdge);
            return;
        }
        // Everything else is tested as a ball around it: as tall as a floor for a wall, door or ladder, and its size
        // otherwise. A pushed Wheelbarrow is drawn a little ahead of its pusher, so its ball is padded out.
        const float baseY = propBaseY(w, e);
        const bool modelled = propModel(kind).vao != 0;
        float side = modelled ? info.size : missingPropSide(kind);
        if (info.wallRule || info.door || info.ladder) side = std::max(side, (float)FLOOR_HEIGHT);
        if (info.wheelbarrow) side += info.pulled ? PULLED_BEHIND : WHEELBARROW_AHEAD;
        if (!sphereVisible(frustum, tr->x, baseY + 0.5f * side, tr->z, side)) return;
        if (info.perimeter && info.solid) {
            // A Perimeter Wall faces the way it was placed (its spikes are on its outer sides, its walkway behind); a side with another
            // Wall or Gate beside it has no spikes. The models face north, so the sides are turned to its facing.
            int x0 = 0, z0 = 0, x1 = 0, z1 = 0;
            const float pyaw = w.get<Prop>(e)->yaw;
            propTileRect(info, tr->x, tr->z, pyaw, x0, z0, x1, z1);
            const int facing = (((int)std::lround(pyaw / 1.5707963f)) % 4 + 4) % 4;
            const int open = perimeterOpenSides([&](int x, int z) { return bt.perim.count(tileKey(x, z)) > 0; }, x0, z0, x1, z1);
            int mask = 0;
            for (int side = 0; side < 4; ++side) {
                if (!((open >> side) & 1)) mask |= 1 << ((side - facing + 4) % 4);
            }
            const Model& m = perimeterWalls_[(size_t)mat][(size_t)mask];
            if (m.vao) {
                queueModel(m, tr->x, tr->y, tr->z, info.size, -pyaw);
                return;
            }
        }
        if (info.slope) {
            // A Slope rises the way it faces: its model rises north, and is turned to its facing like the Perimeter pieces
            const Model* m = materialModel(0);
            if (!m) m = &propModel(kind);
            if (m->vao) {
                queueModel(*m, tr->x, baseY, tr->z, info.size, -w.get<Prop>(e)->yaw);
                return;
            }
        }
        if (info.tower && occupantCount(w, e) > 0 && towerManned_.vao) {
            queueModel(towerManned_, tr->x, baseY, tr->z, info.size, 0.0f);
            return;
        }
        if (info.upperFloor) {
            // A Floor runs to the edge toward a floor beside it, or where no wall is under it; where a wall is, it stops at the
            // wall's outer face, so an edge or a corner doesn't stick out
            const int x = (int)std::floor(tr->x), z = (int)std::floor(tr->z), lv = storeyOf(w, e);
            const bool walled = wallTiles.count(levelKey(x, z, lv - 1)) > 0;
            auto arm = [&](int dx, int dz) { return !walled || bt.floors.count(levelKey(x + dx, z + dz, lv)) > 0; };
            const int mask = (arm(0, -1) ? 1 : 0) | (arm(1, 0) ? 2 : 0) | (arm(0, 1) ? 4 : 0) | (arm(-1, 0) ? 8 : 0);
            const WallShape shape = wallShapeFor(mask);
            const Model* m = &propModel(kind);
            if (shape.shape > 0 && (size_t)idx(kind) < wallShapes_.size() && wallShapes_[(size_t)idx(kind)][(size_t)shape.shape - 1].vao) {
                m = &wallShapes_[(size_t)idx(kind)][(size_t)shape.shape - 1];
            }
            if (const Model* made = materialModel(shape.shape)) m = made;
            if (m->vao) {
                queueModel(*m, tr->x, baseY, tr->z, info.size, shape.yaw);
                return;
            }
        }
        if (info.wallRule) {
            const int x = (int)std::floor(tr->x), z = (int)std::floor(tr->z), lv = storeyOf(w, e);
            const int mask = (wallTiles.count(levelKey(x, z - 1, lv)) ? 1 : 0) | (wallTiles.count(levelKey(x + 1, z, lv)) ? 2 : 0) |
                             (wallTiles.count(levelKey(x, z + 1, lv)) ? 4 : 0) | (wallTiles.count(levelKey(x - 1, z, lv)) ? 8 : 0);
            const WallShape shape = wallShapeFor(mask);
            const Model* m = &propModel(kind);
            if (shape.shape > 0 && (size_t)idx(kind) < wallShapes_.size() && wallShapes_[(size_t)idx(kind)][(size_t)shape.shape - 1].vao) {
                m = &wallShapes_[(size_t)idx(kind)][(size_t)shape.shape - 1];
            }
            if (const Model* made = materialModel(shape.shape)) m = made;
            if (m->vao) {
                queueModel(*m, tr->x, tr->y, tr->z, info.size, shape.yaw);
                return;
            }
        }
        float yaw = w.get<Prop>(e)->yaw;
        if (info.perimeter && !info.solid) yaw = -yaw;   // a Gate's model faces north; its facing turns the model the other way round
        float px = tr->x, pz = tr->z;
        if (info.door && standing.count(levelKey((int)std::floor(tr->x), (int)std::floor(tr->z), storeyOf(w, e)))) {
            // Swung open about its edge: a quarter turn, pushed to the side of the doorway
            const float along = yaw;
            yaw += 1.5707963f;
            px += std::cos(along) * 0.42f;
            pz -= std::sin(along) * 0.42f;
        }
        float py = baseY;
        if (auto pu = info.wheelbarrow ? pushers.find(e) : pushers.end(); pu != pushers.end()) {
            const Transform* ut = w.get<Transform>(pu->second);
            const auto facing = unitFacing_.find(pu->second);
            const float fy = facing != unitFacing_.end() ? facing->second.yaw : yaw + 1.5707963f;
            const float offset = vehicleOffset(w, e);   // ahead, or behind for a pulled Wagon
            px = ut->x + std::sin(fy) * offset;
            pz = ut->z + std::cos(fy) * offset;
            py = ut->y;
            yaw = fy - 1.5707963f;
        }
        if (info.airship && modelled && occupantCount(w, e) == 0) {
            // Moored: a rope off its side, down to the ground (or the floor) under it
            const Model& m = propModel(kind);
            const float extent = std::max({ m.maxX - m.minX, m.maxY - m.minY, m.maxZ - m.minZ });
            const float k = extent > 0.0f ? info.size / extent : 1.0f;
            const float c = std::cos(yaw), sn = std::sin(yaw);
            auto at = [&](float lx, float ly, float lz) {
                return V3{ px + k * (c * lx + sn * lz), baseY + k * ly, pz + k * (-sn * lx + c * lz) };
            };
            const V3 top = at(-0.5f, 0.66f, 0.52f);
            V3 low = at(-0.75f, 0.0f, 1.25f);
            low.y = baseY - AIRSHIP_LIFT;
            const RGBA ropeColor{ 0.74f, 0.62f, 0.42f, 1.0f }, stake{ 0.38f, 0.26f, 0.15f, 1.0f };
            rope(top, low, 0.025f, ropeColor);
            rope(V3{ low.x, low.y - 0.05f, low.z }, V3{ low.x, low.y + 0.3f, low.z }, 0.05f, stake);
        }
        // Adjusted with the Build tab's Adjust tool: moved, turned and stretched from where it stands
        const PropVisual* visual = w.get<PropVisual>(e);
        if (visual && visual->plain()) visual = nullptr;
        if (info.fire) {
            // Its fire, at its spot on the prop as the prop is turned
            const float c = std::cos(yaw), sn = std::sin(yaw);
            FireSpot f{ px + c * info.fireX + sn * info.fireZ, py + info.fireY, pz - sn * info.fireX + c * info.fireZ, info.fireSize,
                        (float)(e % 97u) * 0.37f };
            if (visual) {
                f.x += visual->dx;
                f.y += visual->dy;
                f.z += visual->dz;
            }
            if (info.claimsLand) {
                const Owner* fo = w.get<Owner>(e);
                const double v = motivationOf(w, fo ? fo->faction : FACTION_PLAYER);
                if (v > MOTIVATION_FAST) {
                    f.energy = 0.5f + 0.5f * std::clamp((float)((v - MOTIVATION_FAST) / 25.0), 0.0f, 1.0f);
                    f.lit = true;
                }
            }
            fires_.push_back(f);
        }
        if (info.perimeter && !info.solid && !w.has<Blueprint>(e)) {
            // A Gate stands open (as built) until it is shut
            if (!gateShut(w, e)) {
                const Model* shown = mat == BuildMaterial::Wood ? &wallShapes_[(size_t)idx(kind)][0]
                                                                : &materialModels_[(size_t)idx(kind)][(size_t)mat - 1][1];
                if (shown->vao) {
                    queueModel(*shown, px, py, pz, info.size, yaw);
                    return;
                }
            }
        }
        if (const Model* made = info.door || info.upperFloor || info.ladder || info.perimeter ? materialModel(0) : nullptr) {
            queueModel(*made, px, py, pz, info.size, yaw);
        } else if (modelled && visual) {
            queueModelMatrix(propModel(kind), propVisualMatrix(propModel(kind), px, py, pz, info.size, yaw, *visual), propTint_, 1.0f);
        } else if (modelled) {
            queuePropModel(kind, px, py, pz, info.size, yaw);
        } else {
            drawGlyph(white_, tr->x, baseY, tr->z, missingPropSide(kind), MISSING_PROP_PINK);
        }
    };
    // Props in the order of how often what is drawn of them changes (PropMotion): the ones that stay put, then the
    // vehicles, which go wherever they are taken
    std::vector<ObjectId> moving;
    for (ObjectId e : w.view<Prop, Transform>()) {
        const Transform* tr = w.get<Transform>(e);
        if (!detail.within(tr->x, tr->y, tr->z) || cutAway(w, e)) continue;
        const PropKind kind = w.get<Prop>(e)->kind;
        if (idx(kind) < 0 || idx(kind) >= propKindCount()) continue;
        const PropInfo& info = propInfo(kind);
        if (propMotion(info) == PropMotion::Moves) {
            moving.push_back(e);
            continue;
        }
        drawProp(e, tr, kind, info);
    }
    for (ObjectId e : moving) {
        const PropKind kind = w.get<Prop>(e)->kind;
        drawProp(e, w.get<Transform>(e), kind, propInfo(kind));
    }
    propTint_ = RGBA{ 0.0f, 0.0f, 0.0f, 0.0f };
    drawingGhost = false;
    if (!stairSteps.v.empty()) {
        glUseProgram(colorProgram_);
        drawColorTris(stairSteps, viewProj_);
        glUseProgram(glyphProgram_);
        glUniformMatrix4fv(uMVPGlyph_, 1, GL_FALSE, viewProj_.m);
    }

    // Units with a model join the props' models, one draw for each model; the few without one are pictures
    for (ObjectId e : w.view<Entity, Transform>()) {
        const Transform shown = unitShownAt(w, e);
        const Transform* tr = &shown;
        if (!detail.within(tr->x, tr->y, tr->z) || cutAway(w, e)) continue;
        const float tall = std::max(1.0f, unitHeight(w, e));
        if (!sphereVisible(frustum, tr->x, tr->y + 0.5f * tall, tr->z, 1.5f * tall + 1.0f)) continue;
        const UnitPose pose = unitPose(w, e);
        if (const Model* m = unitModel(w, e)) {
            if (!(m->maxY > m->minY)) continue;
            const float yaw = unitYaw(e, tr->x, tr->z, unitIdle(w, e));
            // A unit far off is drawn from a coarser copy of its model (a quarter of the triangles)
            const Model* shown = m;
            if (!m->lods.empty()) {
                const float lx = tr->x - camX, ly = tr->y - camY, lz = tr->z - camZ;
                if (lx * lx + ly * ly + lz * lz > MODEL_LOD_FAR_TILES * MODEL_LOD_FAR_TILES) shown = &m->lods.front();
            }
            queueModelMatrix(*shown, unitWorldMatrix(w, e, *m, yaw), unitFactionTint(w, e), pose.lying ? unitSaturation(w, e) : 1.0f,
                             unitHue(w, e));
            continue;
        }
        if (pose.lying) {
            setSaturation(unitSaturation(w, e));
            drawLyingGlyph(unitGlyph(w, e), tr->x, tr->y + pose.lift, tr->z, unitHeight(w, e), WHITE, pose.carried ? 0.15f : 0.0f);
            setSaturation(1.0f);
        } else {
            drawGlyph(unitGlyph(w, e), tr->x, tr->y + pose.lift, tr->z, unitHeight(w, e), WHITE);
        }
    }
    drawModelInstances();

    drawGroundItems(w, t, cam);
    if (!plannedBoxes.empty()) {
        std::sort(plannedBoxes.begin(), plannedBoxes.end(), [](const PlannedBox& a, const PlannedBox& b) { return a.dist2 > b.dist2; });
        Batch blueprints;
        for (const PlannedBox& p : plannedBoxes) {
            const float x0 = p.x0, y0 = p.y0, z0 = p.z0, x1 = p.x1, y1 = p.y1, z1 = p.z1;
            const RGBA& side = p.ghost ? ghostFill : planned;
            const RGBA& top = p.ghost ? ghostEdge : plannedEdge;
            blueprints.quad(V3{ x0, y0, z0 }, V3{ x1, y0, z0 }, V3{ x1, y1, z0 }, V3{ x0, y1, z0 }, side);
            blueprints.quad(V3{ x0, y0, z1 }, V3{ x1, y0, z1 }, V3{ x1, y1, z1 }, V3{ x0, y1, z1 }, side);
            blueprints.quad(V3{ x0, y0, z0 }, V3{ x0, y0, z1 }, V3{ x0, y1, z1 }, V3{ x0, y1, z0 }, side);
            blueprints.quad(V3{ x1, y0, z0 }, V3{ x1, y0, z1 }, V3{ x1, y1, z1 }, V3{ x1, y1, z0 }, side);
            blueprints.quad(V3{ x0, y1, z0 }, V3{ x1, y1, z0 }, V3{ x1, y1, z1 }, V3{ x0, y1, z1 }, top);
        }
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        glUseProgram(colorProgram_);
        drawColorTris(blueprints, viewProj_);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glUseProgram(glyphProgram_);
        glUniformMatrix4fv(uMVPGlyph_, 1, GL_FALSE, viewProj_.m);
    }
    drawFires(cam);
    drawBuildPreview(t);
    drawClouds(cam);
    grabScene();
    glUseProgram(glyphProgram_);
    glUniformMatrix4fv(uMVPGlyph_, 1, GL_FALSE, viewProj_.m);

    Batch rings;
    for (ObjectId e : w.view<Entity, Transform, Selected>()) {
        if (cutAway(w, e)) continue;
        const Transform* tr = w.get<Transform>(e);
        outlineSquare(rings, tr->x, tr->y + 0.03f, tr->z, 0.6f, 0.07f, RGBA{ 0.3f, 1.0f, 0.3f, 1.0f });
    }
    drawColorTris(rings, viewProj_);

    // The selection box and the HUD lines are measured in window pixels. With the world drawn smaller than the window they
    // wait until it has been put on the window, so they stay sharp.
    const float dragged[4] = { sel.x0, sel.y0, sel.x1, sel.y1 };
    if (sceneActive_) {
        heldDragging_ = sel.dragging;
        for (int i = 0; i < 4; ++i) heldBox_[i] = dragged[i];
        heldHud_ = hud;
    } else {
        drawFrameTop(sel.dragging, dragged, hud, winW, winH);
    }
    glBindVertexArray(0);
}

// Where the world is drawn. At a render scale under 100 that is a picture smaller than the window, which presentScene
// stretches over the window before anything measured in window pixels is drawn.
void Renderer::beginScene(int winW, int winH) {
    sceneActive_ = false;
    const int percent = std::clamp(renderScale_, 25, 100);
    if (percent < 100 && winW > 0 && winH > 0) {
        const int w = std::max(1, (winW * percent + 50) / 100), h = std::max(1, (winH * percent + 50) / 100);
        if (w != sceneW_ || h != sceneH_ || !sceneFbo_) {
            releaseScene();
            GLint texture = 0;
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
            sceneTex_ = makeTexture2D(w, h, GL_RGBA8, GL_RGBA, nullptr, GL_LINEAR, GL_CLAMP_TO_EDGE);
            glBindTexture(GL_TEXTURE_2D, (GLuint)texture);
            glGenRenderbuffers(1, &sceneDepth_);
            glBindRenderbuffer(GL_RENDERBUFFER, sceneDepth_);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
            glBindRenderbuffer(GL_RENDERBUFFER, 0);
            glGenFramebuffers(1, &sceneFbo_);
            glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo_);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sceneTex_, 0);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, sceneDepth_);
            const bool whole = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            if (whole) {
                sceneW_ = w;
                sceneH_ = h;
            } else {
                releaseScene();
            }
        }
        if (sceneFbo_) {
            glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo_);
            glViewport(0, 0, sceneW_, sceneH_);
            sceneActive_ = true;
            sceneWinW_ = winW;
            sceneWinH_ = winH;
            return;
        }
    }
    glViewport(0, 0, winW, winH);
}

void Renderer::presentScene() {
    if (!sceneActive_) return;
    sceneActive_ = false;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, sceneFbo_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glDisable(GL_SCISSOR_TEST);
    glBlitFramebuffer(0, 0, sceneW_, sceneH_, 0, 0, sceneWinW_, sceneWinH_, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, sceneWinW_, sceneWinH_);
    drawFrameTop(heldDragging_, heldBox_, heldHud_, sceneWinW_, sceneWinH_);
    glBindVertexArray(0);
}

void Renderer::releaseScene() {
    if (sceneFbo_) glDeleteFramebuffers(1, &sceneFbo_);
    if (sceneTex_) glDeleteTextures(1, &sceneTex_);
    if (sceneDepth_) glDeleteRenderbuffers(1, &sceneDepth_);
    sceneFbo_ = sceneTex_ = sceneDepth_ = 0;
    sceneW_ = sceneH_ = 0;
    sceneActive_ = false;
}

std::string Renderer::renderScaleText() const {
    char text[96];
    if (sceneW_ > 0 && renderScale_ < 100) {
        std::snprintf(text, sizeof text, "render scale %d%%, world drawn at %d x %d", renderScale_, sceneW_, sceneH_);
    } else {
        std::snprintf(text, sizeof text, "render scale %d%%, world drawn at the window's size", renderScale_);
    }
    return text;
}

// What drawFrame draws in window pixels over the world: the selection box being dragged, and the HUD lines
void Renderer::drawFrameTop(bool dragging, const float* corners, const std::vector<HudEntry>& hud, int winW, int winH) {
    if (dragging) {
        Batch box;
        float x0 = std::min(corners[0], corners[2]), x1 = std::max(corners[0], corners[2]);
        float y0 = std::min(corners[1], corners[3]), y1 = std::max(corners[1], corners[3]);
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
    if (!buildPreview_.on && !buildPreview_.rect && buildPreview_.ringR <= 0.0f && buildPreview_.marks.empty() && !buildPreview_.arrow) return;
    const float lift = 0.04f;
    auto hgt = [&](float x, float z) { return t.heightAt(x, z) + lift + buildPreview_.lift; };
    Batch b;

    for (const BuildPreview::Mark& m : buildPreview_.marks) {
        const RGBA tone = m.tone == 0 ? RGBA{ 0.3f, 0.6f, 1.0f, 0.30f } : m.tone == 1 ? RGBA{ 0.25f, 1.0f, 0.35f, 0.40f }
                        : m.tone == 3 ? RGBA{ 1.0f, 0.78f, 0.2f, 0.17f}
                        : m.tone == 4 ? RGBA{ 1.0f, 0.78f, 0.2f, 0.07f}
                        : m.tone == 5 ? RGBA{ 1.0f, 0.78f, 0.2f, 0.09f}   // the Perimeter's always-there core: half as strong as the rest
                        : m.tone == 6 ? RGBA{ 1.0f, 0.78f, 0.2f, 0.035f}
                                                                        : RGBA{ 1.0f, 0.2f, 0.2f, 0.40f };
        const float a = 0.08f, y = m.y + lift;
        const bool perimeterMark = m.tone >= 3 && m.tone <= 6;   // the Perimeter's tiles: square outlines, so what is under them shows
        if (m.outline || perimeterMark) {
            const float edgeAlpha = !perimeterMark ? 0.95f : m.tone == 3 ? 0.9f : m.tone == 5 ? 0.6f : 0.45f;
            const RGBA edge{ tone.r, tone.g, tone.b, edgeAlpha };
            const float o0 = 0.04f, o1 = 1.0f - o0, w = 0.07f;
            const float x0 = m.x + o0, x1 = m.x + o1, z0 = m.z + o0, z1 = m.z + o1;
            b.quad(V3{ x0, y, z0 }, V3{ x1, y, z0 }, V3{ x1, y, z0 + w }, V3{ x0, y, z0 + w }, edge);
            b.quad(V3{ x0, y, z1 - w }, V3{ x1, y, z1 - w }, V3{ x1, y, z1 }, V3{ x0, y, z1 }, edge);
            b.quad(V3{ x0, y, z0 + w }, V3{ x0 + w, y, z0 + w }, V3{ x0 + w, y, z1 - w }, V3{ x0, y, z1 - w }, edge);
            b.quad(V3{ x1 - w, y, z0 + w }, V3{ x1, y, z0 + w }, V3{ x1, y, z1 - w }, V3{ x1 - w, y, z1 - w }, edge);
            continue;
        }
        b.quad(V3{ m.x + a, y, m.z + a }, V3{ m.x + 1 - a, y, m.z + a }, V3{ m.x + 1 - a, y, m.z + 1 - a }, V3{ m.x + a, y, m.z + 1 - a }, tone);
    }
    if (buildPreview_.rect) {
        // A foundation being dragged out: its tiles, green if it may be laid there
        const RGBA fill = buildPreview_.valid ? RGBA{ 0.25f, 1.0f, 0.35f, 0.25f } : RGBA{ 1.0f, 0.2f, 0.2f, 0.25f };
        for (int z = buildPreview_.rz0; z <= buildPreview_.rz1; ++z) {
            for (int x = buildPreview_.rx0; x <= buildPreview_.rx1; ++x) {
                const float a = 0.06f;
                b.quad(V3{ x + a, hgt(x + a, z + a), z + a }, V3{ x + 1 - a, hgt(x + 1 - a, z + a), z + a },
                       V3{ x + 1 - a, hgt(x + 1 - a, z + 1 - a), z + 1 - a }, V3{ x + a, hgt(x + a, z + 1 - a), z + 1 - a }, fill);
            }
        }
    } else if (buildPreview_.on) {
        const Footprint& f = propInfo(buildPreview_.kind).foot;
        const float x0 = buildPreview_.x, z0 = buildPreview_.z;
        const float reach = footprintReach(f);
        const float cell = 0.25f;
        const RGBA fill = buildPreview_.valid ? RGBA{ 0.25f, 1.0f, 0.35f, 0.25f } : RGBA{ 1.0f, 0.2f, 0.2f, 0.25f };
        const float startX = std::floor((x0 - reach) / cell) * cell, endX = x0 + reach;
        const float startZ = std::floor((z0 - reach) / cell) * cell, endZ = z0 + reach;
        for (float gx = startX; gx < endX; gx += cell) {
            for (float gz = startZ; gz < endZ; gz += cell) {
                const float cx = gx + cell * 0.5f, cz = gz + cell * 0.5f;
                if (!insideFootprint(f, cx - x0, cz - z0, buildPreview_.yaw)) continue;
                const V3 a{ gx, hgt(gx, gz), gz }, bb{ gx + cell, hgt(gx + cell, gz), gz };
                const V3 c{ gx + cell, hgt(gx + cell, gz + cell), gz + cell }, d{ gx, hgt(gx, gz + cell), gz + cell };
                b.quad(a, bb, c, d, fill);
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
        for (const auto& o : buildPreview_.otherRings) ring(o[0], o[1], o[2], RGBA{ 0.95f, 0.95f, 1.0f, 0.85f });
        const RGBA tone = buildPreview_.ringTone == 1 ? RGBA{ 0.25f, 1.0f, 0.35f, 0.85f }
                        : buildPreview_.ringTone == 2 ? RGBA{ 1.0f, 0.2f, 0.2f, 0.85f }
                                                      : RGBA{ 0.95f, 0.95f, 1.0f, 0.85f };
        ring(buildPreview_.x, buildPreview_.z, buildPreview_.ringR, tone);
    }
    if (buildPreview_.arrow) {
        // The front arrow: a shaft and a head, flat on the ground, in gold with a dark edge to show over green and red alike
        const float ux = buildPreview_.arrowDX, uz = buildPreview_.arrowDZ, px = -uz, pz = ux;
        const float cx = buildPreview_.arrowX, cz = buildPreview_.arrowZ;
        auto arrowParts = [&](float grow, const RGBA& col, float raise) {
            const float half = 1.1f + grow, neck = half - 0.85f - grow, shaft = 0.17f + grow, wing = 0.52f + grow;
            auto at = [&](float along, float side) {
                const float x = cx + ux * along + px * side, z = cz + uz * along + pz * side;
                return V3{ x, hgt(x, z) + raise, z };
            };
            b.quad(at(-half, -shaft), at(neck, -shaft), at(neck, shaft), at(-half, shaft), col);
            b.quad(at(neck, -wing), at(half, 0.0f), at(half, 0.0f), at(neck, wing), col);
        };
        arrowParts(0.07f, RGBA{ 0.12f, 0.07f, 0.02f, 0.85f }, 0.01f);
        arrowParts(0.0f, RGBA{ 1.0f, 0.84f, 0.22f, 0.95f }, 0.02f);
    }
    if (b.v.empty()) return;

    // Blended (it was drawn with blending off, so every alpha above was ignored and the marks were solid)
    const GLboolean wasBlending = glIsEnabled(GL_BLEND);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    drawColorTris(b, viewProj_);
    glDepthMask(GL_TRUE);
    if (!wasBlending) glDisable(GL_BLEND);
}

void Renderer::drawGroundItems(World& w, const Terrain& t, const Camera& cam) {
    const unsigned long long ticks = SDL_GetTicks();
    std::vector<ObjectId> items;
    // A pile is kept or left out whole, by a ball around its tile, so its items keep their order front to back
    const FrustumPlanes frustum = framePlanes(viewProj_);
    const DetailRange detail(cam);
    for (ObjectId e : w.view<GroundItem, Transform>()) {
        const Transform* tr = w.get<Transform>(e);
        if (!groundLoaded(t, tr->x, tr->z) || !detail.within(tr->x, tr->y, tr->z) || cutAway(w, e)) continue;
        if (!sphereVisible(frustum, std::floor(tr->x) + 0.5f, tr->y + 0.5f, std::floor(tr->z) + 0.5f, 1.3f)) continue;
        items.push_back(e);
    }
    if (items.empty()) return;
    const std::unordered_map<ObjectId, PileSpot> spots = pileSpots(w, items);
    float ex = 0.0f, ey = 0.0f, ez = 0.0f;
    cam.eye(ex, ey, ez);

    Batch shadows;
    for (ObjectId e : items) {
        const Transform* tr = w.get<Transform>(e);

        // Dark in the middle and fading out at the edge, and a little smaller while the item is high
        constexpr int SEGMENTS = 16;
        constexpr float SHADOW_LIFT = 0.03f;
        const float radius = ITEM_SHADOW_RADIUS * (1.0f - 0.15f * itemBobShare(spots.at(e).bob, ticks));
        const float shadowAlpha = hasTheme_ ? std::min(1.0f, ITEM_SHADOW_ALPHA * (float)theme_.shadows / 50.0f) : ITEM_SHADOW_ALPHA;
        const RGBA middle{ 0.0f, 0.0f, 0.0f, shadowAlpha }, edge{ 0.0f, 0.0f, 0.0f, 0.0f };
        const V3 center{ tr->x, t.standHeight(tr->x, tr->z) + SHADOW_LIFT, tr->z };
        V3 last{};
        for (int i = 0; i <= SEGMENTS; ++i) {
            const float a = TWO_PI * (float)i / (float)SEGMENTS;
            const float x = tr->x + radius * std::cos(a), z = tr->z + radius * std::sin(a);
            const V3 rim{ x, t.standHeight(x, z) + SHADOW_LIFT, z };
            if (i > 0) {
                shadows.v.push_back({ center, middle });
                shadows.v.push_back({ last, edge });
                shadows.v.push_back({ rim, edge });
            }
            last = rim;
        }
    }

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
        const Transform at = pileDrawn(*tr, spots.at(e), ticks, ex, ey, ez);
        glUniform1f(uShine_, itemShineShare(e, ticks));
        drawGlyph(square, at.x, at.y, at.z, ITEM_GLYPH_H, WHITE);
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
        if (!groundLoaded(t, tr->x, tr->z) || cutAway(w, e)) continue;
        const float dx = tr->x - ex, dy = tr->y - ey, dz = tr->z - ez;
        const float dist2 = dx * dx + dy * dy + dz * dz;
        if (!detail.within(tr->x, tr->y, tr->z)) continue;
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

// Fires: each a few soft flames rising and fading, over a warm glow on the ground and round the fire that stands in
// for its light, and shows more the darker it is. Added to what is behind them, so they brighten and never darken.
constexpr float FIRE_LIT_RISE = 3.2f;   // how many times higher a motivated Altar's flames rise

void Renderer::drawFires(const Camera& cam) {
    if (fires_.empty() || !glow_.tex) return;
    (void)cam;
    const float now = (float)(SDL_GetTicks() % 3600000u) / 1000.0f;
    // How dark it is: none at midday, all once the sun is low
    const float dark = std::clamp(1.0f - skySun_[1] * 2.5f, 0.2f, 1.0f);
    // Every quad of every fire goes into one list, each with its own color, and is drawn in a single call. They are added to what
    // is behind them, so the order they are drawn in makes no difference.
    std::vector<float>& quads = glowScratch_;
    quads.clear();
    const float glowAspect = glow_.aspect;
    auto corner = [&](float x, float y, float z, float u, float v, const RGBA& c) {
        const float vert[9] = { x, y, z, u, v, c.r, c.g, c.b, c.a };
        quads.insert(quads.end(), vert, vert + 9);
    };
    // Two triangles for a quad whose corners are given in the order drawQuadVerts takes them
    auto quad = [&](const float* q, const RGBA& c) {
        for (int k : { 0, 1, 2, 0, 2, 3 }) corner(q[k * 5], q[k * 5 + 1], q[k * 5 + 2], q[k * 5 + 3], q[k * 5 + 4], c);
    };
    // The same corners drawGlyph would make: a picture standing up facing the camera
    auto glyph = [&](float x, float y, float z, float h, const RGBA& c) {
        float dx = billX_ - x, dz = billZ_ - z;
        float len = std::sqrt(dx * dx + dz * dz);
        if (len < 1e-4f) { dx = 0.0f; dz = 1.0f; len = 1.0f; }
        dx /= len;
        dz /= len;
        const float rx = dz, rz = -dx, hw = h * glowAspect * 0.5f, hh = h * 0.5f, cy = y + hh;
        const float q[4 * 5] = {
            x - rx * hw, cy - hh, z - rz * hw, 0.0f, 1.0f,
            x + rx * hw, cy - hh, z + rz * hw, 1.0f, 1.0f,
            x + rx * hw, cy + hh, z + rz * hw, 1.0f, 0.0f,
            x - rx * hw, cy + hh, z - rz * hw, 0.0f, 0.0f
        };
        quad(q, c);
    };
    for (const FireSpot& f : fires_) {
        const float flicker = 0.88f + 0.12f * std::sin(now * 3.0f + f.seed * 5.0f) * std::sin(now * 2.0f + f.seed);
        // The glow on the ground under it
        const float e = f.energy;
        const float reach = (2.0f + f.size * 5.0f) * flicker * (1.0f + 0.5f * e);
        const float gy = f.y - std::min(f.y, 0.0f) * 0.0f;
        const float ground[4 * 5] = {
            f.x - reach, gy + 0.03f, f.z - reach, 0.0f, 0.0f,
            f.x + reach, gy + 0.03f, f.z - reach, 1.0f, 0.0f,
            f.x + reach, gy + 0.03f, f.z + reach, 1.0f, 1.0f,
            f.x - reach, gy + 0.03f, f.z + reach, 0.0f, 1.0f
        };
        quad(ground, RGBA{ 1.0f, 0.62f, 0.28f, (0.10f + 0.45f * dark) * flicker * (1.0f + 0.5f * e) });
        // The glow round the fire itself
        const float halo = f.size * 4.5f * (1.0f + 0.6f * e);
        glyph(f.x, f.y + f.size * 0.4f - halo * 0.5f, f.z, halo, RGBA{ 1.0f, 0.55f, 0.2f, (0.12f + 0.3f * dark + 0.3f * e) * flicker });
        // The flames: each rises from the fire's foot, drifting in, shrinking and reddening as it goes
        const int FLAMES = 9 + (int)(10.0f * e);
        for (int i = 0; i < FLAMES; ++i) {
            const float seed = f.seed + (float)i * 0.618f;
            const float life = std::fmod(now * (1.6f + 0.15f * (float)(i % 3)) * (1.0f + 0.8f * e) + seed, 1.0f);
            const float sway = std::sin(seed * 40.0f + now * 2.3f);
            const float sway2 = std::cos(seed * 23.0f + now * 1.9f);
            const float spread = f.size * 0.32f * (1.0f - life * 0.8f);
            const float px = f.x + sway * spread, pz = f.z + sway2 * spread;
            // Over the Motivation threshold the flames climb far higher than the fire's own size
            const float py = f.y + life * f.size * 1.7f * (1.0f + 0.5f * e) * (f.lit ? FIRE_LIT_RISE : 1.0f);
            const float side = f.size * (0.95f - 0.6f * life) * (1.0f + 0.4f * e);
            const RGBA tint{ 1.0f, 0.85f - 0.55f * life, 0.35f - 0.3f * life, (1.0f - life) * 0.75f };
            glyph(px, py - side * 0.3f, pz, side, tint);
        }
    }
    if (!quads.empty() && glowProgram_) {
        glUseProgram(glowProgram_);
        glUniformMatrix4fv(uMVPGlow_, 1, GL_FALSE, viewProj_.m);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, glow_.tex);
        glBindVertexArray(glowVao_);
        glBindBuffer(GL_ARRAY_BUFFER, glowVbo_);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(quads.size() * sizeof(float)), quads.data(), GL_STREAM_DRAW);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glDepthMask(GL_FALSE);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(quads.size() / 9));
        glDepthMask(GL_TRUE);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_BLEND);
        glBindVertexArray(0);
    }
    glUseProgram(glyphProgram_);
    glUniformMatrix4fv(uMVPGlyph_, 1, GL_FALSE, viewProj_.m);
    glUniform4f(uTint_, worldTint_[0], worldTint_[1], worldTint_[2], 1.0f);
}
