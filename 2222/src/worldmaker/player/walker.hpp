#pragma once

#include "core/math.hpp"

namespace wm {


struct Player {
    V3     pos{0.0f, 0.0f, 5.0f};   // feet position
    V3     vel{0.0f, 0.0f, 0.0f};
    float  yaw   = 0.0f;            // degrees, 0 = looking down +Z
    float  pitch = 0.0f;            // degrees, + = up
    bool   flying       = false;
    bool   grounded     = true;
    bool   airJumpUsed  = false;
    bool   prevSpace    = false;
    double lastSpaceTap = -10.0;
};

// Horizontal forward (walking).
V3 HorizontalForward(float yawDeg);

// Full look direction including pitch (flying and camera).
V3 LookDirection(float yawDeg, float pitchDeg);

// keys is the SDL keyboard state. allowInput is false while the console is open.
// groundY is the height of the ground under the player (0 when no world is loaded).
void UpdatePlayer(Player& p, const bool* keys, bool allowInput, float dt, double now, float groundY);

}  // namespace wm
