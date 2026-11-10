#include "app.h"
#include "build_test.h"
#include "debug_log.h"

// A measuring run, for comparing what the graphics options cost. Started by setting the environment variable
// KOBOLD_BENCH before launching: the game switches Debug logging on, loads the newest save, shows it from each camera
// view with each of the build test's cases (app_build_test.cpp) for a few seconds, writes the times to the debug log
// under [bench], and quits. The game itself is not saved.
namespace {
constexpr int VIEW_COUNT = 2;
constexpr const char* VIEW_NAMES[VIEW_COUNT] = { "as saved", "pulled back, looking out to the horizon" };
constexpr Uint64 SETTLE_MS = 1000, MEASURE_MS = 3000;
}

void App::benchStart() {
    if (!std::getenv("KOBOLD_BENCH")) return;
    if (!settings.debugLogging) {
        settings.debugLogging = true;
        saveSettingsFile();
    }
    SDL_GetWindowSizeInPixels(window, &winW, &winH);
    debugLogFrame();
    const std::vector<SaveSummary> found = listSaves();
    if (found.empty()) {
        debuglog::line("bench", "%s: no save to load, nothing measured", BUILD_TEST_NAME);
        return;
    }
    loadSave(found.front().path);
    if (screen != Screen::Playing) {
        debuglog::line("bench", "%s: the newest save (%s) did not load, nothing measured", BUILD_TEST_NAME, found.front().name.c_str());
        return;
    }
    SDL_GL_SetSwapInterval(0);
    bench.on = true;
    bench.step = 0;
    bench.since = SDL_GetTicks();
    bench.saved = cam;
    debuglog::line("bench", "start: %s. Save \"%s\". %d cases from %d views, %.0f s each, vsync and the FPS limit off. "
                   "Times are milliseconds a frame: frame is the whole frame, the rest what the card spends on that surface.",
                   BUILD_TEST_NAME, found.front().name.c_str(), BUILD_TEST_CASE_COUNT, VIEW_COUNT, (double)MEASURE_MS / 1000.0);
}

void App::benchFrame() {
    if (!bench.on) return;
    const int view = bench.step / BUILD_TEST_CASE_COUNT;
    const BenchCase& c = BUILD_TEST_CASES[bench.step % BUILD_TEST_CASE_COUNT];
    cam = bench.saved;
    if (view == 1) {
        cam.distance = 150.0f;
        cam.pitch = 0.30f;
    }
    bench.clouds = c.clouds;
    bench.shadows = c.shadows;
    bench.varied = c.varied;
    bench.renderScale = c.renderScale;
    renderer.setSurfaceBands(c.bands);

    const Uint64 now = SDL_GetTicks();
    const Uint64 spent = now - bench.since;
    if (!bench.measuring && spent >= SETTLE_MS) {
        bench.measuring = true;
        bench.frames = 0;
        bench.ground = bench.water = bench.cloudMs = 0.0;
        bench.measureSince = SDL_GetPerformanceCounter();
    }
    if (!bench.measuring) return;
    const Renderer::PassTimes times = renderer.passTimes();
    bench.ground += times.ground;
    bench.water += times.water;
    bench.cloudMs += times.clouds;
    ++bench.frames;
    if (spent < SETTLE_MS + MEASURE_MS) return;

    const double seconds = (double)(SDL_GetPerformanceCounter() - bench.measureSince) / (double)SDL_GetPerformanceFrequency();
    const double frames = (double)bench.frames;
    float ex, ey, ez;
    cam.eye(ex, ey, ez);
    if (bench.step % BUILD_TEST_CASE_COUNT == 0) {
        debuglog::line("bench", "view %d, %s: camera at %.0f, %.0f, %.0f, %.0f from its focus, pitch %.2f", view + 1, VIEW_NAMES[view],
                       ex, ey, ez, cam.distance, cam.pitch);
    }
    debuglog::line("bench", "view %d | %-36s | frame %6.2f (%3.0f FPS) | ground %6.2f | water %5.2f | clouds %5.2f", view + 1, c.name,
                   seconds * 1000.0 / frames, frames / seconds, bench.ground / frames, bench.water / frames,
                   c.clouds ? bench.cloudMs / frames : 0.0);

    bench.measuring = false;
    bench.since = now;
    if (++bench.step < BUILD_TEST_CASE_COUNT * VIEW_COUNT) return;
    debuglog::line("bench", "end: %s", BUILD_TEST_NAME);
    renderer.setSurfaceBands(true);
    bench.on = false;
    bench.quit = true;
}
