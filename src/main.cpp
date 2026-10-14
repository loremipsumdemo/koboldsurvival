#include <SDL3/SDL.h>
#include <GL/glew.h>
#include <SDL3/SDL_opengl.h>
#include <lua.hpp>
#include <cmath>
#include <cstring>

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
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
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
    bool keyExists;
};

static GameState readState(lua_State* L) {
    GameState s{};
    lua_getglobal(L, "getState");
    lua_call(L, 0, 7);
    s.keyExists = lua_toboolean(L, -1) != 0; lua_pop(L, 1);
    s.doorY = (int)lua_tointeger(L, -1); lua_pop(L, 1);
    s.doorX = (int)lua_tointeger(L, -1); lua_pop(L, 1);
    s.keyY = (int)lua_tointeger(L, -1); lua_pop(L, 1);
    s.keyX = (int)lua_tointeger(L, -1); lua_pop(L, 1);
    s.playerY = (int)lua_tointeger(L, -1); lua_pop(L, 1);
    s.playerX = (int)lua_tointeger(L, -1); lua_pop(L, 1);
    return s;
}

static void sendAction(lua_State* L, const char* action) {
    lua_getglobal(L, "step");
    lua_pushstring(L, action);
    lua_call(L, 1, 0);
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

    lua_State* L = luaL_newstate();
    luaL_openlibs(L);
    if (luaL_dofile(L, "scripts/game.lua") != LUA_OK) {
        SDL_Log("Lua error: %s", lua_tostring(L, -1));
        return 1;
    }

    auto drawQuad = [&](GLuint tex, float cx, float cy, float cz,
        float rightX, float rightY, float rightZ,
        float upX, float upY, float upZ,
        float halfW, float halfH) {
            float verts[20];
            float rx = rightX * halfW, ry = rightY * halfW, rz = rightZ * halfW;
            float ux = upX * halfH, uy = upY * halfH, uz = upZ * halfH;

            float p[4][3] = {
                { cx - rx - ux, cy - ry - uy, cz - rz - uz },
                { cx + rx - ux, cy + ry - uy, cz + rz - uz },
                { cx + rx + ux, cy + ry + uy, cz + rz + uz },
                { cx - rx + ux, cy - ry + uy, cz - rz + uz }
            };
            float uv[4][2] = { { 0,1 },{ 1,1 },{ 1,0 },{ 0,0 } };

            for (int i = 0; i < 4; i++) {
                verts[i * 5 + 0] = p[i][0];
                verts[i * 5 + 1] = p[i][1];
                verts[i * 5 + 2] = p[i][2];
                verts[i * 5 + 3] = uv[i][0];
                verts[i * 5 + 4] = uv[i][1];
            }

            glBindTexture(GL_TEXTURE_2D, tex);
            glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
            glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        };

    auto billboardAxes = [&](float px, float pz,
        float camX, float camZ,
        float& rx, float& rz) {
            float dx = camX - px;
            float dz = camZ - pz;
            float len = sqrtf(dx * dx + dz * dz);
            if (len < 0.0001f) { rx = 1; rz = 0; return; }
            dx /= len; dz /= len;
            rx = -dz;
            rz = dx;
        };

    bool running = true;
    bool fullscreen = true;
    SDL_Event e;

    while (running) {
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) running = false;
            if (e.type == SDL_EVENT_KEY_DOWN) {
                if (e.key.key == SDLK_ESCAPE) { running = false; continue; }
                if (e.key.key == SDLK_F11) {
                    fullscreen = !fullscreen;
                    SDL_SetWindowFullscreen(window, fullscreen);
                    continue;
                }
                if (e.key.key == SDLK_UP)    sendAction(L, "up");
                if (e.key.key == SDLK_DOWN)  sendAction(L, "down");
                if (e.key.key == SDLK_LEFT)  sendAction(L, "left");
                if (e.key.key == SDLK_RIGHT) sendAction(L, "right");
            }
        }

        GameState gs = readState(L);

        int winW, winH;
        SDL_GetWindowSizeInPixels(window, &winW, &winH);
        float aspect = (float)winW / (float)winH;
        glViewport(0, 0, winW, winH);

        glClearColor(0.05f, 0.05f, 0.07f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        float centerX = GRID * TILE_F * 0.5f;
        float centerZ = GRID * TILE_F * 0.5f;
        float camDist = 14.0f;
        float camPitch = 0.9f;
        float camYaw = 0.0f;
        float camX = centerX + camDist * sinf(camYaw) * cosf(camPitch);
        float camY = camDist * sinf(camPitch);
        float camZ = centerZ + camDist * cosf(camYaw) * cosf(camPitch);

        Mat4 proj = perspective(0.9f, aspect, 0.1f, 100.0f);
        Mat4 view = lookAt(camX, camY, camZ, centerX, 0.0f, centerZ, 0.0f, 1.0f, 0.0f);

        glUseProgram(program);
        glUniform1i(uTex, 0);
        glActiveTexture(GL_TEXTURE0);
        glBindVertexArray(vao);

        for (int y = 0; y < GRID; y++) {
            for (int x = 0; x < GRID; x++) {
                float wx = x * TILE_F + TILE_F * 0.5f;
                float wz = y * TILE_F + TILE_F * 0.5f;
                Mat4 model = identity();
                Mat4 mvp = mul(proj, mul(view, model));
                glUniformMatrix4fv(uMVP, 1, GL_FALSE, mvp.m);
                drawQuad(texFloor, wx, 0.0f, wz, 1, 0, 0, 0, 0, 1, TILE_F * 0.5f, TILE_F * 0.5f);
            }
        }

        float spriteH = 1.4f;
        float spriteW = 1.2f;

        auto drawBillboard = [&](GLuint tex, int gx, int gy) {
            float wx = gx * TILE_F + TILE_F * 0.5f;
            float wz = gy * TILE_F + TILE_F * 0.5f;
            float rx, rz;
            billboardAxes(wx, wz, camX, camZ, rx, rz);
            Mat4 mvp = mul(proj, view);
            glUniformMatrix4fv(uMVP, 1, GL_FALSE, mvp.m);
            drawQuad(tex, wx, spriteH * 0.5f, wz, rx, 0, rz, 0, 1, 0, spriteW * 0.5f, spriteH * 0.5f);
            };

        drawBillboard(texDoor, gs.doorX, gs.doorY);
        if (gs.keyExists) drawBillboard(texKey, gs.keyX, gs.keyY);
        drawBillboard(texPlayer, gs.playerX, gs.playerY);

        SDL_GL_SwapWindow(window);
    }

    lua_close(L);
    glDeleteTextures(1, &texFloor);
    glDeleteTextures(1, &texPlayer);
    glDeleteTextures(1, &texKey);
    glDeleteTextures(1, &texDoor);
    glDeleteProgram(program);
    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
    SDL_GL_DestroyContext(ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}