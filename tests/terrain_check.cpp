// Checks the ground the game walks on, using the main map. With a path, also writes a fingerprint of everything the
// simulation sees of the map there, to compare a change against the game before it. Runs beside data.pak.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include "camera.h"
#include "game_tables.h"
#include "language.h"
#include "level.h"
#include "pak.h"
#include "savegame.h"
#include "simulation.h"
#include "terrain.h"

namespace {
int failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

struct Fingerprint {
    uint64_t value = 14695981039346656037ull;
    void add(const void* bytes, size_t size) {
        const unsigned char* p = (const unsigned char*)bytes;
        for (size_t i = 0; i < size; ++i) {
            value ^= p[i];
            value *= 1099511628211ull;
        }
    }
    template <typename T>
    void add(const T& v) { add(&v, sizeof v); }
};

constexpr const char* MAIN_MAP = "pak:data/map/main";

// A huge world made up on the spot: gentle hills, with a walled-off block of water every 32 tiles
class HugeSource : public ChunkSource {
public:
    explicit HugeSource(int size) : size_(size) {}
    static float heightOf(int x, int z) { return (float)((x * 7 + z * 13) % 64) * 0.01f; }
    static bool walkOf(int x, int z) { return !(x % 32 == 5 && z % 32 == 5); }
    int size() const override { return size_; }
    float floorY() const override { return -2.0f; }
    const WorldSurface& surface() const override {
        static const WorldSurface none;
        return none;
    }
    bool streamed() const override { return true; }
    bool fill(int cx, int cz, ChunkData& out) const override {
        ++fills;
        out = emptyChunk(cx, cz);
        for (int j = 0; j < CHUNK_CORNERS; ++j) {
            for (int i = 0; i < CHUNK_CORNERS; ++i) out.heights[(size_t)j * CHUNK_CORNERS + i] = heightOf(cx * CHUNK_TILES + i, cz * CHUNK_TILES + j);
        }
        for (int j = 0; j < CHUNK_TILES; ++j) {
            for (int i = 0; i < CHUNK_TILES; ++i) out.walk[(size_t)j * CHUNK_TILES + i] = walkOf(cx * CHUNK_TILES + i, cz * CHUNK_TILES + j) ? 1 : 0;
        }
        return true;
    }
    mutable int fills = 0;

private:
    int size_;
};

void checkHugeMap() {
    constexpr int SIZE = 8192;
    auto source = std::make_shared<HugeSource>(SIZE);
    Terrain terrain(source);
    check(terrain.size() == SIZE && source->fills == 0, "a huge map opens without reading its chunks");

    Rng rng;
    rng.gen.seed(5);
    bool heightsRight = true, walkRight = true;
    for (int i = 0; i < 200000; ++i) {
        const int x = rng.range(0, SIZE - 1), z = rng.range(0, SIZE - 1);
        bool walk = false;
        float h = 0.0f;
        terrain.tileGround(x, z, walk, h);
        const float want = (HugeSource::heightOf(x, z) + HugeSource::heightOf(x + 1, z) + HugeSource::heightOf(x, z + 1) + HugeSource::heightOf(x + 1, z + 1)) * 0.25f;
        heightsRight = heightsRight && std::fabs(h - want) < 1e-5f && terrain.heightAt((float)x, (float)z) == HugeSource::heightOf(x, z);
        walkRight = walkRight && walk == HugeSource::walkOf(x, z) && terrain.walkable(x, z) == walk;
    }
    check(heightsRight, "heights are right anywhere on a huge map, also after their chunk was dropped");
    check(walkRight, "walkability is right anywhere on a huge map");
    check(terrain.groundChunksHeld() <= MAX_GROUND_CHUNKS, "a huge map keeps only so much ground in memory");
    check(source->fills > (int)MAX_GROUND_CHUNKS, "chunks were really dropped and read again");

    // The corner of a chunk meets its neighbours
    const float seam = (float)CHUNK_TILES * 3.0f;
    check(terrain.heightAt(seam - 0.001f, seam) - terrain.heightAt(seam, seam) < 0.05f, "chunks meet at their edges");

    const std::vector<Vec2> way = terrain.findPath({ 4000.5f, 4000.5f }, { 4100.5f, 4050.5f });
    check(!way.empty() && std::hypot(way.back().x - 4100.5f, way.back().z - 4050.5f) < 0.01f, "a path is found across chunks of a huge map");

    check(terrain.stream({ Vec2{ 4000.0f, 4000.0f } }, 2, 0) > 0 && terrain.chunkAt(62, 62) != nullptr && terrain.chunkAt(0, 0) == nullptr, "only the chunks around a focus are drawn");
    terrain.stream({ Vec2{ 100.0f, 100.0f } }, 2, 0);
    check(terrain.chunkAt(62, 62) == nullptr && terrain.chunkAt(1, 1) != nullptr, "chunks left behind are dropped from drawing");

    // A level and a stretch of play on it
    World world;
    Rng levelRng;
    levelRng.gen.seed(4242);
    GameState gs;
    setupWorld(world, terrain, levelRng, true);
    for (int i = 0; i < 60 * 5; ++i) {
        simStep(world, terrain, levelRng, gs, 1.0f / 60.0f, false);
        takeEvents(world);
    }
    check(!world.view<Entity, Transform>().empty(), "a huge map can be played");
    check(terrain.groundChunksHeld() <= MAX_GROUND_CHUNKS, "play keeps the ground in memory within its limit");
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

    Terrain terrain;
    check(terrain.openWorld(MAIN_MAP, err), "the main map opens " + err);
    const int size = terrain.size();

    // Every tile, sampled at several points inside it
    Fingerprint ground;
    int walkableTiles = 0;
    for (int z = 0; z < size; ++z) {
        for (int x = 0; x < size; ++x) {
            const bool walk = terrain.walkable(x, z);
            walkableTiles += walk ? 1 : 0;
            ground.add(walk);
            bool tileWalk = false;
            float tileHeight = 0.0f;
            terrain.tileGround(x, z, tileWalk, tileHeight);
            ground.add(tileHeight);
            for (const float at : { 0.0f, 0.25f, 0.75f }) {
                const float h = terrain.heightAt((float)x + at, (float)z + (1.0f - at) * 0.9f);
                ground.add(h);
            }
        }
    }
    check(walkableTiles > 0, "the main map has ground to walk on");

    // Paths, spread over the map
    Fingerprint paths;
    Rng pathRng;
    pathRng.gen.seed(99);
    int foundPaths = 0;
    for (int i = 0; i < 200; ++i) {
        const Vec2 from{ (float)pathRng.range(0, size - 1) + 0.5f, (float)pathRng.range(0, size - 1) + 0.5f };
        const Vec2 to{ (float)pathRng.range(0, size - 1) + 0.5f, (float)pathRng.range(0, size - 1) + 0.5f };
        const std::vector<Vec2> way = terrain.findPath(from, to);
        foundPaths += way.empty() ? 0 : 1;
        for (const Vec2& p : way) {
            paths.add(p.x);
            paths.add(p.z);
        }
    }
    check(foundPaths > 0, "paths are found on the main map");

    // A stretch of play on the real map
    World world;
    Rng rng;
    rng.gen.seed(4242);
    GameState gs;
    setupWorld(world, terrain, rng, true);
    constexpr float STEP = 1.0f / 60.0f;
    for (int i = 0; i < 60 * 30; ++i) {
        simStep(world, terrain, rng, gs, STEP, false);
        takeEvents(world);
    }
    check(!world.view<Entity, Transform>().empty(), "units are still there after thirty seconds on the main map");

    const SaveData end = captureWorld(world, gs, Camera{}, rng, 1000, "test", MapChoice{ "packed", "main", "main", "" });
    const char* played = "terrain_check_play.sav";
    writeSaveFile(played, end);
    Fingerprint play;
    if (FILE* f = std::fopen(played, "rb")) {
        char buffer[65536];
        size_t got = 0;
        while ((got = std::fread(buffer, 1, sizeof buffer, f)) > 0) play.add(buffer, got);
        std::fclose(f);
    }
    std::remove(played);

    {
        Terrain untouched(128);
        check(!untouched.openWorld("pak:data/map/none", err) && untouched.size() == 128, "a world that won't open leaves the terrain as it was");
    }
    checkHugeMap();

    if (argc > 1) {
        if (FILE* f = std::fopen(argv[1], "w")) {
            std::fprintf(f, "size %d walkable %d paths found %d\nground %llx\npaths %llx\nplay %llx\n", size, walkableTiles, foundPaths,
                         (unsigned long long)ground.value, (unsigned long long)paths.value, (unsigned long long)play.value);
            std::fclose(f);
        }
    }
    std::printf("%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
