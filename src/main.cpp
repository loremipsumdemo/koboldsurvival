#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <GL/glew.h>
#include <SDL3/SDL_opengl.h>
#include <optional>
#include "camera.h"
#include "components.h"
#include "ecs.h"
#include "model.h"
#include "renderer.h"
#include "resources.h"
#include "systems.h"

namespace {

// Converts a screen pixel into a grid tile by intersecting the view ray with y = 0
std::optional<GridPos> screenToTile(const Camera& cam, float cx, float cz,
                                    float mx, float my, int winW, int winH) {
    Mat4 vp = cam.viewProj(cx, cz, (float)winW / (float)winH);
    float inv[16];
    if (!invert4x4(vp.m, inv)) return std::nullopt;

    float ndcX = (2.0f * mx / (float)winW) - 1.0f;
    float ndcY = 1.0f - (2.0f * my / (float)winH);
    float pNear[4] = { ndcX, ndcY, -1.0f, 1.0f };
    float pFar[4] = { ndcX, ndcY, 1.0f, 1.0f };
    float wNear[4], wFar[4];

    for (int i = 0; i < 4; i++) {
        wNear[i] = inv[0 * 4 + i] * pNear[0] + inv[1 * 4 + i] * pNear[1]
                 + inv[2 * 4 + i] * pNear[2] + inv[3 * 4 + i] * pNear[3];
        wFar[i]  = inv[0 * 4 + i] * pFar[0]  + inv[1 * 4 + i] * pFar[1]
                 + inv[2 * 4 + i] * pFar[2]  + inv[3 * 4 + i] * pFar[3];
    }
    float nX = wNear[0] / wNear[3], nY = wNear[1] / wNear[3], nZ = wNear[2] / wNear[3];
    float fX = wFar[0] / wFar[3], fY = wFar[1] / wFar[3], fZ = wFar[2] / wFar[3];

    float dirY = fY - nY;
    if (dirY == 0.0f) return std::nullopt;
    float t = -nY / dirY;
    if (t <= 0.0f) return std::nullopt;

    float hitX = nX + (fX - nX) * t;
    float hitZ = nZ + (fZ - nZ) * t;
    int gx = (int)std::floor(hitX);
    int gy = (int)std::floor(hitZ);
    if (gx < 0 || gx >= GRID || gy < 0 || gy >= GRID) return std::nullopt;
    return GridPos{ gx, gy };
}

} // namespace

int main(int, char**) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    if (!TTF_Init()) {
        SDL_Log("TTF_Init failed: %s", SDL_GetError());
        return 1;
    }
    TTF_Font* font = TTF_OpenFont("assets/NotoSans_Condensed-Bold.ttf", 64.0f);
    if (!font) {
        SDL_Log("Could not open font: %s", SDL_GetError());
        return 1;
    }

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

    Renderer renderer;
    if (!renderer.init()) return 1;

    // ---- Assets ----
    Assets assets;
    assets.floorTex = loadTexture("assets/floor.bmp");
    assets.playerTex = loadTexture("assets/player.bmp");
    assets.keyTex = loadTexture("assets/key.bmp");
    assets.doorTex = loadTexture("assets/door.bmp");
    assets.backgroundTex = loadTexture("assets/background.bmp");

    assets.glyphPlayer = makeGlyphTexture(font, "@", SDL_Color{ 255, 220, 80, 255 });
    assets.glyphKey    = makeGlyphTexture(font, "K", SDL_Color{ 255, 215, 0, 255 });
    assets.glyphDoor   = makeGlyphTexture(font, "D", SDL_Color{ 170, 110, 60, 255 });
    assets.glyphWagon  = makeGlyphTexture(font, "W", SDL_Color{ 90, 200, 255, 255 });
    assets.glyphFloor  = makeGlyphTexture(font, ".", SDL_Color{ 110, 110, 110, 255 });

    if (!loadGLBModel("assets/wagon.glb", assets.wagonModel)) {
        SDL_Log("Could not load wagon model");
    }

    // ---- World ----
    World world;
    Rng rng;
    Commands commands;
    Camera camera;
    RenderMode mode = RenderMode::Ascii;

    spawnFloor(world, assets);
    generateLevel(world, assets, rng);

    bool running = true;
    bool fullscreen = true;
    bool dragging = false;
    float lastMouseX = 0.0f, lastMouseY = 0.0f;
    Uint64 lastTicks = SDL_GetTicks();
    SDL_Event e;

    while (running) {
        // ---- Input: translate events into commands and camera changes ----
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;

            case SDL_EVENT_KEY_DOWN:
                if (e.key.key == SDLK_ESCAPE) {
                    running = false;
                } else if (e.key.key == SDLK_F11) {
                    fullscreen = !fullscreen;
                    SDL_SetWindowFullscreen(window, fullscreen);
                } else if (e.key.key == SDLK_F2) {
                    mode = (mode == RenderMode::Ascii) ? RenderMode::Sprite : RenderMode::Ascii;
                    SDL_Log("Render mode: %s", mode == RenderMode::Ascii ? "ASCII" : "sprites");
                }
                break;

            case SDL_EVENT_MOUSE_WHEEL:
                if (e.wheel.y > 0.0f) camera.zoom *= 1.1f;
                if (e.wheel.y < 0.0f) camera.zoom /= 1.1f;
                if (camera.zoom < 10.0f) camera.zoom = 10.0f;
                if (camera.zoom > 1000.0f) camera.zoom = 1000.0f;
                break;

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (e.button.button == SDL_BUTTON_MIDDLE) {
                    dragging = true;
                    lastMouseX = e.button.x;
                    lastMouseY = e.button.y;
                } else if (e.button.button == SDL_BUTTON_LEFT) {
                    int winW, winH;
                    SDL_GetWindowSizeInPixels(window, &winW, &winH);
                    Entity p = findPlayer(world);
                    if (p) {
                        Transform* t = world.get<Transform>(p);
                        auto tile = screenToTile(camera, t->x, t->z,
                                                 e.button.x, e.button.y, winW, winH);
                        if (tile) commands.moveTo = *tile;
                    }
                }
                break;

            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (e.button.button == SDL_BUTTON_MIDDLE) dragging = false;
                break;

            case SDL_EVENT_MOUSE_MOTION:
                if (dragging) {
                    float dx = e.motion.x - lastMouseX;
                    float dy = e.motion.y - lastMouseY;
                    lastMouseX = e.motion.x;
                    lastMouseY = e.motion.y;
                    camera.yaw += dx * 0.005f;
                    camera.pitch += dy * 0.005f;
                    if (camera.pitch < 0.15f) camera.pitch = 0.15f;
                    if (camera.pitch > 1.5f) camera.pitch = 1.5f;
                }
                break;
            }
        }

        // ---- Systems ----
        Uint64 now = SDL_GetTicks();
        float dt = (now - lastTicks) / 1000.0f;
        lastTicks = now;
        if (dt > 0.25f) dt = 0.25f;

        moveRequestSystem(world, commands);
        movementSystem(world, dt);
        pickupSystem(world);
        levelSystem(world, assets, rng);

        // ---- Render ----
        int winW, winH;
        SDL_GetWindowSizeInPixels(window, &winW, &winH);
        renderer.drawFrame(world, assets, camera, mode, winW, winH);
        SDL_GL_SwapWindow(window);
    }

    // ---- Shutdown ----
    destroyModel(assets.wagonModel);
    freeAssets(assets);
    renderer.shutdown();
    TTF_CloseFont(font);
    TTF_Quit();
    SDL_GL_DestroyContext(ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
