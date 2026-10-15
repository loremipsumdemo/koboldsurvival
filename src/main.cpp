#include <SDL3/SDL.h>
#include <GL/glew.h>
#include <SDL3/SDL_opengl.h>
#include <lua.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include "model.h"

const int GRID = 16;
const float TILE_F = 1.0f;

struct Mat4 { float m[16]; };

static Mat4 identity() {
    Mat4 r{}; r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f; return r;
}

static Mat4 mul(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int c = 0; c < 4; c++)
        for (int rw = 0; rw < 4; rw++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a.m[k * 4 + rw] * b.m[c * 4 + k];
            r.m[c * 4 + rw] = s;
        }
    return r;
}

static Mat4 perspective(float fovY, float aspect, float zNear, float zFar) {
    Mat4 r{};
    float f = 1.0f / tanf(fovY * 0.5f);
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = (zFar + zNear) / (zNear - zFar);
    r.m[11] = -1.0f;
    r.m[14] = (2.0f * zFar * zNear) / (zNear - zFar);
    return r;
}

static Mat4 lookAt(float ex, float ey, float ez,
    float cx, float cy, float cz,
    float ux, float uy, float uz) {
    float fx = cx - ex, fy = cy - ey, fz = cz - ez;
    float fl = sqrtf(fx * fx + fy * fy + fz * fz);
    fx /= fl; fy /= fl; fz /= fl;
    float sx = fy * uz - fz * uy;
    float sy = fz * ux - fx * uz;
    float sz = fx * uy - fy * ux;
    float sl = sqrtf(sx * sx + sy * sy + sz * sz);
    sx /= sl; sy /= sl; sz /= sl;
    float ux2 = sy * fz - sz * fy;
    float uy2 = sz * fx - sx * fz;
    float uz2 = sx * fy - sy * fx;
    Mat4 r{};
    r.m[0] = sx;  r.m[4] = sy;  r.m[8] = sz;
    r.m[1] = ux2; r.m[5] = uy2; r.m[9] = uz2;
    r.m[2] = -fx; r.m[6] = -fy; r.m[10] = -fz;
    r.m[12] = -(sx * ex + sy * ey + sz * ez);
    r.m[13] = -(ux2 * ex + uy2 * ey + uz2 * ez);
    r.m[14] = (fx * ex + fy * ey + fz * ez);
    r.m[15] = 1.0f;
    return r;
}

static Mat4 ortho(float l, float r, float b, float t, float n, float f) {
    Mat4 m{};
    m.m[0] = 2.0f / (r - l);
    m.m[5] = 2.0f / (t - b);
    m.m[10] = -2.0f / (f - n);
    m.m[12] = -(r + l) / (r - l);
    m.m[13] = -(t + b) / (t - b);
    m.m[14] = -(f + n) / (f - n);
    m.m[15] = 1.0f;
    return m;
}

static void invert4x4(const float* m, float* out) {
    float inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

    float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (det == 0.0f) { memcpy(out, m, 16 * sizeof(float)); return; }
    det = 1.0f / det;
    for (int i = 0; i < 16; i++) out[i] = inv[i] * det;
}

static GLuint compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, 1024, nullptr, log);
        SDL_Log("Shader error: %s", log);
    }
    return s;
}

static GLuint makeProgram(const char* vs, const char* fs) {
    GLuint v = compileShader(GL_VERTEX_SHADER, vs);
    GLuint f = compileShader(GL_FRAGMENT_SHADER, fs);
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    return p;
}

static GLuint loadTexture(const char* path) {
    SDL_Surface* s = SDL_LoadBMP(path);
    if (!s) return 0;
    SDL_Surface* conv = SDL_ConvertSurface(s, SDL_PIXELFORMAT_ABGR8888);
    SDL_DestroySurface(s);
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, conv->w, conv->h, 0,
        GL_RGBA, GL_UNSIGNED_BYTE, conv->pixels);
    SDL_DestroySurface(conv);
    return t;
}

struct GameState {
    int playerX, playerY;
    int keyX, keyY;
    int doorX, doorY;
    int wagonX, wagonY;
    bool keyExists;
    float visualX, visualZ;
};

static GameState readState(lua_State* L) {
    GameState s{};
    lua_getglobal(L, "getState");
    lua_call(L, 0, 11);
    s.wagonY = (int)lua_tointeger(L, -1); lua_pop(L, 1);
    s.wagonX = (int)lua_tointeger(L, -1); lua_pop(L, 1);
    s.visualZ = (float)lua_tonumber(L, -1); lua_pop(L, 1);
    s.visualX = (float)lua_tonumber(L, -1); lua_pop(L, 1);
    s.keyExists = lua_toboolean(L, -1) != 0;  lua_pop(L, 1);
    s.doorY = (int)lua_tointeger(L, -1);  lua_pop(L, 1);
    s.doorX = (int)lua_tointeger(L, -1);  lua_pop(L, 1);
    s.keyY = (int)lua_tointeger(L, -1);  lua_pop(L, 1);
    s.keyX = (int)lua_tointeger(L, -1);  lua_pop(L, 1);
    s.playerY = (int)lua_tointeger(L, -1);  lua_pop(L, 1);
    s.playerX = (int)lua_tointeger(L, -1);  lua_pop(L, 1);
    return s;
}

static void sendMoveTo(lua_State* L, int gx, int gy) {
    lua_getglobal(L, "step");
    lua_pushstring(L, "moveTo");
    lua_pushinteger(L, gx);
    lua_pushinteger(L, gy);
    lua_call(L, 3, 0);
}

static void sendTick(lua_State* L, float dt) {
    lua_getglobal(L, "step");
    lua_pushstring(L, "tick");
    lua_pushnumber(L, dt);
    lua_call(L, 2, 0);
}

static const char* VS_SRC = R"(
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

static const char* FS_SRC = R"(
#version 330 core
in vec2 vUV;
uniform sampler2D uTex;
out vec4 FragColor;
void main() {
    FragColor = texture(uTex, vUV);
}
)";

static void drawSceneQuad(GLuint vbo, GLint uMVP, const Mat4& mvp,
    GLuint tex, float cx, float cy, float cz,
    float rx, float ry, float rz,
    float ux, float uy, float uz,
    float halfW, float halfH) {
    float verts[20];
    float rxh = rx * halfW, ryh = ry * halfW, rzh = rz * halfW;
    float uxh = ux * halfH, uyh = uy * halfH, uzh = uz * halfH;

    float p[4][3] = {
        { cx - rxh - uxh, cy - ryh - uyh, cz - rzh - uzh },
        { cx + rxh - uxh, cy + ryh - uyh, cz + rzh - uzh },
        { cx + rxh + uxh, cy + ryh + uyh, cz + rzh + uzh },
        { cx - rxh + uxh, cy - ryh + uyh, cz - rzh + uzh }
    };
    float uv[4][2] = { { 0,1 },{ 1,1 },{ 1,0 },{ 0,0 } };

    for (int i = 0; i < 4; i++) {
        verts[i * 5 + 0] = p[i][0];
        verts[i * 5 + 1] = p[i][1];
        verts[i * 5 + 2] = p[i][2];
        verts[i * 5 + 3] = uv[i][0];
        verts[i * 5 + 4] = uv[i][1];
    }

    // Bind our own VBO explicitly so we never write into another model's buffer
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBindTexture(GL_TEXTURE_2D, tex);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
    glUniformMatrix4fv(uMVP, 1, GL_FALSE, mvp.m);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
}

// Draws a loaded glTF model standing on tile (wx, wz), bottom-centred
static void drawModel(const Model& m, GLint uMVP, const Mat4& viewProj,
    float wx, float wz, float targetSize, float yaw) {
    float extent = std::max({ m.maxX - m.minX, m.maxY - m.minY, m.maxZ - m.minZ });
    if (extent <= 0.0f) return;
    float s = targetSize / extent;   // largest side becomes targetSize
    float c = cosf(yaw), sn = sinf(yaw);

    Mat4 model{};
    model.m[0] = s * c;   model.m[2] = -s * sn;
    model.m[5] = s;
    model.m[8] = s * sn;  model.m[10] = s * c;
    model.m[15] = 1.0f;

    // Translate so the rotated/scaled model's bottom-centre lands on (wx, 0, wz)
    float cx = (m.minX + m.maxX) * 0.5f;
    float cz = (m.minZ + m.maxZ) * 0.5f;
    float px = model.m[0] * cx + model.m[8] * cz;
    float py = model.m[5] * m.minY;
    float pz = model.m[2] * cx + model.m[10] * cz;
    model.m[12] = wx - px;
    model.m[13] = -py;
    model.m[14] = wz - pz;

    Mat4 mvp = mul(viewProj, model);
    glUniformMatrix4fv(uMVP, 1, GL_FALSE, mvp.m);

    glBindVertexArray(m.vao);
    for (const DrawRange& d : m.draws) {
        glBindTexture(GL_TEXTURE_2D, d.texture);
        glDrawElements(GL_TRIANGLES, d.indexCount, GL_UNSIGNED_INT,
            (void*)(size_t)(d.firstIndex * sizeof(GLuint)));
    }
}

int main(int argc, char* argv[]) {
    SDL_Init(SDL_INIT_VIDEO);

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    SDL_Window* window = SDL_CreateWindow("Game", 1280, 720,
        SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN | SDL_WINDOW_RESIZABLE);
    SDL_GLContext ctx = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, ctx);
    SDL_GL_SetSwapInterval(1);

    glewExperimental = GL_TRUE;
    GLenum glewErr = glewInit();
    if (glewErr != GLEW_OK) {
        SDL_Log("GLEW init failed: %s", glewGetErrorString(glewErr));
        return 1;
    }
    glGetError();

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    GLuint program = makeProgram(VS_SRC, FS_SRC);
    GLint uMVP = glGetUniformLocation(program, "uMVP");
    GLint uTex = glGetUniformLocation(program, "uTex");

    GLuint texFloor = loadTexture("assets/floor.bmp");
    GLuint texPlayer = loadTexture("assets/player.bmp");
    GLuint texKey = loadTexture("assets/key.bmp");
    GLuint texDoor = loadTexture("assets/door.bmp");
    GLuint texBackground = loadTexture("assets/background.bmp");

    GLuint vao, vbo;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, 4 * 5 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    // Wagon model (glTF/GLB). If it fails to load, the game still runs without it.
    Model wagon;
    if (!loadGLBModel("assets/wagon.glb", wagon)) {
        SDL_Log("Could not load wagon model");
    }

    lua_State* L = luaL_newstate();
    luaL_openlibs(L);
    if (luaL_dofile(L, "scripts/game.lua") != LUA_OK) {
        SDL_Log("Lua error: %s", lua_tostring(L, -1));
        return 1;
    }

    bool running = true;
    bool fullscreen = true;
    bool dragging = false;
    float camYaw = 0.0f;
    float camPitch = 0.9f;
    float zoom = 100.0f;
    float lastMouseX = 0.0f, lastMouseY = 0.0f;
    Uint64 lastTicks = SDL_GetTicks();
    SDL_Event e;

    while (running) {
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) running = false;

            if (e.type == SDL_EVENT_KEY_DOWN) {
                if (e.key.key == SDLK_ESCAPE) running = false;
                if (e.key.key == SDLK_F11) {
                    fullscreen = !fullscreen;
                    SDL_SetWindowFullscreen(window, fullscreen);
                }
            }

            if (e.type == SDL_EVENT_MOUSE_WHEEL) {
                if (e.wheel.y > 0.0f) zoom *= 1.1f;
                if (e.wheel.y < 0.0f) zoom /= 1.1f;
                if (zoom < 10.0f)    zoom = 10.0f;
                if (zoom > 1000.0f)  zoom = 1000.0f;
            }

            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                if (e.button.button == SDL_BUTTON_MIDDLE) {
                    dragging = true;
                    lastMouseX = e.button.x;
                    lastMouseY = e.button.y;
                }
            }

            if (e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                if (e.button.button == SDL_BUTTON_MIDDLE) dragging = false;
            }

            if (e.type == SDL_EVENT_MOUSE_MOTION && dragging) {
                float dx = e.motion.x - lastMouseX;
                float dy = e.motion.y - lastMouseY;
                lastMouseX = e.motion.x;
                lastMouseY = e.motion.y;
                camYaw += dx * 0.005f;
                camPitch += dy * 0.005f;
                if (camPitch < 0.15f) camPitch = 0.15f;
                if (camPitch > 1.5f)  camPitch = 1.5f;
            }

            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                e.button.button == SDL_BUTTON_LEFT) {
                int mx = (int)e.button.x;
                int my = (int)e.button.y;
                int winW, winH;
                SDL_GetWindowSizeInPixels(window, &winW, &winH);

                GameState gs = readState(L);
                float centerX = gs.visualX;
                float centerZ = gs.visualZ;

                float camDist = 10.0f * (100.0f / zoom);
                float camX = centerX + camDist * sinf(camYaw) * cosf(camPitch);
                float camY = camDist * sinf(camPitch);
                float camZ = centerZ + camDist * cosf(camYaw) * cosf(camPitch);

                Mat4 proj = perspective(0.9f, (float)winW / (float)winH, 0.1f, 100.0f);
                Mat4 view = lookAt(camX, camY, camZ, centerX, 0.0f, centerZ, 0.0f, 1.0f, 0.0f);
                Mat4 vp = mul(proj, view);

                float invVP[16];
                invert4x4(vp.m, invVP);

                float ndcX = (2.0f * mx / (float)winW) - 1.0f;
                float ndcY = 1.0f - (2.0f * my / (float)winH);
                float pNear[4] = { ndcX, ndcY, -1.0f, 1.0f };
                float pFar[4] = { ndcX, ndcY,  1.0f, 1.0f };
                float wNear[4], wFar[4];

                for (int i = 0; i < 4; i++) {
                    wNear[i] = invVP[0 * 4 + i] * pNear[0] + invVP[1 * 4 + i] * pNear[1]
                        + invVP[2 * 4 + i] * pNear[2] + invVP[3 * 4 + i] * pNear[3];
                    wFar[i] = invVP[0 * 4 + i] * pFar[0] + invVP[1 * 4 + i] * pFar[1]
                        + invVP[2 * 4 + i] * pFar[2] + invVP[3 * 4 + i] * pFar[3];
                }
                float nX = wNear[0] / wNear[3], nY = wNear[1] / wNear[3], nZ = wNear[2] / wNear[3];
                float fX = wFar[0] / wFar[3], fY = wFar[1] / wFar[3], fZ = wFar[2] / wFar[3];

                float dirX = fX - nX, dirY = fY - nY, dirZ = fZ - nZ;
                if (dirY != 0.0f) {
                    float t = -nY / dirY;
                    if (t > 0.0f) {
                        float hitX = nX + dirX * t;
                        float hitZ = nZ + dirZ * t;
                        int gx = (int)floorf(hitX);
                        int gy = (int)floorf(hitZ);
                        if (gx >= 0 && gx < GRID && gy >= 0 && gy < GRID) {
                            sendMoveTo(L, gx, gy);
                        }
                    }
                }
            }
        }

        Uint64 now = SDL_GetTicks();
        float dt = (now - lastTicks) / 1000.0f;
        lastTicks = now;
        if (dt > 0.25f) dt = 0.25f;
        sendTick(L, dt);

        GameState gs = readState(L);

        int winW, winH;
        SDL_GetWindowSizeInPixels(window, &winW, &winH);
        float aspect = (float)winW / (float)winH;
        glViewport(0, 0, winW, winH);

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        float centerX = gs.visualX;
        float centerZ = gs.visualZ;
        float camDist = 10.0f * (100.0f / zoom);
        float camX = centerX + camDist * sinf(camYaw) * cosf(camPitch);
        float camY = camDist * sinf(camPitch);
        float camZ = centerZ + camDist * cosf(camYaw) * cosf(camPitch);

        Mat4 proj = perspective(0.9f, aspect, 0.1f, 100.0f);
        Mat4 view = lookAt(camX, camY, camZ, centerX, 0.0f, centerZ, 0.0f, 1.0f, 0.0f);
        Mat4 viewProj = mul(proj, view);

        glUseProgram(program);
        glUniform1i(uTex, 0);
        glActiveTexture(GL_TEXTURE0);
        glBindVertexArray(vao);

        // Background (screen-space, no depth)
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        Mat4 orthoMat = ortho(-1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f);
        drawSceneQuad(vbo, uMVP, orthoMat, texBackground,
            0.0f, 0.0f, 0.0f,
            1.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f,
            1.0f, 1.0f);
        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);

        // Floor
        for (int y = 0; y < GRID; y++) {
            for (int x = 0; x < GRID; x++) {
                float wx = x * TILE_F + TILE_F * 0.5f;
                float wz = y * TILE_F + TILE_F * 0.5f;
                drawSceneQuad(vbo, uMVP, viewProj, texFloor,
                    wx, 0.0f, wz,
                    1, 0, 0,
                    0, 0, 1,
                    TILE_F * 0.5f, TILE_F * 0.5f);
            }
        }

        float spriteH = 1.4f;
        float spriteW = 1.2f;

        auto drawBillboardAt = [&](GLuint tex, float wx, float wz) {
            float dx = camX - wx;
            float dz = camZ - wz;
            float len = sqrtf(dx * dx + dz * dz);
            if (len < 0.0001f) { dx = 0; dz = 1; len = 1; }
            dx /= len; dz /= len;
            float rx = -dz, rz = dx;
            drawSceneQuad(vbo, uMVP, viewProj, tex,
                wx, spriteH * 0.5f, wz,
                rx, 0, rz,
                0, 1, 0,
                spriteW * 0.5f, spriteH * 0.5f);
            };

        auto drawBillboardTile = [&](GLuint tex, int gx, int gy) {
            float wx = gx * TILE_F + TILE_F * 0.5f;
            float wz = gy * TILE_F + TILE_F * 0.5f;
            drawBillboardAt(tex, wx, wz);
            };

        drawBillboardTile(texDoor, gs.doorX, gs.doorY);
        if (gs.keyExists) drawBillboardTile(texKey, gs.keyX, gs.keyY);
        drawBillboardAt(texPlayer, gs.visualX, gs.visualZ);

        // 3D wagon entity on its tile
        if (wagon.vao) {
            drawModel(wagon, uMVP, viewProj,
                gs.wagonX + 0.5f * TILE_F, gs.wagonY + 0.5f * TILE_F,
                0.9f,   // size in world units (about one tile)
                0.0f);  // yaw in radians
        }

        SDL_GL_SwapWindow(window);
    }

    lua_close(L);
    destroyModel(wagon);
    glDeleteTextures(1, &texFloor);
    glDeleteTextures(1, &texPlayer);
    glDeleteTextures(1, &texKey);
    glDeleteTextures(1, &texDoor);
    glDeleteTextures(1, &texBackground);
    glDeleteProgram(program);
    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
    SDL_GL_DestroyContext(ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}