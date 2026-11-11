#include "level.h"
#include "level_file.h"
#include "world_gen.h"
#include "systems.h"
#include "play_state.h"
#include "loading.h"
#include "renderer.h"

using namespace play;

bool loadLevelGround(const MapChoice& c, Terrain& terrain, Renderer& renderer, std::string& err, const LoadProgress& progress) {
    LoadStages stages(progress);
    stages.stage(0.0f, "LOADING_MAP");
    if (c.source == MAP_SOURCE_FILE) {
        WorldGenSettings settings;
        if (!readMapFile(c.path, settings, err)) return false;
        terrain.open(generatedChunkSource(settings));
    } else if (!terrain.openWorld(c.path, err)) {
        return false;
    }
    stages.stage(0.35f, "LOADING_MAP_GROUND");
    renderer.uploadTerrain(terrain);
    return true;
}

void buildLevel(World& world, Terrain& terrain, Rng& rng, unsigned seed, bool playerCamp, const LoadProgress& progress) {
    LoadStages stages(progress);
    stages.stage(0.7f, "LOADING_WORLD");
    world = World{};
    resetPlayState();
    rng.gen.seed(seed);
    setupWorld(world, terrain, rng, playerCamp);
    stages.stage(0.85f, "LOADING_GROUND_ITEMS");
    spawnStartItems(world, terrain, rng, START_ITEM_COUNT, START_ITEM_RANGE);
}

void buildLevelFrom(World& world, Terrain& terrain, Rng& rng, unsigned seed, const LevelData& level) {
    resetPlayState();
    rng.gen.seed(seed);
    applyLevel(world, terrain, rng, level);
}

std::vector<Vec2> streamFocuses(World& w, const Camera& cam) {
    std::vector<Vec2> out{ Vec2{ cam.focusX, cam.focusZ } };
    for (ObjectId u : w.view<Entity, Transform, Commandable>()) {
        const Transform* t = w.get<Transform>(u);
        out.push_back(Vec2{ t->x, t->z });
    }
    return out;
}

void primeStreaming(World& w, Terrain& t, const Camera& cam) {
    if (t.streamed()) t.stream(streamFocuses(w, cam), STREAM_RADIUS, 0);
}

