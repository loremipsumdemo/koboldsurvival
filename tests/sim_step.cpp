// Runs the simulation with no window: the game keeps going for a stretch of play, and the same seed gives the same game.
// Runs beside data.pak. Prints each failure and returns non-zero if there were any.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <cstdio>
#include <string>
#include "camera.h"
#include "combat.h"
#include "factions.h"
#include "game_tables.h"
#include "language.h"
#include "level.h"
#include "pak.h"
#include "savegame.h"
#include "simulation.h"
#include "systems_internal.h"
#include "terrain.h"

namespace {
int failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

constexpr float STEP = 1.0f / 60.0f;
constexpr int STEPS = 60 * 120;   // two minutes of play

struct Run {
    SimEvents seen;
    int units = 0;
    double clock = 0.0;
    SaveData end;
};

Run play(Terrain& terrain, unsigned seed) {
    Run run;
    World world;
    Rng rng;
    rng.gen.seed(seed);
    GameState gs;
    const double startClock = gs.clockSeconds;
    setupWorld(world, terrain, rng, true);
    for (int i = 0; i < STEPS; ++i) {
        simStep(world, terrain, rng, gs, STEP, false);
        SimEvents ev = takeEvents(world);
        run.seen.log.insert(run.seen.log.end(), ev.log.begin(), ev.log.end());
        run.seen.sounds.insert(run.seen.sounds.end(), ev.sounds.begin(), ev.sounds.end());
    }
    run.units = (int)world.view<Entity, Transform>().size();
    run.clock = gs.clockSeconds - startClock;
    run.end = captureWorld(world, gs, Camera{}, rng, 1000, "test", MapChoice{ "packed", "test", "test", "" });
    return run;
}

std::string saved(const SaveData& d, const char* path) {
    writeSaveFile(path, d);
    std::string text;
    if (FILE* f = std::fopen(path, "rb")) {
        char buffer[65536];
        size_t got = 0;
        while ((got = std::fread(buffer, 1, sizeof buffer, f)) > 0) text.append(buffer, got);
        std::fclose(f);
    }
    std::remove(path);
    return text;
}
}

int main(int argc, char** argv) {
    const char* base = SDL_GetBasePath();
    if (!pakOpen(std::string(base ? base : "") + "data.pak")) {
        std::printf("FAIL: data.pak not found\n");
        return 1;
    }
    std::string err;
    if (!loadGameTables(err)) {
        std::printf("FAIL: %s\n", err.c_str());
        return 1;
    }
    loadLanguage(LANGUAGE_DEFAULT_ID, err);

    Terrain terrain(512);
    const Run a = play(terrain, 4242);
    check(a.units > 0, "units are still there after two minutes");
    check(a.clock > 0.0, "the clock moves");
    check(!a.seen.log.empty(), "things happen: the log has lines");
    check(!a.seen.sounds.empty(), "things happen: sounds are queued");

    const Run b = play(terrain, 4242);
    const std::string textA = saved(a.end, "sim_step_a.sav"), textB = saved(b.end, "sim_step_b.sav");
    check(!textA.empty(), "the end of the run can be saved");
    // With a path, also keeps the end of the run there, to compare a change against the game before it
    if (argc > 1) writeSaveFile(argv[1], a.end);
    check(textA == textB, "the same seed gives the same game");
    if (textA != textB) {
        size_t at = 0;
        while (at < textA.size() && at < textB.size() && textA[at] == textB[at]) ++at;
        const size_t from = at > 80 ? at - 80 : 0;
        std::printf("     first difference:\n     %s\n     %s\n", textA.substr(from, 160).c_str(), textB.substr(from, 160).c_str());
    }

    const Run c = play(terrain, 777);
    check(saved(c.end, "sim_step_c.sav") != textA, "another seed gives another game");

    // A crowd on one target: at most five attack it, the rest wait, nobody overlaps, and they settle
    {
        World fight;
        Rng fightRng;
        fightRng.gen.seed(11);
        GameState fightState;
        const Vec2 at = systems_detail::findSpawn(terrain);
        const ObjectId target = spawnUnit(fight, terrain, fightRng, FACTION_PLAYER, at.x, at.z);
        Stats tough = *fight.get<Stats>(target);
        for (int i = 0; i < STAT_COUNT; ++i) tough.xp[i] = xpForLevel(MAX_LEVEL);
        fight.add(target, tough);
        std::vector<ObjectId> crowd;
        for (int i = 0; i < 9; ++i) {
            const float turn = 6.2831853f * (float)i / 9.0f;
            const ObjectId u = spawnUnit(fight, terrain, fightRng, 2, at.x + std::cos(turn) * 5.0f, at.z + std::sin(turn) * 5.0f);
            fight.add(u, Equipment{});
            fight.get<Behaviors>(u)->rules.clear();
            fight.get<Combat>(u)->target = target;
            crowd.push_back(u);
        }
        int mostInSlots = 0, inSlots = 0;
        float deepest = 0.0f;
        bool slowSeen = false, slowCorrect = true;
        int lastHp = -1;
        std::vector<Transform> before(crowd.size());
        std::vector<float> walked(crowd.size(), 0.0f);   // standing in place, over the last two seconds
        const int steps = 60 * 8;
        for (int step = 0; step < steps; ++step) {
            for (size_t i = 0; i < crowd.size(); ++i) before[i] = *fight.get<Transform>(crowd[i]);
            simStep(fight, terrain, fightRng, fightState, STEP, false);
            takeEvents(fight);
            inSlots = 0;
            for (ObjectId u : crowd) {
                const AttackSlot* held = fight.get<AttackSlot>(u);
                if (held && held->target == target && held->slot >= 0) ++inSlots;
            }
            mostInSlots = std::max(mostInSlots, inSlots);
            if (step >= 60 * 4) {
                for (size_t i = 0; i < crowd.size(); ++i) {
                    if (outCold(fight, crowd[i])) continue;
                    for (size_t j = i + 1; j < crowd.size(); ++j) {
                        if (!outCold(fight, crowd[j])) deepest = std::max(deepest, 1.0f - flatGap(fight, crowd[i], crowd[j]));
                    }
                }
            }
            if (step >= steps - 120) {
                for (size_t i = 0; i < crowd.size(); ++i) {
                    // Walking into a slot that just opened is fine: this is for those already in their places
                    const AttackSlot* held = fight.get<AttackSlot>(crowd[i]);
                    if (!held || held->closing) continue;
                    const Transform* now = fight.get<Transform>(crowd[i]);
                    walked[i] += std::hypot(now->x - before[i].x, now->z - before[i].z);
                }
            }
            const Needs* needs = fight.get<Needs>(target);
            if (lastHp >= 0 && needs->hp < lastHp && !isDowned(fight, target)) {
                const float normal = fight.get<Entity>(target)->speed * (1.0f - encumbranceOf(fight, target));
                slowSeen = true;
                slowCorrect = slowCorrect && fight.get<Combat>(target)->slowedLeft > 0.0f &&
                              std::fabs(walkSpeedOf(fight, target, false) - normal * DAMAGE_SLOW_FACTOR) < 1e-3f;
            }
            lastHp = needs->hp;
        }
        float mostWalked = 0.0f;
        for (float d : walked) mostWalked = std::max(mostWalked, d);
        std::printf("note: most in slots %d, deepest overlap %.3f, most moved while settled in the last two seconds %.3f tiles\n", mostInSlots, deepest, mostWalked);
        check(mostInSlots == ATTACK_SLOTS, "five fighters take the five slots around a target");
        check(deepest < 0.1f, "fighters in a crowd don't stand inside each other");
        check(mostWalked < 0.25f, "a settled crowd doesn't jitter");
        check(slowSeen && slowCorrect, "taking damage halves speed, however many hits land");
    }

    std::printf("%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
