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

// Times the stretches of a function one after another: each PROFILE_LAP ends the stretch before it and starts its own, and the last
// one ends when the Laps goes out of scope. For a long function whose parts share their variables, where a scope can't be put round one.
struct Laps {
    Site* site = nullptr;
    Uint64 start = 0;
    void to(Site& next) {
        end();
        site = &next;
        start = SDL_GetPerformanceCounter();
    }
    void end() {
        if (!site) return;
        const Uint64 spent = SDL_GetPerformanceCounter() - start;
        site->total += spent;
        site->worst = std::max(site->worst, spent);
        ++site->calls;
        site = nullptr;
    }
    ~Laps() { end(); }
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
// Times the rest of the block it is put in, as PROFILED times one call
#define PROFILE_JOIN2(a, b) a##b
#define PROFILE_JOIN(a, b) PROFILE_JOIN2(a, b)
#define PROFILE_SCOPE(name)                                                         \
    static profile::Site PROFILE_JOIN(profileSite, __LINE__)(name);                \
    const profile::Scope PROFILE_JOIN(profileScope, __LINE__)(PROFILE_JOIN(profileSite, __LINE__))
// PROFILE_LAPS(lap); then PROFILE_LAP(lap, "name") at the start of each stretch of the function to time (see profile::Laps)
#define PROFILE_LAPS(laps) profile::Laps laps
#define PROFILE_LAP(laps, name)                      \
    do {                                             \
        static profile::Site profileLapSite(name);   \
        (laps).to(profileLapSite);                   \
    } while (0)

#else

#define PROFILED(name, ...) (__VA_ARGS__)
#define PROFILE_FRAME_END() ((void)0)
#define PROFILE_SCOPE(name) ((void)0)
#define PROFILE_LAPS(laps) ((void)0)
#define PROFILE_LAP(laps, name) ((void)0)

#endif
