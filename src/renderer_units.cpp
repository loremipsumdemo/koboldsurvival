// Renderer: units and props in the world: their sprites and models, which way a unit faces, and picking them by pixel.
#include "renderer_internal.h"

using namespace render_detail;

namespace {

constexpr float FACTION_TINT = 0.85f;    // how far a unit model's colors are pulled toward its faction's color

// Where a unit model stands: scaled to GLYPH_H tall, turned by yaw about the vertical axis (0 faces +z), with the centre
// of its bottom on (x, y, z). Lying, it is first tipped onto its side so its head points along its facing's right, and
// it rests on y.
// The unit models face their own +x, so they are turned a quarter turn more to face +z at yaw 0
constexpr float UNIT_MODEL_FRONT_YAW = -1.5707963f;

Mat4 unitModelMatrix(const Model& m, float x, float y, float z, float yaw, bool lying, bool flip = false,
                            float height = GLYPH_H) {
    const float k = height / (m.maxY - m.minY);
    yaw += UNIT_MODEL_FRONT_YAW;
    const float cs = std::cos(yaw), sn = std::sin(yaw);
    // R row by row: the turn by yaw, after the tip onto its side when lying: (x, y, z) -> (y, -x, z), its head along
    // its +x, or flipped (x, y, z) -> (-y, x, z), its head the other way and its other side up
    const float f = flip ? -1.0f : 1.0f;
    const float R[3][3] = {
        { lying ? 0.0f : cs, lying ? f * cs : 0.0f, sn },
        { lying ? -f : 0.0f, lying ? 0.0f : 1.0f, 0.0f },
        { lying ? 0.0f : -sn, lying ? -f * sn : 0.0f, cs }
    };
    // The model point put on the world point: the centre of its bottom standing, its middle lying
    const float c[3] = { (m.minX + m.maxX) * 0.5f, lying ? (m.minY + m.maxY) * 0.5f : m.minY, (m.minZ + m.maxZ) * 0.5f };
    const float t[3] = { x, lying ? y + k * (m.maxX - m.minX) * 0.5f : y, z };
    Mat4 out{};
    for (int r = 0; r < 3; ++r) {
        for (int col = 0; col < 3; ++col) out.m[col * 4 + r] = k * R[r][col];
        out.m[12 + r] = t[r] - k * (R[r][0] * c[0] + R[r][1] * c[1] + R[r][2] * c[2]);
    }
    out.m[15] = 1.0f;
    return out;
}

// A unit turns toward the way it walks, easing round rather than snapping. One seen for the first time faces the camera.
// One idle for UNIT_SETTLE_MS settles and keeps its facing: being pushed about by units near it no longer turns it, until
// it stops being idle or is moved more than UNIT_SETTLE_SLIP tiles from where it settled.
constexpr unsigned long long UNIT_SETTLE_MS = 3000;

constexpr float UNIT_SETTLE_SLIP = 0.25f;

} // namespace

// A prop kind with no model is drawn as a pink square this many tiles on a side: the height of a unit's sprite, so it
// stands like a unit would, whatever the kind
float missingPropSide(PropKind) {
    return GLYPH_H;
}

// How high the top of a unit's sprite is above its feet: its height standing, or lying (Unconscious) its width, over
// its carrier's shoulders if it is carried
float Renderer::unitTop(World& w, Entity u) {
    const float h = unitHeight(w, u);
    if (w.has<Carried>(u)) return h * unitAspect(w, u) + (unitModel(w, u) ? CARRIED_MODEL_LIFT : CARRIED_LIFT);
    if (const float bed = bedLift(w, u); bed >= 0.0f) return h * unitAspect(w, u) + bed;
    if (w.has<Unconscious>(u) || w.has<PlayingDead>(u)) return h * unitAspect(w, u);
    return h + unitHover(w, u);
}

float Renderer::bedLift(World& w, Entity u) const {
    const Occupying* oc = w.get<Occupying>(u);
    if (!oc || w.has<Carried>(u)) return -1.0f;
    const Prop* p = w.get<Prop>(oc->prop);
    const Transform* pt = w.get<Transform>(oc->prop);
    const Transform* ut = w.get<Transform>(u);
    if (!p || !pt || !ut || (p->kind != PropKind::Bed && p->kind != PropKind::Bedroll)) return -1.0f;
    const float dx = ut->x - pt->x, dz = ut->z - pt->z;
    if (dx * dx + dz * dz > 0.25f * 0.25f) return -1.0f;   // not on the bed's spot: drawn as usual
    return std::max(0.0f, propBaseY(w, oc->prop) - ut->y + propTop(p->kind) * 0.5f);
}

float Renderer::unitHover(World& w, Entity u) const {
    const Mob* mb = w.get<Mob>(u);
    if (!mb) return 0.0f;
    if (w.has<Unconscious>(u) || w.has<PlayingDead>(u) || w.has<Corpse>(u) || w.has<Carried>(u) || w.has<Occupying>(u)) {
        return 0.0f;
    }
    return mobHoverFraction(mb->kind) * unitHeight(w, u);
}

void Renderer::drawLyingGlyph(const TextTex& g, float x, float y, float z, float h, RGBA tint, float toward) {
    if (!g.tex) return;
    // The sprite turned 90 degrees, lying on its side across the camera's view: its height runs along the right
    // vector and its width up from the ground. Its head is to the right.
    float dx = billX_ - x, dz = billZ_ - z;
    float len = std::sqrt(dx * dx + dz * dz);
    if (len < 1e-4f) { dx = 0.0f; dz = 1.0f; len = 1.0f; }
    dx /= len;
    dz /= len;
    const float rx = dz, rz = -dx;
    const float hw = h * 0.5f;                 // half its length, lying
    const float up = h * g.aspect;             // how high it is, lying: its width
    // Moved a little toward the camera, so a carried unit shows in front of its carrier
    const float cx = x + dx * toward, cz = z + dz * toward;
    const float y0 = y, y1 = y + up;
    float v[4 * 5] = {
        cx - rx * hw, y0, cz - rz * hw, 1.0f, 1.0f,   // bottom-left: the sprite's bottom right
        cx + rx * hw, y0, cz + rz * hw, 1.0f, 0.0f,   // bottom-right: its top right
        cx + rx * hw, y1, cz + rz * hw, 0.0f, 0.0f,   // top-right: its top left
        cx - rx * hw, y1, cz - rz * hw, 0.0f, 1.0f    // top-left: its bottom left
    };
    drawQuadVerts(g.tex, v, tint);
}

void Renderer::drawGlyph(const TextTex& g, float x, float y, float z, float h, RGBA tint) {
    if (!g.tex) return;

    // Right vector perpendicular to the direction from the sprite to the billboard point
    float dx = billX_ - x, dz = billZ_ - z;
    float len = std::sqrt(dx * dx + dz * dz);
    if (len < 1e-4f) { dx = 0.0f; dz = 1.0f; len = 1.0f; }
    dx /= len;
    dz /= len;
    float rx = dz, rz = -dx;

    float hw = h * g.aspect * 0.5f;
    float hh = h * 0.5f;
    float cy = y + hh;   // the bottom of the sprite sits on the ground

    float v[4 * 5] = {
        x - rx * hw, cy - hh, z - rz * hw, 0.0f, 1.0f,   // bottom-left
        x + rx * hw, cy - hh, z + rz * hw, 1.0f, 1.0f,   // bottom-right
        x + rx * hw, cy + hh, z + rz * hw, 1.0f, 0.0f,   // top-right
        x - rx * hw, cy + hh, z - rz * hw, 0.0f, 0.0f    // top-left
    };
    drawQuadVerts(g.tex, v, tint);
}

// A placement as a model matrix. Column-major: the turn about the vertical axis, scaled by k.
// x' = cs*x + sn*z, z' = -sn*x + cs*z
static Mat4 placementMatrix(const ModelPlacement& p) {
    Mat4 model{};
    model.m[0] = p.k * p.cs;
    model.m[2] = -p.k * p.sn;
    model.m[5] = p.k;
    model.m[8] = p.k * p.sn;
    model.m[10] = p.k * p.cs;
    model.m[15] = 1.0f;
    model.m[12] = p.ox;
    model.m[13] = p.oy;
    model.m[14] = p.oz;
    return model;
}

void Renderer::queuePropModel(PropKind kind, float x, float y, float z, float targetSize, float yaw) {
    const Model& m = propModel(kind);
    if (!m.vao) return;
    // Without the instanced program, each is drawn on its own as before
    if (!modelInstProgram_) {
        drawModel(m, x, y, z, targetSize, yaw);
        return;
    }
    const ModelPlacement p = placeModel(m, x, y, z, targetSize, yaw);
    if (p.ok) propInstances_[(size_t)kind].push_back(placementMatrix(p));
}

GLuint Renderer::propInstVao(PropKind kind) {
    GLuint& vao = propInstVao_[(size_t)kind];
    if (vao) return vao;
    const Model& m = propModel(kind);
    if (!m.vao || !instVbo_) return 0;
    // The model's own vertices and triangles (as loadGLBModel lays them out), and the placements per instance
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ebo);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindBuffer(GL_ARRAY_BUFFER, instVbo_);
    for (int c = 0; c < 4; ++c) {
        glVertexAttribPointer(2 + c, 4, GL_FLOAT, GL_FALSE, sizeof(Mat4), (void*)(c * 4 * sizeof(float)));
        glEnableVertexAttribArray(2 + c);
        glVertexAttribDivisor(2 + c, 1);
    }
    glBindVertexArray(0);
    return vao;
}

void Renderer::drawPropInstances() {
    bool any = false;
    for (const auto& list : propInstances_) any = any || !list.empty();
    if (!any) return;
    glUseProgram(modelInstProgram_);
    glUniformMatrix4fv(uVPInst_, 1, GL_FALSE, viewProj_.m);
    for (int k = 0; k < PROP_KIND_COUNT; ++k) {
        std::vector<Mat4>& list = propInstances_[(size_t)k];
        if (list.empty()) continue;
        const GLuint vao = propInstVao((PropKind)k);
        if (vao) {
            glBindBuffer(GL_ARRAY_BUFFER, instVbo_);
            glBufferData(GL_ARRAY_BUFFER, list.size() * sizeof(Mat4), list.data(), GL_STREAM_DRAW);
            glBindVertexArray(vao);
            for (const DrawRange& d : propModel((PropKind)k).draws) {
                glBindTexture(GL_TEXTURE_2D, d.texture);
                glDrawElementsInstanced(GL_TRIANGLES, d.indexCount, GL_UNSIGNED_INT,
                                        (void*)(size_t)(d.firstIndex * sizeof(GLuint)), (GLsizei)list.size());
            }
        }
        list.clear();   // keeps its room for the next frame
    }
    glBindVertexArray(0);
    glUseProgram(glyphProgram_);
    glUniformMatrix4fv(uMVPGlyph_, 1, GL_FALSE, viewProj_.m);
}

void Renderer::drawModel(const Model& m, float x, float y, float z, float targetSize, float yaw) {
    if (!m.vao) return;
    // Scale, turn by yaw, then move so the center of the bottom sits on (x, y, z): the same placement picking uses
    const ModelPlacement p = placeModel(m, x, y, z, targetSize, yaw);
    if (!p.ok) return;
    const Mat4 model = placementMatrix(p);

    Mat4 mvp = mul(viewProj_, model);
    glUniformMatrix4fv(uMVPGlyph_, 1, GL_FALSE, mvp.m);
    glUniform4f(uTint_, 1.0f, 1.0f, 1.0f, 1.0f);

    glBindVertexArray(m.vao);
    for (const DrawRange& d : m.draws) {
        glBindTexture(GL_TEXTURE_2D, d.texture);
        glDrawElements(GL_TRIANGLES, d.indexCount, GL_UNSIGNED_INT,
                       (void*)(size_t)(d.firstIndex * sizeof(GLuint)));
    }
    glBindVertexArray(0);

    // Restore the shared matrix for the sprites that follow
    glUniformMatrix4fv(uMVPGlyph_, 1, GL_FALSE, viewProj_.m);
}

void Renderer::drawUnitModel(const Model& m, float x, float y, float z, float yaw, bool lying, float toward, bool carried,
                             float height) {
    if (!m.vao || !(m.maxY > m.minY)) return;
    // Moved a little toward the camera, so a carried unit shows in front of its carrier
    float dx = billX_ - x, dz = billZ_ - z;
    const float len = std::sqrt(dx * dx + dz * dz);
    if (len > 1e-4f) {
        x += dx / len * toward;
        z += dz / len * toward;
    }
    const Mat4 mvp = mul(viewProj_, unitModelMatrix(m, x, y, z, yaw, lying, carried, height));
    glUniformMatrix4fv(uMVPGlyph_, 1, GL_FALSE, mvp.m);
    glBindVertexArray(m.vao);
    for (const DrawRange& d : m.draws) {
        glBindTexture(GL_TEXTURE_2D, d.texture);
        glDrawElements(GL_TRIANGLES, d.indexCount, GL_UNSIGNED_INT, (void*)(size_t)(d.firstIndex * sizeof(GLuint)));
    }
    glBindVertexArray(0);
    glUniformMatrix4fv(uMVPGlyph_, 1, GL_FALSE, viewProj_.m);
}

float Renderer::unitYaw(Entity e, float x, float z, bool idle) {
    const unsigned long long now = std::max<unsigned long long>(1, SDL_GetTicks());
    auto it = unitFacing_.find(e);
    if (it == unitFacing_.end()) {
        UnitFacing f;
        f.x = x;
        f.z = z;
        f.yaw = std::atan2(billX_ - x, billZ_ - z);
        f.idleSince = idle ? now : 0;
        unitFacing_[e] = f;
        return f.yaw;
    }
    UnitFacing& f = it->second;
    if (!idle) {
        f.idleSince = 0;
        f.settled = false;
    } else if (f.idleSince == 0) {
        f.idleSince = now;
    }
    if (f.settled) {
        const float sx = x - f.settleX, sz = z - f.settleZ;
        if (sx * sx + sz * sz <= UNIT_SETTLE_SLIP * UNIT_SETTLE_SLIP) {
            f.x = x;
            f.z = z;
            return f.yaw;   // settled: nudges don't turn it
        }
        f.settled = false;   // pushed well off its spot: it turns again, and settles anew once idle long enough
        f.idleSince = now;
    }
    if (idle && now - f.idleSince >= UNIT_SETTLE_MS) {
        f.settled = true;
        f.settleX = x;
        f.settleZ = z;
    }
    const float dx = x - f.x, dz = z - f.z;
    if (dx * dx + dz * dz > 1e-6f) {
        const float want = std::atan2(dx, dz);
        float turn = std::remainder(want - f.yaw, 6.2831853f);
        f.yaw += turn * 0.25f;
    }
    f.x = x;
    f.z = z;
    return f.yaw;
}

// A unit's model seen in a box of the UI, with its own depth: it stands in the middle, the whole of it in view, and
// turns about its vertical axis only
void Renderer::drawUnitModelPreview(int sprite, const Rect& r, float yaw) {
    drawModelPreview(unitModelNumber(sprite), r, yaw);
}

void Renderer::drawMobPreview(MobKind kind, const Rect& r, float yaw) {
    const int i = idx(kind) - 1;
    if (i >= 0 && i < MOB_KIND_COUNT && mobModels_[(size_t)i].vao) {
        drawModelPreview(&mobModels_[(size_t)i], r, yaw);
        return;
    }
    drawFitted(mobPicture(kind), r, WHITE);
}

const TextTex& Renderer::mobPicture(MobKind kind) const {
    const int i = idx(kind) - 1;
    if (i >= 0 && i < MOB_KIND_COUNT && mobInfo(kind).model > 0) return pink_;
    if (i >= 0 && i < (int)mobSprites_.size() && mobSprites_[(size_t)i].tex) return mobSprites_[(size_t)i];
    return pink_;
}

void Renderer::drawModelPreview(const Model* m, const Rect& r, float yaw) {
    if (!m || r.w < 1.0f || r.h < 1.0f) {
        drawFitted(pink_, r, WHITE);   // the stand-in for a missing model
        return;
    }
    const int vx = (int)std::lround(r.x), vy = (int)std::lround((float)uiH_ - r.y - r.h);
    const int vw = (int)std::lround(r.w), vh = (int)std::lround(r.h);
    glViewport(vx, vy, vw, vh);
    glEnable(GL_SCISSOR_TEST);
    glScissor(vx, vy, vw, vh);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    // The model GLYPH_H tall with its feet at the origin; the camera in front of it on +z, far enough back that its
    // height and width both fit
    const float aspect = r.w / r.h;
    const float fov = 30.0f * 3.14159265f / 180.0f, tanHalf = std::tan(fov * 0.5f);
    const float k = GLYPH_H / (m->maxY - m->minY);
    const float halfH = GLYPH_H * 0.5f;
    const float halfW = k * std::max(m->maxX - m->minX, m->maxZ - m->minZ) * 0.5f;
    const float dist = std::max(halfH / tanHalf, halfW / (tanHalf * aspect)) * 1.12f + halfW;
    const Mat4 vp = mul(perspective(fov, aspect, 0.05f, dist * 4.0f), lookAt(0.0f, halfH, dist, 0.0f, halfH, 0.0f, 0.0f, 1.0f, 0.0f));
    const Mat4 mvp = mul(vp, unitModelMatrix(*m, 0.0f, 0.0f, 0.0f, yaw, false));

    glUseProgram(glyphProgram_);
    glUniformMatrix4fv(uMVPGlyph_, 1, GL_FALSE, mvp.m);
    glUniform4f(uTint_, 1.0f, 1.0f, 1.0f, 1.0f);
    glBindVertexArray(m->vao);
    for (const DrawRange& d : m->draws) {
        glBindTexture(GL_TEXTURE_2D, d.texture);
        glDrawElements(GL_TRIANGLES, d.indexCount, GL_UNSIGNED_INT, (void*)(size_t)(d.firstIndex * sizeof(GLuint)));
    }
    glBindVertexArray(0);

    // Back to the flat UI over the whole window
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, uiW_, uiH_);
    glUniformMatrix4fv(uMVPGlyph_, 1, GL_FALSE, pixels_.m);
}

// The sprite for a unit, or the stand-in if its image is missing
const TextTex& Renderer::unitGlyph(World& w, Entity e) const {
    // A mob's picture is its kind's (mob_<kind>.png), with the pink square for a missing one. A kind drawn by a model has
    // the pink square for its portrait, as a person has.
    if (const Mob* m = w.get<Mob>(e)) {
        const int i = idx(m->kind) - 1;
        // A kind meant to be drawn by a model has no picture of its own: the pink square, standing in the world as a
        // billboard when the model is missing, and its portrait always
        if (mobInfo(m->kind).model > 0) return pink_;
        if (i >= 0 && i < (int)mobSprites_.size() && mobSprites_[(size_t)i].tex) return mobSprites_[(size_t)i];
        return pink_;
    }
    return pink_;   // a person is drawn by a model; the pink square stands in for a missing one
}

// The profile picture of model n (profile_<nn>.png), or the pink square when it is missing
const TextTex& Renderer::unitPicture(int n) const {
    const int idx = ((n - 1) % UNIT_SPRITE_COUNT + UNIT_SPRITE_COUNT) % UNIT_SPRITE_COUNT;
    return profilePics_[(size_t)idx].tex ? profilePics_[(size_t)idx] : pink_;
}

const TextTex& Renderer::unitPortrait(World& w, Entity e) const {
    if (w.has<Mob>(e)) return unitGlyph(w, e);
    const UnitSprite* us = w.get<UnitSprite>(e);
    return unitPicture(us ? us->id : 1);
}

const Model* Renderer::unitModelNumber(int n) const {
    const int idx = ((n - 1) % UNIT_SPRITE_COUNT + UNIT_SPRITE_COUNT) % UNIT_SPRITE_COUNT;
    return unitModelFor_[(size_t)idx];
}

RGBA Renderer::unitFactionTint(World& w, Entity e) const {
    const Faction* f = w.get<Faction>(e);
    if (!f || f->id == FACTION_PLAYER || w.has<Mob>(e)) return RGBA{ 0.0f, 0.0f, 0.0f, 0.0f };
    const FactionColor c = factionColor(f->id);
    return RGBA{ c.r, c.g, c.b, FACTION_TINT };
}

float Renderer::unitHeight(World& w, Entity e) const {
    if (const Mob* mb = w.get<Mob>(e)) {
        return unitModel(w, e) ? mobInfo(mb->kind).modelHeight : GLYPH_H * mobSpriteScale(mb->kind);
    }
    return GLYPH_H;
}

const Model* Renderer::unitModel(World& w, Entity e) const {
    if (const Mob* mb = w.get<Mob>(e)) {
        const int i = idx(mb->kind) - 1;
        if (i >= 0 && i < MOB_KIND_COUNT && mobModels_[(size_t)i].vao) return &mobModels_[(size_t)i];
        return nullptr;
    }
    const UnitSprite* us = w.get<UnitSprite>(e);
    return unitModelNumber(us ? us->id : 1);
}

float Renderer::unitAspect(World& w, Entity e) const {
    if (const Model* m = unitModel(w, e)) {
        return std::max(m->maxX - m->minX, m->maxZ - m->minZ) / (m->maxY - m->minY);
    }
    return unitGlyph(w, e).aspect;
}

// Projects each unit's billboard to the screen and keeps those under the pixel.
// The test uses the same size and aspect that drawGlyph uses.
std::vector<Entity> Renderer::unitsAtPixel(World& w, const Camera& cam, float mx, float my, int winW, int winH) {
    std::vector<Entity> out;
    Mat4 vp = cam.viewProj((float)winW / (float)winH);

    auto project = [&](float x, float y, float z, float& sx, float& sy) {
        return worldToScreen(vp, x, y, z, winW, winH, sx, sy);
    };

    for (Entity u : w.view<Unit, Transform, Name>()) {
        const Transform* tr = w.get<Transform>(u);
        if (!withinDetail(cam, tr->x, tr->y, tr->z)) continue;
        float bottomX, bottomY, topX, topY, midX, midY;
        const float hover = unitHover(w, u);   // a flying mob is found where it is drawn, up off the ground
        if (!project(tr->x, tr->y + hover, tr->z, bottomX, bottomY)) continue;
        const float tall = unitHeight(w, u);
        if (!project(tr->x, tr->y + hover + tall, tr->z, topX, topY)) continue;
        if (!project(tr->x, tr->y + hover + tall * 0.5f, tr->z, midX, midY)) continue;

        float heightPx = std::fabs(bottomY - topY);
        const float aspect = unitAspect(w, u);
        float halfW = heightPx * 0.5f * aspect;
        float top = std::min(bottomY, topY), bottom = std::max(bottomY, topY);
        const float bed = bedLift(w, u);
        if (w.has<Unconscious>(u) || w.has<Carried>(u) || w.has<PlayingDead>(u) || bed >= 0.0f) {
            // Lying on its side: as wide as it is tall standing, and as high as it is wide; carried ones higher up,
            // and ones in a bed up on it
            float liftX = 0.0f, liftY = 0.0f;
            const float lift = w.has<Carried>(u) ? (unitModel(w, u) ? CARRIED_MODEL_LIFT : CARRIED_LIFT)
                                                 : std::max(0.0f, bed);
            if (!project(tr->x, tr->y + lift, tr->z, liftX, liftY)) continue;
            const float lyingH = heightPx * aspect;
            halfW = heightPx * 0.5f;
            midX = liftX;
            bottom = liftY;
            top = liftY - lyingH;
        }
        if (mx >= midX - halfW && mx <= midX + halfW && my >= top && my <= bottom) {
            out.push_back(u);
        }
    }
    return out;
}

// The props under a pixel, nearest first. A prop with a model is hit only on the model's own triangles, placed exactly
// as drawModel places them (placeModel), so the mouse finds a prop by its shape and never where it is not drawn. A
// prop whose model did not load is found by its pink stand-in, a square facing the camera.
std::vector<Entity> Renderer::propsAtPixel(World& w, const Camera& cam, float mx, float my, int winW, int winH) {
    const Mat4 vp = cam.viewProj((float)winW / (float)winH);
    // The ray through the pixel, from the near plane toward the far one
    float o[3] = {}, d[3] = {};
    {
        float inv[16];
        if (!invert4x4(vp.m, inv)) return {};
        const float ndcX = 2.0f * mx / (float)winW - 1.0f, ndcY = 1.0f - 2.0f * my / (float)winH;
        auto unproject = [&](float ndcZ, float out3[3]) {
            const float p[4] = { ndcX, ndcY, ndcZ, 1.0f };
            float r[4];
            for (int i = 0; i < 4; ++i) r[i] = inv[0 * 4 + i] * p[0] + inv[1 * 4 + i] * p[1] + inv[2 * 4 + i] * p[2] + inv[3 * 4 + i] * p[3];
            out3[0] = r[0] / r[3];
            out3[1] = r[1] / r[3];
            out3[2] = r[2] / r[3];
        };
        float f[3];
        unproject(-1.0f, o);
        unproject(1.0f, f);
        for (int i = 0; i < 3; ++i) d[i] = f[i] - o[i];
    }
    std::vector<std::pair<float, Entity>> hits;
    const DetailRange detail(cam);
    for (Entity e : w.view<Prop, Transform>()) {
        const Transform* tr = w.get<Transform>(e);
        if (!detail.within(tr->x, tr->y, tr->z)) continue;
        const PropKind kind = w.get<Prop>(e)->kind;
        if (idx(kind) < 0 || idx(kind) >= PROP_KIND_COUNT) continue;
        const Model& m = propModel(kind);
        const float baseY = propBaseY(w, e);
        if (m.vao) {
            const float t = rayHitModel(m, placeModel(m, tr->x, baseY, tr->z, propInfo(kind).size,
                                                      w.get<Prop>(e)->yaw), o, d);
            if (t >= 0.0f) hits.push_back({ t, e });
            continue;
        }
        // The pink stand-in: the box that holds the square from any side, on the screen
        const float hx = 0.5f * missingPropSide(kind), hz = hx, h = missingPropSide(kind);
        float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
        bool seen = true;
        for (int c = 0; c < 8 && seen; ++c) {
            float sx = 0.0f, sy = 0.0f;
            seen = worldToScreen(vp, tr->x + ((c & 1) ? hx : -hx), baseY + ((c & 2) ? h : 0.0f),
                                tr->z + ((c & 4) ? hz : -hz), winW, winH, sx, sy);
            if (c == 0) { x0 = x1 = sx; y0 = y1 = sy; }
            x0 = std::min(x0, sx); x1 = std::max(x1, sx);
            y0 = std::min(y0, sy); y1 = std::max(y1, sy);
        }
        if (seen && mx >= x0 && mx <= x1 && my >= y0 && my <= y1) {
            const float dx = tr->x - o[0], dy = baseY - o[1], dz = tr->z - o[2];
            const float len2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
            hits.push_back({ len2 > 0.0f ? (dx * d[0] + dy * d[1] + dz * d[2]) / len2 : 0.0f, e });
        }
    }
    std::sort(hits.begin(), hits.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<Entity> out;
    for (const auto& h : hits) out.push_back(h.second);
    return out;
}

float Renderer::propBaseY(World& w, Entity e) const {
    const Transform* tr = w.get<Transform>(e);
    float y = tr ? tr->y : 0.0f;
    if (const OnBase* ob = w.get<OnBase>(e)) {
        const Transform* ft = w.get<Transform>(ob->foundation);
        const Prop* fp = w.get<Prop>(ob->foundation);
        if (ft && fp && idx(fp->kind) >= 0 && idx(fp->kind) < PROP_KIND_COUNT) {
            // The foundation's top, held to a slab's height in case its model is missing (the pink stand-in is tall)
            const float top = std::clamp(propTop(fp->kind), 0.0f, 0.6f);
            y = std::max(y, ft->y + top);
        }
    }
    return y;
}

float Renderer::propTop(PropKind kind) const {
    float top = missingPropSide(kind);   // the pink stand-in's height, if the kind has no model
    if (idx(kind) >= 0 && idx(kind) < PROP_KIND_COUNT) {
        const Model& m = propModel(kind);
        const float extent = std::max({ m.maxX - m.minX, m.maxY - m.minY, m.maxZ - m.minZ });
        if (m.vao && extent > 0.0f) top = (m.maxY - m.minY) * propInfo(kind).size / extent;
    }
    return top;
}
