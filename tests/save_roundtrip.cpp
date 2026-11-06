// Saves a level, loads it back and saves it again: both files must match, and what changed in play must stay changed.
// Runs beside data.pak. Prints each failure and returns non-zero if there were any.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include "build.h"
#include "camera.h"
#include "claims.h"
#include "factions.h"
#include "language.h"
#include "level.h"
#include "pak.h"
#include "play_state.h"
#include "systems.h"
#include "terrain.h"

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

std::string slurp(const std::string& path) {
    std::ifstream in(path);
    std::ostringstream all;
    all << in.rdbuf();
    return all.str();
}

Entity firstProp(World& w, PropKind kind) {
    std::vector<Entity> props = w.view<Prop, Transform>();
    std::sort(props.begin(), props.end());
    for (Entity e : props) {
        if (w.get<Prop>(e)->kind == kind) return e;
    }
    return NULL_ENTITY;
}

Entity firstStocked(World& w) {
    for (Entity e : w.view<Prop, Storage, Transform>()) {
        for (const ItemStack& s : w.get<Storage>(e)->slots) {
            if (s.count > 0) return e;
        }
    }
    return NULL_ENTITY;
}

int stacksIn(const Storage& st) {
    int n = 0;
    for (const ItemStack& s : st.slots) n += s.count > 0 ? 1 : 0;
    return n;
}

} // namespace

int main(int, char**) {
    const char* base = SDL_GetBasePath();
    if (!pakOpen(std::string(base ? base : "") + "data.pak")) {
        std::printf("FAIL: data.pak not found\n");
        return 1;
    }
    {
        const unsigned char* bytes = nullptr;
        size_t size = 0;
        std::string err;
        if (pakFind("data/factioninfo.txt", bytes, size)) loadFactions(std::string((const char*)bytes, size), err);
        loadLanguage(LANGUAGE_DEFAULT_ID, err);
    }

    Terrain terrain(512);
    World world;
    Rng rng;
    rng.gen.seed(12345);
    play::resetPlayState();
    resetPlayerReputation();
    setupWorld(world, terrain, rng, true);
    spawnGroundItems(world, terrain, rng, 2);

    // Things a reload used to undo
    const Entity emptied = firstStocked(world);
    check(emptied != NULL_ENTITY, "the level has a stocked container");
    if (emptied) *world.get<Storage>(emptied) = Storage{};
    const size_t propsBefore = world.view<Prop, Transform>().size();
    const Entity altar = firstProp(world, PropKind::Altar);
    check(altar != NULL_ENTITY, "the level has an Altar");
    const int altarOwner = altar ? world.get<Owner>(altar)->faction : -1;
    if (altar) breakBuilding(world, altar);
    size_t ownedByLoser = 0;
    for (Entity e : world.view<Prop, Owner>()) ownedByLoser += world.get<Owner>(e)->faction == altarOwner ? 1 : 0;

    // A resting unit and a unit with a standing order
    std::vector<Entity> people = world.view<Unit, Commandable>();
    std::sort(people.begin(), people.end());
    Entity bed = firstProp(world, PropKind::Bedroll);
    check(people.size() >= 2 && bed != NULL_ENTITY, "the level has two units and a bedroll");
    Behaviors* resting = world.get<Behaviors>(people[0]);
    for (const BehaviorRule& r : resting->rules) {
        if (r.kind != BehaviorKind::Rest) continue;
        resting->busy = true;
        resting->step = r;
        resting->stepProp = bed;
    }
    world.add(people[0], Occupying{ bed });
    *world.get<Transform>(people[0]) = *world.get<Transform>(bed);
    issueFollowOrder(world, { people[1] }, people[0]);
    world.get<Needs>(people[1])->healSeconds = 12.5;
    world.add(people[1], XpBuffer{});
    world.get<XpBuffer>(people[1])->frac[0] = 0.25f;

    const std::string first = "roundtrip_a.sav", second = "roundtrip_b.sav", cut = "roundtrip_cut.sav";
    const Camera cam;
    const GameState gs;
    const MapChoice map{ "data", "test", "test", 0, "" };
    check(writeSaveFile(first, captureWorld(world, gs, cam, rng, 1000, "test", map)), "the save is written");

    SaveData loaded;
    check(readSaveFile(first, loaded), "the save is read");
    Rng after;
    World restored;
    restoreWorld(restored, terrain, after, loaded);
    check(writeSaveFile(second, captureWorld(restored, gs, cam, after, 1000, "test", map)), "the reloaded game is saved");
    check(slurp(first) == slurp(second), "both saves match");

    check(restored.view<Prop, Transform>().size() == propsBefore, "broken props stay broken (only Debris takes their place)");
    size_t emptyContainers = 0;
    for (Entity e : restored.view<Prop, Storage>()) emptyContainers += stacksIn(*restored.get<Storage>(e)) == 0 ? 1 : 0;
    check(emptyContainers > 0, "an emptied container stays empty");
    size_t ownedAfter = 0;
    for (Entity e : restored.view<Prop, Owner>()) ownedAfter += restored.get<Owner>(e)->faction == altarOwner ? 1 : 0;
    check(ownedAfter == ownedByLoser, "props abandoned with their Altar stay ownerless");

    std::vector<Entity> again = restored.view<Unit, Commandable>();
    std::sort(again.begin(), again.end());
    const Behaviors* b = restored.get<Behaviors>(again[0]);
    check(b && b->busy && b->step.kind == BehaviorKind::Rest && restored.has<Occupying>(again[0]), "a resting unit is still on its Rest step");
    check(restored.has<FollowOrder>(again[1]), "a standing order comes back");
    check(restored.get<Needs>(again[1])->healSeconds == 12.5, "partial healing comes back");
    check(restored.has<XpBuffer>(again[1]) && restored.get<XpBuffer>(again[1])->frac[0] == 0.25f, "partial XP comes back");
    check(rng.gen == after.gen, "the random numbers carry on where they left off");

    // Damage is refused
    std::string text = slurp(first);
    {
        std::ofstream out(cut);
        out << text.substr(0, text.size() / 2);
    }
    SaveData damaged;
    check(!readSaveFile(cut, damaged), "a truncated save is refused");
    {
        std::ofstream out(cut);
        out << text << "nonsense line\n";
    }
    SaveData unknown;
    check(!readSaveFile(cut, unknown), "an unknown line is refused");

    std::remove(first.c_str());
    std::remove(second.c_str());
    std::remove(cut.c_str());
    std::printf("%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
