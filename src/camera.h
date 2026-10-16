#pragma once
#include <cmath>
#include "geometry.h"

// Orbit camera. Its state is a plain resource, not an entity.
struct Camera {
    float yaw = 0.0f;
    float pitch = 0.9f;
    float zoom = 100.0f;

    float distance() const { return 10.0f * (100.0f / zoom); }

    void eye(float targetX, float targetZ, float& ex, float& ey, float& ez) const {
        float d = distance();
        ex = targetX + d * std::sin(yaw) * std::cos(pitch);
        ey = d * std::sin(pitch);
        ez = targetZ + d * std::cos(yaw) * std::cos(pitch);
    }

    Mat4 viewProj(float targetX, float targetZ, float aspect) const {
        float ex, ey, ez;
        eye(targetX, targetZ, ex, ey, ez);
        return mul(perspective(0.9f, aspect, 0.1f, 100.0f),
                   lookAt(ex, ey, ez, targetX, 0.0f, targetZ, 0.0f, 1.0f, 0.0f));
    }
};
