#pragma once

#include <algorithm>

namespace wm {


// Values that can be changed live from the console with "set".
struct Tunables {
    float walkSpeed;
    float flySpeed;
    float sprintMult;
    float gravity;
    float jumpVelocity;
    float eyeHeight;
    float mouseSens;
    float doubleTapTime;
    float gridRadius;
    float majorEvery;
    float fov;
    float viewDistance;   // terrain draw distance, world units (default 8000)
    float lodDistance;    // closer than this uses full-detail terrain, world units
};

extern Tunables T;

// Clamps v to [lo, hi].
inline float Limit(float v, float lo, float hi) {
    return std::clamp(v, lo, hi);
}

}  // namespace wm
