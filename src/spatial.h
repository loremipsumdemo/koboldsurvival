#pragma once
#include <cmath>
#include "components.h"
#include "ecs.h"

// Puts an item stack on the ground. at is a copy, since creating an entity can move the stored components.
inline Entity spawnGroundItem(World& w, Transform at, int item, int count) {
    const Entity e = w.create();
    w.add(e, at);
    w.add(e, GroundItem{ item, count });
    return e;
}

// The flat distance in tiles between two entities, or a large number if either has no place
inline float flatGap(World& w, Entity a, Entity b) {
    const Transform* ta = w.get<Transform>(a);
    const Transform* tb = w.get<Transform>(b);
    if (!ta || !tb) return 1e9f;
    return std::hypot(ta->x - tb->x, ta->z - tb->z);
}
