#include "camera_limit.h"
#include <cmath>
#include "claims.h"
#include "components.h"
#include "factions.h"
#include "hostility.h"
#include "squads.h"

namespace {
struct Spot {
    float x, z, range;
};

float overshoot(const Camera& cam, const Spot& s) {
    return std::hypot(cam.focusX - s.x, cam.focusZ - s.z) - s.range;
}
}

bool keepCameraLeashed(World& w, Camera& cam, CameraLeash& leash, float dt) {
    // The lost-focus anchor. Selected living units keep the camera as they always did; when the last of them goes (it died, or it left
    // the squad) the anchor is dropped where it was, unless it is only deselected and still alive, which needs none.
    std::vector<CameraLeash::Watched> selected;
    for (ObjectId u : w.view<Entity, Selected, Commandable>()) {
        if (const Transform* t = w.get<Transform>(u)) selected.push_back({ u, t->x, t->z });
    }
    if (!selected.empty()) {
        leash.anchored = false;
    } else if (!leash.watched.empty()) {
        bool alive = false;
        for (const CameraLeash::Watched& wd : leash.watched) {
            if (w.has<Entity>(wd.id) && w.has<Commandable>(wd.id)) alive = true;
        }
        if (!alive) {
            // The one nearest the camera, if several went together: the one it was looking at
            const CameraLeash::Watched* at = &leash.watched.front();
            float best = std::hypot(cam.focusX - at->x, cam.focusZ - at->z);
            for (const CameraLeash::Watched& wd : leash.watched) {
                const float d = std::hypot(cam.focusX - wd.x, cam.focusZ - wd.z);
                if (d < best) {
                    best = d;
                    at = &wd;
                }
            }
            leash.anchored = true;
            leash.anchorX = std::floor(at->x) + 0.5f;
            leash.anchorZ = std::floor(at->z) + 0.5f;
        }
    }
    leash.watched = std::move(selected);

    // What the camera may stay near: your units, then (only if they are not enough) your claims
    std::vector<Spot> spots;
    for (ObjectId u : w.view<Entity, Commandable>()) {
        if (const Transform* t = w.get<Transform>(u)) spots.push_back({ t->x, t->z, CAMERA_UNIT_RANGE });
    }
    for (ObjectId u : w.view<Entity, Citizen>()) {
        if (factionOf(w, u) != FACTION_PLAYER) continue;
        if (const Transform* t = w.get<Transform>(u)) spots.push_back({ t->x, t->z, CAMERA_UNIT_RANGE });
    }
    auto reachable = [&]() {
        for (const Spot& s : spots) {
            if (overshoot(cam, s) <= 0.0f) return true;
        }
        return false;
    };
    bool claimsAdded = false;
    auto addClaims = [&]() {
        if (claimsAdded) return;
        claimsAdded = true;
        for (const Claim& c : claimsOf(w)) {
            if (c.owner == FACTION_PLAYER) spots.push_back({ c.x, c.z, c.radius + CAMERA_UNIT_RANGE });
        }
    };
    if (!reachable()) addClaims();
    // The anchor has done its purpose once the camera is in reach of anything else
    if (leash.anchored && reachable()) leash.anchored = false;
    // Whatever happens, something holds the camera: with nothing of yours left to hold it, it keeps where it is
    if (!leash.anchored && spots.empty()) {
        addClaims();
        if (spots.empty()) {
            leash.anchored = true;
            leash.anchorX = cam.focusX;
            leash.anchorZ = cam.focusZ;
        }
    }
    if (leash.anchored) spots.push_back({ leash.anchorX, leash.anchorZ, CAMERA_UNIT_RANGE });

    const Spot* nearest = nullptr;
    float best = 0.0f;
    for (const Spot& s : spots) {
        const float over = overshoot(cam, s);
        if (!nearest || over < best) {
            nearest = &s;
            best = over;
        }
    }
    if (!nearest || best <= 0.0f) return false;

    // Back to the edge of the nearest reach: stopped at once when only just past it, eased in when far (a unit has gone, or the
    // camera was moved a long way)
    const float away = best + nearest->range;
    const float tx = nearest->x + (cam.focusX - nearest->x) * nearest->range / away;
    const float tz = nearest->z + (cam.focusZ - nearest->z) * nearest->range / away;
    if (best <= CAMERA_SNAP_BAND) {
        cam.focusX = tx;
        cam.focusZ = tz;
    } else {
        const float step = 1.0f - std::exp(-CAMERA_GLIDE_RATE * std::max(dt, 1.0f / 120.0f));
        cam.focusX += (tx - cam.focusX) * step;
        cam.focusZ += (tz - cam.focusZ) * step;
    }
    cam.clampState();
    return true;
}

void selectReturningMilitary(World& w, CameraLeash& leash) {
    const std::vector<ObjectId> military = militaryUnits(w);
    const bool have = !military.empty();
    if (leash.started && !leash.hadMilitary && have) {
        std::vector<ObjectId> was;
        for (ObjectId u : w.view<Selected>()) was.push_back(u);
        for (ObjectId u : was) w.remove<Selected>(u);
        for (ObjectId u : military) w.add(u, Selected{});
        leash.anchored = false;
    }
    leash.hadMilitary = have;
    leash.started = true;
}
