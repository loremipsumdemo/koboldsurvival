// Checks the world generator and the map and level files, with no window. Runs beside data.pak.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include "build.h"
#include "camera.h"
#include "camera_limit.h"
#include "game_tables.h"
#include "items.h"
#include "language.h"
#include "level_file.h"
#include "overview.h"
#include "pak.h"
#include "simulation.h"
#include "terrain.h"
#include "world_gen.h"

inline constexpr unsigned long long WORLD_GEN_GOLDEN_HASH = 0x256c8f26dd51cda1ull;

namespace {
int failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

uint64_t hashChunk(const WorldGenerator& g, int cx, int cz) {
    ChunkData c;
    g.fill(cx, cz, c);
    uint64_t h = 14695981039346656037ull;
    const auto add = [&](const void* bytes, size_t size) {
        const unsigned char* p = (const unsigned char*)bytes;
        for (size_t i = 0; i < size; ++i) {
            h ^= p[i];
            h *= 1099511628211ull;
        }
    };
    for (int j = 0; j < CHUNK_CORNERS; ++j) {
        for (int i = 0; i < CHUNK_CORNERS; ++i) {
            const int corner = g.heightAt(cx * CHUNK_TILES + i, cz * CHUNK_TILES + j);
            add(&corner, sizeof corner);
        }
    }
    add(c.biome.data(), c.biome.size());
    add(c.veg.data(), c.veg.size());
    add(c.walk.data(), c.walk.size());
    return h;
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

    WorldGenSettings settings;
    settings.seed = 12345;
    settings.size = 8192;
    const WorldGenerator gen(settings);

    // With a path, also keeps raw pictures of a few maps there, to look at how the generator's results read
    if (argc > 1) {
        for (const int seed : { 12345, 777, 2024 }) {
            WorldGenSettings look = settings;
            look.seed = (uint32_t)seed;
            look.size = 2048;
            const std::vector<uint8_t> px = WorldGenerator(look).preview(512);
            const std::string file = std::string(argv[1]) + "_" + std::to_string(seed) + ".rgba";
            if (FILE* f = std::fopen(file.c_str(), "wb")) {
                std::fwrite(px.data(), 1, px.size(), f);
                std::fclose(f);
            }
        }
    }

    // This number is the promise that a saved recipe keeps making the same ground. If it changes, raise WORLD_GEN_VERSION
    // and keep the old generator, or every existing map changes under it.
    const uint64_t golden = hashChunk(gen, 60, 70);
    std::printf("note: chunk hash %llx\n", (unsigned long long)golden);
    check(golden == hashChunk(WorldGenerator(settings), 60, 70), "the same recipe makes the same chunk");
    check(golden == WORLD_GEN_GOLDEN_HASH, "the generator still makes what version 1 made");
    WorldGenSettings other = settings;
    other.seed = 54321;
    check(golden != hashChunk(WorldGenerator(other), 60, 70), "another seed makes another chunk");

    // Chunks meet exactly
    ChunkData a, b;
    gen.fill(60, 70, a);
    gen.fill(61, 70, b);
    bool seam = true;
    for (int j = 0; j < CHUNK_CORNERS; ++j) {
        seam = seam && a.heights[(size_t)j * CHUNK_CORNERS + CHUNK_TILES] == b.heights[(size_t)j * CHUNK_CORNERS];
    }
    check(seam, "neighbouring chunks share their edge exactly");

    // The land setting does what it says, and the map's edge is sea
    int walkAtLow = 0, walkAtHigh = 0;
    WorldGenSettings low = settings, high = settings;
    low.size = high.size = 1024;
    low.land = 25;
    high.land = 85;
    const WorldGenerator lowGen(low), highGen(high);
    bool edgeIsSea = true;
    for (int z = 16; z < 1024; z += 32) {
        for (int x = 16; x < 1024; x += 32) {
            walkAtLow += lowGen.heightAt(x, z) > 0 ? 1 : 0;
            walkAtHigh += highGen.heightAt(x, z) > 0 ? 1 : 0;
        }
        edgeIsSea = edgeIsSea && highGen.heightAt(0, z) < 0 && highGen.heightAt(1024, z) < 0;
    }
    check(walkAtLow < walkAtHigh && walkAtHigh > 0, "more land makes more land");
    check(edgeIsSea, "the edge of the map is sea");
    check(gen.preview(64).size() == 64u * 64u * 4u, "the preview is a picture of the map");

    // The world map's picture comes straight from the recipe, the right way round
    {
        const std::shared_ptr<const ChunkSource> source = generatedChunkSource(settings);
        OverviewPicture pic;
        std::atomic<bool> stop{ false };
        const Uint64 started = SDL_GetTicks();
        BuildOverview(settings.size, nullptr,
                      [&](int px, std::vector<uint8_t>& rgba, const std::atomic<bool>& cancel) { return source->picture(px, rgba, cancel); },
                      pic, stop);
        std::printf("note: the world map picture of an 8192-wide map took %d ms\n", (int)(SDL_GetTicks() - started));
        check(pic.px > 0 && pic.rgba.size() == (size_t)pic.px * pic.px * 4, "a generated map makes a world map picture");
        bool rightWayRound = pic.px > 0;
        for (int z = 0; z < settings.size && rightWayRound; z += 97) {
            for (int x = 0; x < settings.size; x += 97) {
                const int sx = x / pic.step * pic.step, sz = z / pic.step * pic.step;
                const int h = gen.heightAt(sx, sz);
                                const uint8_t* p = &pic.rgba[((size_t)((settings.size - 1 - sz) / pic.step) * pic.px + (settings.size - 1 - sx) / pic.step) * 4];
                const bool seaColoured = p[2] > p[0] && p[0] < 100;   // snow is bluish too, but much lighter
                if ((h < 0) != seaColoured) rightWayRound = false;
            }
        }
        check(rightWayRound, "the world map picture shows land and sea where they are");
        std::atomic<bool> stopped{ true };
        std::vector<uint8_t> none;
        check(!source->picture(64, none, stopped), "a world map picture can be cancelled");
    }

    // The map file
    const std::string mapText = mapFileText(settings);
    std::printf("note: an 8192-wide map file is %d bytes\n", (int)mapText.size());
    check(mapText.size() < 256, "a generated map file is under 256 bytes");
    WorldGenSettings read;
    check(mapFromText(mapText, read, err) && mapFileText(read) == mapText, "a map file reads back the same");
    check(mapFromText(mapText + "laterfeature 7\n", read, err), "a key from a later game is skipped");
    check(!mapFromText("format 2\ngenerator 1\nsize 1024\n", read, err), "another format is refused");
    check(!mapFromText("format 1\ngenerator 99\nsize 1024\n", read, err), "another generator is refused");
    check(!mapFromText("format 1\ngenerator 1\nsize 100\n", read, err), "a size that isn't whole chunks is refused");
    check(safeFileName("  My/Map:1 ") == "MyMap1" && safeFileName("///").empty(), "names become safe file names");

    // Play on a generated map, with the level set-up the game uses when no level is picked
    WorldGenSettings small = settings;
    small.size = 2048;
    Terrain terrain(generatedChunkSource(small));
    check(terrain.groundChunksHeld() <= MAX_GROUND_CHUNKS, "a generated map holds its ground within the limit");
    World random;
    Rng rng;
    rng.gen.seed(7);
    setupWorld(random, terrain, rng, true);
    GameState gs;
    for (int i = 0; i < 120; ++i) {
        simStep(random, terrain, rng, gs, 1.0f / 60.0f, false);
        takeEvents(random);
    }
    check(!random.view<Unit, Commandable>().empty(), "a generated map can be played");

    // A level: placed, saved, read and placed again the same
    World edited;
    Rng editRng;
    editRng.gen.seed(1);
    LevelData made;
    made.map = "testmap";
    made.hasSpawn = true;
    made.spawnX = 1000.5f;
    made.spawnZ = 1000.5f;
    made.nextId = 4;
    LevelObject bed;
    bed.id = 1;
    bed.kind = idx(PropKind::Bed);
    bed.x = 1001.5f;
    bed.z = 1002.5f;
    bed.yaw = 1.5f;
    made.objects.push_back(bed);
    LevelObject wolf;
    wolf.type = LevelObject::Type::Mob;
    wolf.id = 2;
    wolf.kind = idx(MobKind::Wolf);
    wolf.x = 1004.5f;
    wolf.z = 1004.5f;
    made.objects.push_back(wolf);
    LevelObject coins;
    coins.type = LevelObject::Type::Item;
    coins.id = 3;
    coins.kind = itemTagged(ITEM_TAG_GOLD_COIN);
    coins.count = 5;
    coins.x = 1005.5f;
    coins.z = 1005.5f;
    made.objects.push_back(coins);
    applyLevel(edited, terrain, editRng, made);
    check(edited.view<Prop>().size() == 1 && edited.view<Mob>().size() == 1 && edited.view<GroundItem>().size() == 1, "a level places what it lists");

    // Something placed the way regular play does is part of the level too
    const Entity later = makeProp(edited, PropKind::Barrel, Transform{ 1007.5f, terrain.heightAt(1007.5f, 1007.5f), 1007.5f });
    const LevelData captured = captureLevel(edited, "testmap");
    check(captured.objects.size() == 4 && edited.get<LevelId>(later)->id == 4 && captured.nextId == 5, "a level reads back what the world holds, with new ids");
    const std::string text = levelToText(captured);
    std::printf("note: a level of 4 objects is %d bytes\n", (int)text.size());
    LevelData parsed;
    check(levelFromText(text, parsed, err) && levelToText(parsed) == text, "a level file reads back the same");
    World again;
    Rng againRng;
    againRng.gen.seed(1);
    applyLevel(again, terrain, againRng, parsed);
    check(levelToText(captureLevel(again, "testmap")) == text, "a level placed again saves the same");
    check(!levelFromText("format 1\nmap x\nprop 1 NoSuchProp 1 1 0 -1\n", parsed, err), "a level naming a prop this game lacks is refused");
    check(!levelFromText("prop 1 Bed 1 1 0 -1\n", parsed, err), "a file with no format is refused");

    // The player starts at the level's spawn point
    ensurePlayerUnits(again, terrain, againRng, 2);
    bool nearSpawn = !again.view<Unit, Commandable>().empty();
    for (Entity u : again.view<Unit, Commandable>()) {
        const Transform* tr = again.get<Transform>(u);
        nearSpawn = nearSpawn && std::fabs(tr->x - 1000.5f) < 30.0f && std::fabs(tr->z - 1000.5f) < 30.0f;
    }
    check(nearSpawn, "your units start at the level's spawn point");

    // In regular play the camera stays near your units
    {
        const Entity unit = *again.view<Unit, Commandable>().begin();
        const Transform* at = again.get<Transform>(unit);
        Camera near;
        near.mapSize = 2048.0f;
        near.focusX = at->x + 600.0f;
        near.focusZ = at->z;
        again.add(unit, Selected{});
        keepCameraNearUnits(again, near);
        check(std::hypot(near.focusX - at->x, near.focusZ - at->z) <= CAMERA_UNIT_RANGE + 0.01f, "the camera is pulled back to the selected unit");
        near.focusX = at->x + 50.0f;
        keepCameraNearUnits(again, near);
        check(near.focusX == at->x + 50.0f, "the camera moves freely near the selected unit");
        again.remove<Selected>(unit);
        near.focusX = at->x + 600.0f;
        near.focusZ = at->z;
        keepCameraNearUnits(again, near);
        check(near.focusX < at->x + 600.0f, "with nothing selected, the camera stays near your units");
    }

    // Files on disk
    const std::string folder = "world_gen_check_files";
    const std::string mapPath = folder + "/one.map";
    check(writeMapFile(mapPath, settings, err) && readMapFile(mapPath, read, err) && read.seed == settings.seed, "a map file is written and read");
    const std::string levelPath = folder + "/one.level";
    check(writeLevelFile(levelPath, captured, err) && readLevelFile(levelPath, parsed, err) && parsed.objects.size() == 4, "a level file is written and read");
    std::error_code ec;
    std::filesystem::remove_all(folder, ec);

    std::printf("%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
