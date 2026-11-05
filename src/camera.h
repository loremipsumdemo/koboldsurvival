#pragma once
#include <algorithm>
#include <cmath>
#include "geometry.h"
#include "grid.h"
#include "terrain.h"

// Free camera. The focus point is the point the camera looks at and orbits around.
// How far the camera draws: the terrain beyond the mid range is drawn at quarter detail out to here
inline constexpr float CAMERA_FAR = 8000.0f;
// The near plane. Depth precision at a distance z is about z * z / (near * 2^bits), so the depth buffer resolves
// distant surfaces (the sea against the shore) far better with the near plane further out. A fixed near plane has
// to suit the closest zoom, so it was 0.5 everywhere and the sea and shore flickered when zoomed out.
// The near plane now follows the zoom: CAMERA_NEAR_PER_DISTANCE of the camera's distance, kept between
// CAMERA_NEAR_MIN and CAMERA_NEAR_MAX, and never more than CAMERA_NEAR_CLEARANCE of the eye's height above the
// ground, so ground close under the eye is not cut away.
inline constexpr float CAMERA_NEAR_MIN = 0.5f;            // the closest zoom, as before (0.1 flickered with 24-bit depth)
inline constexpr float CAMERA_NEAR_MAX = 6.0f;            // at the widest zoom (distance 200): 12 times the precision
inline constexpr float CAMERA_NEAR_PER_DISTANCE = 0.03f;  // 0.6 at the default distance of 20
inline constexpr float CAMERA_NEAR_CLEARANCE = 0.5f;

struct Camera {
    // How high the eye can go above the focus: 65 degrees, well short of looking straight down. Below, pitch stops at 95%
    // of straight under, so the view never goes directly under.
    static constexpr float MAX_PITCH = 65.0f * PI / 180.0f;
    static constexpr float MIN_PITCH = -0.95f * (PI * 0.5f);
    static constexpr float MIN_CLEARANCE = 0.6f;   // how far above the ground the eye must stay
    static constexpr float FOCUS_HEIGHT = 0.5f;    // the focus point sits this far above the ground

    float focusX = 13.0f, focusY = 0.0f, focusZ = 13.0f;
    float yaw = 0.8f;
    float pitch = 0.6f;       // starts low, about 34 degrees above the focus, looking out across the ground
    float distance = 13.0f;   // and zoomed in
    float lift = 0.0f;   // temporary height added to the eye while it is near or under the ground
    float mapSize = 1024.0f;   // tiles per side of the loaded map. The game sets this each frame.
    float eyeClearance = 1000.0f;   // the eye's height above the ground under it, set by updateClearance
    float viewRange = 96.0f;   // entities further than this across the ground from the focus are not drawn. The game sets this each frame.

    // Eye position, including the temporary lift
    void eye(float& ex, float& ey, float& ez) const {
        ex = focusX + distance * std::sin(yaw) * std::cos(pitch);
        ey = focusY + distance * std::sin(pitch) + lift;
        ez = focusZ + distance * std::cos(yaw) * std::cos(pitch);
    }

    // The near plane for this frame (see CAMERA_NEAR_MIN). Every projection of the world, and the sky,
    // must use this one value so their depths agree.
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

    // Called once per frame. One terrain height lookup, no ray tests.
    // The lift is set to exactly what the eye needs, with no easing, so the camera
    // reaches its clear position at once and never lags behind a move.
    void updateClearance(const Terrain& t) {
        // Keep the focus half a tile above the ground under it, so the camera follows the terrain
        focusY = t.heightAt(focusX, focusZ) + FOCUS_HEIGHT;
        float ex = focusX + distance * std::sin(yaw) * std::cos(pitch);
        float ey = focusY + distance * std::sin(pitch);
        float ez = focusZ + distance * std::cos(yaw) * std::cos(pitch);

        const float ground = t.heightAt(ex, ez);
        lift = std::max(0.0f, ground + MIN_CLEARANCE - ey);
        eyeClearance = ey + lift - ground;
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
        pitch = std::clamp(pitch, MIN_PITCH, MAX_PITCH);
    }
};
