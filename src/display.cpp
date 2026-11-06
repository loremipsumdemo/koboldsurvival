#include "display.h"
#include "language.h"

#include <cmath>

DisplayInfo queryDisplays(int monitorSetting) {
    DisplayInfo d;

    int count = 0;
    SDL_DisplayID* ids = SDL_GetDisplays(&count);
    for (int i = 0; ids && i < count; ++i) {
        MonitorInfo m;
        m.id = ids[i];
        const char* name = SDL_GetDisplayName(ids[i]);
        m.name = name ? name : langf("UILABEL_MONITOR_N", { std::to_string(i + 1) });
        SDL_Rect bounds{};
        if (SDL_GetDisplayBounds(ids[i], &bounds)) {
            m.x = bounds.x;
            m.y = bounds.y;
            m.w = bounds.w;
            m.h = bounds.h;
        }
        d.monitors.push_back(m);
    }
    SDL_free(ids);

    if (d.monitors.empty()) {
        MonitorInfo m;
        m.id = SDL_GetPrimaryDisplay();
        m.name = langf("UILABEL_MONITOR_N", { "1" });
        m.w = 1280;
        m.h = 720;
        d.monitors.push_back(m);
    }

    d.chosen = std::clamp(monitorSetting, 0, (int)d.monitors.size() - 1);
    const MonitorInfo& m = d.monitors[d.chosen];

    if (const SDL_DisplayMode* desktop = SDL_GetDesktopDisplayMode(m.id)) {
        d.desktopW = desktop->w;
        d.desktopH = desktop->h;
    } else if (m.w > 0 && m.h > 0) {
        d.desktopW = m.w;
        d.desktopH = m.h;
    }

    int modeCount = 0;
    SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(m.id, &modeCount);
    for (int i = 0; modes && i < modeCount; ++i) {
        const SDL_DisplayMode* sm = modes[i];
        if (!sm || sm->w <= 0 || sm->h <= 0) continue;
        VideoMode v;
        v.w = sm->w;
        v.h = sm->h;
        v.hz = (int)std::lround(sm->refresh_rate);
        const bool seen = std::any_of(d.modes.begin(), d.modes.end(), [&](const VideoMode& o) {
            return o.w == v.w && o.h == v.h && o.hz == v.hz;
        });
        if (!seen) d.modes.push_back(v);
    }
    SDL_free(modes);

    std::sort(d.modes.begin(), d.modes.end(), [](const VideoMode& a, const VideoMode& b) {
        const long long pa = (long long)a.w * a.h, pb = (long long)b.w * b.h;
        if (pa != pb) return pa > pb;
        if (a.w != b.w) return a.w > b.w;
        return a.hz > b.hz;
    });
    return d;
}

void applyDisplay(SDL_Window* window, GameSettings& s, const DisplayInfo& d) {
    normalizeVideo(s, d);
    const MonitorInfo* m = d.monitors.empty() ? nullptr : &d.monitors[d.chosen];

    if (s.displayMode == DISPLAY_FULLSCREEN) {
        if (m) SDL_SetWindowPosition(window, m->x, m->y);
        SDL_DisplayMode closest{};
        if (m && SDL_GetClosestFullscreenDisplayMode(m->id, s.width, s.height, (float)s.refreshHz, true, &closest)) {
            SDL_SetWindowFullscreenMode(window, &closest);
        }
        SDL_SetWindowFullscreen(window, true);
    } else if (s.displayMode == DISPLAY_BORDERLESS) {
        SDL_SetWindowFullscreen(window, false);
        SDL_SetWindowBordered(window, false);
        SDL_SetWindowSize(window, s.width, s.height);
        if (m) SDL_SetWindowPosition(window, m->x, m->y);
    } else {
        SDL_SetWindowFullscreen(window, false);
        SDL_SetWindowBordered(window, true);
        SDL_SetWindowSize(window, s.width, s.height);
        if (m) {
            SDL_SetWindowPosition(window, m->x + (m->w - s.width) / 2, m->y + (m->h - s.height) / 2);
        }
    }
}
