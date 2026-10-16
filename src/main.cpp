#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <GL/glew.h>
#include <SDL3/SDL_opengl.h>
#include <algorithm>
#include <cmath>
#include <ctime>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include "camera.h"
#include "components.h"
#include "ecs.h"
#include "renderer.h"
#include "resources.h"
#include "systems.h"
#include "terrain.h"

namespace {

constexpr float OVERLAY_HOLD_MS = 1000.0f;   // level-start image stays fully visible
constexpr float OVERLAY_FADE_MS = 500.0f;    // then fades out over this long

// Ray through a pixel, intersected with the terrain surface. Returns world (x, z).
std::optional<std::pair<float, float>> pickGround(const Camera& cam, const Terrain& t,
                                                  float mx, float my, int W, int H) {
    Mat4 vp = cam.viewProj((float)W / (float)H);
    float inv[16];
    if (!invert4x4(vp.m, inv)) return std::nullopt;

    float ndcX = 2.0f * mx / (float)W - 1.0f;
    float ndcY = 1.0f - 2.0f * my / (float)H;

    auto unproject = [&](float ndcZ, float out[3]) {
        float p[4] = { ndcX, ndcY, ndcZ, 1.0f };
        float r[4];
        for (int i = 0; i < 4; ++i) {
            r[i] = inv[0 * 4 + i] * p[0] + inv[1 * 4 + i] * p[1]
                 + inv[2 * 4 + i] * p[2] + inv[3 * 4 + i] * p[3];
        }
        out[0] = r[0] / r[3];
        out[1] = r[1] / r[3];
        out[2] = r[2] / r[3];
    };

    float n[3], f[3];
    unproject(-1.0f, n);
    unproject(1.0f, f);

    // Iterate: intersect with the plane at the terrain height under the last hit
    float groundY = 0.0f, hx = 0.0f, hz = 0.0f;
    for (int iter = 0; iter < 6; ++iter) {
        float dy = f[1] - n[1];
        if (std::fabs(dy) < 1e-6f) return std::nullopt;
        float s = (groundY - n[1]) / dy;
        hx = n[0] + (f[0] - n[0]) * s;
        hz = n[2] + (f[2] - n[2]) * s;
        groundY = t.heightAt(hx, hz);
    }

    if (hx < 0.0f || hz < 0.0f || hx >= MAP_SIZE || hz >= MAP_SIZE) return std::nullopt;
    return std::make_pair(hx, hz);
}

// World point to pixel coordinates. Returns false if it's behind the camera.
bool worldToScreen(const Mat4& vp, float x, float y, float z, int W, int H, float& sx, float& sy) {
    float c[4];
    for (int i = 0; i < 4; ++i) {
        c[i] = vp.m[0 * 4 + i] * x + vp.m[1 * 4 + i] * y + vp.m[2 * 4 + i] * z + vp.m[3 * 4 + i];
    }
    if (c[3] <= 0.0f) return false;
    sx = (c[0] / c[3] * 0.5f + 0.5f) * (float)W;
    sy = (1.0f - (c[1] / c[3] * 0.5f + 0.5f)) * (float)H;
    return true;
}

void clearSelection(World& w) {
    for (Entity e : w.view<Selected>()) w.remove<Selected>(e);
}

std::vector<Entity> selectedUnits(World& w) {
    std::vector<Entity> out;
    for (Entity u : w.view<Unit, Selected, Commandable>()) out.push_back(u);
    return out;
}

// Click selection: nearest unit within one tile of the clicked ground point
void clickSelect(World& w, const Terrain& t, const Camera& cam,
                 float mx, float my, int W, int H, bool additive) {
    if (!additive) clearSelection(w);
    auto hit = pickGround(cam, t, mx, my, W, H);
    if (!hit) return;

    Entity best = NULL_ENTITY;
    float bestD2 = 1.0f;
    for (Entity u : w.view<Unit, Transform>()) {
        Transform* tr = w.get<Transform>(u);
        float dx = tr->x - hit->first, dz = tr->z - hit->second;
        float d2 = dx * dx + dz * dz;
        if (d2 < bestD2) { bestD2 = d2; best = u; }
    }
    if (best) w.add(best, Selected{});
}

// Box selection: every unit whose screen position falls inside the rectangle
void boxSelect(World& w, const Camera& cam, float x0, float y0, float x1, float y1,
               int W, int H, bool additive) {
    if (!additive) clearSelection(w);
    float minX = std::min(x0, x1), maxX = std::max(x0, x1);
    float minY = std::min(y0, y1), maxY = std::max(y0, y1);
    Mat4 vp = cam.viewProj((float)W / (float)H);

    for (Entity u : w.view<Unit, Transform, Commandable>()) {
        Transform* tr = w.get<Transform>(u);
        float sx, sy;
        if (worldToScreen(vp, tr->x, tr->y + 0.5f, tr->z, W, H, sx, sy)
            && sx >= minX && sx <= maxX && sy >= minY && sy <= maxY) {
            w.add(u, Selected{});
        }
    }
}

// Right click: order the selected units to a tile, or into the door if the building was clicked
void rightClickOrder(World& w, Terrain& t, const Camera& cam, float mx, float my, int W, int H) {
    auto hit = pickGround(cam, t, mx, my, W, H);
    if (!hit) return;

    GridPos tile{ (int)std::floor(hit->first), (int)std::floor(hit->second) };
    Door* door = findDoor(w);
    bool onDoor = door
               && tile.x >= door->origin.x && tile.x < door->origin.x + 3
               && tile.y >= door->origin.y && tile.y < door->origin.y + 3;

    issueMoveOrder(w, t, selectedUnits(w), tile, onDoor);
}

// Draws one loading frame and keeps the window responsive while the level loads
void loadingFrame(SDL_Window* window, Renderer& renderer, float progress) {
    SDL_PumpEvents();
    int winW, winH;
    SDL_GetWindowSizeInPixels(window, &winW, &winH);
    renderer.drawLoadingScreen(progress, winW, winH);
    SDL_GL_SwapWindow(window);
}

// Builds a fresh level: new terrain, new entities. Used at startup and on restart.
// progress, if given, is called as the level loads (0..1), for the loading screen.
void startLevel(World& world, Terrain& terrain, Renderer& renderer, Rng& rng, unsigned seed,
                const std::function<void(float)>& progress = {}) {
    auto report = [&](float p) { if (progress) progress(p); };
    terrain.generate(seed);
    report(0.35f);
    renderer.uploadTerrain(terrain);
    report(0.7f);
    world = World{};
    rng.gen.seed(seed);
    setupWorld(world, terrain, rng);
    report(0.9f);
}

// Puts the camera on the tile under the lowest-numbered unit, so the game starts there
void focusCameraOnFirstUnit(World& w, Camera& cam) {
    Entity first = NULL_ENTITY;
    for (Entity u : w.view<Unit, Transform, Commandable>()) {
        if (first == NULL_ENTITY || u < first) first = u;
    }
    if (first == NULL_ENTITY) return;

    const Transform* t = w.get<Transform>(first);
    cam.focusX = std::floor(t->x) + 0.5f;
    cam.focusY = t->y;
    cam.focusZ = std::floor(t->z) + 0.5f;
    cam.clampState();
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
    TTF_Font* font = TTF_OpenFont("assets/font/NotoSans_Condensed-Bold.ttf", 64.0f);
    if (!font) {
        SDL_Log("Could not open font: %s", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    SDL_Window* window = SDL_CreateWindow("RTS", 1280, 720,
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
    if (!renderer.init(font)) return 1;

    // ---- Level setup ----
    Terrain terrain;
    World world;
    Rng rng;
    loadingFrame(window, renderer, 0.1f);
    startLevel(world, terrain, renderer, rng, (unsigned)std::time(nullptr),
               [&](float p) { loadingFrame(window, renderer, p); });
    loadingFrame(window, renderer, 1.0f);

    GameState gs;
    Selection sel;
    Camera cam;
    focusCameraOnFirstUnit(world, cam);

    bool leftDown = false, middleDown = false;
    bool running = true, fullscreen = true;
    float pressX = 0.0f, pressY = 0.0f;
    float lastX = 0.0f, lastY = 0.0f;
    std::string lastTitle;

    // Level-start image: visible for OVERLAY_HOLD_MS, then fades out
    float overlayMs = 0.0f;
    bool overlayOn = true;

    Uint64 lastTicks = SDL_GetTicks();
    SDL_Event e;

    while (running) {
        int winW, winH;
        SDL_GetWindowSizeInPixels(window, &winW, &winH);

        // ---- Input ----
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;

            case SDL_EVENT_KEY_DOWN:
                if (e.key.key == SDLK_ESCAPE) running = false;
                if (e.key.key == SDLK_F11) {
                    fullscreen = !fullscreen;
                    SDL_SetWindowFullscreen(window, fullscreen);
                }
                break;

            case SDL_EVENT_MOUSE_WHEEL:
                cam.distance *= (e.wheel.y > 0.0f) ? 0.9f : 1.1f;
                cam.clampState();
                break;

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (e.button.button == SDL_BUTTON_LEFT) {
                    leftDown = true;
                    pressX = e.button.x;
                    pressY = e.button.y;
                    sel.dragging = false;
                } else if (e.button.button == SDL_BUTTON_MIDDLE) {
                    // Pressing alone changes nothing. Dragging orbits around the current focus.
                    middleDown = true;
                    lastX = e.button.x;
                    lastY = e.button.y;
                } else if (e.button.button == SDL_BUTTON_RIGHT) {
                    rightClickOrder(world, terrain, cam, e.button.x, e.button.y, winW, winH);
                }
                break;

            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (e.button.button == SDL_BUTTON_LEFT && leftDown) {
                    leftDown = false;
                    bool additive = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
                    if (sel.dragging) {
                        boxSelect(world, cam, sel.x0, sel.y0, sel.x1, sel.y1, winW, winH, additive);
                    } else {
                        clickSelect(world, terrain, cam, e.button.x, e.button.y, winW, winH, additive);
                    }
                    sel.dragging = false;
                }
                if (e.button.button == SDL_BUTTON_MIDDLE) middleDown = false;
                break;

            case SDL_EVENT_MOUSE_MOTION:
                if (leftDown) {
                    float dx = e.motion.x - pressX, dy = e.motion.y - pressY;
                    if (!sel.dragging && dx * dx + dy * dy > 36.0f) sel.dragging = true;
                    if (sel.dragging) {
                        sel.x0 = pressX; sel.y0 = pressY;
                        sel.x1 = e.motion.x; sel.y1 = e.motion.y;
                    }
                }
                if (middleDown) {
                    cam.yaw += (e.motion.x - lastX) * 0.005f;
                    cam.pitch += (e.motion.y - lastY) * 0.005f;
                    cam.clampState();
                    lastX = e.motion.x;
                    lastY = e.motion.y;
                }
                break;
            }
        }

        // ---- Timing ----
        Uint64 now = SDL_GetTicks();
        float dt = (now - lastTicks) / 1000.0f;
        lastTicks = now;
        if (dt > 0.25f) dt = 0.25f;

        // ---- Camera: keyboard panning and edge scrolling ----
        const bool* keys = SDL_GetKeyboardState(nullptr);
        float right = 0.0f, forward = 0.0f;
        if (keys[SDL_SCANCODE_D]) right += 1.0f;
        if (keys[SDL_SCANCODE_A]) right -= 1.0f;
        if (keys[SDL_SCANCODE_W]) forward += 1.0f;
        if (keys[SDL_SCANCODE_S]) forward -= 1.0f;

        if (!leftDown && !middleDown) {
            float mx, my;
            SDL_GetMouseState(&mx, &my);
            if (mx < 6.0f) right -= 1.0f;
            if (mx > winW - 6.0f) right += 1.0f;
            if (my < 6.0f) forward += 1.0f;
            if (my > winH - 6.0f) forward -= 1.0f;
        }

        float panSpeed = cam.distance * 0.9f * dt;
        cam.pan(right * panSpeed, forward * panSpeed);
        cam.updateClearance(terrain, dt);

        // ---- Systems ----
        squadSystem(world, dt);
        steeringSystem(world, terrain, gs, dt);
        pickupSystem(world, terrain, gs);
        cleanupSquads(world);
        updateWinState(world, gs);

        // ---- Restart: every unit has passed through the door, so start a new level ----
        if (gs.won) {
            startLevel(world, terrain, renderer, rng, (unsigned)rng.gen());
            focusCameraOnFirstUnit(world, cam);
            gs = GameState{};
            sel = Selection{};
            leftDown = false;
            overlayMs = 0.0f;
            overlayOn = true;
        }

        // ---- Level-start image timing ----
        float overlayAlpha = 0.0f;
        if (overlayOn) {
            overlayMs += dt * 1000.0f;
            if (overlayMs >= OVERLAY_HOLD_MS + OVERLAY_FADE_MS) {
                overlayOn = false;
            } else if (overlayMs < OVERLAY_HOLD_MS) {
                overlayAlpha = 1.0f;
            } else {
                overlayAlpha = 1.0f - (overlayMs - OVERLAY_HOLD_MS) / OVERLAY_FADE_MS;
            }
        }

        // ---- HUD: shown in the window title, since there's no text rendering ----
        std::string title = "RTS   Keys " + std::to_string(gs.keysCollected) + "/" + std::to_string(KEY_COUNT)
                          + "   Entered " + std::to_string(gs.unitsEntered) + "/" + std::to_string(UNIT_COUNT);
        if (title != lastTitle) {
            SDL_SetWindowTitle(window, title.c_str());
            lastTitle = title;
        }

        // ---- Render ----
        renderer.drawFrame(world, terrain, cam, sel, overlayAlpha, winW, winH);
        SDL_GL_SwapWindow(window);
    }

    // ---- Shutdown ----
    renderer.shutdown();
    TTF_CloseFont(font);
    TTF_Quit();
    SDL_GL_DestroyContext(ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
