#pragma once

#include "core/math.hpp"
#include "render/gl_api.hpp"

#include <string>

namespace wm {


// A cube map loaded from six images: posx, negx, posy, negy, posz, negz (.jpg,
// .jpeg or .png) in one folder. The faces line up with the world axes: +X east,
// +Y up, +Z south in the sun's compass convention.
class Skybox {
public:
    // Reads and uploads the faces. Needs a GL context. On failure err says which
    // face was missing or unreadable, and nothing is left loaded.
    bool Load(const std::string& folder, std::string& err);
    void Unload();   // needs a GL context

    bool loaded() const { return tex_ != 0; }
    GLuint texture() const { return tex_; }

    // Average color of the four side faces around the horizon, 0 to 1 per channel.
    V3 horizon() const { return horizon_; }

private:
    GLuint tex_ = 0;
    V3 horizon_{0.5f, 0.5f, 0.5f};
};

}  // namespace wm
