#pragma once
#include <algorithm>
#include <cmath>
#include "geometry.h"
#include "grid.h"

// Free camera. The focus point is the pivot the camera orbits around.
struct Camera {
    float focusX = 13.0f, focusY = 0.0f, focusZ = 13.0f;
    float yaw = 0.8f;
    float pitch = 1.0f;
    float distance = 20.0f;

    void eye(float& ex, float& ey, float& ez) const {
        ex = focusX + distance * std::sin(yaw) * std::cos(pitch);
        ey = focusY + distance * std::sin(pitch);
        ez = focusZ + distance * std::cos(yaw) * std::cos(pitch);
    }

    Mat4 viewProj(float aspect) const {
        float ex, ey, ez;
        eye(ex, ey, ez);
        return mul(perspective(0.9f, aspect, 0.5f, 4000.0f),
                   lookAt(ex, ey, ez, focusX, focusY, focusZ, 0.0f, 1.0f, 0.0f));
    }

    // Moves the focus across the ground, relative to the current yaw
    void pan(float right, float forward) {
        float fx = -std::sin(yaw), fz = -std::cos(yaw);
        float rx = std::cos(yaw), rz = -std::sin(yaw);
        focusX += rx * right + fx * forward;
        focusZ += rz * right + fz * forward;
        clampState();
    }

    // Makes (px, py, pz) the orbit pivot. The camera doesn't move, only the
    // pivot and the angles that describe the camera relative to it change.
    void setPivot(float px, float py, float pz) {
        float ex, ey, ez;
        eye(ex, ey, ez);
        float ox = ex - px, oy = ey - py, oz = ez - pz;
        float d = std::sqrt(ox * ox + oy * oy + oz * oz);
        if (d < 1e-4f) return;

        focusX = px;
        focusY = py;
        focusZ = pz;
        distance = d;
        pitch = std::asin(std::clamp(oy / d, -1.0f, 1.0f));
        yaw = std::atan2(ox, oz);
        clampState();
    }

    void clampState() {
        focusX = std::clamp(focusX, 0.0f, (float)MAP_SIZE);
        focusZ = std::clamp(focusZ, 0.0f, (float)MAP_SIZE);
        distance = std::clamp(distance, 8.0f, 200.0f);
        pitch = std::clamp(pitch, 0.25f, 1.45f);
    }
};
