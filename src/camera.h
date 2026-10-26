#pragma once
#include <algorithm>
#include <cmath>
#include "geometry.h"
#include "grid.h"
#include "terrain.h"

// Free camera. The focus point is the point the camera looks at and orbits around.
// How far the camera draws: the terrain beyond the mid range is drawn at quarter detail out to here
inline constexpr float CAMERA_FAR = 8000.0f;

struct Camera {
    static constexpr float PI_F = 3.14159265f;
    // Pitch stops at 95% of straight up or down, so the view never goes directly over or under
    static constexpr float MAX_PITCH = 0.95f * (PI_F * 0.5f);
    static constexpr float MIN_CLEARANCE = 0.6f;   // how far above the ground the eye must stay
    static constexpr float FOCUS_HEIGHT = 0.5f;    // the focus point sits this far above the ground

    float focusX = 13.0f, focusY = 0.0f, focusZ = 13.0f;
    float yaw = 0.8f;
    float pitch = 1.0f;
    float distance = 20.0f;
    float lift = 0.0f;   // temporary height added to the eye while it is near or under the ground
    float mapSize = 1024.0f;   // tiles per side of the loaded map. The game sets this each frame.

    // Eye position, including the temporary lift
    // The compass bearing the camera looks along, in degrees from north (0) clockwise through east (90).
    // North is +Z and east is -X, as the world's map is drawn.
    float bearingDegrees() const {
        float d = std::atan2(std::sin(yaw), -std::cos(yaw)) * 180.0f / PI_F;
        if (d < 0.0f) d += 360.0f;
        return d;
    }

    void eye(float& ex, float& ey, float& ez) const {
        ex = focusX + distance * std::sin(yaw) * std::cos(pitch);
        ey = focusY + distance * std::sin(pitch) + lift;
        ez = focusZ + distance * std::cos(yaw) * std::cos(pitch);
    }

    Mat4 viewProj(float aspect) const {
        float ex, ey, ez;
        eye(ex, ey, ez);
        return mul(perspective(0.9f, aspect, 0.1f, CAMERA_FAR),
                   lookAt(ex, ey, ez, focusX, focusY, focusZ, 0.0f, 1.0f, 0.0f));
    }

    // Called once per frame. One terrain height lookup, no ray tests.
    // The lift is set to exactly what the eye needs, with no easing, so the camera
    // reaches its clear position at once and never lags behind a move.
    void updateClearance(const Terrain& t) {
        // Keep the focus half a tile above the ground under it, so the camera follows the terrain
        focusY = t.heightAt(focusX, focusZ) + FOCUS_HEIGHT;
        float ex = focusX + distance * std::sin(yaw) * std::cos(pitch);
        float ey = focusY + distance * std::sin(pitch);
        float ez = focusZ + distance * std::cos(yaw) * std::cos(pitch);

        lift = std::max(0.0f, t.heightAt(ex, ez) + MIN_CLEARANCE - ey);
    }

    // Moves the focus across the ground, relative to the current yaw
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
        pitch = std::clamp(pitch, -MAX_PITCH, MAX_PITCH);
    }
};
