#pragma once
// Timers for the game's systems. Built with KOBOLD_PROFILE (the x64-Profile configuration), the time each one takes goes
// to log.txt every 10 seconds, the most costly first. Built without it, PROFILED is only the call it wraps.
//     PROFILED("combatSystem", combatSystem(world, ...));
//     const Result r = PROFILED("workSystem", workSystem(world));
#ifdef KOBOLD_PROFILE

#include <SDL3/SDL.h>
#include <algorithm>
#include <vector>

namespace profile {

inline constexpr Uint64 REPORT_SECONDS = 10;

// One timed place in the code. The game loop is on one thread, so these need no locks.
struct Site {
    const char* name;
    Uint64 total = 0, worst = 0, calls = 0;
    explicit Site(const char* siteName);
};

inline std::vector<Site*>& sites() {
    static std::vector<Site*> all;
    return all;
}

inline Site::Site(const char* siteName) : name(siteName) { sites().push_back(this); }

struct Scope {
    Site& site;
    Uint64 start;
    explicit Scope(Site& s) : site(s), start(SDL_GetPerformanceCounter()) {}
    ~Scope() {
        const Uint64 spent = SDL_GetPerformanceCounter() - start;
        site.total += spent;
        site.worst = std::max(site.worst, spent);
        ++site.calls;
    }
};

// Counts a frame, and every REPORT_SECONDS writes the report and starts over
inline void frameEnd() {
    static Uint64 frames = 0;
    static Uint64 since = SDL_GetPerformanceCounter();
    ++frames;
    const Uint64 now = SDL_GetPerformanceCounter();
    const double perSecond = (double)SDL_GetPerformanceFrequency();
    const double seconds = (double)(now - since) / perSecond;
    if (seconds < (double)REPORT_SECONDS) return;

    std::vector<Site*> order = sites();
    std::sort(order.begin(), order.end(), [](const Site* a, const Site* b) { return a->total > b->total; });
    SDL_Log("PROFILE %.1f s, %.1f FPS: ms per frame | ms per call | worst ms | calls", seconds, (double)frames / seconds);
    for (Site* s : order) {
        if (s->calls == 0) continue;
        const double totalMs = (double)s->total * 1000.0 / perSecond;
        SDL_Log("PROFILE %-22s %8.3f %8.3f %8.3f %8llu", s->name, totalMs / (double)frames, totalMs / (double)s->calls,
                (double)s->worst * 1000.0 / perSecond, (unsigned long long)s->calls);
        s->total = s->worst = s->calls = 0;
    }
    frames = 0;
    since = SDL_GetPerformanceCounter();
}

} // namespace profile

#define PROFILED(name, ...) ([&]() -> decltype(auto) {       \
    static profile::Site profileSite(name);                  \
    const profile::Scope profileScope(profileSite);          \
    return __VA_ARGS__;                                      \
}())
#define PROFILE_FRAME_END() profile::frameEnd()

#else

#define PROFILED(name, ...) (__VA_ARGS__)
#define PROFILE_FRAME_END() ((void)0)

#endif
