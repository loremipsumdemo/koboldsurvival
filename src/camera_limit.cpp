#include "camera_limit.h"
#include <cmath>
#include <vector>
#include "components.h"

void keepCameraNearUnits(World& w, Camera& cam) {
    std::vector<ObjectId> units;
    for (ObjectId u : w.view<Entity, Selected, Commandable>()) units.push_back(u);
    if (units.empty()) {
        for (ObjectId u : w.view<Entity, Commandable>()) units.push_back(u);
    }
    const Transform* nearest = nullptr;
    float best = 0.0f;
    for (ObjectId u : units) {
        const Transform* t = w.get<Transform>(u);
        if (!t) continue;
        const float away = std::hypot(cam.focusX - t->x, cam.focusZ - t->z);
        if (!nearest || away < best) {
            nearest = t;
            best = away;
        }
    }
    if (!nearest || best <= CAMERA_UNIT_RANGE) return;
    cam.focusX = nearest->x + (cam.focusX - nearest->x) * CAMERA_UNIT_RANGE / best;
    cam.focusZ = nearest->z + (cam.focusZ - nearest->z) * CAMERA_UNIT_RANGE / best;
    cam.clampState();
}
