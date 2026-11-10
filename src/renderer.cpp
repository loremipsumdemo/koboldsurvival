#include "renderer_internal.h"

using namespace render_detail;

namespace {
constexpr float GRAIN_TILE = 1.25f;   // world units one copy of a building material's texture covers

const char* COLOR_VS = R"(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec4 aColor;
uniform mat4 uMVP;
out vec4 vColor;
void main() {
    vColor = aColor;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

const char* COLOR_FS = R"(
#version 330 core
in vec4 vColor;
out vec4 FragColor;
void main() {
    FragColor = vColor;
}
)";

const char* GLYPH_VS = R"(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec2 aUV;
uniform mat4 uMVP;
out vec2 vUV;
void main() {
    vUV = aUV;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

const char* GLYPH_FS = R"(
#version 330 core
in vec2 vUV;
uniform sampler2D uTex;
uniform vec4 uTint;
uniform float uSat;   // 1 keeps the colors, 0 is fully gray
uniform float uShine;   // how far a white shine has crossed, up to 1. 0 is no shine
uniform vec4 uFaction;   // a faction's color the texture's colors are pulled toward, by a (0 leaves them as they are)
uniform float uHue;      // turns of hue shift, a boss's Coloration (0 leaves the colors as they are)
out vec4 FragColor;
// The color turned round the gray axis by h turns: a hue shift that keeps brightness and shading
vec3 hueTurn(vec3 c, float h) {
    if (h == 0.0) return c;
    const vec3 k = vec3(0.57735);
    float a = h * 6.2831853;
    float ca = cos(a);
    return c * ca + cross(k, c) * sin(a) + k * dot(k, c) * (1.0 - ca);
}
void main() {
    vec4 c = texture(uTex, vUV);
    if (c.a < 0.1) discard;   // keep only the visible pixels
    c.rgb = clamp(hueTurn(c.rgb, uHue), 0.0, 1.0);
    float l = dot(c.rgb, vec3(0.299, 0.587, 0.114));
    // The faction's color at the texture's own brightness, so shading and detail stay
    vec3 fl = uFaction.rgb / max(dot(uFaction.rgb, vec3(0.299, 0.587, 0.114)), 0.05);
    c.rgb = mix(c.rgb, clamp(fl * l, 0.0, 1.0), uFaction.a);
    l = dot(c.rgb, vec3(0.299, 0.587, 0.114));
    c.rgb = mix(vec3(l), c.rgb, uSat);
    if (uShine > 0.0) {
        float across = abs((vUV.x + vUV.y) * 0.5 - (uShine * 1.6 - 0.3));
        c.rgb = mix(c.rgb, vec3(1.0), (1.0 - smoothstep(0.02, 0.16, across)) * 0.9);
    }
    FragColor = c * uTint;
}
)";

const char* MODEL_INST_VS = R"(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec2 aUV;
layout(location=2) in mat4 aModel;   // locations 2 to 5, one per instance
layout(location=6) in vec4 aFaction;
layout(location=7) in float aSat;
layout(location=8) in float aHue;
uniform mat4 uVP;
out vec2 vUV;
out vec4 vFaction;
out float vSat;
out float vHue;
out vec3 vWorld;
void main() {
    vUV = aUV;
    vFaction = aFaction;
    vSat = aSat;
    vHue = aHue;
    vec4 world = aModel * vec4(aPos, 1.0);
    vWorld = world.xyz;
    gl_Position = uVP * world;
}
)";

// GLYPH_FS for instanced models: the faction color and saturation come with each copy instead of from uniforms
const char* MODEL_INST_FS = R"(
#version 330 core
in vec2 vUV;
in vec4 vFaction;
in float vSat;
in float vHue;
in vec3 vWorld;
uniform sampler2D uTex;
uniform sampler2D uGrain;   // a building material's gray texture around 0.5, tiling; on unit 1
uniform float uGrainOn;     // 1 for building pieces, 0 for everything else
uniform float uGrainTile;   // world units one copy of the texture covers
uniform vec4 uTint;
out vec4 FragColor;
// The color turned round the gray axis by h turns: a hue shift that keeps brightness and shading
vec3 hueTurn(vec3 c, float h) {
    if (h == 0.0) return c;
    const vec3 k = vec3(0.57735);
    float a = h * 6.2831853;
    float ca = cos(a);
    return c * ca + cross(k, c) * sin(a) + k * dot(k, c) * (1.0 - ca);
}
void main() {
    vec4 c = texture(uTex, vUV);
    if (c.a < 0.1) discard;
    c.rgb = clamp(hueTurn(c.rgb, vHue), 0.0, 1.0);
    if (uGrainOn > 0.0) {
        // Laid on in world space along the face's own plane, so it runs on from one tile's piece into the next.
        // The face's facing comes from how the position changes across the pixel: the models have no normals here.
        vec3 n = abs(normalize(cross(dFdx(vWorld), dFdy(vWorld))));
        vec2 p;
        if (n.y >= n.x && n.y >= n.z) p = vWorld.xz;        // floors and tops: grain along z
        else if (n.x >= n.z) p = vec2(vWorld.z, vWorld.y);  // walls facing x: grain upright
        else p = vec2(vWorld.x, vWorld.y);                  // walls facing z: grain upright
        float g = texture(uGrain, p / uGrainTile).r;
        c.rgb = clamp(c.rgb * (g * 2.0), 0.0, 1.0);
    }
    float l = dot(c.rgb, vec3(0.299, 0.587, 0.114));
    vec3 fl = vFaction.rgb / max(dot(vFaction.rgb, vec3(0.299, 0.587, 0.114)), 0.05);
    c.rgb = mix(c.rgb, clamp(fl * l, 0.0, 1.0), vFaction.a);
    l = dot(c.rgb, vec3(0.299, 0.587, 0.114));
    c.rgb = mix(vec3(l), c.rgb, vSat);
    FragColor = c * uTint;
}
)";

void setGlyphDefaults(GLuint program) {
    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program, "uTex"), 0);
    glUniform4f(glGetUniformLocation(program, "uTint"), 1.0f, 1.0f, 1.0f, 1.0f);
    glUniform1f(glGetUniformLocation(program, "uSat"), 1.0f);
    glUniform4f(glGetUniformLocation(program, "uFaction"), 0.0f, 0.0f, 0.0f, 0.0f);
    glUniform1f(glGetUniformLocation(program, "uHue"), 0.0f);
    glUseProgram(0);
}

void setupColorLayout(GLuint vao, GLuint vbo) {
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}

void setupGlyphLayout(GLuint vao, GLuint vbo) {
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}

// Transparent and nearly transparent pixels still carry a color, often junk (an alpha of 2 with a bright
// red, say). Texture filtering and mipmaps blend it into the visible edge, which shows as stray specks
// and fringes. So each such pixel takes the average color of its nearest visible neighbors, spreading
// outward; alpha stays as it was.
static void bleedIntoTransparent(SDL_Surface* rgba) {
    const int w = rgba->w, h = rgba->h;
    if (w <= 0 || h <= 0) return;
    constexpr unsigned char VISIBLE_ALPHA = 26;   // the glyph shader's 0.1 cutoff
    unsigned char* base = static_cast<unsigned char*>(rgba->pixels);
    auto px = [&](int x, int y) { return base + (size_t)y * (size_t)rgba->pitch + (size_t)x * 4; };

    std::vector<unsigned char> known((size_t)w * (size_t)h, 0);
    std::vector<int> frontier, next;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (px(x, y)[3] >= VISIBLE_ALPHA) known[(size_t)y * (size_t)w + (size_t)x] = 1;
        }
    }
    // The unknown pixels next to the given known ones, each once. `queued` marks pixels already in a ring.
    std::vector<unsigned char> queued((size_t)w * (size_t)h, 0);
    auto ringAround = [&](const std::vector<int>& from, std::vector<int>& out) {
        out.clear();
        for (int i : from) {
            const int x = i % w, y = i / w;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = x + dx, ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                    const size_t j = (size_t)ny * (size_t)w + (size_t)nx;
                    if (known[j] || queued[j]) continue;
                    queued[j] = 1;
                    out.push_back((int)j);
                }
            }
        }
    };
    // The visible pixels that border a see-through one: only these can start the first ring
    std::vector<int> edge;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (!known[(size_t)y * (size_t)w + (size_t)x]) continue;
            const bool border = (x > 0 && !known[(size_t)y * (size_t)w + (size_t)(x - 1)]) ||
                                (x + 1 < w && !known[(size_t)y * (size_t)w + (size_t)(x + 1)]) ||
                                (y > 0 && !known[(size_t)(y - 1) * (size_t)w + (size_t)x]) ||
                                (y + 1 < h && !known[(size_t)(y + 1) * (size_t)w + (size_t)x]) ||
                                (x > 0 && y > 0 && !known[(size_t)(y - 1) * (size_t)w + (size_t)(x - 1)]) ||
                                (x + 1 < w && y > 0 && !known[(size_t)(y - 1) * (size_t)w + (size_t)(x + 1)]) ||
                                (x > 0 && y + 1 < h && !known[(size_t)(y + 1) * (size_t)w + (size_t)(x - 1)]) ||
                                (x + 1 < w && y + 1 < h && !known[(size_t)(y + 1) * (size_t)w + (size_t)(x + 1)]);
            if (border) edge.push_back(y * w + x);
        }
    }
    // A few rings outward covers what filtering and the first mipmaps can reach
    constexpr int RINGS = 8;
    ringAround(edge, frontier);
    for (int ring = 0; ring < RINGS && !frontier.empty(); ++ring) {
        for (int i : frontier) {
            const int x = i % w, y = i / w;
            int r = 0, g = 0, b = 0, n = 0;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = x + dx, ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                    if (!known[(size_t)ny * (size_t)w + (size_t)nx]) continue;
                    const unsigned char* q = px(nx, ny);
                    r += q[0]; g += q[1]; b += q[2]; ++n;
                }
            }
            unsigned char* p = px(x, y);
            p[0] = (unsigned char)(r / n); p[1] = (unsigned char)(g / n); p[2] = (unsigned char)(b / n);
        }
        for (int i : frontier) known[(size_t)i] = 1;
        if (ring + 1 < RINGS) {
            ringAround(frontier, next);
            frontier.swap(next);
        }
    }
}

SDL_Surface* decodeRgba(const char* path) {
    const unsigned char* bytes = nullptr;
    size_t size = 0;
    if (!pakFind(path, bytes, size)) return nullptr;
    SDL_Surface* s = IMG_Load_IO(SDL_IOFromConstMem(bytes, size), true);
    if (!s) {
        SDL_Log("Could not load %s: %s", path, SDL_GetError());
        return nullptr;
    }
    SDL_Surface* conv = SDL_ConvertSurface(s, SDL_PIXELFORMAT_ABGR8888);
    SDL_DestroySurface(s);
    if (conv) bleedIntoTransparent(conv);
    return conv;
}

TextTex textureOf(const SDL_Surface* rgba) {
    return TextTex{ MakeRgbaTexture(rgba->w, rgba->h, static_cast<const uint8_t*>(rgba->pixels)),
                    (float)rgba->w / (float)rgba->h };
}

AlphaMask alphaOf(const SDL_Surface* rgba) {
    AlphaMask m;
    m.w = rgba->w;
    m.h = rgba->h;
    m.alpha.resize((size_t)m.w * (size_t)m.h);
    for (int y = 0; y < m.h; ++y) {
        const unsigned char* row = static_cast<const unsigned char*>(rgba->pixels) + (size_t)y * (size_t)rgba->pitch;
        for (int x = 0; x < m.w; ++x) m.alpha[(size_t)y * (size_t)m.w + (size_t)x] = row[(size_t)x * 4 + 3];
    }
    return m;
}

const char* SKY_VS = R"(
#version 330 core
layout(location=0) in vec2 aNdc;
uniform mat4 uInvVP;
out vec3 vDir;
void main() {
    vec4 p = uInvVP * vec4(aNdc, 1.0, 1.0);
    vDir = p.xyz / p.w;
    gl_Position = vec4(aNdc, 1.0, 1.0);
}
)";

const char* SKY_FS = R"(
#version 330 core
in vec3 vDir;
uniform vec3 uSunDir;     // unit vector towards the sun
uniform float uSunAlpha;  // 0 when the sun is below the horizon, 1 when fully up
uniform vec3 uSunCol;     // color of the sun's glow and disc
uniform vec3 uMoonDir;    // unit vector towards the moon, opposite the sun
uniform float uMoonAlpha; // 0 when the moon is hidden
uniform vec3 uMoonCol;
uniform float uStarAlpha; // 0 when there are no stars
uniform vec3 uZenith;     // sky color straight up
uniform vec3 uHorizon;    // sky color at the horizon
uniform int uSkyKind;     // as SkyKind says
uniform int uUseBaked;    // 1: the cave or galaxy has been drawn once into uBaked, so it only has to be looked up
uniform samplerCube uBaked;
out vec4 FragColor;

float hash1(float n) { return fract(sin(n) * 43758.5453); }
// A whole-number hash, so a grid corner gets exactly the same value from every cell that shares it. A sin() hash
// doesn't: the GPU works the corner out a hair differently from each side, and the big multiply turns that into
// a different value, which showed as hard seams through the noise.
float hashI(ivec3 c) {
    uvec3 v = uvec3(c) * 1664525u + 1013904223u;
    v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
    v ^= v >> 16u;
    v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
    return float((v.x ^ v.y ^ v.z) >> 8u) * (1.0 / 16777216.0);
}
float hash3(vec3 p) { return hashI(ivec3(floor(p + 0.5))); }

// Smooth noise over the sphere's directions
float skyNoise(vec3 p) {
    vec3 fl = floor(p), f = p - fl;
    ivec3 i = ivec3(fl);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(hashI(i), hashI(i + ivec3(1, 0, 0)), f.x), mix(hashI(i + ivec3(0, 1, 0)), hashI(i + ivec3(1, 1, 0)), f.x), f.y),
               mix(mix(hashI(i + ivec3(0, 0, 1)), hashI(i + ivec3(1, 0, 1)), f.x), mix(hashI(i + ivec3(0, 1, 1)), hashI(i + ivec3(1, 1, 1)), f.x), f.y), f.z);
}
float clouds(vec3 p) {
    // Each octave turned against the last, so the noise's grid doesn't show as straight edges
    const mat3 turn = mat3(0.00, 0.80, 0.60, -0.80, 0.36, -0.48, -0.60, -0.48, 0.64);
    float sum = 0.5 * skyNoise(p);
    p = turn * p * 2.03;
    sum += 0.25 * skyNoise(p);
    p = turn * p * 2.01;
    sum += 0.125 * skyNoise(p);
    p = turn * p * 2.04;
    sum += 0.0625 * skyNoise(p);
    p = turn * p * 2.02;
    return sum + 0.03125 * skyNoise(p);
}

// How far a row of sharp points reaches at this angle around the viewer, from 0 to 1. It wraps all the way round.
float points(float around, float count, float seed) {
    float at = around * count;
    float cell = floor(at);
    float inCell = at - cell;
    float wrapped = mod(cell, count);
    float tall = hash1(wrapped * 1.37 + seed);
    float middle = 0.3 + 0.4 * hash1(wrapped * 2.11 + seed);
    float tip = inCell < middle ? inCell / middle : (1.0 - inCell) / (1.0 - middle);
    return tall * tall * pow(tip, 1.4);
}

// Inside a cave: a far dark back, then rows of stalactites and stalagmites, nearer rows larger and lighter
vec3 caveSky(vec3 d) {
    float around = atan(d.z, d.x) / 6.2831853 + 0.5;
    float up = asin(clamp(d.y, -1.0, 1.0)) / 1.5707963;
    float rough = clouds(d * 14.0);
    vec3 col = uHorizon * (0.55 + 0.5 * rough);
    // The wobble follows a circle round the viewer, so it meets itself where the angle wraps instead of jumping there
    vec2 ring = normalize(d.xz + vec2(1e-6, 0.0)) * (40.0 / 6.2831853);
    for (int i = 0; i < 4; ++i) {
        float layer = float(i);
        float near = layer / 3.0;
        float count = 90.0 - layer * 20.0;
        float wobble = 0.03 * (skyNoise(vec3(ring.x, layer * 5.0 + 3.0, ring.y)) - 0.5);
        float roof = 0.50 - near * 0.10 + wobble;
        float drop = (0.26 + near * 0.26) * points(around, count, 11.0 + layer * 7.0);
        float floorAt = -0.12 + near * 0.05 + wobble;
        float rise = (0.16 + near * 0.26) * points(around, count * 0.8, 47.0 + layer * 5.0);
        vec3 rock = mix(uHorizon, uZenith, 0.25 + 0.5 * near) * (0.7 + 0.6 * rough);
        float hang = smoothstep(0.0, 0.006, up - (roof - drop));
        float stand = smoothstep(0.0, 0.006, (floorAt + rise) - up);
        col = mix(col, rock * (0.75 + 0.5 * clamp((up - (roof - drop)) * 2.0, 0.0, 1.0)), hang);
        col = mix(col, rock * (0.9 - 0.4 * clamp(((floorAt + rise) - up) * 2.0, 0.0, 1.0)), stand);
    }
    return col;
}

// Open space: black, with two colors of nebula along a band across the sky, and stars all the way round
vec3 galaxySky(vec3 d, float stars) {
    vec3 axis = normalize(vec3(0.35, 0.82, 0.45));
    float band = exp(-pow(dot(d, axis) * 2.6, 2.0));
    vec3 warp = vec3(clouds(d * 2.0 + 1.0), clouds(d * 2.0 + 9.0), clouds(d * 2.0 + 17.0)) * 2.5;
    float a = clouds(d * 3.0 + warp);
    float b = clouds(d * 4.5 + warp.yzx + 4.0);
    vec3 col = vec3(0.006, 0.004, 0.018);
    col += uZenith * pow(a, 2.2) * (0.55 + 2.4 * band);
    col += uHorizon * pow(b, 3.0) * (0.25 + 2.2 * band);
    col += vec3(0.9, 0.85, 1.0) * pow(band, 4.0) * pow(a, 1.5) * 0.65;

    for (int i = 0; i < 2; ++i) {
        float scale = i == 0 ? 170.0 : 340.0;
        vec3 cell = floor(d * scale);
        float h = hash3(cell + float(i));
        float keep = i == 0 ? 0.990 : 0.975 - band * 0.03;
        if (h > keep) {
            // Fades out before the cell's edge, so no star is cut off square
            float round_ = smoothstep(0.5, 0.1, length(d * scale - cell - 0.5));
            float twinkle = 0.5 + 0.5 * hash3(cell + 7.0);
            vec3 tint = mix(vec3(0.75, 0.82, 1.0), vec3(1.0, 0.8, 0.75), hash3(cell + 3.0));
            col += tint * round_ * twinkle * (i == 0 ? 1.4 : 0.55) * stars;
        }
    }
    return col;
}

void main() {
    vec3 d = normalize(vDir);
    if (uSkyKind == 2) {
        FragColor = vec4(uUseBaked == 1 ? texture(uBaked, d).rgb : caveSky(d), 1.0);
        return;
    }
    if (uSkyKind == 3) {
        vec3 space = uUseBaked == 1 ? texture(uBaked, d).rgb : galaxySky(d, uStarAlpha);
        float galaxyMoon = max(dot(d, uMoonDir), 0.0);
        space = mix(space, uMoonCol, smoothstep(0.9990, 0.9994, galaxyMoon) * uMoonAlpha);
        float galaxySun = max(dot(d, uSunDir), 0.0);
        space += uSunCol * (0.25 * pow(galaxySun, 8.0) + 0.5 * pow(galaxySun, 128.0)) * uSunAlpha;
        space = mix(space, uSunCol, smoothstep(0.9994, 0.9997, galaxySun) * uSunAlpha);
        FragColor = vec4(space, 1.0);
        return;
    }
    // Gradient: the horizon color blends up to the zenith color
    vec3 col = mix(uHorizon, uZenith, pow(clamp(d.y, 0.0, 1.0), 0.6));

    // Stars: one per cell of a fine grid over the sphere, where a hash says so. Upper sky only.
    if (uStarAlpha > 0.0 && d.y > 0.0) {
        vec3 cell = floor(d * 180.0);
        float h = hash3(cell);
        col += vec3(step(0.994, h) * smoothstep(0.0, 0.3, d.y) * uStarAlpha);
    }

    // Moon: a soft halo and a small pale disc
    float m = max(dot(d, uMoonDir), 0.0);
    col += uMoonCol * 0.15 * pow(m, 6.0) * uMoonAlpha;
    col = mix(col, uMoonCol, smoothstep(0.9990, 0.9994, m) * uMoonAlpha);

    // Sun: a wide glow and a hard-edged disc
    float s = max(dot(d, uSunDir), 0.0);
    col += uSunCol * (0.25 * pow(s, 8.0) + 0.5 * pow(s, 128.0)) * uSunAlpha;
    float disc = smoothstep(0.9994, 0.9997, s) * uSunAlpha;
    col = mix(col, uSunCol, disc);
    FragColor = vec4(col, 1.0);
}
)";
}

namespace render_detail {
TextTex loadOptionalImage(const char* path) {
    SDL_Surface* conv = decodeRgba(path);
    if (!conv) return {};
    const TextTex t = textureOf(conv);
    SDL_DestroySurface(conv);
    return t;
}

void addMipmaps(const TextTex& t) {
    if (!t.tex) return;
    glBindTexture(GL_TEXTURE_2D, t.tex);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
}

bool loadDrawableModel(const char* path, Model& m, bool needsHeight) {
    const bool read = loadGLBModel(path, m);
    const float extent = std::max({ m.maxX - m.minX, m.maxY - m.minY, m.maxZ - m.minZ });
    const bool drawable = read && m.vao && !m.draws.empty() && (needsHeight ? m.maxY > m.minY : extent > 0.0f);
    if (!drawable) {
        destroyModel(m);
        m = Model{};
    }
    return drawable;
}

bool itemOnScreen(const Mat4& vp, int W, int H, const Transform& tr, float aspect, ItemOnScreen& out) {
    float bx, by, tx, ty, mx, my;
    if (!worldToScreen(vp, tr.x, tr.y, tr.z, W, H, bx, by)) return false;
    if (!worldToScreen(vp, tr.x, tr.y + ITEM_GLYPH_H, tr.z, W, H, tx, ty)) return false;
    if (!worldToScreen(vp, tr.x, tr.y + ITEM_GLYPH_H * 0.5f, tr.z, W, H, mx, my)) return false;
    out.cx = mx;
    out.top = std::min(by, ty);
    out.bottom = std::max(by, ty);
    out.halfW = (out.bottom - out.top) * 0.5f * aspect;
    return true;
}

GLuint MakeRgbaTexture(int w, int h, const uint8_t* rgba, GLint filter) {
    return makeTexture2D(w, h, GL_RGBA, GL_RGBA, rgba, filter, GL_CLAMP_TO_EDGE);
}

GLuint MakeDotTexture(bool white) {
    constexpr int N = 16;
    std::vector<uint8_t> px(static_cast<size_t>(N) * N * 4, 0);
    for (int y = 0; y < N; ++y) {
        for (int x = 0; x < N; ++x) {
            const float dx = x + 0.5f - N * 0.5f, dy = y + 0.5f - N * 0.5f;
            const float d = std::sqrt(dx * dx + dy * dy);
            const bool green = d < 5.0f;
            const float alpha = std::clamp(7.6f - d, 0.0f, 1.0f);
            uint8_t* p = &px[static_cast<size_t>(y * N + x) * 4];
            p[0] = green ? (white ? 255 : 64) : 0;
            p[1] = green ? (white ? 255 : 230) : 0;
            p[2] = green ? (white ? 255 : 80) : 0;
            p[3] = static_cast<uint8_t>(alpha * 255.0f);
        }
    }
    return MakeRgbaTexture(N, N, px.data());
}

float unitSaturation(World& w, ObjectId u) {
    if (w.has<Corpse>(u)) return 0.0f;
    if (outCold(w, u)) return 0.5f;
    return 1.0f;
}

RGBA mixColor(RGBA a, RGBA b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return RGBA{ a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t };
}

RGBA rgb255(int r, int g, int b, float a) { return RGBA{ r / 255.0f, g / 255.0f, b / 255.0f, a }; }

std::string withCommas(long long n) {
    const unsigned long long magnitude = n < 0 ? 0ULL - (unsigned long long)n : (unsigned long long)n;
    const std::string digits = std::to_string(magnitude);
    std::string out;
    for (size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0) out += ',';
        out += digits[i];
    }
    return n < 0 ? "-" + out : out;
}

void cutCornerShape(std::vector<UiVert>& v, const Rect& r, float cut, RGBA top, RGBA bottom) {
    cut = std::min(cut, std::min(r.w, r.h));
    auto at = [&](float x, float y) { return UiVert{ x, y, mixColor(top, bottom, r.h > 0.0f ? (y - r.y) / r.h : 0.0f) }; };
    const UiVert a = at(r.x, r.y), b = at(r.x + r.w, r.y), c = at(r.x + r.w, r.y + r.h - cut);
    const UiVert d = at(r.x + r.w - cut, r.y + r.h), e = at(r.x, r.y + r.h);
    v.insert(v.end(), { a, b, c, a, c, d, a, d, e });
}

RGBA logTint(Tint t) {
    switch (t) {
    case Tint::Green: return theme::GOOD;
    case Tint::Red:   return theme::BAD;
    case Tint::Gray:  break;
    }
    return theme::LOG_TEXT;
}
}

bool Renderer::init(TTF_Font* hudFont, TTF_Font* uiFont,
                    const std::function<void(float, const std::string&)>& progress) {
    LoadStages stages(progress);
    text_.hud = hudFont;
    text_.ui.font = uiFont;

    if (!initGl()) return false;
    loadLoadingArt();
    loadPictures(stages);
    loadUnitModels(stages);
    loadMobArt(stages);
    stages.stage(0.55f, "LOADING_SKY");
    initSky();
    makeTexels();
    loadPropModels(stages);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    return colorProgram_ != 0 && glyphProgram_ != 0;
}

bool Renderer::initGl() {
    colorProgram_ = linkProgram("colour", COLOR_VS, COLOR_FS);
    glyphProgram_ = linkProgram("glyph", GLYPH_VS, GLYPH_FS);
    if (!colorProgram_ || !glyphProgram_) return false;
    uMVPColor_ = glGetUniformLocation(colorProgram_, "uMVP");
    uMVPGlyph_ = glGetUniformLocation(glyphProgram_, "uMVP");
    uTint_ = glGetUniformLocation(glyphProgram_, "uTint");
    uSat_ = glGetUniformLocation(glyphProgram_, "uSat");
    uHue_ = glGetUniformLocation(glyphProgram_, "uHue");
    uShine_ = glGetUniformLocation(glyphProgram_, "uShine");
    uFaction_ = glGetUniformLocation(glyphProgram_, "uFaction");
    setGlyphDefaults(glyphProgram_);

    modelInstProgram_ = linkProgram("instanced model", MODEL_INST_VS, MODEL_INST_FS);
    if (modelInstProgram_) {
        uVPInst_ = glGetUniformLocation(modelInstProgram_, "uVP");
        setGlyphDefaults(modelInstProgram_);
        uTintInst_ = glGetUniformLocation(modelInstProgram_, "uTint");
        uGrainOnInst_ = glGetUniformLocation(modelInstProgram_, "uGrainOn");
        glUseProgram(modelInstProgram_);
        glUniform1i(glGetUniformLocation(modelInstProgram_, "uGrain"), 1);
        glUniform1f(glGetUniformLocation(modelInstProgram_, "uGrainTile"), GRAIN_TILE);
        glUniform1f(uGrainOnInst_, 0.0f);
        glUseProgram(0);
        glGenBuffers(1, &instVbo_);
    }

    if (!initTerrainGl()) return false;

    glGenVertexArrays(1, &dynVao_);
    glGenBuffers(1, &dynVbo_);
    setupColorLayout(dynVao_, dynVbo_);

    glGenVertexArrays(1, &glyphVao_);
    glGenBuffers(1, &glyphVbo_);
    glBindBuffer(GL_ARRAY_BUFFER, glyphVbo_);
    glBufferData(GL_ARRAY_BUFFER, 4 * 5 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    setupGlyphLayout(glyphVao_, glyphVbo_);
    return true;
}

void Renderer::loadLoadingArt() {
    std::vector<std::string> found;
    for (int i = 1; i <= 3; ++i) {
        const std::string path = "data/illustration/loading" + std::to_string(i) + ".png";
        if (pakHas(path)) found.push_back(path);
    }
    if (!found.empty()) {
        std::random_device device;
        std::seed_seq seed{ device(), device(), (unsigned)SDL_GetPerformanceCounter(),
                            (unsigned)(SDL_GetPerformanceCounter() >> 32) };
        std::mt19937 rng(seed);
        const std::string& pick = found[std::uniform_int_distribution<size_t>(0, found.size() - 1)(rng)];
        loadingArt_ = loadOptionalImage(pick.c_str());
        addMipmaps(loadingArt_);
    }
}

void Renderer::pickLoadingArt() {
    if (loadingArt_.tex) glDeleteTextures(1, &loadingArt_.tex);
    loadingArt_ = TextTex{};
    loadLoadingArt();
}

void Renderer::shutdown() {
    auto drop = [](TextTex& g) {
        if (g.tex) glDeleteTextures(1, &g.tex);
        g = TextTex{};
    };
    drop(white_);
    drop(pink_);
    drop(titleLogo_);
    for (TextTex& s : surfaces_) drop(s);
    drop(loadingArt_);
    for (TextTex& g : profilePics_) drop(g);
    for (TextTex& g : mobSprites_) drop(g);
    mobSprites_.clear();
    for (GLuint* t : { &dotTex_, &dotWhiteTex_ }) {
        if (*t) glDeleteTextures(1, t);
        *t = 0;
    }
    for (Model& m : unitModels_) destroyModel(m);
    for (Model& m : mobModels_) destroyModel(m);
    unitModelFor_.fill(nullptr);
    unitFacing_.clear();
    itemPics_.clear();
    emotePics_.clear();
    uiIcons_.clear();
    text_.clear();
    instQueues_.clear();
    instSlot_.clear();
    if (instVbo_) glDeleteBuffers(1, &instVbo_);
    if (modelInstProgram_) glDeleteProgram(modelInstProgram_);
    instVbo_ = modelInstProgram_ = 0;
    for (Model& m : propModels_) destroyModel(m);
    for (auto& shapes : wallShapes_) {
        for (Model& m : shapes) destroyModel(m);
    }
    wallShapes_.clear();
    for (auto& mats : materialModels_) {
        for (auto& shapes : mats) {
            for (Model& m : shapes) destroyModel(m);
        }
    }
    materialModels_.clear();
    for (Model& m : stairSteps_) destroyModel(m);

    glDeleteProgram(skyProgram_);
    if (skyCube_) glDeleteTextures(1, &skyCube_);
    if (skyBakeFbo_) glDeleteFramebuffers(1, &skyBakeFbo_);
    skyCube_ = skyBakeFbo_ = 0;
    glDeleteBuffers(1, &skyVbo_);
    glDeleteVertexArrays(1, &skyVao_);

    glDeleteProgram(colorProgram_);
    glDeleteProgram(glyphProgram_);
    glDeleteBuffers(1, &dynVbo_);
    glDeleteVertexArrays(1, &dynVao_);
    glDeleteBuffers(1, &glyphVbo_);
    glDeleteVertexArrays(1, &glyphVao_);
    releasePreviewGl();
    releaseTerrainGl();
    colorProgram_ = glyphProgram_ = 0;
}

void Renderer::drawQuadVerts(GLuint tex, const float* v, RGBA tint) {
    glUniform4f(uTint_, tint.r, tint.g, tint.b, tint.a);
    glBindVertexArray(glyphVao_);
    glBindBuffer(GL_ARRAY_BUFFER, glyphVbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, 4 * 5 * sizeof(float), v);
    glBindTexture(GL_TEXTURE_2D, tex);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
}

void Renderer::setSaturation(float sat) {
    glUseProgram(glyphProgram_);
    glUniform1f(uSat_, sat);
}

void Renderer::drawRoundedRect(const Rect& r, float radius, RGBA c) {
    if (r.w <= 0.0f || r.h <= 0.0f) return;
    const float rad = std::clamp(radius, 0.0f, std::min(r.w, r.h) * 0.5f);
    static std::vector<UiVert> v;
    v.clear();
    auto quad = [&](float x0, float y0, float x1, float y1) {
        if (x1 <= x0 || y1 <= y0) return;
        v.insert(v.end(), { { x0, y0, c }, { x1, y0, c }, { x1, y1, c }, { x0, y0, c }, { x1, y1, c }, { x0, y1, c } });
    };
    quad(r.x + rad, r.y, r.x + r.w - rad, r.y + r.h);
    quad(r.x, r.y + rad, r.x + rad, r.y + r.h - rad);
    quad(r.x + r.w - rad, r.y + rad, r.x + r.w, r.y + r.h - rad);
    if (rad > 0.0f) {
        const int steps = std::clamp((int)(rad * 0.6f), 4, 24);
        const float cx[4] = { r.x + rad, r.x + r.w - rad, r.x + r.w - rad, r.x + rad };
        const float cy[4] = { r.y + rad, r.y + rad, r.y + r.h - rad, r.y + r.h - rad };
        const float start[4] = { PI, PI + HALF_PI, 0.0f, HALF_PI };
        for (int k = 0; k < 4; ++k) {
            for (int i = 0; i < steps; ++i) {
                const float a0 = start[k] + HALF_PI * (float)i / (float)steps;
                const float a1 = start[k] + HALF_PI * (float)(i + 1) / (float)steps;
                v.push_back({ cx[k], cy[k], c });
                v.push_back({ cx[k] + std::cos(a0) * rad, cy[k] + std::sin(a0) * rad, c });
                v.push_back({ cx[k] + std::cos(a1) * rad, cy[k] + std::sin(a1) * rad, c });
            }
        }
    }
    drawUiTris(v);
}

void Renderer::beginUi(int winW, int winH) {
    constexpr size_t TEXT_CACHE_LIMIT = 512;
    text_.ui.trim(TEXT_CACHE_LIMIT);
    text_.list.trim(TEXT_CACHE_LIMIT);
    presentScene();
    uiW_ = winW;
    uiH_ = winH;
    glViewport(0, 0, winW, winH);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    // The UI backgrounds are translucent, so blending must be on. The sea pass in drawTerrain leaves it off.
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    pixels_ = ortho(0.0f, (float)winW, (float)winH, 0.0f, -1.0f, 1.0f);
}

void Renderer::endUi() {
    drawTip();
    glBindVertexArray(0);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::drawColorTris(const Vertex* v, size_t count, const Mat4& mvp) {
    if (count == 0) return;
    glUseProgram(colorProgram_);
    glUniformMatrix4fv(uMVPColor_, 1, GL_FALSE, mvp.m);
    glBindVertexArray(dynVao_);
    glBindBuffer(GL_ARRAY_BUFFER, dynVbo_);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(count * sizeof(Vertex)), v, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)count);
}

void Renderer::drawColorTris(const Batch& b, const Mat4& mvp) {
    drawColorTris(b.v.data(), b.v.size(), mvp);
}

void Renderer::drawUiTris(const std::vector<UiVert>& in) {
    static std::vector<Vertex> v;
    v.clear();
    for (const UiVert& u : in) v.push_back(Vertex{ { u.x, u.y, 0.0f }, u.c });
    drawColorTris(v.data(), v.size(), pixels_);
}

void Renderer::noteHover(const Rect& r) {
    if (hoverAny_) return;
    hoverAny_ = true;
    hoverNow_ = r;
}

bool Renderer::hoverFrameEnd() {
    auto same = [](const Rect& a, const Rect& b) {
        return std::fabs(a.x - b.x) < 1.0f && std::fabs(a.y - b.y) < 1.0f && std::fabs(a.w - b.w) < 1.0f && std::fabs(a.h - b.h) < 1.0f;
    };
    const bool ping = hoverAny_ && (!hoverHad_ || !same(hoverNow_, hoverLast_));
    hoverHad_ = hoverAny_;
    hoverLast_ = hoverNow_;
    return ping;
}

void Renderer::drawRectGradientH(const Rect& r, RGBA left, RGBA right) {
    const float x0 = r.x, x1 = r.x + r.w, y0 = r.y, y1 = r.y + r.h;
    drawUiTris({ { x0, y0, left }, { x1, y0, right }, { x1, y1, right }, { x0, y0, left }, { x1, y1, right }, { x0, y1, left } });
}

void Renderer::drawRect(const Rect& r, RGBA c) {
    const float x0 = r.x, x1 = r.x + r.w, y0 = r.y, y1 = r.y + r.h;
    const Vertex v[6] = {
        { { x0, y0, 0.0f }, c }, { { x1, y0, 0.0f }, c }, { { x1, y1, 0.0f }, c },
        { { x0, y0, 0.0f }, c }, { { x1, y1, 0.0f }, c }, { { x0, y1, 0.0f }, c },
    };
    drawColorTris(v, 6, pixels_);
}

void Renderer::drawRectGradient(const Rect& r, RGBA top, RGBA bottom) {
    const float x0 = r.x, x1 = r.x + r.w, y0 = r.y, y1 = r.y + r.h;
    const Vertex v[6] = {
        { { x0, y0, 0.0f }, top }, { { x1, y0, 0.0f }, top }, { { x1, y1, 0.0f }, bottom },
        { { x0, y0, 0.0f }, top }, { { x1, y1, 0.0f }, bottom }, { { x0, y1, 0.0f }, bottom },
    };
    drawColorTris(v, 6, pixels_);
}

void Renderer::setWorldPreview(int px, const std::vector<uint8_t>& rgba) {
    if (worldPreviewTex_) glDeleteTextures(1, &worldPreviewTex_);
    worldPreviewTex_ = MakeRgbaTexture(px, px, rgba.data());
}

void Renderer::drawWorldPreview(const Rect& r) {
    if (worldPreviewTex_) drawUiQuad(worldPreviewTex_, r.x, r.y, r.x + r.w, r.y + r.h, 0.0f, 0.0f, 1.0f, 1.0f, WHITE);
}

void Renderer::pollOverview() {
    OverviewPicture pic;
    if (!overview_.take(pic)) return;
    if (overviewTex_) glDeleteTextures(1, &overviewTex_);
    overviewTex_ = pic.px > 0 ? MakeRgbaTexture(pic.px, pic.px, pic.rgba.data()) : 0;
    overviewSize_ = pic.px > 0 ? static_cast<float>(pic.size) : 0.0f;
    overviewExtent_ = pic.px > 0 ? pic.extent() : 1.0f;
}

void Renderer::drawTexture(GLuint tex, float x, float y, float w, float h, RGBA tint) {
    drawUiQuad(tex, x, y, x + w, y + h, 0.0f, 0.0f, 1.0f, 1.0f, tint);
}

void Renderer::drawUiQuad(GLuint tex, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1,
                          RGBA tint) {
    const float v[4 * 5] = {
        x0, y1, 0.0f, u0, v1,
        x1, y1, 0.0f, u1, v1,
        x1, y0, 0.0f, u1, v0,
        x0, y0, 0.0f, u0, v0
    };
    glUseProgram(glyphProgram_);
    glUniformMatrix4fv(uMVPGlyph_, 1, GL_FALSE, pixels_.m);
    drawQuadVerts(tex, v, tint);
}

void Renderer::drawDot(float cx, float cy, float d) {
    if (!dotTex_) dotTex_ = MakeDotTexture();
    const float h = d * 0.5f;
    drawUiQuad(dotTex_, cx - h, cy - h, cx + h, cy + h, 0.0f, 0.0f, 1.0f, 1.0f, WHITE);
}

void Renderer::drawRing(float cx, float cy, float outer, float inner, float from, float to, RGBA color) {
    from = std::clamp(from, 0.0f, 1.0f);
    to = std::clamp(to, 0.0f, 1.0f);
    if (to <= from) return;
    constexpr int FULL_SEGMENTS = 48;
    const int n = std::max(1, (int)std::ceil(FULL_SEGMENTS * (to - from)));
    Batch b;
    for (int i = 0; i < n; ++i) {
        const float a0 = TWO_PI * (from + (to - from) * (float)i / n);
        const float a1 = TWO_PI * (from + (to - from) * (float)(i + 1) / n);
        const float s0 = std::sin(a0), c0 = std::cos(a0), s1 = std::sin(a1), c1 = std::cos(a1);
        b.quad({ cx + s0 * inner, cy - c0 * inner, 0 }, { cx + s0 * outer, cy - c0 * outer, 0 },
               { cx + s1 * outer, cy - c1 * outer, 0 }, { cx + s1 * inner, cy - c1 * inner, 0 }, color);
    }
    drawColorTris(b, pixels_);
}

void Renderer::beginClip(const Rect& r) {
    const int x0 = std::max(0, (int)std::floor(r.x));
    const int x1 = std::min(uiW_, (int)std::ceil(r.x + r.w));
    const int top = std::max(0, (int)std::floor(r.y));
    const int bottom = std::min(uiH_, (int)std::ceil(r.y + r.h));
    glEnable(GL_SCISSOR_TEST);
    glScissor(x0, uiH_ - bottom, std::max(0, x1 - x0), std::max(0, bottom - top));
}

void Renderer::endClip() {
    glDisable(GL_SCISSOR_TEST);
}

const TextTex& PictureCache::get(int n) {
    auto it = pics_.find(n);
    if (it == pics_.end()) {
        const std::string path = prefix_ + (fileOf_ ? fileOf_(n) : std::to_string(n) + ".png");
        TextTex t;
        AlphaMask m;
        if (SDL_Surface* conv = decodeRgba(path.c_str())) {
            t = textureOf(conv);
            if (keepMasks_) m = alphaOf(conv);
            SDL_DestroySurface(conv);
        }
        if (keepMasks_) masks_.emplace(n, std::move(m));
        it = pics_.emplace(n, t).first;
    }
    return it->second;
}

const AlphaMask& PictureCache::mask(int n) {
    static const AlphaMask none;
    get(n);
    auto it = masks_.find(n);
    return it != masks_.end() ? it->second : none;
}

void PictureCache::clear() {
    for (auto& entry : pics_) {
        if (entry.second.tex) glDeleteTextures(1, &entry.second.tex);
    }
    pics_.clear();
    masks_.clear();
}

const TextTex* Renderer::itemTexture(int item) {
    const TextTex& t = itemPics_.get(baseItemId(item));
    return t.tex ? &t : &pink_;
}

bool Renderer::itemPixelSolid(int item, float u, float v) {
    const AlphaMask& m = itemPics_.mask(baseItemId(item));
    if (m.alpha.empty()) return true;
    const int x = std::clamp((int)(u * (float)m.w), 0, m.w - 1);
    const int y = std::clamp((int)(v * (float)m.h), 0, m.h - 1);
    return m.alpha[(size_t)y * (size_t)m.w + (size_t)x] >= 26;
}

const TextTex* Renderer::emoteTexture(EmoteId emote) {
    const TextTex& t = emotePics_.get(idx(emote));
    return t.tex ? &t : &pink_;
}

const Model& Renderer::propModel(PropKind kind) const {
    return propModels_[(size_t)kind];
}

void Renderer::drawFitted(const TextTex& g, const Rect& r, RGBA tint) {
    if (!g.tex) return;

    const float aspect = g.aspect > 0.0f ? g.aspect : 1.0f;
    float w = r.h * aspect, h = r.h;
    if (w > r.w) {
        w = r.w;
        h = r.w / aspect;
    }
    const float x = r.x + (r.w - w) * 0.5f, y = r.y + (r.h - h) * 0.5f;
    drawTexture(g.tex, x, y, w, h, tint);
}

void Renderer::initSky() {
    skyProgram_ = linkProgram("sky", SKY_VS, SKY_FS);
    uSkyInvVP_ = glGetUniformLocation(skyProgram_, "uInvVP");
    uSunDir_   = glGetUniformLocation(skyProgram_, "uSunDir");
    uSunAlpha_ = glGetUniformLocation(skyProgram_, "uSunAlpha");
    uZenith_   = glGetUniformLocation(skyProgram_, "uZenith");
    uHorizon_  = glGetUniformLocation(skyProgram_, "uHorizon");
    uSunCol_   = glGetUniformLocation(skyProgram_, "uSunCol");
    uMoonDir_   = glGetUniformLocation(skyProgram_, "uMoonDir");
    uMoonAlpha_ = glGetUniformLocation(skyProgram_, "uMoonAlpha");
    uMoonCol_   = glGetUniformLocation(skyProgram_, "uMoonCol");
    uStarAlpha_ = glGetUniformLocation(skyProgram_, "uStarAlpha");
    uSkyKind_ = glGetUniformLocation(skyProgram_, "uSkyKind");
    uSkyBaked_ = glGetUniformLocation(skyProgram_, "uBaked");
    uSkyUseBaked_ = glGetUniformLocation(skyProgram_, "uUseBaked");
    glGenFramebuffers(1, &skyBakeFbo_);
    skyBakeWanted_ = true;

    const float skyQuad[8] = { -1.0f, -1.0f,  1.0f, -1.0f,  1.0f, 1.0f,  -1.0f, 1.0f };
    glGenVertexArrays(1, &skyVao_);
    glGenBuffers(1, &skyVbo_);
    glBindVertexArray(skyVao_);
    glBindBuffer(GL_ARRAY_BUFFER, skyVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(skyQuad), skyQuad, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}
