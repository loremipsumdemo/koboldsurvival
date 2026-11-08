#include "selection.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include "language.h"
#include "components.h"
#include "ecs.h"
#include "items.h"
#include "inventory.h"
#include "equip.h"
#include "combat.h"
#include "stats.h"
#include "props.h"
#include "systems.h"
#include "textcase.h"
#include "ui_layout.h"
#include "play_state.h"
#include "camera.h"
#include "terrain.h"
#include "mobs.h"
#include "renderer.h"

using namespace play;

std::optional<std::pair<float, float>> pickPlane(const Camera& cam, float mx, float my, int W, int H, float y) {
    const Mat4 vp = cam.viewProj((float)W / (float)H);
    float inv[16];
    if (!invert4x4(vp.m, inv)) return std::nullopt;
    const float ndcX = 2.0f * mx / (float)W - 1.0f, ndcY = 1.0f - 2.0f * my / (float)H;
    auto unproject = [&](float ndcZ, float out[3]) {
        const float p[4] = { ndcX, ndcY, ndcZ, 1.0f };
        float r[4];
        for (int i = 0; i < 4; ++i) r[i] = inv[0 * 4 + i] * p[0] + inv[1 * 4 + i] * p[1] + inv[2 * 4 + i] * p[2] + inv[3 * 4 + i] * p[3];
        out[0] = r[0] / r[3];
        out[1] = r[1] / r[3];
        out[2] = r[2] / r[3];
    };
    float n[3], f[3];
    unproject(-1.0f, n);
    unproject(1.0f, f);
    const float dy = f[1] - n[1];
    if (std::fabs(dy) < 1e-6f) return std::nullopt;
    const float s = (y - n[1]) / dy;
    if (s < 0.0f) return std::nullopt;
    return std::make_pair(n[0] + (f[0] - n[0]) * s, n[2] + (f[2] - n[2]) * s);
}

std::optional<std::pair<float, float>> pickGround(const Camera& cam, const Terrain& t,
                                                  float mx, float my, int W, int H) {
    Mat4 vp = cam.viewProj((float)W / (float)H);
    float inv[16];
    if (!invert4x4(vp.m, inv)) return std::nullopt;

    float ndcX = 2.0f * mx / (float)W - 1.0f;
    float ndcY = 1.0f - 2.0f * my / (float)H;

    auto unproject = [&](float ndcZ, float out[3]) {
        float p[4] = { ndcX, ndcY, ndcZ, 1.0f };
        float r[4];
        for (int i = 0; i < 4; ++i) {
            r[i] = inv[0 * 4 + i] * p[0] + inv[1 * 4 + i] * p[1]
                 + inv[2 * 4 + i] * p[2] + inv[3 * 4 + i] * p[3];
        }
        out[0] = r[0] / r[3];
        out[1] = r[1] / r[3];
        out[2] = r[2] / r[3];
    };

    float n[3], f[3];
    unproject(-1.0f, n);
    unproject(1.0f, f);

    const float dir[3] = { f[0] - n[0], f[1] - n[1], f[2] - n[2] };
    const float len = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (len < 1e-6f) return std::nullopt;
    const float ux = dir[0] / len, uy = dir[1] / len, uz = dir[2] / len;
    auto surface = [&](float x, float z) { return std::max(t.heightAt(x, z), 0.0f); };
    auto gap = [&](float d) { return n[1] + uy * d - surface(n[0] + ux * d, n[2] + uz * d); };
    auto below = [&](float d) { return gap(d) <= 0.0f; };
    constexpr float MAX_PICK = 4000.0f;
    constexpr float MIN_STEP = 0.02f, FIRST_STEP = 0.1f, STEP_SHARE = 0.01f;
    float lo = 0.0f, hi = -1.0f;
    for (float d = FIRST_STEP; d <= MAX_PICK;) {
        const float g = gap(d);
        if (g <= 0.0f) {
            hi = d;
            break;
        }
        lo = d;
        d += std::clamp(g * 0.25f, MIN_STEP, std::max(FIRST_STEP, d * STEP_SHARE));
        if (uy >= 0.0f && n[1] + uy * d > 2000.0f) break;
    }
    if (hi < 0.0f) return std::nullopt;
    for (int i = 0; i < 20; ++i) {
        const float mid = 0.5f * (lo + hi);
        if (below(mid)) hi = mid;
        else lo = mid;
    }
    const float hx = n[0] + ux * hi, hz = n[2] + uz * hi;
    if (hx < 0.0f || hz < 0.0f || hx >= t.size() || hz >= t.size()) return std::nullopt;
    return std::make_pair(hx, hz);
}

void clearSelection(World& w) {
    for (ObjectId e : w.view<Selected>()) w.remove<Selected>(e);
}

std::vector<ObjectId> selectedUnits(World& w) {
    std::vector<ObjectId> out;
    for (ObjectId u : w.view<Entity, Selected, Commandable>()) out.push_back(u);
    return out;
}
ObjectId sidebarUnitOf(World& w) {
    if (session.tradeOpen && session.dialoguePlayer != NO_OBJECT && w.has<Entity>(session.dialoguePlayer)) return session.dialoguePlayer;
    if (session.storageProp != NO_OBJECT && w.has<Entity>(session.storageUnit)) return session.storageUnit;
    if (session.lootUnit != NO_OBJECT && w.has<Entity>(session.lootBy)) return session.lootBy;
    if (session.inspectUnit != NO_OBJECT && w.has<Entity>(session.inspectUnit)) return session.inspectUnit;
    const std::vector<ObjectId> mine = selectedUnits(w);
    return mine.size() == 1 ? mine[0] : NO_OBJECT;
}

std::vector<ObjectId> panelUnits(World& w) {
    if (session.tradeOpen && session.dialoguePlayer != NO_OBJECT && w.has<Entity>(session.dialoguePlayer)) return { session.dialoguePlayer };
    if (session.storageProp != NO_OBJECT && w.has<Entity>(session.storageUnit)) return { session.storageUnit };
    if (session.lootUnit != NO_OBJECT && w.has<Entity>(session.lootBy)) return { session.lootBy };
    std::vector<ObjectId> out;
    for (ObjectId u : selectedUnits(w)) {
        if (!inventoryLocked(w, u)) out.push_back(u);
    }
    return out;
}

ObjectId unitClicked(World& w, const std::vector<ObjectId>& underPointer) {
    for (ObjectId u : underPointer) {
        if (w.has<Commandable>(u)) return u;
    }
    return underPointer.empty() ? NO_OBJECT : underPointer[0];
}

void clickSelect(World& w, ObjectId hit, bool additive) {
    if (hit == NO_OBJECT) {
        session.inspectUnit = NO_OBJECT;
        return;
    }
    if (!w.has<Commandable>(hit)) {
        session.inspectUnit = hit;
        return;
    }
    session.inspectUnit = NO_OBJECT;
    if (!additive) clearSelection(w);
    if (!w.has<Selected>(hit)) w.add(hit, Selected{});
}

std::vector<ObjectId> unitsInBox(World& w, const Camera& cam, float x0, float y0, float x1, float y1, int W, int H) {
    const float minX = std::min(x0, x1), maxX = std::max(x0, x1);
    const float minY = std::min(y0, y1), maxY = std::max(y0, y1);
    const Mat4 vp = cam.viewProj((float)W / (float)H);
    std::vector<ObjectId> inBox;
    for (ObjectId u : w.view<Entity, Transform, Commandable>()) {
        const Transform* tr = w.get<Transform>(u);
        if (!withinDetail(cam, tr->x, tr->y, tr->z)) continue;
        float sx, sy;
        if (worldToScreen(vp, tr->x, tr->y + 0.5f, tr->z, W, H, sx, sy)
            && sx >= minX && sx <= maxX && sy >= minY && sy <= maxY) {
            inBox.push_back(u);
        }
    }
    return inBox;
}

bool boxSelect(World& w, const Camera& cam, float x0, float y0, float x1, float y1,
               int W, int H, bool additive) {
    const std::vector<ObjectId> inBox = unitsInBox(w, cam, x0, y0, x1, y1, W, H);
    if (inBox.empty()) return false;
    session.inspectUnit = NO_OBJECT;
    if (!additive) clearSelection(w);
    for (ObjectId u : inBox) {
        if (!w.has<Selected>(u)) w.add(u, Selected{});
    }
    return true;
}

std::vector<ObjectId> groundItemsInBox(World& w, const Camera& cam, float x0, float y0, float x1, float y1, int W, int H) {
    const float minX = std::min(x0, x1), maxX = std::max(x0, x1);
    const float minY = std::min(y0, y1), maxY = std::max(y0, y1);
    const Mat4 vp = cam.viewProj((float)W / (float)H);
    std::vector<ObjectId> out;
    for (ObjectId it : w.view<GroundItem, Transform>()) {
        const Transform* tr = w.get<Transform>(it);
        if (!withinDetail(cam, tr->x, tr->y, tr->z)) continue;
        float sx, sy;
        if (worldToScreen(vp, tr->x, tr->y + 0.2f, tr->z, W, H, sx, sy)
            && sx >= minX && sx <= maxX && sy >= minY && sy <= maxY) {
            out.push_back(it);
        }
    }
    return out;
}

bool rightClickOrder(World& w, Terrain& t, const Camera& cam, float mx, float my, int W, int H,
                     float& gx, float& gz) {
    if (selectedUnits(w).empty()) return false;
    auto hit = pickGround(cam, t, mx, my, W, H);
    if (!hit) return false;
    gx = hit->first;
    gz = hit->second;
    return true;
}
ObjectId firstSquadUnit(World& w) {
    std::vector<ObjectId> squad = w.view<Entity, Commandable>();
    std::sort(squad.begin(), squad.end());
    ObjectId best = NO_OBJECT;
    int bestRank = 4;
    for (ObjectId u : squad) {
        const int rank = (w.has<Mob>(u) ? 1 : 0) + (outCold(w, u) ? 2 : 0);
        if (rank < bestRank) {
            best = u;
            bestRank = rank;
        }
    }
    return best;
}

void focusCameraOnFirstUnit(World& w, Camera& cam) {
    const ObjectId first = firstSquadUnit(w);
    if (first == NO_OBJECT || !w.get<Transform>(first)) return;

    const Transform* t = w.get<Transform>(first);
    cam.focusX = std::floor(t->x) + 0.5f;
    cam.focusY = t->y;
    cam.focusZ = std::floor(t->z) + 0.5f;
    cam.clampState();
}

void focusCameraOnUnit(World& w, Camera& cam, ObjectId u) {
    const Transform* t = w.get<Transform>(u);
    if (!t) return;
    cam.focusX = t->x;
    cam.focusY = t->y;
    cam.focusZ = t->z;
    cam.clampState();
}

bool followUnitCamera(World& w, Camera& cam, ObjectId u) {
    const Transform* t = w.get<Transform>(u);
    if (!t || !w.has<Entity>(u)) return false;
    cam.focusX = t->x;
    cam.focusZ = t->z;
    cam.clampState();
    return true;
}
