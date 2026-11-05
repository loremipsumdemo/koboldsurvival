#include "level.h"
#include "systems.h"
#include "play_state.h"
#include "loading.h"
#include "pak.h"
#include "renderer.h"

using namespace play;

// Puts a map's ground into the terrain: a premade or custom map whole, or a world folder to be streamed
bool loadTerrain(const MapChoice& c, Terrain& terrain, std::string& err) {
    if (c.source == "world" || c.source == "packed") return terrain.openWorld(c.path, err);

    MapData data;
    bool ok = false;
    if (c.source == "data") {
        const unsigned char* bytes = nullptr;
        size_t size = 0;
        ok = pakFind(c.pakName(), bytes, size) && parseMap(bytes, size, data);
    } else {
        ok = readMapFile(c.path, data);
    }
    if (!ok) {
        err = langf("MISC_CANNOT_READ", { c.name });
        return false;
    }
    terrain.load(data);
    return true;
}

// Builds a fresh level on a map: the ground, then the entities from the seed, then the items on the
// ground if spawnItems is set. progress, if given, is called as the level loads (0..1), for the loading screen.
bool prepareLevel(const MapChoice& c, World& world, Terrain& terrain, Renderer& renderer, Rng& rng,
                  unsigned seed, bool spawnItems, std::string& err,
                  const std::function<void(float, const std::string&)>& progress) {
    // Each stage named under the bar, and timed in log.txt
    LoadStages stages("Level", progress);
    stages.stage(0.0f, "LOADING_MAP");
    if (!loadTerrain(c, terrain, err)) return false;
    stages.stage(0.35f, "LOADING_MAP_GROUND");
    renderer.uploadTerrain(terrain);
    stages.stage(0.7f, "LOADING_WORLD");
    world = World{};
    dialogueUnit = NULL_ENTITY;   // entity ids restart with the new world, so a talk box can't keep pointing at one
    dialogueConvo.clear();
    discardQueuedLog();
    tradeOpen = false;
    clearTradeTable();
    closeStorage();
    contextMenu = ContextMenu{};
    freeTrade = false;
    paused = false;
    rng.gen.seed(seed);
    buildPick = {};   // a new level starts with nothing picked to build
    resetPlayerReputation();   // a new game starts every faction at its default standing
    setupWorld(world, terrain, rng, levelPlayerCamp);
    // log.txt: how many of each prop kind went down, and how far the nearest is from the first unit
    {
        std::array<int, PROP_KIND_COUNT> placed{};
        std::array<float, PROP_KIND_COUNT> nearest{};
        nearest.fill(1e9f);
        Vec2 home{ 0.0f, 0.0f };
        for (Entity u : world.view<Unit, Transform, Commandable>()) {
            home = Vec2{ world.get<Transform>(u)->x, world.get<Transform>(u)->z };
            break;
        }
        for (Entity e : world.view<Prop, Transform>()) {
            const PropKind kind = world.get<Prop>(e)->kind;
            if (idx(kind) < 0 || idx(kind) >= PROP_KIND_COUNT) continue;
            ++placed[(size_t)idx(kind)];
            const Transform* pt = world.get<Transform>(e);
            nearest[(size_t)idx(kind)] = std::min(nearest[(size_t)idx(kind)], std::hypot(pt->x - home.x, pt->z - home.z));
        }
        std::string line;
        for (int k = 0; k < PROP_KIND_COUNT; ++k) {
            char buf[96];
            std::snprintf(buf, sizeof(buf), "%s%s %d (%.0f)", k ? ", " : "", lang(propInfo((PropKind)k).nameKey).c_str(),
                          placed[(size_t)k], placed[(size_t)k] ? nearest[(size_t)k] : -1.0f);
            line += buf;
        }
        SDL_Log("Props placed, with the nearest one's distance in tiles: %s", line.c_str());
    }
    if (spawnItems) {
        stages.stage(0.85f, "LOADING_GROUND_ITEMS");
        spawnGroundItems(world, terrain, rng, ITEM_COPIES_PER_LEVEL);
    }
    stages.finish();
    return true;
}

std::vector<Vec2> streamFocuses(World& w, const Camera& cam) {
    std::vector<Vec2> out{ Vec2{ cam.focusX, cam.focusZ } };
    for (Entity u : w.view<Unit, Transform, Commandable>()) {
        const Transform* t = w.get<Transform>(u);
        out.push_back(Vec2{ t->x, t->z });
    }
    return out;
}

// Reads in the ground around the camera and the units straight away, so a level starts with its ground in place
void primeStreaming(World& w, Terrain& t, const Camera& cam) {
    if (t.streamed()) t.stream(streamFocuses(w, cam), STREAM_RADIUS, 0);
}

