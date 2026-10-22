// Floor grid drawn in the fragment shader. Replaces building and uploading
// hundreds of line vertices every frame. Lines are anti-aliased with fwidth().
#include "render/renderer.hpp"

#include <cmath>

namespace wm {


namespace {

const char* const kGridVS = R"GLSL(
#version 330 core
uniform mat4 uViewProj;
uniform float uCentreX;
uniform float uCentreZ;
uniform float uHalf;
out vec3 vWorld;
const vec2 kCorner[6] = vec2[6](
    vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),
    vec2(-1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0));
void main() {
    vec2 c = kCorner[gl_VertexID];
    vec3 p = vec3(uCentreX + c.x * uHalf, 0.0, uCentreZ + c.y * uHalf);
    vWorld = p;
    gl_Position = uViewProj * vec4(p, 1.0);
}
)GLSL";

const char* const kGridFS = R"GLSL(
#version 330 core
in vec3 vWorld;
uniform vec3 uEye;
uniform vec3 uMinor;
uniform vec3 uMajor;
uniform float uMajorEvery;
uniform vec3 uFogColour;
uniform float uFog;
out vec4 fragColour;
void main() {
    vec2 g = vWorld.xz;
    vec2 fw = max(fwidth(g), vec2(1e-4));

    // Distance to the nearest grid line, in screen pixels. Coverage is 1 on the
    // line and fades over about one pixel, so lines stay sharp at any distance.
    vec2 nearest = round(g);
    vec2 r = abs(g - nearest) / fw;
    vec2 cov = clamp(1.0 - r, 0.0, 1.0);
    float a = max(cov.x, cov.y);
    if (a < 0.01) discard;

    vec3 cx = (mod(nearest.x, uMajorEvery) < 0.5) ? uMajor : uMinor;
    vec3 cz = (mod(nearest.y, uMajorEvery) < 0.5) ? uMajor : uMinor;
    vec3 col = (cov.x >= cov.y) ? cx : cz;

    float dist = distance(g, uEye.xz);
    float fd = dist * uFog * 0.004;
    float f = clamp(1.0 - exp(-fd * fd), 0.0, 1.0);
    fragColour = vec4(col, a * (1.0 - f));
}
)GLSL";

}  // namespace

bool Renderer::InitGrid(std::string& err) {
    gridProg_ = BuildProgram(kGridVS, kGridFS, err);
    if (!gridProg_) return false;
    grid_.viewProj = glGetUniformLocation(gridProg_, "uViewProj");
    grid_.eye = glGetUniformLocation(gridProg_, "uEye");
    grid_.centreX = glGetUniformLocation(gridProg_, "uCentreX");
    grid_.centreZ = glGetUniformLocation(gridProg_, "uCentreZ");
    grid_.half = glGetUniformLocation(gridProg_, "uHalf");
    grid_.minor = glGetUniformLocation(gridProg_, "uMinor");
    grid_.major = glGetUniformLocation(gridProg_, "uMajor");
    grid_.majorEvery = glGetUniformLocation(gridProg_, "uMajorEvery");
    grid_.horizon = glGetUniformLocation(gridProg_, "uFogColour");
    grid_.fog = glGetUniformLocation(gridProg_, "uFog");
    return true;
}

void Renderer::DrawGrid(const Camera& cam, const SkyState& sky, float radius, int major, V3 minorCol, V3 majorCol) {
    // Snap the quad to whole cells so the lines stay on integer positions.
    const float cx = std::round(cam.eye.x);
    const float cz = std::round(cam.eye.z);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);

    glUseProgram(gridProg_);
    glUniformMatrix4fv(grid_.viewProj, 1, GL_FALSE, cam.viewProj.m);
    glUniform3f(grid_.eye, cam.eye.x, cam.eye.y, cam.eye.z);
    glUniform1f(grid_.centreX, cx);
    glUniform1f(grid_.centreZ, cz);
    glUniform1f(grid_.half, radius);
    glUniform3f(grid_.minor, minorCol.x, minorCol.y, minorCol.z);
    glUniform3f(grid_.major, majorCol.x, majorCol.y, majorCol.z);
    glUniform1f(grid_.majorEvery, static_cast<float>(major < 1 ? 1 : major));
    glUniform3f(grid_.horizon, sky.fogColour.x, sky.fogColour.y, sky.fogColour.z);
    glUniform1f(grid_.fog, sky.fog);

    glBindVertexArray(emptyVao_);
    ++gStats.drawCalls;
    glDrawArrays(GL_TRIANGLES, 0, 6);

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

}  // namespace wm
