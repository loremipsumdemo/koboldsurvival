// Runs the simulation with no window: the game keeps going for a stretch of play, and the same seed gives the same game.
// Runs beside data.pak. Prints each failure and returns non-zero if there were any.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <cstdio>
#include <string>
#include "camera.h"
#include "combat.h"
#include "encumbrance.h"
#include "factions.h"
#include "game_tables.h"
#include "airship.h"
#include "build.h"
#include "items.h"
#include "inventory.h"
#include "hostility.h"
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

    // Formation shapes: every unit gets a place, and nobody's place is too close to another's
    for (int shape = 0; shape < (int)FormationShape::Count; ++shape) {
        for (int count : { 2, 5, 12, 30 }) {
            const std::vector<Vec2> places = formationOffsets((FormationShape)shape, count, FORMATION_TIGHT_SPACING);
            float nearest = 1e9f;
            for (size_t i = 0; i < places.size(); ++i) {
                for (size_t j = i + 1; j < places.size(); ++j) nearest = std::min(nearest, std::hypot(places[i].x - places[j].x, places[i].z - places[j].z));
            }
            check((int)places.size() == count && nearest >= 1.0f, "formation " + std::to_string(shape) + " of " + std::to_string(count) + " has room for everyone");
        }
    }
    check(formationOffsets(FormationShape::Line, 5, FORMATION_TIGHT_SPACING, 20.0f).back().x > 9.0f, "a drag widens a line");

    // Slots go to the nearest units: a line walking onto its own slots doesn't cross over
    {
        const std::vector<Vec2> from{ { 0.0f, 0.0f }, { 2.0f, 0.0f }, { 4.0f, 0.0f } };
        const std::vector<Vec2> to{ { 4.0f, 9.0f }, { 2.0f, 9.0f }, { 0.0f, 9.0f } };
        const std::vector<int> slots = assignFormationSlots(from, to, 0.0f, 1.0f);
        check(slots[0] == 2 && slots[1] == 1 && slots[2] == 0, "units take the slots nearest them");
    }

    // Every shape, walked to and formed up where an outsider stands in the way, all get settled and nobody overlaps
    for (int shape = 0; shape < (int)FormationShape::Count; ++shape) {
        World squad;
        Rng squadRng;
        squadRng.gen.seed(21);
        GameState squadState;
        const Vec2 at = systems_detail::findSpawn(terrain);
        std::vector<ObjectId> units;
        for (int i = 0; i < 12; ++i) {
            const ObjectId u = spawnUnit(squad, terrain, squadRng, FACTION_PLAYER, at.x - 5.0f + (float)(i % 4) * 1.5f, at.z - 5.0f + (float)(i / 4) * 1.5f);
            squad.get<Behaviors>(u)->rules.clear();
            units.push_back(u);
        }
        const Vec2 dest{ at.x + 3.0f, at.z + 3.0f };
        const ObjectId outsider = spawnUnit(squad, terrain, squadRng, FACTION_NOMAD, dest.x, dest.z);
        squad.get<Behaviors>(outsider)->rules.clear();
        squad.add(outsider, InConversation{});
        FormationSpec spec;
        spec.choice.shape = (FormationShape)shape;
        issueFormationOrder(squad, terrain, units, dest, spec);
        for (int step = 0; step < 60 * 30; ++step) {
            simStep(squad, terrain, squadRng, squadState, STEP, false);
            takeEvents(squad);
        }
        int settled = 0, near = 0;
        float deepest = 0.0f, far = 0.0f;
        for (size_t i = 0; i < units.size(); ++i) {
            const ActiveSquadMember* sm = squad.get<ActiveSquadMember>(units[i]);
            const ActiveSquad* sq = sm ? squad.get<ActiveSquad>(sm->squad) : nullptr;
            if (!sq) continue;
            settled += sm->settled ? 1 : 0;
            const Transform* tr = squad.get<Transform>(units[i]);
            const Vec2 spot = sq->finalSpots[(size_t)sm->slot];
            const float gap = std::hypot(tr->x - spot.x, tr->z - spot.z);
            far = std::max(far, gap);
            near += gap <= 0.5f ? 1 : 0;
            for (size_t j = i + 1; j < units.size(); ++j) deepest = std::max(deepest, 1.0f - flatGap(squad, units[i], units[j]));
        }
        std::printf("note: shape %d: settled %d, within half a tile of their slots %d, furthest %.2f, deepest overlap %.3f\n", shape, settled, near, far, deepest);
        check(settled == 12, "shape " + std::to_string(shape) + ": everyone stops trying");
        check(near == 12, "shape " + std::to_string(shape) + ": everyone reaches a slot despite an outsider in the way");
        check(deepest < 0.1f, "shape " + std::to_string(shape) + ": nobody overlaps");

        // Then in place: they change shape where they stand
        reformUnits(squad, terrain, units, FormationChoice{ (FormationShape)((shape + 1) % (int)FormationShape::Count), true });
        for (int step = 0; step < 60 * 20; ++step) {
            simStep(squad, terrain, squadRng, squadState, STEP, false);
            takeEvents(squad);
        }
        int reformed = 0;
        for (ObjectId u : units) {
            const ActiveSquadMember* sm = squad.get<ActiveSquadMember>(u);
            reformed += sm && sm->settled ? 1 : 0;
        }
        check(reformed == 12, "shape " + std::to_string(shape) + ": everyone settles after changing shape");
    }

    // Looking ahead: two units of different factions walking head on pass each other without touching, and one walking
    // through a bystander goes round it while the bystander steps aside. Units of one faction walk through each other.
    int otherFaction = -1;
    for (const FactionDef& f : factions()) {
        World probe;
        if (f.id != FACTION_PLAYER && f.id != FACTION_NOMAD && !isMobFaction(f.id) && !factionsHostile(probe, f.id, FACTION_PLAYER)) {
            otherFaction = f.id;
            break;
        }
    }
    for (int bystander = 0; bystander < 3; ++bystander) {
        const bool allies = bystander == 2;
        World pass;
        Rng passRng;
        passRng.gen.seed(5);
        GameState passState;
        const Vec2 at = systems_detail::findSpawn(terrain);
        const Vec2 left = terrain.snap({ at.x - 4.0f, at.z }), right = terrain.snap({ at.x + 4.0f, at.z });
        const ObjectId a = spawnUnit(pass, terrain, passRng, FACTION_PLAYER, left.x, left.z);
        const ObjectId b = spawnUnit(pass, terrain, passRng, allies ? FACTION_PLAYER : otherFaction, bystander == 1 ? at.x : right.x,
                                     bystander == 1 ? at.z : right.z);
        for (ObjectId u : { a, b }) pass.get<Behaviors>(u)->rules.clear();
        issueMoveOrder(pass, terrain, { a }, right);
        if (bystander != 1) issueMoveOrder(pass, terrain, { b }, left);
        float closest = 1e9f;
        for (int step = 0; step < 60 * 8; ++step) {
            simStep(pass, terrain, passRng, passState, STEP, false);
            takeEvents(pass);
            closest = std::min(closest, flatGap(pass, a, b));
        }
        const Transform* ta = pass.get<Transform>(a);
        const bool arrived = std::hypot(ta->x - right.x, ta->z - right.z) <= 0.5f;
        const std::string what = allies ? "two walkers of one faction" : bystander ? "a walker and a bystander" : "two walkers head on";
        std::printf("note: %s: closest %.2f\n", what.c_str(), closest);
        if (allies) check(closest < 0.9f, what + " walk through each other");
        else check(closest > 1.05f, what + " never touch");
        check(arrived, what + ": the walker still gets where it was going");
    }

    // Attacking a unit of a friendly faction: it and its friends nearby fight back, and follow you a little way.
    // Once with plain camp units, and once with an Altar's citizen and military.
    for (const bool homes : { false, true }) {
        int friendly = -1;
        for (const FactionDef& f : factions()) {
            World probe;
            if (f.id != FACTION_PLAYER && f.id != FACTION_NOMAD && !isMobFaction(f.id) && !factionsHostile(probe, f.id, FACTION_PLAYER)) {
                friendly = f.id;
                break;
            }
        }
        check(friendly >= 0, "there is a faction that is friendly to you");
        World peace;
        Rng peaceRng;
        peaceRng.gen.seed(21);
        GameState peaceState;
        const Vec2 at = systems_detail::findSpawn(terrain);
        const ObjectId you = spawnUnit(peace, terrain, peaceRng, FACTION_PLAYER, at.x, at.z);
        const Vec2 nAt = terrain.snap({ at.x + 1.2f, at.z }), aAt = terrain.snap({ at.x + 1.2f, at.z + 4.0f });
        const ObjectId npc = spawnUnit(peace, terrain, peaceRng, friendly, nAt.x, nAt.z);
        const ObjectId ally = spawnUnit(peace, terrain, peaceRng, friendly, aAt.x, aAt.z);
        for (ObjectId u : { you, npc, ally }) peace.add(u, Equipment{});
        peace.get<Behaviors>(you)->rules.clear();
        if (homes) {
            BehaviorRule stay;
            stay.kind = BehaviorKind::PatrolSpawn;
            peace.get<Behaviors>(npc)->rules = { stay };
            peace.add(npc, Citizen{});
            peace.add(npc, SpawnHome{ nAt.x, nAt.z, false });
            peace.add(ally, SquadMember{ 0 });
        }
        auto keepUp = [&]() {
            Needs* n = peace.get<Needs>(you);
            n->hp = n->maxHp;
        };
        auto run = [&](float seconds) {
            for (int step = 0; step < (int)(seconds * 60.0f); ++step) {
                keepUp();
                simStep(peace, terrain, peaceRng, peaceState, STEP, false);
                takeEvents(peace);
            }
        };
        const std::string how = homes ? " (an Altar's citizen and military)" : " (camp units)";
        makeHostile(peace, npc);
        issueAttackOrder(peace, { you }, npc);
        run(1.0f);
        check(peace.get<Combat>(npc)->target == you, "the unit you attack fights back" + how);
        check(peace.get<Combat>(ally)->target == you, "its friend nearby joins in" + how);
        // Step back a little: they follow and keep at it
        issueMoveOrder(peace, terrain, { you }, terrain.snap({ at.x - 3.0f, at.z }));
        float npcGap = 0.0f, allyGap = 0.0f;
        for (int step = 0; step < 60 * 4; ++step) {
            run(1.0f / 60.0f);
            if (step >= 60 * 3) {
                npcGap = std::max(npcGap, flatGap(peace, npc, you));
                allyGap = std::max(allyGap, flatGap(peace, ally, you));
            }
        }
        std::printf("note: stepping away%s: npc target %d gap %.2f, ally target %d gap %.2f\n", how.c_str(),
                    (int)(peace.get<Combat>(npc)->target == you), npcGap, (int)(peace.get<Combat>(ally)->target == you), allyGap);
        check(peace.get<Combat>(npc)->target == you && npcGap < 2.0f, "it follows you when you step away" + how);
        check(peace.get<Combat>(ally)->target == you && allyGap < 2.5f, "and so does its friend" + how);
        // Out of their sight for long enough, they calm down
        Transform* far = peace.get<Transform>(you);
        const Vec2 gone = terrain.snap({ at.x - 30.0f, at.z });
        far->x = gone.x;
        far->z = gone.z;
        peace.remove<SoloMove>(you);
        peace.remove<IdleSpot>(you);
        run(PROVOKED_SECONDS + 2.0f);
        check(!unitsHostile(peace, npc, you) && !unitsHostile(peace, ally, you) && peace.get<Combat>(npc)->target == NO_OBJECT &&
              peace.get<Combat>(ally)->target == NO_OBJECT, "out of their sight for a while, they calm down" + how);
    }

    // An archer aboard an Airship: melee can't reach it, and a unit it shoots that can't shoot back flees out of reach
    {
        World sky;
        Rng skyRng;
        skyRng.gen.seed(5);
        GameState skyState;
        const Vec2 at = systems_detail::findSpawn(terrain);
        const ObjectId ship = placeBuiltProp(sky, terrain, PropKind::Airship, at.x, at.z, 0.0f, FACTION_PLAYER);
        const ObjectId archer = spawnUnit(sky, terrain, skyRng, FACTION_PLAYER, at.x, at.z);
        sky.get<Behaviors>(archer)->rules.clear();
        Equipment bow;
        bow.items[GearSlot::MainHand] = itemTagged("bow_long");
        sky.add(archer, bow);
        addToInventory(*sky.get<Inventory>(archer), itemTagged("arrow"), 60);
        sky.add(archer, Occupying{ ship });
        int friendly = -1, foes = -1;
        for (const FactionDef& f : factions()) {
            if (f.id == FACTION_PLAYER || f.id == FACTION_NOMAD || isMobFaction(f.id)) continue;
            if (factionsHostile(sky, f.id, FACTION_PLAYER)) {
                if (foes < 0) foes = f.id;
            } else if (friendly < 0) {
                friendly = f.id;
            }
        }
        const Vec2 nAt = terrain.snap({ at.x + 4.0f, at.z }), bAt = terrain.snap({ at.x - 2.0f, at.z });
        const ObjectId target = spawnUnit(sky, terrain, skyRng, friendly, nAt.x, nAt.z);
        const ObjectId brute = spawnUnit(sky, terrain, skyRng, foes, bAt.x, bAt.z);
        for (ObjectId u : { target, brute }) sky.add(u, Equipment{});
        makeHostile(sky, target);
        issueAttackOrder(sky, { archer }, target);
        const int hpStart = sky.get<Needs>(archer)->hp;
        bool fled = false, bruteChased = false;
        float bruteClosest = 1e9f;
        for (int step = 0; step < 60 * 12; ++step) {
            simStep(sky, terrain, skyRng, skyState, STEP, false);
            takeEvents(sky);
            fled = fled || sky.has<Fleeing>(target);
            bruteChased = bruteChased || sky.get<Combat>(brute)->target == archer;
            bruteClosest = std::min(bruteClosest, flatGap(sky, brute, archer));
        }
        const float reach = reachOf(sky, archer);
        std::printf("note: airship archer: reach %.1f, target ends %.1f away, fled %d, brute chased %d\n", reach,
                    flatGap(sky, target, archer), (int)fled, (int)bruteChased);
        check(airshipOf(sky, archer) == ship, "the archer stays aboard while it fights");
        check(sky.get<Needs>(archer)->hp == hpStart, "nobody on the ground hurts it with melee");
        check(!bruteChased, "a melee foe doesn't take it for a target");
        check(fled && (outCold(sky, target) || flatGap(sky, target, archer) > reach), "the unit it shoots, with no bow, runs out of reach");
    }

    std::printf("%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
