#include "player/walker.hpp"

#include <SDL3/SDL.h>

#include <cmath>

#include "core/tunables.hpp"

namespace wm {


V3 HorizontalForward(float yawDeg) {
    const float r = yawDeg * kDeg2Rad;
    return {std::sin(r), 0.0f, std::cos(r)};
}

V3 LookDirection(float yawDeg, float pitchDeg) {
    const float y = yawDeg * kDeg2Rad;
    const float p = pitchDeg * kDeg2Rad;
    return {std::sin(y) * std::cos(p), std::sin(p), std::cos(y) * std::cos(p)};
}

void UpdatePlayer(Player& p, const bool* keys, bool allowInput, float dt, double now, float groundY) {
    float ix = 0.0f, iy = 0.0f, iz = 0.0f;
    bool sprint = false, space = false;

    if (allowInput) {
        if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP])    iz += 1.0f;
        if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN])  iz -= 1.0f;
        if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) ix += 1.0f;
        if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT])  ix -= 1.0f;
        if (keys[SDL_SCANCODE_E]) iy += 1.0f;   // up, while flying
        if (keys[SDL_SCANCODE_Q]) iy -= 1.0f;   // down, while flying
        sprint = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
        space  = keys[SDL_SCANCODE_SPACE];
    }

    // Q and E only count while flying, so they do not change walking speed.
    if (!p.flying) iy = 0.0f;
    const float len = std::sqrt(ix * ix + iy * iy + iz * iz);
    if (len > 0.0f) { ix /= len; iy /= len; iz /= len; }

    const float baseSpeed = p.flying ? T.flySpeed : T.walkSpeed;
    const float speed = baseSpeed * (sprint ? T.sprintMult : 1.0f);

    // Space: single tap = jump / double jump, double tap = toggle fly.
    const bool spacePressed = space && !p.prevSpace;
    p.prevSpace = space;

    if (spacePressed) {
        if (now - p.lastSpaceTap < T.doubleTapTime) {
            p.flying       = !p.flying;
            p.vel          = {0.0f, 0.0f, 0.0f};
            p.airJumpUsed  = false;
            p.lastSpaceTap = -10.0;
        } else {
            p.lastSpaceTap = now;
            if (!p.flying) {
                if (p.grounded) {
                    p.vel.y = T.jumpVelocity;
                } else if (!p.airJumpUsed) {
                    p.vel.y = T.jumpVelocity;
                    p.airJumpUsed = true;
                }
            }
        }
    }

    const V3 up{0.0f, 1.0f, 0.0f};

    if (p.flying) {
        // Fly: W/S along the full camera direction, A/D strafe, Q/E down and up.
        const V3 look  = LookDirection(p.yaw, p.pitch);
        const V3 right = Normalize(Cross(look, up));
        const V3 move  = look * iz + right * ix + up * iy;
        p.pos += move * (speed * dt);
        p.vel = {0.0f, 0.0f, 0.0f};
    } else {
        // Walk: horizontal movement relative to yaw, plus gravity.
        const V3 fwd   = HorizontalForward(p.yaw);
        const V3 right = Cross(fwd, up);
        const V3 wish  = fwd * iz + right * ix;
        p.pos.x += wish.x * speed * dt;
        p.pos.z += wish.z * speed * dt;

        p.vel.y += T.gravity * dt;
        p.pos.y += p.vel.y * dt;
    }

    // Floor: the ground height under the player.
    if (p.pos.y <= groundY) {
        p.pos.y = groundY;
        if (p.vel.y < 0.0f) p.vel.y = 0.0f;
    }
    p.grounded = (p.pos.y <= groundY);
    if (p.grounded) p.airJumpUsed = false;
}

}  // namespace wm
