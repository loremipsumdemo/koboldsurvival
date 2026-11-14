#pragma once
#include <vector>
#include "camera.h"
#include "ecs.h"

// In regular play the camera's focus is always kept within reach of something of yours, so it can never end up free: it stays within
// CAMERA_UNIT_RANGE of any of your units (selected or not, military or citizen), within that much beyond the edge of any of your claims
// (Altar and campfires), or of the lost-focus anchor below. It is never limited to the nearest single thing, so losing one leaves the
// others already in reach.
inline constexpr float CAMERA_UNIT_RANGE = 100.0f;
// Past the edge by up to this much the camera is simply stopped (panning against the edge); further out it eases back in
inline constexpr float CAMERA_SNAP_BAND = 8.0f;
inline constexpr float CAMERA_GLIDE_RATE = 4.0f;   // how fast it eases in, per second

// What the leash remembers from frame to frame. Reset with a new or loaded game.
struct CameraLeash {
    struct Watched {
        ObjectId id = NO_OBJECT;
        float x = 0.0f, z = 0.0f;
    };
    // The selected units as of last frame, and where they were. Their positions only are kept for the anchor: no unit is ever followed
    // once it is gone, so a corpse that is deleted cannot leave the camera looking at nothing.
    std::vector<Watched> watched;
    // The lost-focus anchor: when the last selected unit dies or leaves the squad, the camera may stay where it was (the middle of that
    // tile) for as long as it needs to watch what happens. A plain point, never a unit. There is one at most; it ends once a living unit
    // is selected, or once the camera is back in reach of something else of yours.
    bool anchored = false;
    float anchorX = 0.0f, anchorZ = 0.0f;
    bool started = false, hadMilitary = false;
};

// Keeps the camera's focus leashed (call every frame in regular play). True if it moved the camera.
bool keepCameraLeashed(World& w, Camera& cam, CameraLeash& leash, float dt);

// When the squads were empty and a unit joins one, it is selected, and the anchor ends with that (call every frame in regular play)
void selectReturningMilitary(World& w, CameraLeash& leash);
