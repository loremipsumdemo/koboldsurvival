#include "renderer.h"
#include "frustum.h"
#include "surface_tiles.h"
#include "look.h"
#include "gl_util.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
const char* const kGroundVS = R"GLSL(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNrm;
layout(location = 2) in vec3 aCol;
uniform mat4 uViewProj;
uniform vec3 uEye;
uniform vec2 uOrigin;
out vec3 vNrm;
out vec3 vCol;
out float vDist;
out vec2 vXZ;
out vec3 vPos;
void main() {
    gl_Position = uViewProj * vec4(aPos, 1.0);
    vNrm = aNrm;
    vCol = aCol;
    vDist = distance(aPos, uEye);
    // Measured from a point near the camera, so the numbers stay small however far out on a big map it is
    vXZ = aPos.xz - uOrigin;
    vPos = aPos;
}
)GLSL";

// What the ground, the water and the clouds share: the noise picture, the varied look, and the clouds' shadows.
// Positions here are measured from uOrigin (a point near the camera), and every lookup has its shift worked out on the CPU
// in doubles, so nothing drifts or swims however big the map is.
const char* const kSurfaceCommon = R"GLSL(
uniform sampler2D uNoise;
uniform int uVaried;          // 1: tiles pushed by the slow macro noise, and the slow changes across the map
uniform int uSalt;            // from the world's seed
uniform vec3 uMacro;          // xy: shift, z: 1 / the size the slow changes repeat at
uniform float uCloudShadow;   // 0: no cloud shadows
uniform vec3 uShadowSun;      // towards the sun
uniform vec4 uCloudShift;     // xy: the lower layer's shift, zw: the upper's (they drift with the wind)
uniform vec2 uCloudScale;     // the size each layer repeats at
uniform vec2 uCloudHeight;
uniform vec2 uCloudOpacity;
uniform float uCloudCover;    // the noise above this is cloud
// Past this distance from the eye the ground's tile detail has faded out and isn't looked up at all
uniform float uDetailEnd;

// Whole-number hashing, so a cell gets the same value wherever it is looked up from
vec3 cellRandom(ivec2 c, int salt) {
    uvec3 v = uvec3(uint(c.x), uint(c.y), uint(salt)) * 1664525u + 1013904223u;
    v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
    v ^= v >> 16u;
    v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
    return vec3(v >> 8u) * (1.0 / 16777216.0);
}

// A tile texel as a normal tilted in the world's x and z (back turns it from the tile's way round), its z, and its height
vec4 decodeTile(vec4 t, mat2 back) {
    return vec4(back * (t.rg * 2.0 - 1.0), t.b * 2.0 - 1.0, t.a);
}

// How far, in world units, the varied look pushes the tile from the plain grid, by the slow macro noise (see the surfaces)
const float VARY_WARP = 4.0;

vec4 macroAt(vec2 p) { return texture(uNoise, p * uMacro.z + uMacro.xy); }

float cloudAmount(float n) { return smoothstep(uCloudCover, uCloudCover + 0.18, n); }

// 1 in full sun, less under a cloud: the point on each layer between here and the sun
float cloudShade(vec2 p, float y) {
    if (uCloudShadow <= 0.0) return 1.0;
    vec2 slope = uShadowSun.xz / max(uShadowSun.y, 0.2);
    vec2 at0 = p + slope * (uCloudHeight.x - y);
    vec2 at1 = p + slope * (uCloudHeight.y - y);
    float c0 = cloudAmount(texture(uNoise, at0 / uCloudScale.x + uCloudShift.xy).r) * uCloudOpacity.x;
    float c1 = cloudAmount(texture(uNoise, at1 / uCloudScale.y + uCloudShift.zw).g) * uCloudOpacity.y;
    return 1.0 - uCloudShadow * (1.0 - (1.0 - c0) * (1.0 - c1));
}
)GLSL";

const char* const kGroundFS = R"GLSL(
uniform sampler2D uTile;
uniform vec2 uTileShift;      // tiles in a plain grid: where the grid falls
uniform float uTileSize;      // world units one tile covers
uniform float uSeaY;
uniform float uDepthScale;
uniform vec3 uRockRgb;
uniform float uRockSlope;
uniform vec3 uSunDir;
uniform vec3 uSunLight;
uniform vec3 uAmbient;
uniform vec3 uUnderwater;
uniform vec3 uFogColor;
uniform float uFog;
uniform float uVoidCut;
uniform vec3 uGroundTint;
uniform float uTintAmount;
uniform vec3 uEye;
in vec3 vNrm;
in vec3 vCol;
in float vDist;
in vec2 vXZ;
in vec3 vPos;
out vec4 fragColor;
void main() {
    // Over a void, ground far below sea level isn't there. Where it stops wanders up and down smoothly, like a broken-off
    // underside, and the last pixel is blended into what is behind so the edge doesn't stair-step.
    vec2 dx = dFdx(vXZ), dy = dFdy(vXZ);
    float dYx = dFdx(vPos.y), dYy = dFdy(vPos.y);
    float cover = 1.0;
    float cutY = -1.0e9;
    if (uVoidCut > -1.0e5) {
        // 64 and 16 divide the 64-tile step uOrigin moves in, so the line doesn't jump when it moves
        float wide = texture(uNoise, vXZ / 64.0).b;
        float fine = texture(uNoise, vXZ / 16.0 + vec2(0.37, 0.61)).a;
        cutY = uVoidCut - 9.0 * wide - 4.0 * fine;
        float above = vPos.y - cutY;
        cover = clamp(above / max(fwidth(above), 1e-4) + 0.5, 0.0, 1.0);
        if (cover <= 0.0) discard;
    }
    // The fine grain fades out with distance so far ground does not shimmer.
    float grain = 1.0 - smoothstep(40.0, 220.0, vDist);

    // The face's own slope (the vertex normals are smoothed over several tiles, so a one-tile-wide wall gets an up-facing
    // one). Walls take the tile from the side rather than from above, so it isn't smeared down them.
    vec3 face = normalize(cross(dFdx(vPos), dFdy(vPos)));
    if (dot(face, uEye - vPos) < 0.0) face = -face;
    vec3 aw = pow(abs(face), vec3(4.0));
    aw /= aw.x + aw.y + aw.z;
    float side = 1.0 - aw.y;

    // Ground detail from the tile: its normal tilts the lighting, and its height darkens the color a little
    vec4 t = vec4(0.0);
    float bumpBy = 0.35, shadeBy = 1.0;
    // Past uDetailEnd the grain is 0, so nothing the tile gives is seen and it isn't looked up
    bool detailed = vDist < uDetailEnd;
    // The slow macro noise, read once: it pushes the tile a little (so no stretch of ground repeats the same way) and
    // shades the ground
    vec4 m = uVaried == 1 ? macroAt(vXZ) : vec4(0.5);
    vec2 warp = uVaried == 1 ? (m.rg - 0.5) * 2.0 * VARY_WARP : vec2(0.0);
    if (aw.y > 0.02 && detailed) {
        t = decodeTile(textureGrad(uTile, (vXZ + warp) / uTileSize + uTileShift, dx / uTileSize, dy / uTileSize), mat2(1.0));
        t.xyw *= aw.y;
    }
    vec3 bump = vec3(t.x, 0.0, t.y);
    if (side > 0.02 && detailed) {
        // Facing along z: the tile laid on x and y. Facing along x: on z and y. Its tilt goes along the wall and up it.
        float vy = vPos.y / uTileSize;
        vec4 tz = decodeTile(textureGrad(uTile, vec2((vXZ.x + warp.x) / uTileSize + uTileShift.x, vy),
                                         vec2(dx.x, dYx) / uTileSize, vec2(dy.x, dYy) / uTileSize), mat2(1.0));
        vec4 tx = decodeTile(textureGrad(uTile, vec2((vXZ.y + warp.y) / uTileSize + uTileShift.y, vy + 0.5),
                                         vec2(dx.y, dYx) / uTileSize, vec2(dy.y, dYy) / uTileSize), mat2(1.0));
        bump += vec3(tz.x, tz.y, 0.0) * aw.z + vec3(0.0, tx.y, tx.x) * aw.x;
        t.w += tz.w * aw.z + tx.w * aw.x;
    }
    if (uVaried == 1) {
        // Slow changes across the map: some ground a little lighter or darker, some rougher or smoother
        shadeBy = mix(0.86, 1.12, m.b);
        bumpBy *= mix(0.55, 1.35, m.a);
    }
    bump *= grain;
    // On walls the face's own slope lights them, so they shade by which way they face
    vec3 nBase = normalize(mix(normalize(vNrm), face, smoothstep(0.5, 0.9, side)));
    vec3 n = normalize(nBase + bump * bumpBy);
    float diff = max(dot(n, normalize(uSunDir)), 0.0) * cloudShade(vXZ, vPos.y);

    // Steep faces turn to bare rock.
    float steep = max(1.0 - clamp(normalize(vNrm).y, 0.0, 1.0), smoothstep(0.6, 0.9, side));
    float rock = smoothstep(uRockSlope, uRockSlope + 0.15, steep);
    vec3 base = mix(vCol, uRockRgb, rock);
    float grey = dot(vCol, vec3(0.333));
    base = mix(base, mix(grey * uGroundTint * 2.0, uRockRgb, rock), uTintAmount);
    // Steep faces take more of the sky's light, so they read as rock rather than black.
    vec3 lit = base * shadeBy * mix(1.0, 0.85 + 0.3 * t.w, grain) * (uAmbient * (1.0 + 0.8 * steep) + uSunLight * diff);

    // Under the sea, the water between the eye and the ground absorbs light and tints
    // it. The deeper the ground, the more of the water it has been seen through.
    float depth = max(uSeaY - vPos.y, 0.0) * uDepthScale;
    float absorb = 1.0 - exp(-depth * 0.25);
    vec3 underwater = uUnderwater * (uAmbient + uSunLight * 0.5);
    lit = mix(lit, underwater, absorb);
    // A void wall darkens towards where it stops, so the broken edge is a dim fringe rather than a hard line
    lit *= mix(0.18, 1.0, smoothstep(cutY, cutY + 10.0, vPos.y));

    float d = vDist * uFog * 0.004;
    float f = clamp(1.0 - exp(-d * d), 0.0, 1.0);
    fragColor = vec4(mix(lit, uFogColor, f), cover);
}
)GLSL";

const char* const kSeaVS = R"GLSL(
#version 330 core
uniform mat4 uViewProj;
uniform vec3 uCenter;
uniform float uHalf;
out vec3 vWorld;
const vec2 kCorner[6] = vec2[6](
    vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),
    vec2(-1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0));
void main() {
    vec2 c = kCorner[gl_VertexID];
    vec3 p = vec3(uCenter.x + c.x * uHalf, uCenter.y, uCenter.z + c.y * uHalf);
    vWorld = p;
    gl_Position = uViewProj * vec4(p, 1.0);
}
)GLSL";

const char* const kSeaFS = R"GLSL(
in vec3 vWorld;
uniform vec2 uOrigin;
uniform vec4 uTileShift;      // tiles in a plain grid: where each layer's grid falls
uniform vec2 uTileSizes;      // world units one tile of each layer covers
uniform vec3 uEye;
uniform vec3 uSunDir;
uniform vec3 uSunLight;
uniform vec3 uAmbient;
uniform vec3 uColor;
uniform vec3 uFogColor;
uniform float uFog;
uniform float uTime;
uniform float uOpacity;
uniform float uPattern;
uniform float uSpeed;
uniform float uFadeStart;
uniform float uFadeEnd;
uniform float uGlitter;
uniform float uGlow;
uniform float uMapSize;
uniform vec2 uFar;            // from x to y from the eye the ripples even out into the tile's average, and past y aren't looked up
uniform sampler2D uTile;
out vec4 fragColor;
void main() {
    if (uMapSize > 0.0 && (vWorld.x < 0.0 || vWorld.z < 0.0 || vWorld.x > uMapSize || vWorld.z > uMapSize)) discard;
    // Two drifting copies of the tile, at different scales and directions. Nothing here reflects the sky: the light only
    // changes the shading and the bright caustic bands.
    vec2 local = vWorld.xz - uOrigin;
    vec2 p = vWorld.xz;
    float tm = uTime * uSpeed;
    vec2 drift1 = vec2(tm * 0.20, tm * 0.13), drift2 = -vec2(tm * 0.102, tm * 0.15);
    vec2 dx = dFdx(local), dy = dFdy(local);
    float dist = distance(vWorld, uEye);
    // The varied look: two layers, each read once from the plain grid and pushed by the macro noise, the second the other
    // way. Far off, where a whole tile falls in a pixel or two, the ripples are the tile's average (its smallest mip).
    vec4 m = macroAt(local);
    float tone = uVaried == 1 ? mix(0.92, 1.08, m.b) : 1.0;
    vec2 warp = uVaried == 1 ? (m.rg - 0.5) * 2.0 * VARY_WARP : vec2(0.0);
    float evened = smoothstep(uFar.x, uFar.y, dist);
    vec4 t = vec4(0.0);
    if (evened < 1.0) {
        vec4 t1 = decodeTile(textureGrad(uTile, (local + drift1 + warp) / uTileSizes.x + uTileShift.xy, dx / uTileSizes.x, dy / uTileSizes.x), mat2(1.0));
        vec4 t2 = decodeTile(textureGrad(uTile, (local + drift2 - warp) / uTileSizes.y + uTileShift.zw, dx / uTileSizes.y, dy / uTileSizes.y), mat2(1.0));
        t = mix(t1, t2, 0.5);
    }
    if (evened > 0.0) t = mix(t, decodeTile(textureLod(uTile, vec2(0.5), 16.0), mat2(1.0)), evened);
    vec3 n = normalize(vec3(t.x * 1.4, 1.0, t.y * 1.4));
    vec3 sun = normalize(uSunDir);
    float shade = cloudShade(local, vWorld.y);
    float ndl = max(dot(n, sun), 0.0) * shade;

    // Caustic-like light play: thin bright bands along the height ridges, brightest
    // when the sun is high.
    float ridge = 1.0 - abs(2.0 * t.w - 1.0);
    float caustic = pow(ridge, 4.0) * max(sun.y, 0.0) * uPattern * shade;

    vec3 base = uColor * tone * (0.85 + 0.3 * t.w);
    vec3 lit = base * (uAmbient + uSunLight * ndl * 0.6) + uSunLight * caustic * 0.35;

    // Fairly see-through, a little more opaque where the ripples are high.
    float alpha = clamp((0.42 + 0.10 * t.w) * uOpacity, 0.0, 1.0);

    // Sun glint: a bright spot where the sun's reflection points at the eye. Only the
    // sun is reflected, never the sky, so the surface stays non-reflective.
    vec3 viewDir = normalize(uEye - vWorld);
    vec3 halfV = normalize(sun + viewDir);
    float glint = pow(max(dot(n, halfV), 0.0), 180.0) * max(sun.y, 0.0) * shade;
    lit += uSunLight * glint * 2.0 * uPattern;

    // Glow: the water gives its own light. Glitter: small points that flash at their own times, none of it reflecting anything.
    lit = mix(lit, base, uGlow);
    alpha = mix(alpha, 1.0, uGlow * 0.5);
    if (uGlitter > 0.0) {
        float near = 1.0 - smoothstep(60.0, 420.0, dist);
        for (int i = 0; i < 2; ++i) {
            float size = i == 0 ? 0.55 : 1.3;
            vec2 at = (p + (i == 0 ? vec2(tm * 0.3, tm * 0.2) : vec2(-tm * 0.2, tm * 0.25))) / size;
            vec2 cell = floor(at);
            float h = cellRandom(ivec2(cell), 11 + i).x;
            float round_ = smoothstep(0.5, 0.05, length(at - cell - 0.5));
            float flash = pow(0.5 + 0.5 * sin(uTime * (1.5 + 3.0 * h) + h * 300.0), 10.0);
            float sparkle = step(1.0 - 0.22 * uGlitter, h) * round_ * flash * mix(0.35, 1.0, near);
            lit += mix(uColor, vec3(1.0), 0.6) * sparkle * 2.2;
            alpha = max(alpha, sparkle);
        }
    }

    float d = dist * uFog * 0.004;
    float f = clamp(1.0 - exp(-d * d), 0.0, 1.0);

    // Far out the sea fades into the horizon color and becomes opaque, so the edge
    // of the drawn water is never visible and the sea looks endless.
    float far = smoothstep(uFadeStart, uFadeEnd, dist);
    vec3 col = mix(mix(lit, uFogColor, f), uFogColor, far);
    fragColor = vec4(col, mix(alpha, 1.0, far));
}
)GLSL";

// One layer of cloud: a flat sheet high over the map, drawn from the same noise as its shadows below
const char* const kCloudFS = R"GLSL(
in vec3 vWorld;
uniform vec2 uOrigin;
uniform vec3 uEye;
uniform int uLayer;           // 0 the lower layer (the noise's red), 1 the upper (green)
uniform vec3 uLit;            // the cloud's color in the light
uniform vec3 uFogColor;
uniform float uFog;
uniform float uFadeStart;     // from here out the clouds even out into a haze of their average cover...
uniform float uFadeEnd;
uniform float uEdge;          // ...and from here they thin away, so the sheet's edge never shows against the sky
out vec4 fragColor;
void main() {
    vec2 p = vWorld.xz - uOrigin;
    vec4 nz = texture(uNoise, p / uCloudScale[uLayer] + (uLayer == 0 ? uCloudShift.xy : uCloudShift.zw));
    float n = uLayer == 0 ? nz.r : nz.g;
    // The soft edge is never narrower than a pixel, so it can't crawl or stair-step
    float soft = max(0.18, 1.5 * fwidth(n));
    float amount = smoothstep(uCloudCover, uCloudCover + soft, n);
    // Thicker cloud is darker underneath
    float thick = smoothstep(uCloudCover, 1.0, n);
    vec3 col = uLit * mix(1.0, 0.62, thick);

    float dist = distance(vWorld, uEye);
    float flat_ = distance(vWorld.xz, uEye.xz);
    // Far off, many clouds fall in one pixel: they become an even haze of how much of the sky they cover
    amount = mix(amount, 0.30, smoothstep(uFadeStart, uFadeEnd, dist));
    amount *= 1.0 - smoothstep(uEdge * 0.55, uEdge, flat_);
    // Near the sheet itself (a high camera), it fades, so it never hides the ground
    amount *= smoothstep(15.0, 80.0, abs(vWorld.y - uEye.y));
    amount *= uCloudOpacity[uLayer];
    if (amount <= 0.002) discard;

    float d = dist * uFog * 0.004;
    float f = clamp(1.0 - exp(-d * d), 0.0, 1.0);
    fragColor = vec4(mix(col, uFogColor, f), amount);
}
)GLSL";

const char* const kFloorFS = R"GLSL(
#version 330 core
in vec3 vWorld;
uniform vec3 uEye;
uniform vec3 uAmbient;
uniform vec3 uSunLight;
uniform vec3 uUnderwater;
uniform vec3 uFogColor;
uniform float uFog;
uniform float uMapSize;
out vec4 fragColor;
void main() {
    if (uMapSize > 0.0 && (vWorld.x < 0.0 || vWorld.z < 0.0 || vWorld.x > uMapSize || vWorld.z > uMapSize)) discard;
    vec3 lit = uUnderwater * (uAmbient + uSunLight * 0.5);
    float d = distance(vWorld, uEye) * uFog * 0.004;
    float f = clamp(1.0 - exp(-d * d), 0.0, 1.0);
    fragColor = vec4(mix(lit, uFogColor, f), 1.0);
}
)GLSL";

constexpr float kWaterDrop = 0.05f;
constexpr float kWaterBiasSlope = 0.0f;
constexpr float kWaterBiasUnits = 4.0f;
constexpr float kFarPlane = CAMERA_FAR;
constexpr int MAX_BUILDS_PER_FRAME = 8;

bool chunkVisible(const FrustumPlanes& f, const ChunkBounds& b) {
    for (const auto& p : f.p) {
        const float px = p[0] >= 0.0f ? b.x1 : b.x0;
        const float py = p[1] >= 0.0f ? b.maxY : b.minY;
        const float pz = p[2] >= 0.0f ? b.z1 : b.z0;
        if (p[0] * px + p[1] * py + p[2] * pz + p[3] < 0.0f) return false;
    }
    return true;
}

// How the surfaces are laid, in world units
constexpr double GROUND_TILE = 8.0;
constexpr double WATER_TILE_1 = 10.0;
constexpr double WATER_TILE_2 = 6.0;
constexpr float GROUND_DETAIL_END = 220.0f;   // where kGroundFS's grain reaches 0
constexpr float WATER_EVEN_START = 600.0f, WATER_EVEN_END = 900.0f;
constexpr float NO_BAND = 1.0e9f;
constexpr double MACRO_SIZE = 173.0;      // the slow changes across the ground repeat this far apart
constexpr double ORIGIN_STEP = 64.0;      // the point positions are measured from moves with the camera in steps this big
// The two layers of cloud: how far each repeats, how high it floats, which way and how fast (world units a second) the
// wind takes it, and how solid it is
constexpr double CLOUD_SCALE[2] = { 1100.0, 1700.0 };
constexpr float CLOUD_HEIGHT[2] = { 260.0f, 380.0f };
constexpr double CLOUD_WIND[2][2] = { { 3.2, 1.4 }, { 5.5, -2.3 } };
constexpr float CLOUD_OPACITY[2] = { 0.85f, 0.55f };
constexpr float CLOUD_COVER = 0.52f;
constexpr float CLOUD_SHADOW = 0.45f;     // how much of the sun a full cloud takes

GLuint makeSurfaceTexture(const std::vector<uint8_t>& pixels, int side) {
    const GLuint tex = makeTexture2D(side, side, GL_RGBA8, GL_RGBA, pixels.data(), GL_LINEAR, GL_REPEAT);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    // Ground and clouds seen at a low angle stay sharp without shimmering, where the card can
    if (GLEW_EXT_texture_filter_anisotropic) {
        GLfloat most = 1.0f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &most);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, std::min(most, 4.0f));
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

float SecondsSinceStart() {
    static const auto start = std::chrono::steady_clock::now();
    return std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
}

double SecondsSinceStartExact() {
    static const auto start = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

// Where a lookup that repeats every size falls, from the origin: only the part past whole repeats, so it stays small
float wrapShift(double v, double size) {
    const double r = v / size;
    return (float)(r - std::floor(r));
}

std::string withCommon(const char* body) {
    return std::string("#version 330 core\n") + kSurfaceCommon + body;
}
}

void Renderer::SurfaceUniforms::find(GLuint prog) {
    origin = glGetUniformLocation(prog, "uOrigin");
    noise = glGetUniformLocation(prog, "uNoise");
    varied = glGetUniformLocation(prog, "uVaried");
    salt = glGetUniformLocation(prog, "uSalt");
    macro = glGetUniformLocation(prog, "uMacro");
    cloudShadow = glGetUniformLocation(prog, "uCloudShadow");
    shadowSun = glGetUniformLocation(prog, "uShadowSun");
    cloudShift = glGetUniformLocation(prog, "uCloudShift");
    cloudScale = glGetUniformLocation(prog, "uCloudScale");
    cloudHeight = glGetUniformLocation(prog, "uCloudHeight");
    cloudOpacity = glGetUniformLocation(prog, "uCloudOpacity");
    cloudCover = glGetUniformLocation(prog, "uCloudCover");
    bands = glGetUniformLocation(prog, "uDetailEnd");
}

// Binds the noise to texture unit 1 and leaves unit 0 active
void Renderer::setSurfaceUniforms(const SurfaceUniforms& u) const {
    const SurfaceFrame& F = surfaceFrame_;
    glUniform2f(u.origin, (float)F.originX, (float)F.originZ);
    glUniform1i(u.noise, 1);
    glUniform1i(u.varied, variedSurfaces_ ? 1 : 0);
    glUniform1i(u.salt, (int)(surfaceSeed_ & 0x7FFFFFFFu));
    glUniform3fv(u.macro, 1, F.macro);
    glUniform1f(u.cloudShadow, F.cloudShadow);
    glUniform3fv(u.shadowSun, 1, F.sun);
    glUniform4fv(u.cloudShift, 1, F.cloudShift);
    glUniform2fv(u.cloudScale, 1, F.cloudScale);
    glUniform2fv(u.cloudHeight, 1, F.cloudHeight);
    glUniform2fv(u.cloudOpacity, 1, F.cloudOpacity);
    glUniform1f(u.cloudCover, F.cloudCover);
    glUniform1f(u.bands, surfaceBands_ ? GROUND_DETAIL_END : NO_BAND);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, noiseTex_);
    glActiveTexture(GL_TEXTURE0);
}

void Renderer::updateSurfaceFrame(const Camera& cam, const SkyLight& sky) {
    SurfaceFrame& F = surfaceFrame_;
    float ex, ey, ez;
    cam.eye(ex, ey, ez);
    const double ox = std::floor((double)ex / ORIGIN_STEP) * ORIGIN_STEP;
    const double oz = std::floor((double)ez / ORIGIN_STEP) * ORIGIN_STEP;
    F.originX = ox;
    F.originZ = oz;
    F.groundShift[0] = wrapShift(ox, GROUND_TILE);
    F.groundShift[1] = wrapShift(oz, GROUND_TILE);
    F.waterShift[0] = wrapShift(ox, WATER_TILE_1);
    F.waterShift[1] = wrapShift(oz, WATER_TILE_1);
    F.waterShift[2] = wrapShift(ox, WATER_TILE_2);
    F.waterShift[3] = wrapShift(oz, WATER_TILE_2);
    F.macro[0] = wrapShift(ox, MACRO_SIZE);
    F.macro[1] = wrapShift(oz, MACRO_SIZE);
    F.macro[2] = (float)(1.0 / MACRO_SIZE);

    const double now = SecondsSinceStartExact();
    for (int k = 0; k < 2; ++k) {
        F.cloudShift[2 * k] = wrapShift(ox - CLOUD_WIND[k][0] * now, CLOUD_SCALE[k]);
        F.cloudShift[2 * k + 1] = wrapShift(oz - CLOUD_WIND[k][1] * now, CLOUD_SCALE[k]);
        F.cloudScale[k] = (float)CLOUD_SCALE[k];
        F.cloudHeight[k] = CLOUD_HEIGHT[k];
        F.cloudOpacity[k] = CLOUD_OPACITY[k];
    }
    F.cloudCover = CLOUD_COVER;
    const bool themeClouds = !hasTheme_ || theme_.cloudsShown != 0;
    F.clouds = cloudsOn_ && themeClouds && noiseTex_ != 0;
    const float sunUp = std::clamp((sky.sunDir.y - 0.05f) / 0.30f, 0.0f, 1.0f);
    F.cloudShadow = cloudShadowsOn_ && themeClouds && noiseTex_ != 0 ? CLOUD_SHADOW * sunUp * sunUp * (3.0f - 2.0f * sunUp) : 0.0f;
    F.sun[0] = sky.sunDir.x;
    F.sun[1] = sky.sunDir.y;
    F.sun[2] = sky.sunDir.z;
    const float up = std::max(sky.sunDir.y, 0.0f);
    const float lit[3] = { sky.ambientLight.x * 1.4f + sky.sunLight.x * 0.85f * up + 0.04f,
                           sky.ambientLight.y * 1.4f + sky.sunLight.y * 0.85f * up + 0.04f,
                           sky.ambientLight.z * 1.4f + sky.sunLight.z * 0.85f * up + 0.04f };
    for (int i = 0; i < 3; ++i) F.cloudLit[i] = std::min(lit[i], 1.0f);
    F.fogColor[0] = sky.fogColor.x;
    F.fogColor[1] = sky.fogColor.y;
    F.fogColor[2] = sky.fogColor.z;
    F.fog = sky.fog;
}

void Renderer::drawClouds(const Camera& cam) {
    const SurfaceFrame& F = surfaceFrame_;
    if (!F.clouds || !cloudShader_.prog) return;
    float ex, ey, ez;
    cam.eye(ex, ey, ez);
    passTimerBegin(cloudTimer_);
    glUseProgram(cloudShader_.prog);
    setSurfaceUniforms(cloudShader_.common);
    glUniformMatrix4fv(cloudShader_.viewProj, 1, GL_FALSE, viewProj_.m);
    glUniform1f(cloudShader_.half, kFarPlane * 1.2f);
    glUniform3f(cloudShader_.eye, ex, ey, ez);
    glUniform3fv(cloudShader_.lit, 1, F.cloudLit);
    glUniform3fv(cloudShader_.fogColor, 1, F.fogColor);
    glUniform1f(cloudShader_.fog, F.fog);
    glUniform1f(cloudShader_.fadeStart, 1500.0f);
    glUniform1f(cloudShader_.fadeEnd, 4500.0f);
    glUniform1f(cloudShader_.edge, kFarPlane * 1.15f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glBindVertexArray(emptyVao_);
    // The upper layer first: from below, it is the farther one
    for (int layer = 1; layer >= 0; --layer) {
        glUniform1i(cloudShader_.layer, layer);
        glUniform3f(cloudShader_.center, ex, F.cloudHeight[layer], ez);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    passTimerEnd(cloudTimer_);
}

// Times a pass on the graphics card. The answer comes a frame or more later, so a pass is only timed again once its last
// time has been read: nothing here ever waits for the card.
void Renderer::passTimerBegin(PassTimer& t) {
    if (!passTimersOn_) return;
    if (!t.query) glGenQueries(1, &t.query);
    if (t.waiting) {
        GLint ready = 0;
        glGetQueryObjectiv(t.query, GL_QUERY_RESULT_AVAILABLE, &ready);
        if (!ready) return;
        GLuint64 ns = 0;
        glGetQueryObjectui64v(t.query, GL_QUERY_RESULT, &ns);
        const float ms = (float)((double)ns / 1.0e6);
        t.ms = t.ms <= 0.0f ? ms : t.ms + (ms - t.ms) * 0.1f;
        t.waiting = false;
    }
    glBeginQuery(GL_TIME_ELAPSED, t.query);
    t.running = true;
}

void Renderer::passTimerEnd(PassTimer& t) {
    if (!t.running) return;
    glEndQuery(GL_TIME_ELAPSED);
    t.running = false;
    t.waiting = true;
}

std::string Renderer::surfaceBandsText() const {
    char text[160];
    if (!surfaceBands_) return "distance bands off (detail at every distance)";
    const float range = (float)std::max(detailRange_, 0);
    (void)range;
    std::snprintf(text, sizeof text, "varied ground and water: one lookup each, ground detail to %.0f, water ripples even out %.0f-%.0f",
                  GROUND_DETAIL_END, WATER_EVEN_START, WATER_EVEN_END);
    return text;
}

bool Renderer::initTerrainGl() {
    groundShader_.prog = linkProgram("terrain", kGroundVS, withCommon(kGroundFS).c_str());
    if (!groundShader_.prog) return false;
    GLuint g = groundShader_.prog;
    groundShader_.common.find(g);
    groundShader_.tileShift = glGetUniformLocation(g, "uTileShift");
    groundShader_.tileSize = glGetUniformLocation(g, "uTileSize");
    groundShader_.viewProj = glGetUniformLocation(g, "uViewProj");
    groundShader_.eye = glGetUniformLocation(g, "uEye");
    groundShader_.seaY = glGetUniformLocation(g, "uSeaY");
    groundShader_.depthScale = glGetUniformLocation(g, "uDepthScale");
    groundShader_.rockRgb = glGetUniformLocation(g, "uRockRgb");
    groundShader_.rockSlope = glGetUniformLocation(g, "uRockSlope");
    groundShader_.sunDir = glGetUniformLocation(g, "uSunDir");
    groundShader_.sunLight = glGetUniformLocation(g, "uSunLight");
    groundShader_.ambient = glGetUniformLocation(g, "uAmbient");
    groundShader_.underwater = glGetUniformLocation(g, "uUnderwater");
    groundShader_.fogColor = glGetUniformLocation(g, "uFogColor");
    groundShader_.fog = glGetUniformLocation(g, "uFog");
    groundShader_.tile = glGetUniformLocation(g, "uTile");
    groundShader_.voidCut = glGetUniformLocation(g, "uVoidCut");
    groundShader_.groundTint = glGetUniformLocation(g, "uGroundTint");
    groundShader_.tintAmount = glGetUniformLocation(g, "uTintAmount");

    seaShader_.prog = linkProgram("sea", kSeaVS, withCommon(kSeaFS).c_str());
    if (!seaShader_.prog) return false;
    GLuint s = seaShader_.prog;
    seaShader_.common.find(s);
    seaShader_.tileShift = glGetUniformLocation(s, "uTileShift");
    seaShader_.tileSizes = glGetUniformLocation(s, "uTileSizes");
    seaShader_.evenOut = glGetUniformLocation(s, "uFar");
    seaShader_.viewProj = glGetUniformLocation(s, "uViewProj");
    seaShader_.center = glGetUniformLocation(s, "uCenter");
    seaShader_.half = glGetUniformLocation(s, "uHalf");
    seaShader_.eye = glGetUniformLocation(s, "uEye");
    seaShader_.sunDir = glGetUniformLocation(s, "uSunDir");
    seaShader_.sunLight = glGetUniformLocation(s, "uSunLight");
    seaShader_.ambient = glGetUniformLocation(s, "uAmbient");
    seaShader_.color = glGetUniformLocation(s, "uColor");
    seaShader_.fogColor = glGetUniformLocation(s, "uFogColor");
    seaShader_.fog = glGetUniformLocation(s, "uFog");
    seaShader_.time = glGetUniformLocation(s, "uTime");
    seaShader_.opacity = glGetUniformLocation(s, "uOpacity");
    seaShader_.pattern = glGetUniformLocation(s, "uPattern");
    seaShader_.speed = glGetUniformLocation(s, "uSpeed");
    seaShader_.fadeStart = glGetUniformLocation(s, "uFadeStart");
    seaShader_.fadeEnd = glGetUniformLocation(s, "uFadeEnd");
    seaShader_.tile = glGetUniformLocation(s, "uTile");
    seaShader_.glitter = glGetUniformLocation(s, "uGlitter");
    seaShader_.glow = glGetUniformLocation(s, "uGlow");
    seaShader_.mapSize = glGetUniformLocation(s, "uMapSize");

    floorShader_.prog = linkProgram("seabed", kSeaVS, kFloorFS);
    if (!floorShader_.prog) return false;
    GLuint f = floorShader_.prog;
    floorShader_.viewProj = glGetUniformLocation(f, "uViewProj");
    floorShader_.center = glGetUniformLocation(f, "uCenter");
    floorShader_.half = glGetUniformLocation(f, "uHalf");
    floorShader_.eye = glGetUniformLocation(f, "uEye");
    floorShader_.ambient = glGetUniformLocation(f, "uAmbient");
    floorShader_.sunLight = glGetUniformLocation(f, "uSunLight");
    floorShader_.underwater = glGetUniformLocation(f, "uUnderwater");
    floorShader_.fogColor = glGetUniformLocation(f, "uFogColor");
    floorShader_.fog = glGetUniformLocation(f, "uFog");
    floorShader_.mapSize = glGetUniformLocation(f, "uMapSize");

    cloudShader_.prog = linkProgram("clouds", kSeaVS, withCommon(kCloudFS).c_str());
    if (!cloudShader_.prog) return false;
    GLuint c = cloudShader_.prog;
    cloudShader_.common.find(c);
    cloudShader_.viewProj = glGetUniformLocation(c, "uViewProj");
    cloudShader_.center = glGetUniformLocation(c, "uCenter");
    cloudShader_.half = glGetUniformLocation(c, "uHalf");
    cloudShader_.eye = glGetUniformLocation(c, "uEye");
    cloudShader_.layer = glGetUniformLocation(c, "uLayer");
    cloudShader_.lit = glGetUniformLocation(c, "uLit");
    cloudShader_.fogColor = glGetUniformLocation(c, "uFogColor");
    cloudShader_.fog = glGetUniformLocation(c, "uFog");
    cloudShader_.fadeStart = glGetUniformLocation(c, "uFadeStart");
    cloudShader_.fadeEnd = glGetUniformLocation(c, "uFadeEnd");
    cloudShader_.edge = glGetUniformLocation(c, "uEdge");

    glGenVertexArrays(1, &emptyVao_);
    return true;
}

void Renderer::releaseTerrainGl() {
    releaseTerrain();
    releaseScene();
    glDeleteProgram(groundShader_.prog);
    glDeleteProgram(seaShader_.prog);
    glDeleteProgram(floorShader_.prog);
    glDeleteProgram(cloudShader_.prog);
    glDeleteTextures(1, &groundTex_);
    glDeleteTextures(1, &seaTex_);
    glDeleteTextures(1, &noiseTex_);
    glDeleteVertexArrays(1, &emptyVao_);
    for (PassTimer* t : { &groundTimer_, &waterTimer_, &cloudTimer_ }) {
        if (t->query) glDeleteQueries(1, &t->query);
        *t = PassTimer{};
    }
    groundShader_ = TerrainShader{};
    seaShader_ = SeaShader{};
    floorShader_ = FloorShader{};
    cloudShader_ = CloudShader{};
    groundTex_ = seaTex_ = noiseTex_ = 0;
    emptyVao_ = 0;
    surfaceVersion_ = 0;
}

void Renderer::uploadSurfaceTiles(const Terrain& t) {
    glDeleteTextures(1, &groundTex_);
    glDeleteTextures(1, &seaTex_);
    groundTex_ = makeSurfaceTexture(t.surface().ground, SURFACE_TILE_SIZE);
    seaTex_ = makeSurfaceTexture(t.surface().water, SURFACE_TILE_SIZE);
    // The noise is the same for the same seed, so it is only made again for a new one
    if (!noiseTex_ || surfaceSeed_ != t.surface().seed) {
        glDeleteTextures(1, &noiseTex_);
        surfaceSeed_ = t.surface().seed;
        noiseTex_ = makeSurfaceTexture(skyNoisePixels(surfaceSeed_), SKY_NOISE_SIZE);
    }
    surfaceVersion_ = t.surfaceVersion();
}

void Renderer::releaseChunk(TerrainChunk& c) {
    if (c.ebo) glDeleteBuffers(1, &c.ebo);
    if (c.vbo) glDeleteBuffers(1, &c.vbo);
    if (c.vao) glDeleteVertexArrays(1, &c.vao);
    c = TerrainChunk{};
}

void Renderer::releaseTerrain() {
    for (TerrainChunk& c : chunks_) releaseChunk(c);
    chunks_.clear();
    terrainSize_ = 0;
    chunksPerSide_ = 0;
}

void Renderer::setMapTheme(const MapTheme& theme) {
    hasTheme_ = true;
    theme_ = theme;
    themeLook_ = themeLook(theme);
    themeSky_ = themeSkyColors(theme);
    skyBakeWanted_ = true;
}

void Renderer::uploadTerrain(const Terrain& t) {
    releaseTerrain();
    unitFacing_.clear();
    if (overviewTex_) {
        glDeleteTextures(1, &overviewTex_);
        overviewTex_ = 0;
    }
    overviewSize_ = 0.0f;
    const std::shared_ptr<const ChunkSource> source = t.source();
    hasTheme_ = source->theme() != nullptr;
    seaBounded_ = source->walledIn();
    if (hasTheme_) setMapTheme(*source->theme());
    overview_.start(t.size(), [source](int cx, int cz, ChunkData& c) { return source->fill(cx, cz, c); },
                    [source](int px, std::vector<uint8_t>& rgba, const std::atomic<bool>& cancel) { return source->picture(px, rgba, cancel); },
                    t.streamed());
    terrainSize_ = t.size();
    chunksPerSide_ = t.chunksPerSide();
    chunks_.resize((size_t)chunksPerSide_ * chunksPerSide_);
}

void Renderer::buildChunk(const Terrain& t, const ChunkData& c, int id, int step, const int* nbStep) {
    TerrainChunk& slot = chunks_[(size_t)id];
    if (!slot.vao) {
        glGenVertexArrays(1, &slot.vao);
        glGenBuffers(1, &slot.vbo);
        glGenBuffers(1, &slot.ebo);
        glBindVertexArray(slot.vao);
        glBindBuffer(GL_ARRAY_BUFFER, slot.vbo);
        const GLsizei stride = sizeof(TerrainVertex);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride, (void*)(6 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, slot.ebo);
        glBindVertexArray(0);
    }
    buildChunkMesh(t, c, step, chunkScratch_, nbStep);
    glBindVertexArray(slot.vao);
    glBindBuffer(GL_ARRAY_BUFFER, slot.vbo);
    glBufferData(GL_ARRAY_BUFFER, chunkScratch_.verts.size() * sizeof(TerrainVertex),
                 chunkScratch_.verts.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, slot.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, chunkScratch_.idx.size() * sizeof(uint32_t),
                 chunkScratch_.idx.data(), GL_DYNAMIC_DRAW);
    slot.indexCount = (GLsizei)chunkScratch_.idx.size();
    slot.step = step;
    for (int k = 0; k < 4; ++k) slot.nbStep[k] = nbStep[k];
    glBindVertexArray(0);
}

void Renderer::drawTerrain(const Terrain& t, const Camera& cam) {
    if (t.size() != terrainSize_ || chunks_.empty()) uploadTerrain(t);
    if (t.surfaceVersion() != surfaceVersion_) uploadSurfaceTiles(t);

    const SkyLight sky = skyLightFor(hasTheme_ ? themeLook_ : t.surface().look, Vec3{ skySun_[0], skySun_[1], skySun_[2] });
    updateSurfaceFrame(cam, sky);
    const SurfaceFrame& frame = surfaceFrame_;
    const bool hasWater = !hasTheme_ || theme_.waterShown;
    const float seaEnd = hasTheme_ && seaBounded_ ? (float)t.size() : 0.0f;
    // The light that reaches props and units
    for (int i = 0; i < 3; ++i) worldTint_[i] = 1.0f;
    if (hasTheme_ && theme_.modelLight > 0) {
        const float amount = (float)theme_.modelLight / 100.0f;
        const float up = std::max(sky.sunDir.y, 0.0f);
        const float reach[3] = { sky.ambientLight.x * 1.4f + sky.sunLight.x * up, sky.ambientLight.y * 1.4f + sky.sunLight.y * up, sky.ambientLight.z * 1.4f + sky.sunLight.z * up };
        for (int i = 0; i < 3; ++i) worldTint_[i] = 1.0f + (std::min(reach[i], 1.0f) - 1.0f) * amount;
    }
    const float hs = t.heightScale();
    const float seaY = 0.0f;
    const float waterY = seaY - kWaterDrop * hs;
    float ex, ey, ez;
    cam.eye(ex, ey, ez);

    glEnable(GL_DEPTH_TEST);

    passTimerBegin(groundTimer_);
    glUseProgram(floorShader_.prog);
    glUniformMatrix4fv(floorShader_.viewProj, 1, GL_FALSE, viewProj_.m);
    glUniform3f(floorShader_.eye, ex, ey, ez);
    glUniform3f(floorShader_.center, ex, t.floorY(), ez);
    glUniform1f(floorShader_.half, kFarPlane * 1.2f);
    glUniform3f(floorShader_.ambient, sky.ambientLight.x, sky.ambientLight.y, sky.ambientLight.z);
    glUniform3f(floorShader_.sunLight, sky.sunLight.x, sky.sunLight.y, sky.sunLight.z);
    glUniform3f(floorShader_.underwater, sky.underwaterRgb.x, sky.underwaterRgb.y, sky.underwaterRgb.z);
    glUniform3f(floorShader_.fogColor, sky.fogColor.x, sky.fogColor.y, sky.fogColor.z);
    glUniform1f(floorShader_.fog, sky.fog);
    glUniform1f(floorShader_.mapSize, seaEnd);
    if (hasWater) {
        glDepthMask(GL_FALSE);
        glBindVertexArray(emptyVao_);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glDepthMask(GL_TRUE);
    }

    glUseProgram(groundShader_.prog);
    glUniformMatrix4fv(groundShader_.viewProj, 1, GL_FALSE, viewProj_.m);
    glUniform3f(groundShader_.eye, ex, ey, ez);
    glUniform3f(groundShader_.sunDir, sky.sunDir.x, sky.sunDir.y, sky.sunDir.z);
    glUniform3f(groundShader_.sunLight, sky.sunLight.x, sky.sunLight.y, sky.sunLight.z);
    glUniform3f(groundShader_.ambient, sky.ambientLight.x, sky.ambientLight.y, sky.ambientLight.z);
    glUniform3f(groundShader_.underwater, sky.underwaterRgb.x, sky.underwaterRgb.y, sky.underwaterRgb.z);
    glUniform3f(groundShader_.fogColor, sky.fogColor.x, sky.fogColor.y, sky.fogColor.z);
    glUniform1f(groundShader_.fog, sky.fog);
    glUniform3f(groundShader_.rockRgb, sky.rockRgb.x, sky.rockRgb.y, sky.rockRgb.z);
    glUniform1f(groundShader_.rockSlope, sky.rockSlope);
    glUniform1f(groundShader_.seaY, seaY);
    glUniform1f(groundShader_.depthScale, hasWater ? 1.0f / hs : 0.0f);
    glUniform1f(groundShader_.voidCut, hasWater ? -1000000.0f : -5.0f);
    float groundTint[3] = { 1.0f, 1.0f, 1.0f };
    if (hasTheme_) themeRgb(theme_.groundTint, groundTint);
    glUniform3fv(groundShader_.groundTint, 1, groundTint);
    glUniform1f(groundShader_.tintAmount, hasTheme_ ? (float)theme_.groundTintAmount / 100.0f : 0.0f);
    setSurfaceUniforms(groundShader_.common);
    glUniform2fv(groundShader_.tileShift, 1, frame.groundShift);
    glUniform1f(groundShader_.tileSize, (float)GROUND_TILE);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, groundTex_);
    glUniform1i(groundShader_.tile, 0);

    // Over a void the ground's broken-off edge blends its last pixel into the sky behind it; everywhere else it's opaque
    const bool blendEdge = !hasWater;
    if (blendEdge) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    const FrustumPlanes planes = framePlanes(viewProj_);
    int builds = 0;
    for (int id = 0; id < (int)chunks_.size(); ++id) {
        TerrainChunk& slot = chunks_[(size_t)id];
        const ChunkData* c = t.chunkAt(id % chunksPerSide_, id / chunksPerSide_);
        if (!c) {
            if (slot.vao) releaseChunk(slot);
            continue;
        }
        if (slot.version != c->version) {
            slot.version = c->version;
            slot.bounds = chunkBounds(t, *c);
            slot.step = 0;
        }
        if (!chunkVisible(planes, slot.bounds)) continue;

        const float mx = 0.5f * (slot.bounds.x0 + slot.bounds.x1);
        const float my = 0.5f * (slot.bounds.minY + slot.bounds.maxY);
        const float mz = 0.5f * (slot.bounds.z0 + slot.bounds.z1);
        // Past the view distance from the view's centre, a chunk is not drawn at all (the half diagonal keeps a chunk
        // that reaches into range)
        {
            const float reach = viewDistance_ + 0.5f * std::hypot(slot.bounds.x1 - slot.bounds.x0, slot.bounds.z1 - slot.bounds.z0);
            const float fx = mx - cam.focusX, fz = mz - cam.focusZ;
            if (fx * fx + fz * fz > reach * reach) continue;
        }
        const float dx = mx - ex, dy = my - ey, dz = mz - ez;
        const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        const int step = dist < LOD_NEAR_TILES ? 1 : (dist < LOD_MID_TILES ? 2 : 4);

        const int cx = id % chunksPerSide_, cz = id / chunksPerSide_;
        int nb[4] = { 0, 0, 0, 0 };
        if (cx > 0) nb[0] = chunks_[(size_t)(id - 1)].step;
        if (cx < chunksPerSide_ - 1) nb[1] = chunks_[(size_t)(id + 1)].step;
        if (cz > 0) nb[2] = chunks_[(size_t)(id - chunksPerSide_)].step;
        if (cz < chunksPerSide_ - 1) nb[3] = chunks_[(size_t)(id + chunksPerSide_)].step;
        const bool neighboursChanged = std::memcmp(nb, slot.nbStep, sizeof nb) != 0;

        if ((slot.step != step || neighboursChanged) && builds < MAX_BUILDS_PER_FRAME) {
            buildChunk(t, *c, id, step, nb);
            ++builds;
        }
        if (slot.step == 0) continue;
        glBindVertexArray(slot.vao);
        glDrawElements(GL_TRIANGLES, slot.indexCount, GL_UNSIGNED_INT, nullptr);
    }
    glBindVertexArray(0);
    if (blendEdge) glDisable(GL_BLEND);
    passTimerEnd(groundTimer_);
    if (!hasWater) return;

    passTimerBegin(waterTimer_);
    const float fadeEnd = kFarPlane * 0.9f;
    const float fadeStart = fadeEnd * 0.6f;
    glUseProgram(seaShader_.prog);
    glUniformMatrix4fv(seaShader_.viewProj, 1, GL_FALSE, viewProj_.m);
    glUniform3f(seaShader_.center, ex, waterY, ez);
    glUniform1f(seaShader_.half, kFarPlane * 1.2f);
    glUniform3f(seaShader_.eye, ex, ey, ez);
    glUniform3f(seaShader_.sunDir, sky.sunDir.x, sky.sunDir.y, sky.sunDir.z);
    glUniform3f(seaShader_.sunLight, sky.sunLight.x, sky.sunLight.y, sky.sunLight.z);
    glUniform3f(seaShader_.ambient, sky.ambientLight.x, sky.ambientLight.y, sky.ambientLight.z);
    glUniform3f(seaShader_.color, sky.waterRgb.x, sky.waterRgb.y, sky.waterRgb.z);
    glUniform3f(seaShader_.fogColor, sky.fogColor.x, sky.fogColor.y, sky.fogColor.z);
    glUniform1f(seaShader_.fog, sky.fog);
    glUniform1f(seaShader_.time, SecondsSinceStart());
    glUniform1f(seaShader_.opacity, sky.waterOpacity);
    glUniform1f(seaShader_.pattern, sky.waterPattern);
    glUniform1f(seaShader_.speed, sky.waterSpeed);
    glUniform1f(seaShader_.fadeStart, fadeStart);
    glUniform1f(seaShader_.fadeEnd, fadeEnd);
    glUniform1f(seaShader_.glitter, hasTheme_ ? (float)theme_.waterGlitter / 100.0f : 0.0f);
    glUniform1f(seaShader_.glow, hasTheme_ ? (float)theme_.waterGlow / 100.0f : 0.0f);
    glUniform1f(seaShader_.mapSize, seaEnd);
    if (surfaceBands_) glUniform2f(seaShader_.evenOut, WATER_EVEN_START, WATER_EVEN_END);
    else glUniform2f(seaShader_.evenOut, NO_BAND, 2.0f * NO_BAND);
    setSurfaceUniforms(seaShader_.common);
    glUniform4fv(seaShader_.tileShift, 1, frame.waterShift);
    glUniform2f(seaShader_.tileSizes, (float)WATER_TILE_1, (float)WATER_TILE_2);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, seaTex_);
    glUniform1i(seaShader_.tile, 0);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(kWaterBiasSlope, kWaterBiasUnits);
    glBindVertexArray(emptyVao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glBindVertexArray(0);
    passTimerEnd(waterTimer_);
}
