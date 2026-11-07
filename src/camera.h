#pragma once
#include <algorithm>
#include <cmath>
#include "geometry.h"
#include "grid.h"
#include "terrain.h"

inline constexpr float CAMERA_FAR = 8000.0f;
inline constexpr float CAMERA_NEAR_MIN = 0.5f;
inline constexpr float CAMERA_NEAR_MAX = 6.0f;
inline constexpr float CAMERA_NEAR_PER_DISTANCE = 0.03f;
inline constexpr float CAMERA_NEAR_CLEARANCE = 0.5f;

struct Camera {
    static constexpr float MAX_PITCH = 65.0f * PI / 180.0f;
    static constexpr float MIN_PITCH = -0.95f * (PI * 0.5f);
    static constexpr float MIN_CLEARANCE = 0.6f;
    static constexpr float FOCUS_HEIGHT = 0.5f;

    float focusX = 13.0f, focusY = 0.0f, focusZ = 13.0f;
    float yaw = 0.8f;
    float pitch = 0.6f;
    float distance = 13.0f;
    float lift = 0.0f;
    float mapSize = 1024.0f;
    float eyeClearance = 1000.0f;
    float viewRange = 256.0f;

    void eye(float& ex, float& ey, float& ez) const {
        ex = focusX + distance * std::sin(yaw) * std::cos(pitch);
        ey = focusY + distance * std::sin(pitch) + lift;
        ez = focusZ + distance * std::cos(yaw) * std::cos(pitch);
    }

    float nearPlane() const {
        const float byZoom = std::clamp(distance * CAMERA_NEAR_PER_DISTANCE, CAMERA_NEAR_MIN, CAMERA_NEAR_MAX);
        const float byGround = std::max(CAMERA_NEAR_MIN, eyeClearance * CAMERA_NEAR_CLEARANCE);
        return std::min(byZoom, byGround);
    }

    Mat4 projection(float aspect) const {
        return perspective(0.9f, aspect, nearPlane(), CAMERA_FAR);
    }

    Mat4 viewProj(float aspect) const {
        float ex, ey, ez;
        eye(ex, ey, ez);
        return mul(projection(aspect), lookAt(ex, ey, ez, focusX, focusY, focusZ, 0.0f, 1.0f, 0.0f));
    }

    void updateClearance(const Terrain& t) {
        focusY = t.heightAt(focusX, focusZ) + FOCUS_HEIGHT;
        float ex = focusX + distance * std::sin(yaw) * std::cos(pitch);
        float ey = focusY + distance * std::sin(pitch);
        float ez = focusZ + distance * std::cos(yaw) * std::cos(pitch);

        const float ground = t.heightAt(ex, ez);
        lift = std::max(0.0f, ground + MIN_CLEARANCE - ey);
        eyeClearance = ey + lift - ground;
    }

    void pan(float right, float forward) {
        float fx = -std::sin(yaw), fz = -std::cos(yaw);
        float rx = std::cos(yaw), rz = -std::sin(yaw);
        focusX += rx * right + fx * forward;
        focusZ += rz * right + fz * forward;
        clampState();
    }

    void clampState() {
        focusX = std::clamp(focusX, 0.0f, mapSize);
        focusZ = std::clamp(focusZ, 0.0f, mapSize);
        distance = std::clamp(distance, 1.5f, 200.0f);
        pitch = std::clamp(pitch, MIN_PITCH, MAX_PITCH);
    }
};
