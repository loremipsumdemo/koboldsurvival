#include "renderer_internal.h"

using namespace render_detail;

namespace {
constexpr float FACTION_TINT = 0.85f;

constexpr float UNIT_MODEL_FRONT_YAW = -1.5707963f;

Mat4 unitModelMatrix(const Model& m, float x, float y, float z, float yaw, bool lying, float height) {
    const float k = height / (m.maxY - m.minY);
    yaw += UNIT_MODEL_FRONT_YAW;
    const float cs = std::cos(yaw), sn = std::sin(yaw);
    const float R[3][3] = {
        { lying ? 0.0f : cs, lying ? -cs : 0.0f, sn },
        { lying ? 1.0f : 0.0f, lying ? 0.0f : 1.0f, 0.0f },
        { lying ? 0.0f : -sn, lying ? sn : 0.0f, cs }
    };
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

constexpr unsigned long long UNIT_SETTLE_MS = 3000;

constexpr float UNIT_SETTLE_SLIP = 0.25f;
}

float missingPropSide(PropKind) {
    return GLYPH_H;
}

float Renderer::unitTop(World& w, ObjectId u) {
    const UnitPose pose = unitPose(w, u);
    return unitHeight(w, u) * (pose.lying ? unitAspect(w, u) : 1.0f) + pose.lift;
}

Renderer::UnitPose Renderer::unitPose(World& w, ObjectId e) const {
    UnitPose p;
    const float bed = bedLift(w, e);
    p.carried = w.has<Carried>(e);
    p.lying = p.carried || bed >= 0.0f || outCold(w, e) || w.has<PlayingDead>(e);
    if (p.carried) p.lift = unitModel(w, e) ? CARRIED_MODEL_LIFT : CARRIED_LIFT;
    else if (bed >= 0.0f) p.lift = bed;
    else p.lift = unitHover(w, e);
    return p;
}

float Renderer::bedLift(World& w, ObjectId u) const {
    const Occupying* oc = w.get<Occupying>(u);
    if (!oc || w.has<Carried>(u)) return -1.0f;
    const Prop* p = w.get<Prop>(oc->prop);
    const Transform* pt = w.get<Transform>(oc->prop);
    const Transform* ut = w.get<Transform>(u);
    if (!p || !pt || !ut || !isBed(p->kind)) return -1.0f;
    const float dx = ut->x - pt->x, dz = ut->z - pt->z;
    if (dx * dx + dz * dz > 0.25f * 0.25f) return -1.0f;
    return std::max(0.0f, propBaseY(w, oc->prop) - ut->y + propTop(p->kind) * 0.5f);
}

float Renderer::unitHover(World& w, ObjectId u) const {
    const Mob* mb = w.get<Mob>(u);
    if (!mb) return 0.0f;
    if (outCold(w, u) || w.has<PlayingDead>(u) || w.has<Corpse>(u) || w.has<Carried>(u) || w.has<Occupying>(u)) {
        return 0.0f;
    }
    return mobHoverFraction(mb->kind) * unitHeight(w, u);
}

void Renderer::drawLyingGlyph(const TextTex& g, float x, float y, float z, float h, RGBA tint, float toward) {
    if (!g.tex) return;
    float dx = billX_ - x, dz = billZ_ - z;
    float len = std::sqrt(dx * dx + dz * dz);
    if (len < 1e-4f) { dx = 0.0f; dz = 1.0f; len = 1.0f; }
    dx /= len;
    dz /= len;
    const float rx = dz, rz = -dx;
    const float hw = h * 0.5f;
    const float up = h * g.aspect;
    const float cx = x + dx * toward, cz = z + dz * toward;
    const float y0 = y, y1 = y + up;
    float v[4 * 5] = {
        cx - rx * hw, y0, cz - rz * hw, 1.0f, 1.0f,
        cx + rx * hw, y0, cz + rz * hw, 1.0f, 0.0f,
        cx + rx * hw, y1, cz + rz * hw, 0.0f, 0.0f,
        cx - rx * hw, y1, cz - rz * hw, 0.0f, 1.0f
    };
    drawQuadVerts(g.tex, v, tint);
}

void Renderer::drawGlyph(const TextTex& g, float x, float y, float z, float h, RGBA tint) {
    if (!g.tex) return;

    float dx = billX_ - x, dz = billZ_ - z;
    float len = std::sqrt(dx * dx + dz * dz);
    if (len < 1e-4f) { dx = 0.0f; dz = 1.0f; len = 1.0f; }
    dx /= len;
    dz /= len;
    float rx = dz, rz = -dx;

    float hw = h * g.aspect * 0.5f;
    float hh = h * 0.5f;
    float cy = y + hh;

    float v[4 * 5] = {
        x - rx * hw, cy - hh, z - rz * hw, 0.0f, 1.0f,
        x + rx * hw, cy - hh, z + rz * hw, 1.0f, 1.0f,
        x + rx * hw, cy + hh, z + rz * hw, 1.0f, 0.0f,
        x - rx * hw, cy + hh, z - rz * hw, 0.0f, 0.0f
    };
    drawQuadVerts(g.tex, v, tint);
}

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
    glUniform4f(uTintInst_, worldTint_[0], worldTint_[1], worldTint_[2], 1.0f);
    glUniformMatrix4fv(uVPInst_, 1, GL_FALSE, viewProj_.m);
    for (int k = 0; k < propKindCount(); ++k) {
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
        list.clear();
    }
    glBindVertexArray(0);
    glUseProgram(glyphProgram_);
    glUniformMatrix4fv(uMVPGlyph_, 1, GL_FALSE, viewProj_.m);
}

void Renderer::drawModel(const Model& m, float x, float y, float z, float targetSize, float yaw) {
    if (!m.vao) return;
    const ModelPlacement p = placeModel(m, x, y, z, targetSize, yaw);
    if (!p.ok) return;
    const Mat4 model = placementMatrix(p);

    Mat4 mvp = mul(viewProj_, model);
    glUniformMatrix4fv(uMVPGlyph_, 1, GL_FALSE, mvp.m);
    glUniform4f(uTint_, worldTint_[0], worldTint_[1], worldTint_[2], 1.0f);

    glBindVertexArray(m.vao);
    for (const DrawRange& d : m.draws) {
        glBindTexture(GL_TEXTURE_2D, d.texture);
        glDrawElements(GL_TRIANGLES, d.indexCount, GL_UNSIGNED_INT,
                       (void*)(size_t)(d.firstIndex * sizeof(GLuint)));
    }
    glBindVertexArray(0);

    glUniformMatrix4fv(uMVPGlyph_, 1, GL_FALSE, viewProj_.m);
}

Mat4 Renderer::unitWorldMatrix(World& w, ObjectId e, const Model& m, float yaw) const {
    const Transform* tr = w.get<Transform>(e);
    const UnitPose pose = unitPose(w, e);
    float x = tr->x, z = tr->z;
    if (pose.carried) {
        const float dx = billX_ - x, dz = billZ_ - z, len = std::sqrt(dx * dx + dz * dz);
        if (len > 1e-4f) {
            x += dx / len * 0.15f;
            z += dz / len * 0.15f;
        }
    }
    return unitModelMatrix(m, x, tr->y + pose.lift, z, yaw, pose.lying, unitHeight(w, e));
}

void Renderer::drawUnitModel(const Model& m, const Mat4& placement) {
    if (!m.vao || !(m.maxY > m.minY)) return;
    const Mat4 mvp = mul(viewProj_, placement);
    glUniformMatrix4fv(uMVPGlyph_, 1, GL_FALSE, mvp.m);
    glUniform4f(uTint_, worldTint_[0], worldTint_[1], worldTint_[2], 1.0f);   // the UI's last quad of the frame before leaves its tint set
    glBindVertexArray(m.vao);
    for (const DrawRange& d : m.draws) {
        glBindTexture(GL_TEXTURE_2D, d.texture);
        glDrawElements(GL_TRIANGLES, d.indexCount, GL_UNSIGNED_INT, (void*)(size_t)(d.firstIndex * sizeof(GLuint)));
    }
    glBindVertexArray(0);
    glUniformMatrix4fv(uMVPGlyph_, 1, GL_FALSE, viewProj_.m);
}

float Renderer::unitYaw(ObjectId e, float x, float z, bool idle) {
    const unsigned long long now = std::max<unsigned long long>(1, SDL_GetTicks());
    auto it = unitFacing_.find(e);
    if (it == unitFacing_.end()) {
        UnitFacing f;
        f.x = x;
        f.z = z;
        f.yaw = std::atan2(billX_ - x, billZ_ - z);
        f.idleSince = idle ? now : 0;
        f.turnedAt = now;
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
            return f.yaw;
        }
        f.settled = false;
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
        float turn = std::remainder(want - f.yaw, TWO_PI);
        const float seconds = std::min(0.25f, (float)(now - f.turnedAt) / 1000.0f);
        f.yaw += turn * (1.0f - std::pow(0.75f, seconds * 60.0f));
    }
    f.turnedAt = now;
    f.x = x;
    f.z = z;
    return f.yaw;
}

void Renderer::drawUnitModelPreview(int sprite, const Rect& r, float yaw) {
    drawModelPreview(unitModelNumber(sprite), r, yaw);
}

void Renderer::drawMobPreview(MobKind kind, const Rect& r, float yaw) {
    const int i = idx(kind) - 1;
    if (i >= 0 && i < mobKindCount() && mobModels_[(size_t)i].vao) {
        drawModelPreview(&mobModels_[(size_t)i], r, yaw);
        return;
    }
    drawFitted(mobPicture(kind), r, WHITE);
}

const TextTex& Renderer::mobPicture(MobKind kind) const {
    const int i = idx(kind) - 1;
    if (i >= 0 && i < mobKindCount() && !mobInfo(kind).model.empty()) return pink_;
    if (i >= 0 && i < (int)mobSprites_.size() && mobSprites_[(size_t)i].tex) return mobSprites_[(size_t)i];
    return pink_;
}

void Renderer::drawModelPreview(const Model* m, const Rect& r, float yaw) {
    if (!m || r.w < 1.0f || r.h < 1.0f) {
        drawFitted(pink_, r, WHITE);
        return;
    }
    const int vx = (int)std::lround(r.x), vy = (int)std::lround((float)uiH_ - r.y - r.h);
    const int vw = (int)std::lround(r.w), vh = (int)std::lround(r.h);
    glViewport(vx, vy, vw, vh);
    glEnable(GL_SCISSOR_TEST);
    glScissor(vx, vy, vw, vh);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    const float aspect = r.w / r.h;
    const float fov = 30.0f * PI / 180.0f, tanHalf = std::tan(fov * 0.5f);
    const float k = GLYPH_H / (m->maxY - m->minY);
    const float halfH = GLYPH_H * 0.5f;
    const float halfW = k * std::max(m->maxX - m->minX, m->maxZ - m->minZ) * 0.5f;
    const float dist = std::max(halfH / tanHalf, halfW / (tanHalf * aspect)) * 1.12f + halfW;
    const Mat4 vp = mul(perspective(fov, aspect, 0.05f, dist * 4.0f), lookAt(0.0f, halfH, dist, 0.0f, halfH, 0.0f, 0.0f, 1.0f, 0.0f));
    const Mat4 mvp = mul(vp, unitModelMatrix(*m, 0.0f, 0.0f, 0.0f, yaw, false, GLYPH_H));

    glUseProgram(glyphProgram_);
    glUniformMatrix4fv(uMVPGlyph_, 1, GL_FALSE, mvp.m);
    glUniform4f(uTint_, 1.0f, 1.0f, 1.0f, 1.0f);
    glBindVertexArray(m->vao);
    for (const DrawRange& d : m->draws) {
        glBindTexture(GL_TEXTURE_2D, d.texture);
        glDrawElements(GL_TRIANGLES, d.indexCount, GL_UNSIGNED_INT, (void*)(size_t)(d.firstIndex * sizeof(GLuint)));
    }
    glBindVertexArray(0);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, uiW_, uiH_);
    glUniformMatrix4fv(uMVPGlyph_, 1, GL_FALSE, pixels_.m);
}

const TextTex& Renderer::unitGlyph(World& w, ObjectId e) const {
    if (const Mob* m = w.get<Mob>(e)) {
        const int i = idx(m->kind) - 1;
        if (!mobInfo(m->kind).model.empty()) return pink_;
        if (i >= 0 && i < (int)mobSprites_.size() && mobSprites_[(size_t)i].tex) return mobSprites_[(size_t)i];
        return pink_;
    }
    return pink_;
}

const TextTex& Renderer::unitPicture(int n) const {
    const int idx = ((n - 1) % UNIT_SPRITE_COUNT + UNIT_SPRITE_COUNT) % UNIT_SPRITE_COUNT;
    return profilePics_[(size_t)idx].tex ? profilePics_[(size_t)idx] : pink_;
}

const TextTex& Renderer::unitPortrait(World& w, ObjectId e) const {
    if (w.has<Mob>(e)) return unitGlyph(w, e);
    const UnitSprite* us = w.get<UnitSprite>(e);
    return unitPicture(us ? us->id : 1);
}

const Model* Renderer::unitModelNumber(int n) const {
    const int idx = ((n - 1) % UNIT_SPRITE_COUNT + UNIT_SPRITE_COUNT) % UNIT_SPRITE_COUNT;
    return unitModelFor_[(size_t)idx];
}

RGBA Renderer::unitFactionTint(World& w, ObjectId e) const {
    const Faction* f = w.get<Faction>(e);
    if (!f || f->id == FACTION_PLAYER || w.has<Mob>(e)) return RGBA{ 0.0f, 0.0f, 0.0f, 0.0f };
    const FactionColor c = factionColor(f->id);
    return RGBA{ c.r, c.g, c.b, FACTION_TINT };
}

float Renderer::unitHeight(World& w, ObjectId e) const {
    if (const Mob* mb = w.get<Mob>(e)) {
        return unitModel(w, e) ? mobInfo(mb->kind).modelHeight : GLYPH_H * mobSpriteScale(mb->kind);
    }
    return GLYPH_H;
}

const Model* Renderer::unitModel(World& w, ObjectId e) const {
    if (const Mob* mb = w.get<Mob>(e)) {
        const int i = idx(mb->kind) - 1;
        if (i >= 0 && i < mobKindCount() && mobModels_[(size_t)i].vao) return &mobModels_[(size_t)i];
        return nullptr;
    }
    const UnitSprite* us = w.get<UnitSprite>(e);
    return unitModelNumber(us ? us->id : 1);
}

float Renderer::unitAspect(World& w, ObjectId e) const {
    if (const Model* m = unitModel(w, e)) {
        return std::max(m->maxX - m->minX, m->maxZ - m->minZ) / (m->maxY - m->minY);
    }
    return unitGlyph(w, e).aspect;
}

static bool pixelRay(const Mat4& vp, float mx, float my, int winW, int winH, float o[3], float d[3]) {
    float inv[16];
    if (!invert4x4(vp.m, inv)) return false;
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
    return true;
}

std::vector<ObjectId> Renderer::entitiesAtPixel(World& w, const Camera& cam, float mx, float my, int winW, int winH) {
    Mat4 vp = cam.viewProj((float)winW / (float)winH);
    float o[3] = {}, d[3] = {};
    if (!pixelRay(vp, mx, my, winW, winH, o, d)) return {};
    std::vector<std::pair<float, ObjectId>> hits;
    const float rayLen2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];

    auto project = [&](float x, float y, float z, float& sx, float& sy) {
        return worldToScreen(vp, x, y, z, winW, winH, sx, sy);
    };

    for (ObjectId u : w.view<Entity, Transform, Name>()) {
        const Transform* tr = w.get<Transform>(u);
        if (!withinDetail(cam, tr->x, tr->y, tr->z)) continue;
        if (const Model* m = unitModel(w, u)) {
            const float tall = std::max(1.0f, unitHeight(w, u)), reach = 1.5f * tall + 1.0f;
            const float cx = tr->x - o[0], cy = tr->y + 0.5f * tall - o[1], cz = tr->z - o[2];
            const float along = cx * d[0] + cy * d[1] + cz * d[2];
            if (rayLen2 > 0.0f && cx * cx + cy * cy + cz * cz - along * along / rayLen2 > reach * reach) continue;
            const auto facing = unitFacing_.find(u);
            const float yaw = facing != unitFacing_.end() ? facing->second.yaw : std::atan2(billX_ - tr->x, billZ_ - tr->z);
            const float t = rayHitModelMatrix(*m, unitWorldMatrix(w, u, *m, yaw).m, o, d);
            if (t >= 0.0f) hits.push_back({ t, u });
            continue;
        }
        float bottomX, bottomY, topX, topY, midX, midY;
        const float hover = unitHover(w, u);
        if (!project(tr->x, tr->y + hover, tr->z, bottomX, bottomY)) continue;
        const float tall = unitHeight(w, u);
        if (!project(tr->x, tr->y + hover + tall, tr->z, topX, topY)) continue;
        if (!project(tr->x, tr->y + hover + tall * 0.5f, tr->z, midX, midY)) continue;

        float heightPx = std::fabs(bottomY - topY);
        const float aspect = unitAspect(w, u);
        float halfW = heightPx * 0.5f * aspect;
        float top = std::min(bottomY, topY), bottom = std::max(bottomY, topY);
        const UnitPose pose = unitPose(w, u);
        if (pose.lying) {
            float liftX = 0.0f, liftY = 0.0f;
            if (!project(tr->x, tr->y + pose.lift, tr->z, liftX, liftY)) continue;
            const float lyingH = heightPx * aspect;
            halfW = heightPx * 0.5f;
            midX = liftX;
            bottom = liftY;
            top = liftY - lyingH;
        }
        if (mx >= midX - halfW && mx <= midX + halfW && my >= top && my <= bottom) {
            const float dx = tr->x - o[0], dy = tr->y - o[1], dz = tr->z - o[2];
            const float len2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
            hits.push_back({ len2 > 0.0f ? (dx * d[0] + dy * d[1] + dz * d[2]) / len2 : 0.0f, u });
        }
    }
    std::sort(hits.begin(), hits.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<ObjectId> out;
    for (const auto& h : hits) out.push_back(h.second);
    return out;
}

std::vector<ObjectId> Renderer::propsAtPixel(World& w, const Camera& cam, float mx, float my, int winW, int winH) {
    const Mat4 vp = cam.viewProj((float)winW / (float)winH);
    float o[3] = {}, d[3] = {};
    if (!pixelRay(vp, mx, my, winW, winH, o, d)) return {};
    std::vector<std::pair<float, ObjectId>> hits;
    const DetailRange detail(cam);
    for (ObjectId e : w.view<Prop, Transform>()) {
        const Transform* tr = w.get<Transform>(e);
        if (!detail.within(tr->x, tr->y, tr->z)) continue;
        const PropKind kind = w.get<Prop>(e)->kind;
        if (idx(kind) < 0 || idx(kind) >= propKindCount()) continue;
        const Model& m = propModel(kind);
        const float baseY = propBaseY(w, e);
        if (m.vao) {
            const float t = rayHitModel(m, placeModel(m, tr->x, baseY, tr->z, propInfo(kind).size,
                                                      w.get<Prop>(e)->yaw), o, d);
            if (t >= 0.0f) hits.push_back({ t, e });
            continue;
        }
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
    std::vector<ObjectId> out;
    for (const auto& h : hits) out.push_back(h.second);
    return out;
}

float Renderer::propBaseY(World& w, ObjectId e) const {
    const Transform* tr = w.get<Transform>(e);
    return tr ? tr->y : 0.0f;
}

float Renderer::propTop(PropKind kind) const {
    float top = missingPropSide(kind);
    if (idx(kind) >= 0 && idx(kind) < propKindCount()) {
        const Model& m = propModel(kind);
        const float extent = std::max({ m.maxX - m.minX, m.maxY - m.minY, m.maxZ - m.minZ });
        if (m.vao && extent > 0.0f) top = (m.maxY - m.minY) * propInfo(kind).size / extent;
    }
    return top;
}
