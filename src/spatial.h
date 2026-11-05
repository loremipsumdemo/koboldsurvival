#pragma once
#include <cmath>
#include "components.h"
#include "ecs.h"

// The flat distance in tiles between two entities, or a large number if either has no place
inline float flatGap(World& w, Entity a, Entity b) {
    const Transform* ta = w.get<Transform>(a);
    const Transform* tb = w.get<Transform>(b);
    if (!ta || !tb) return 1e9f;
    return std::hypot(ta->x - tb->x, ta->z - tb->z);
}
