#include "app.h"
#include "debug_log.h"

// The standard part of the debug log (logs.txt, see debug_log.h), the same from build to build:
//     [session]   once when logging starts: the build, the graphics card, the window and the graphics settings
//     [level]     once for each map entered: which map, how big, how much is on it
//     [overview]  every OVERVIEW_SECONDS while playing: frame rate, the worst frame, what the card spends on the big
//                 surfaces, and where the camera is
// What the build being tested needs on top of that is in app_build_test.cpp, under [test].
namespace {
constexpr Uint64 OVERVIEW_SECONDS = 10;

const char* glText(GLenum name) {
    const GLubyte* text = glGetString(name);
    return text ? reinterpret_cast<const char*>(text) : "?";
}
}

void App::debugLogSession() {
    debuglog::line("session", "start: build %s", BUILD_STAMP);
    debuglog::line("session", "card: %s | %s | OpenGL %s", glText(GL_VENDOR), glText(GL_RENDERER), glText(GL_VERSION));
    debuglog::line("session", "window: %d x %d pixels, display mode %d, vsync %d, FPS limit %d", winW, winH, settings.displayMode,
                   settings.vsync ? 1 : 0, settings.fpsLimit);
    debuglog::line("session", "graphics: render scale %d%%, view range %d, clouds %d, cloud shadows %d, varied ground and water %d",
                   settings.renderScale, settings.viewRange, settings.clouds ? 1 : 0, settings.cloudShadows ? 1 : 0,
                   settings.variedSurfaces ? 1 : 0);
}

void App::debugLogFrame() {
    DebugLogState& d = debugLogState;
    if (settings.debugLogging != d.on) {
        d.on = settings.debugLogging;
        if (!d.on) debuglog::line("session", "logging switched off");
        debuglog::setEnabled(d.on);
        if (d.on) debugLogSession();
        d.level.clear();
        d.since = d.lastFrame = 0;
    }
    if (!d.on) return;

    const bool playing = screen == Screen::Playing && gameActive;
    if (!playing) {
        d.since = d.lastFrame = 0;
        return;
    }
    const std::string level = currentMap.source + "/" + currentMap.stem + "/" + std::to_string(terrain.size());
    if (level != d.level) {
        d.level = level;
        int units = 0, mobs = 0, props = 0, items = 0;
        for (ObjectId e : world.view<Entity, Commandable>()) { (void)e; ++units; }
        for (ObjectId e : world.view<Entity, Mob>()) { (void)e; ++mobs; }
        for (ObjectId e : world.view<Prop>()) { (void)e; ++props; }
        for (ObjectId e : world.view<GroundItem>()) { (void)e; ++items; }
        debuglog::line("level", "map \"%s\" (%s), %d tiles a side, %s; %d player units, %d mobs, %d props, %d ground items",
                       currentMap.name.c_str(), currentMap.stem.c_str(), terrain.size(), terrain.streamed() ? "streamed" : "whole",
                       units, mobs, props, items);
    }

    // A measuring run writes its own lines, and its changing options would make an overview mean nothing
    if (bench.on || bench.quit) {
        d.since = d.lastFrame = 0;
        return;
    }

    const Uint64 now = SDL_GetPerformanceCounter();
    const double perSecond = (double)SDL_GetPerformanceFrequency();
    if (d.since == 0) {
        d.since = d.lastFrame = now;
        d.frames = 0;
        d.worstMs = 0.0;
        return;
    }
    d.worstMs = std::max(d.worstMs, (double)(now - d.lastFrame) * 1000.0 / perSecond);
    d.lastFrame = now;
    ++d.frames;
    const double seconds = (double)(now - d.since) / perSecond;
    if (seconds < (double)OVERVIEW_SECONDS) return;

    const Renderer::PassTimes times = renderer.passTimes();
    float ex, ey, ez;
    cam.eye(ex, ey, ez);
    debuglog::line("overview", "%.1f FPS, worst frame %.1f ms; card ms: ground %.2f, water %.2f, clouds %.2f; camera at %.0f, %.0f, %.0f, "
                   "%.0f from its focus, pitch %.2f", (double)d.frames / seconds, d.worstMs, times.ground, times.water, times.clouds,
                   ex, ey, ez, cam.distance, cam.pitch);
    debugLogBuildTest();
    d.since = now;
    d.frames = 0;
    d.worstMs = 0.0;
}
