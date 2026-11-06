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

// Ray through a pixel, intersected with the terrain surface. Returns world (x, z).
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

    // March along the ray from the eye and stop at the first point under the surface the player sees: the ground,
    // or the sea where the ground lies under it (the sea is drawn at sea level, so a click on the water lands where
    // the water is, not on the seabed far below it). Then halve the last step until the point is on the surface.
    // A plane fitted at the height under the last guess would slide through a hill to the ground behind it, or
    // wander on a slope, when the ray meets the ground at a low angle.
    const float dir[3] = { f[0] - n[0], f[1] - n[1], f[2] - n[2] };
    const float len = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (len < 1e-6f) return std::nullopt;
    const float ux = dir[0] / len, uy = dir[1] / len, uz = dir[2] / len;
    auto surface = [&](float x, float z) { return std::max(t.heightAt(x, z), 0.0f); };
    auto gap = [&](float d) { return n[1] + uy * d - surface(n[0] + ux * d, n[2] + uz * d); };
    auto below = [&](float d) { return gap(d) <= 0.0f; };
    // Steps grow with the distance, up to a hundredth of it, but shrink to a quarter of the ray's height over the
    // ground, so a ray that skims a ridge seen against the sky slows down over it and can't step through its crest
    constexpr float MAX_PICK = 4000.0f;          // tiles along the ray; past this the ground is fogged out anyway
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
        // Going up and already above the highest ground there is, it will meet nothing
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
    for (Entity e : w.view<Selected>()) w.remove<Selected>(e);
}

std::vector<Entity> selectedUnits(World& w) {
    std::vector<Entity> out;
    for (Entity u : w.view<Unit, Selected, Commandable>()) out.push_back(u);
    return out;
}
// The unit the Unit Panel shows: the talking unit during a trade, the unit using a storage prop while its panel is
// open, otherwise the lone selected unit
Entity sidebarUnitOf(World& w) {
    if (tradeOpen && dialoguePlayer != NULL_ENTITY && w.has<Unit>(dialoguePlayer)) return dialoguePlayer;
    if (storageProp != NULL_ENTITY && w.has<Unit>(storageUnit)) return storageUnit;
    if (lootUnit != NULL_ENTITY && w.has<Unit>(lootBy)) return lootBy;
    if (inspectUnit != NULL_ENTITY && w.has<Unit>(inspectUnit)) return inspectUnit;
    const std::vector<Entity> mine = selectedUnits(w);
    return mine.size() == 1 ? mine[0] : NULL_ENTITY;
}

// The units the Unit Panel acts on: the talking unit during a trade, the storage panel's unit, otherwise the selected units.
// An awake mob's inventory is shut (mobs.h), so a mob is never one of them: its items can't be dragged, used or dropped.
std::vector<Entity> panelUnits(World& w) {
    if (tradeOpen && dialoguePlayer != NULL_ENTITY && w.has<Unit>(dialoguePlayer)) return { dialoguePlayer };
    if (storageProp != NULL_ENTITY && w.has<Unit>(storageUnit)) return { storageUnit };
    if (lootUnit != NULL_ENTITY && w.has<Unit>(lootBy)) return { lootBy };
    std::vector<Entity> out;
    for (Entity u : selectedUnits(w)) {
        if (!inventoryLocked(w, u)) out.push_back(u);
    }
    return out;
}

// The unit a click lands on: the one whose model is under the pointer, yours first. NULL_ENTITY for none.
Entity unitClicked(World& w, const std::vector<Entity>& underPointer) {
    for (Entity u : underPointer) {
        if (w.has<Commandable>(u)) return u;
    }
    return underPointer.empty() ? NULL_ENTITY : underPointer[0];
}

// Click selection. One of your squad is selected (added, with Shift or Alt; else in place of the others), and the Unit
// Panel goes back to your selection. Any other unit (an NPC, a corpse) is shown in the Unit Panel without touching your
// selection. Empty ground changes nothing: your squad stays selected.
void clickSelect(World& w, Entity hit, bool additive) {
    if (hit == NULL_ENTITY) {
        inspectUnit = NULL_ENTITY;
        return;
    }
    if (!w.has<Commandable>(hit)) {
        inspectUnit = hit;
        return;
    }
    inspectUnit = NULL_ENTITY;
    if (!additive) clearSelection(w);
    if (!w.has<Selected>(hit)) w.add(hit, Selected{});
}

// Box selection: every unit of your squad whose screen position falls inside the rectangle. A box with none of them in
// it changes nothing, so your squad is never left with nobody selected, and returns false.
bool boxSelect(World& w, const Camera& cam, float x0, float y0, float x1, float y1,
               int W, int H, bool additive) {
    float minX = std::min(x0, x1), maxX = std::max(x0, x1);
    float minY = std::min(y0, y1), maxY = std::max(y0, y1);
    Mat4 vp = cam.viewProj((float)W / (float)H);

    std::vector<Entity> inBox;
    for (Entity u : w.view<Unit, Transform, Commandable>()) {
        Transform* tr = w.get<Transform>(u);
        if (!withinDetail(cam, tr->x, tr->y, tr->z)) continue;
        float sx, sy;
        if (worldToScreen(vp, tr->x, tr->y + 0.5f, tr->z, W, H, sx, sy)
            && sx >= minX && sx <= maxX && sy >= minY && sy <= maxY) {
            inBox.push_back(u);
        }
    }
    if (inBox.empty()) return false;
    inspectUnit = NULL_ENTITY;
    if (!additive) clearSelection(w);
    for (Entity u : inBox) {
        if (!w.has<Selected>(u)) w.add(u, Selected{});
    }
    return true;
}

// Every item on the ground whose screen position falls inside the rectangle
std::vector<Entity> groundItemsInBox(World& w, const Camera& cam, float x0, float y0, float x1, float y1, int W, int H) {
    const float minX = std::min(x0, x1), maxX = std::max(x0, x1);
    const float minY = std::min(y0, y1), maxY = std::max(y0, y1);
    const Mat4 vp = cam.viewProj((float)W / (float)H);
    std::vector<Entity> out;
    for (Entity it : w.view<GroundItem, Transform>()) {
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

// Right click: order the selected units to a ground point.
// Returns true, with the ground point in gx and gz, if an order was given.
bool rightClickOrder(World& w, Terrain& t, const Camera& cam, float mx, float my, int W, int H,
                     float& gx, float& gz) {
    if (selectedUnits(w).empty()) return false;
    auto hit = pickGround(cam, t, mx, my, W, H);
    if (!hit) return false;
    gx = hit->first;
    gz = hit->second;
    return true;
}
// The unit of your squad that is selected when none is (a new game, the selected unit died): the lowest-numbered one,
// preferring a person to a mob and an awake unit to an unconscious one. NULL_ENTITY with no squad left. The world's
// views come out in no set order, so the units are sorted to make the pick the same every time.
Entity firstSquadUnit(World& w) {
    std::vector<Entity> squad = w.view<Unit, Commandable>();
    std::sort(squad.begin(), squad.end());
    Entity best = NULL_ENTITY;
    int bestRank = 4;
    for (Entity u : squad) {
        const int rank = (w.has<Mob>(u) ? 1 : 0) + (outCold(w, u) ? 2 : 0);
        if (rank < bestRank) {
            best = u;
            bestRank = rank;
        }
    }
    return best;
}

// Puts the camera on the tile under the first unit of your squad (firstSquadUnit), the one selected at the start
void focusCameraOnFirstUnit(World& w, Camera& cam) {
    const Entity first = firstSquadUnit(w);
    if (first == NULL_ENTITY || !w.get<Transform>(first)) return;

    const Transform* t = w.get<Transform>(first);
    cam.focusX = std::floor(t->x) + 0.5f;
    cam.focusY = t->y;
    cam.focusZ = std::floor(t->z) + 0.5f;
    cam.clampState();
}

// Moves the camera's focus onto a unit, wherever it is on the map
void focusCameraOnUnit(World& w, Camera& cam, Entity u) {
    const Transform* t = w.get<Transform>(u);
    if (!t) return;
    cam.focusX = t->x;
    cam.focusY = t->y;
    cam.focusZ = t->z;
    cam.clampState();
}

// Holds the camera's focus on a unit as it moves. False once the unit is gone, so following stops.
bool followUnitCamera(World& w, Camera& cam, Entity u) {
    const Transform* t = w.get<Transform>(u);
    if (!t || !w.has<Unit>(u)) return false;
    cam.focusX = t->x;
    cam.focusZ = t->z;
    cam.clampState();
    return true;
}
