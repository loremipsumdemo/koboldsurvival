#pragma once
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <string>
#include <thread>
#include <vector>
#include <SDL3/SDL.h>
#include "settings.h"

// Monitors, the modes they offer, and how the window is shown. The functions here do arithmetic on the lists
// they are given, so they can be tested without a monitor. queryDisplays and applyDisplay talk to SDL and are
// in display.cpp.

struct Resolution { int w = 0, h = 0; };

// A fullscreen mode: its size and refresh rate, rounded. hz is 0 when the system gives none.
struct VideoMode { int w = 0, h = 0, hz = 0; };

struct MonitorInfo {
    SDL_DisplayID id = 0;
    std::string name;
    int x = 0, y = 0, w = 0, h = 0;      // where the monitor sits on the desktop, and its size
};

struct DisplayInfo {
    std::vector<MonitorInfo> monitors;   // at least one, in the order the system lists them
    int chosen = 0;                      // the monitor the settings use, after clamping
    std::vector<VideoMode> modes;        // exclusive fullscreen modes of the chosen monitor, unique
    int desktopW = 1280, desktopH = 720; // desktop size of the chosen monitor
};

// Standard window sizes that fit inside a desktop of the given size, smallest first
inline std::vector<Resolution> windowedSizes(int maxW, int maxH) {
    static const Resolution kSizes[] = {
        { 800, 600 }, { 1024, 768 }, { 1152, 864 }, { 1280, 720 }, { 1280, 800 }, { 1280, 1024 },
        { 1366, 768 }, { 1440, 900 }, { 1600, 900 }, { 1600, 1200 }, { 1680, 1050 }, { 1920, 1080 },
        { 1920, 1200 }, { 2048, 1152 }, { 2560, 1080 }, { 2560, 1440 }, { 2560, 1600 },
        { 3440, 1440 }, { 3840, 2160 },
    };
    std::vector<Resolution> out;
    for (const Resolution& r : kSizes) {
        if (r.w <= maxW && r.h <= maxH) out.push_back(r);
    }
    return out;
}

// The distinct sizes of the fullscreen modes, largest first
inline std::vector<Resolution> fullscreenSizes(const std::vector<VideoMode>& modes) {
    std::vector<Resolution> out;
    for (const VideoMode& m : modes) {
        const bool seen = std::any_of(out.begin(), out.end(),
                                      [&](const Resolution& r) { return r.w == m.w && r.h == m.h; });
        if (!seen && m.w > 0 && m.h > 0) out.push_back({ m.w, m.h });
    }
    std::sort(out.begin(), out.end(), [](const Resolution& a, const Resolution& b) {
        const long long pa = (long long)a.w * a.h, pb = (long long)b.w * b.h;
        return pa != pb ? pa > pb : a.w > b.w;
    });
    return out;
}

// The distinct refresh rates the modes give at w x h, fastest first. The "default" choice (0) is not in it.
inline std::vector<int> refreshRates(const std::vector<VideoMode>& modes, int w, int h) {
    std::vector<int> out;
    for (const VideoMode& m : modes) {
        if (m.w != w || m.h != h || m.hz <= 0) continue;
        if (std::find(out.begin(), out.end(), m.hz) == out.end()) out.push_back(m.hz);
    }
    std::sort(out.begin(), out.end(), std::greater<int>());
    return out;
}

// The entry of a non-empty list nearest to (w, h)
inline Resolution nearestSize(const std::vector<Resolution>& list, int w, int h) {
    Resolution best = list.front();
    long long bestD = -1;
    for (const Resolution& r : list) {
        const long long dw = r.w - w, dh = r.h - h;
        const long long d = dw * dw + dh * dh;
        if (bestD < 0 || d < bestD) {
            bestD = d;
            best = r;
        }
    }
    return best;
}

// The refresh rate in a non-empty list nearest to hz
inline int nearestRate(const std::vector<int>& rates, int hz) {
    int best = rates.front();
    for (int r : rates) {
        if (std::abs(r - hz) < std::abs(best - hz)) best = r;
    }
    return best;
}

// Makes the settings values the monitor can show. A size or rate the monitor lacks is replaced by the
// nearest one it has, so the menus always show what really applies. Borderless uses the desktop size.
inline void normalizeVideo(GameSettings& s, const DisplayInfo& d) {
    s.monitor = d.chosen;
    const int dw = d.desktopW > 0 ? d.desktopW : 1280;
    const int dh = d.desktopH > 0 ? d.desktopH : 720;

    if (s.displayMode == DISPLAY_BORDERLESS) {
        s.width = dw;
        s.height = dh;
        s.refreshHz = 0;
    } else if (s.displayMode == DISPLAY_WINDOWED) {
        const std::vector<Resolution> sizes = windowedSizes(dw, dh);
        if (!sizes.empty()) {
            const Resolution r = nearestSize(sizes, s.width, s.height);
            s.width = r.w;
            s.height = r.h;
        }
        s.refreshHz = 0;
    } else if (!d.modes.empty()) {
        // Full screen: the size from the monitor's list, then a rate that size really has, or the default
        const Resolution r = nearestSize(fullscreenSizes(d.modes), s.width, s.height);
        s.width = r.w;
        s.height = r.h;
        const std::vector<int> rates = refreshRates(d.modes, s.width, s.height);
        if (rates.empty()) s.refreshHz = 0;
        else if (s.refreshHz != 0) s.refreshHz = nearestRate(rates, s.refreshHz);
    }
}

// Reads the monitors, and the fullscreen modes of the one the setting chooses, from the system
DisplayInfo queryDisplays(int monitorSetting);

// Makes the window show what the settings say. The settings are normalized first, so they are left
// holding what was actually applied.
void applyDisplay(SDL_Window* window, GameSettings& s, const DisplayInfo& d);

// Holds the frame until its budget of 1/fps seconds has passed, counted from frameStart. Sleeps for most of
// the wait and spins the last millisecond, since sleeps overshoot. fps of 0 or less does nothing.
inline void limitFrameRate(std::chrono::steady_clock::time_point frameStart, int fps) {
    using clock = std::chrono::steady_clock;
    if (fps <= 0) return;
    const clock::time_point target = frameStart + std::chrono::nanoseconds(1000000000LL / fps);
    const std::chrono::milliseconds spin(1);
    const clock::duration left = target - clock::now();
    if (left > spin) std::this_thread::sleep_for(left - spin);
    while (clock::now() < target) std::this_thread::yield();
}
