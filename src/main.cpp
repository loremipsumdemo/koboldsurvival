#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <GL/glew.h>
#include <SDL3/SDL_opengl.h>
#include <SDL3_image/SDL_image.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "build_info.h"
#include "camera.h"
#include "gui.h"
#include "sky.h"
#include "components.h"
#include "ecs.h"
#include "renderer.h"
#include "resources.h"
#include "map.h"
#include "audio.h"
#include "pak.h"
#include "savegame.h"
#include "settings.h"
#include "settings_menu.h"
#include "display.h"
#include "inventory.h"
#include "party.h"
#include "party_ui.h"
#include "behavior.h"
#include "combat.h"
#include "equip.h"
#include "items.h"
#include "systems.h"
#include "terrain.h"
#include "ui.h"
#include "useitem.h"
#include "world_format.h"

namespace {

constexpr size_t HOVER_NAME_LIMIT = 10;      // hover label lists up to this many names
constexpr size_t MIN_SAVE_ROWS = 5;          // the save list shows between these many rows,
constexpr size_t MAX_SAVE_ROWS = 10;         // depending on how tall the window is
constexpr Uint64 DELETE_HOLD_MS = 3000;      // holding Delete this long deletes the save
constexpr size_t NAME_LIMIT = 32;            // longest save name, in bytes
constexpr Uint64 UNSAVED_WARN_MS = 30000;    // loading over a game not saved this recently asks first
constexpr const char* SETTINGS_PATH = "settings.cfg";

// Save list columns, as fractions of the list width. The name column starts at the left.
constexpr float COL_TYPE = 0.54f;
constexpr float COL_TIME = 0.70f;

// What the save list is sorted by. Autosaves always sit above manual saves, whatever the sort.
enum class SortKey { Name, Time };

// Which question the popout is asking, if any
enum class Ask { None, Overwrite, LoadUnsaved, Delete };

// Ray through a pixel, intersected with the terrain surface. Returns world (x, z).
std::optional<std::pair<float, float>> pickGround(const Camera& cam, const Terrain& t,
                                                  float mx, float my, int W, int H) {
    Mat4 vp = cam.viewProj((float)W / (float)H);
    float inv[16];
    if (!invert4x4(vp.m, inv)) return std::nullopt;

    float ndcX = 2.0f * mx / (float)W - 1.0f;
    float ndcY = 1.0f - 2.0f * my / (float)H;

    auto unproject = [&](float ndcZ, float out[3]) {
        float p[4] = { ndcX, ndcY, ndcZ, 1.0f };
        float r[4];
        for (int i = 0; i < 4; ++i) {
            r[i] = inv[0 * 4 + i] * p[0] + inv[1 * 4 + i] * p[1]
                 + inv[2 * 4 + i] * p[2] + inv[3 * 4 + i] * p[3];
        }
        out[0] = r[0] / r[3];
        out[1] = r[1] / r[3];
        out[2] = r[2] / r[3];
    };

    float n[3], f[3];
    unproject(-1.0f, n);
    unproject(1.0f, f);

    // Iterate: intersect with the plane at the terrain height under the last hit
    float groundY = 0.0f, hx = 0.0f, hz = 0.0f;
    for (int iter = 0; iter < 6; ++iter) {
        float dy = f[1] - n[1];
        if (std::fabs(dy) < 1e-6f) return std::nullopt;
        float s = (groundY - n[1]) / dy;
        hx = n[0] + (f[0] - n[0]) * s;
        hz = n[2] + (f[2] - n[2]) * s;
        groundY = t.heightAt(hx, hz);
    }

    if (hx < 0.0f || hz < 0.0f || hx >= t.size() || hz >= t.size()) return std::nullopt;
    return std::make_pair(hx, hz);
}

// World point to pixel coordinates. Returns false if it's behind the camera.
bool worldToScreen(const Mat4& vp, float x, float y, float z, int W, int H, float& sx, float& sy) {
    float c[4];
    for (int i = 0; i < 4; ++i) {
        c[i] = vp.m[0 * 4 + i] * x + vp.m[1 * 4 + i] * y + vp.m[2 * 4 + i] * z + vp.m[3 * 4 + i];
    }
    if (c[3] <= 0.0f) return false;
    sx = (c[0] / c[3] * 0.5f + 0.5f) * (float)W;
    sy = (1.0f - (c[1] / c[3] * 0.5f + 0.5f)) * (float)H;
    return true;
}

void clearSelection(World& w) {
    for (Entity e : w.view<Selected>()) w.remove<Selected>(e);
}

std::vector<Entity> selectedUnits(World& w) {
    std::vector<Entity> out;
    for (Entity u : w.view<Unit, Selected, Commandable>()) out.push_back(u);
    return out;
}

// The unit the sidebar shows: the lone one of yours that is selected, or, when none of yours is, the lone red
// unit. NULL when nothing is selected or more than one of yours is.
Entity sidebarUnitOf(World& w) {
    const std::vector<Entity> mine = selectedUnits(w);
    if (mine.size() == 1) return mine[0];
    if (!mine.empty()) return NULL_ENTITY;
    const std::vector<Entity> any = w.view<Unit, Selected>();
    return any.size() == 1 ? any[0] : NULL_ENTITY;
}

// The first red unit in a list, or NULL_ENTITY if there is none
Entity firstRedUnit(World& w, const std::vector<Entity>& units) {
    for (Entity u : units) {
        if (w.has<Hostile>(u)) return u;
    }
    return NULL_ENTITY;
}

// Click selection: nearest unit within one tile of the clicked ground point
void clickSelect(World& w, const Terrain& t, const Camera& cam,
                 float mx, float my, int W, int H, bool additive) {
    if (!additive) clearSelection(w);
    auto hit = pickGround(cam, t, mx, my, W, H);
    if (!hit) return;

    Entity best = NULL_ENTITY;
    float bestD2 = 1.0f;
    for (Entity u : w.view<Unit, Transform>()) {
        Transform* tr = w.get<Transform>(u);
        float dx = tr->x - hit->first, dz = tr->z - hit->second;
        float d2 = dx * dx + dz * dz;
        if (d2 < bestD2) { bestD2 = d2; best = u; }
    }
    if (best) w.add(best, Selected{});
}

// Box selection: every unit whose screen position falls inside the rectangle
void boxSelect(World& w, const Camera& cam, float x0, float y0, float x1, float y1,
               int W, int H, bool additive) {
    if (!additive) clearSelection(w);
    float minX = std::min(x0, x1), maxX = std::max(x0, x1);
    float minY = std::min(y0, y1), maxY = std::max(y0, y1);
    Mat4 vp = cam.viewProj((float)W / (float)H);

    for (Entity u : w.view<Unit, Transform, Commandable>()) {
        Transform* tr = w.get<Transform>(u);
        float sx, sy;
        if (worldToScreen(vp, tr->x, tr->y + 0.5f, tr->z, W, H, sx, sy)
            && sx >= minX && sx <= maxX && sy >= minY && sy <= maxY) {
            w.add(u, Selected{});
        }
    }
}

// Right click: order the selected units to a ground point.
// Returns true, with the ground point in gx and gz, if an order was given.
bool rightClickOrder(World& w, Terrain& t, const Camera& cam, float mx, float my, int W, int H,
                     float& gx, float& gz) {
    std::vector<Entity> units = selectedUnits(w);
    if (units.empty()) return false;
    auto hit = pickGround(cam, t, mx, my, W, H);
    if (!hit) return false;
    gx = hit->first;
    gz = hit->second;

    // Units go to the exact ground point clicked, not to the center of its tile
    issueMoveOrder(w, t, units, Vec2{ hit->first, hit->second });
    return true;
}

// Draws one loading frame and keeps the window responsive while things load
void loadingFrame(SDL_Window* window, Renderer& renderer, float progress, const std::string& label = std::string()) {
    SDL_PumpEvents();
    int winW, winH;
    SDL_GetWindowSizeInPixels(window, &winW, &winH);
    renderer.drawLoadingScreen(progress, winW, winH, label);
    SDL_GL_SwapWindow(window);
}

// Game worlds are kept in this folder, beside the game.
std::string mapsDir() {
    const char* base = SDL_GetBasePath();
    return std::string(base ? base : "") + "maps";
}

// The folder beside the game where F12 screenshots are saved
std::string screenshotsDir() {
    const char* base = SDL_GetBasePath();
    return std::string(base ? base : "") + "screenshots";
}

// Reads back the frame that is about to be shown and saves it as a PNG in the screenshots folder.
// Returns the file's name, or an empty string if it could not be saved.
std::string saveScreenshot(SDL_Window* window) {
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    if (w <= 0 || h <= 0) return "";

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    std::vector<unsigned char> pixels((size_t)w * h * 4);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    // OpenGL counts rows from the bottom, the picture from the top. The window's alpha is not part of the picture.
    const size_t rowBytes = (size_t)w * 4;
    std::vector<unsigned char> flipped(pixels.size());
    for (int y = 0; y < h; ++y) {
        std::copy_n(pixels.data() + (size_t)(h - 1 - y) * rowBytes, rowBytes, flipped.data() + (size_t)y * rowBytes);
    }
    for (size_t i = 3; i < flipped.size(); i += 4) flipped[i] = 255;

    std::error_code ec;
    const std::filesystem::path dir = screenshotsDir();
    std::filesystem::create_directories(dir, ec);
    if (ec) return "";

    // Named by the time it was taken. A number is added if that name is already used.
    const std::time_t now = std::time(nullptr);
    const std::tm* tm = std::localtime(&now);
    char stamp[32] = "screenshot";
    if (tm) std::strftime(stamp, sizeof(stamp), "screenshot_%Y%m%d_%H%M%S", tm);
    std::string name = std::string(stamp) + ".png";
    for (int n = 2; std::filesystem::exists(dir / name, ec); ++n) {
        name = std::string(stamp) + "_" + std::to_string(n) + ".png";
    }

    SDL_Surface* surface = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_ABGR8888, flipped.data(), (int)rowBytes);
    if (!surface) return "";
    const bool ok = IMG_SavePNG(surface, (dir / name).string().c_str());
    SDL_DestroySurface(surface);
    return ok ? name : "";
}

// A folder left as custom_maps by an earlier version moves to maps the first time the game starts
void moveOldMapsFolder() {
    const char* base = SDL_GetBasePath();
    const std::filesystem::path oldDir = std::string(base ? base : "") + "custom_maps";
    const std::filesystem::path newDir = mapsDir();
    std::error_code ec;
    if (!std::filesystem::is_directory(oldDir, ec) || std::filesystem::exists(newDir, ec)) return;
    std::filesystem::rename(oldDir, newDir, ec);
    if (ec) SDL_Log("Could not move custom_maps to maps: %s", ec.message().c_str());
}

// A map the New Game list can start: a premade map from data.pak ("data"), a game world folder in
// maps ("world"), or an older .map file in maps ("custom")
struct MapChoice {
    std::string source;      // "data", "world" or "custom"
    std::string stem;        // file or folder name without the .map extension. Saves refer to it.
    std::string name;        // what the list shows: the saved name of a world, or the stem
    long long created = 0;   // when a world was saved, milliseconds since 1970; 0 if unknown
    std::string path;        // where a world folder or .map file is on disk

    std::string pakName() const { return "data/map/" + stem + ".map"; }
};

// Every map the New Game list can start, not yet sorted: the premade maps, then worlds and .map files
std::vector<MapChoice> listMaps() {
    std::vector<MapChoice> out;
    const std::string prefix = "data/map/", suffix = ".map";
    for (const std::string& pak : pakNames(prefix, suffix)) {
        const std::string stem = pak.substr(prefix.size(), pak.size() - prefix.size() - suffix.size());
        out.push_back(MapChoice{ "data", stem, stem, 0, "" });
    }

    const std::string dir = mapsDir();
    for (const WorldEntry& e : listWorlds(dir)) {
        const std::string stem = std::filesystem::path(e.folder).filename().string();
        out.push_back(MapChoice{ "world", stem, e.name, e.created, e.folder });
    }

    std::error_code ec;
    if (std::filesystem::is_directory(dir, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (entry.path().extension() != ".map") continue;
            const std::string stem = entry.path().stem().string();
            out.push_back(MapChoice{ "custom", stem, stem, 0, entry.path().string() });
        }
    }
    return out;
}

// Puts a map's ground into the terrain: a premade or custom map whole, or a world folder to be streamed
bool loadTerrain(const MapChoice& c, Terrain& terrain, std::string& err) {
    if (c.source == "world") return terrain.openWorld(c.path, err);

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
        err = "could not read " + c.name;
        return false;
    }
    terrain.load(data);
    return true;
}

// Every item in the database is put on the ground this many times when a level starts
constexpr int ITEM_COPIES_PER_LEVEL = 10;

// Builds a fresh level on a map: the ground, then the entities from the seed, then the items on the
// ground if spawnItems is set. progress, if given, is called as the level loads (0..1), for the loading screen.
bool prepareLevel(const MapChoice& c, World& world, Terrain& terrain, Renderer& renderer, Rng& rng,
                  unsigned seed, bool spawnItems, std::string& err, const std::function<void(float)>& progress = {}) {
    auto report = [&](float p) { if (progress) progress(p); };
    if (!loadTerrain(c, terrain, err)) return false;
    report(0.35f);
    renderer.uploadTerrain(terrain);
    report(0.7f);
    world = World{};
    rng.gen.seed(seed);
    setupWorld(world, terrain, rng);
    if (spawnItems) spawnGroundItems(world, terrain, rng, ITEM_COPIES_PER_LEVEL);
    report(0.9f);
    return true;
}

// Open worlds keep the ground loaded around the camera and every unit the player commands
constexpr int STREAM_RADIUS = 4;            // chunks kept loaded around each focus point
constexpr int STREAM_LOADS_PER_FRAME = 4;   // chunks read in per frame while playing, so play never stalls

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

// Rebuilds a level from a save. The ground comes from the map, and the wagon from the seed.
// The units are then swapped for the saved ones.
bool restoreLevel(World& world, Terrain& terrain, Renderer& renderer, Rng& rng, const SaveData& d,
                  const MapChoice& map, std::string& err) {
    // A save from before items existed has none, so its items are placed from the seed as on a new level.
    // A save that has items gets exactly those, and nothing is placed from the seed.
    if (!prepareLevel(map, world, terrain, renderer, rng, d.seed, !d.hasItems, err)) return false;

    // Drop the units that setupWorld generated
    for (Entity e : world.view<Unit>()) world.destroy(e);

    for (const SaveUnit& u : d.units) {
        Entity e = world.create();
        world.add(e, Transform{ u.x, u.y, u.z });
        // Hostiles walk at their own pace, as on a new level. Every unit fights, so each one gets Combat.
        Unit unit;
        if (u.hostile) unit.speed = HOSTILE_WALK_SPEED;
        world.add(e, unit);
        world.add(e, Combat{});
        if (u.hostile) {
            world.add(e, Hostile{});
        } else {
            world.add(e, Commandable{});
        }
        // Every unit carries items and has behaviors, whichever side it is on
        Inventory inv;
        for (const SaveStack& s : u.stacks) {
            if (s.slot >= 0 && s.slot < INVENTORY_SLOTS && s.stack.item > 0 && s.stack.count > 0) {
                inv.slots[s.slot] = s.stack;
            }
        }
        world.add(e, inv);
        Equipment worn;
        for (const SaveGear& g : u.gear) worn.items[g.slot] = g.item;
        world.add(e, worn);
        world.add(e, u.hasNeeds ? u.needs : Needs{});
        Behaviors behaviors;   // older saves have none, so their units get the defaults
        if (u.hasBehaviors) behaviors.rules = u.behaviors;
        world.add(e, behaviors);
        world.add(e, u.hasStats ? Stats{ u.stats } : defaultStats());
        world.add(e, Name{ u.name });
        world.add(e, UnitSprite{ u.sprite });
    }

    if (d.hasItems) {
        for (const SaveGround& g : d.ground) {
            if (g.item <= 0 || g.count <= 0) continue;
            Entity e = world.create();
            world.add(e, Transform{ g.x, g.y, g.z });
            world.add(e, GroundItem{ g.item, g.count });
        }
    }
    return true;
}

// Captures what a save needs from the running world, including the camera
SaveData captureWorld(World& w, const GameState& gs, const Camera& cam, unsigned seed,
                      long long ts, const std::string& name, const MapChoice& map) {
    SaveData d;
    d.name = name;
    d.timestamp = ts;
    d.seed = seed;
    d.mapSource = map.source;
    d.mapStem = map.stem;
    d.gs = gs;

    d.cam.present = true;
    d.cam.fx = cam.focusX;
    d.cam.fy = cam.focusY;
    d.cam.fz = cam.focusZ;
    d.cam.yaw = cam.yaw;
    d.cam.pitch = cam.pitch;
    d.cam.distance = cam.distance;

    for (Entity u : w.view<Unit, Transform>()) {
        Transform* t = w.get<Transform>(u);
        Name* n = w.get<Name>(u);
        UnitSprite* us = w.get<UnitSprite>(u);
        SaveUnit su;
        su.x = t->x;
        su.y = t->y;
        su.z = t->z;
        su.hostile = w.has<Hostile>(u);
        su.sprite = us ? us->id : 1;
        su.name = n ? n->text : "Unnamed Unit";
        if (const Needs* nd = w.get<Needs>(u)) {
            su.hasNeeds = true;
            su.needs = *nd;
        }
        if (const Stats* st = w.get<Stats>(u)) {
            su.hasStats = true;
            su.stats = *st;
        }
        if (const Behaviors* b = w.get<Behaviors>(u)) {
            su.hasBehaviors = true;
            su.behaviors = b->rules;
        }
        if (const Inventory* inv = w.get<Inventory>(u)) {
            for (int i = 0; i < INVENTORY_SLOTS; ++i) {
                const ItemStack& st = inv->slots[i];
                if (st.item > 0 && st.count > 0) su.stacks.push_back(SaveStack{ i, st });
            }
        }
        if (const Equipment* worn = w.get<Equipment>(u)) {
            for (int g = 0; g < GEAR_SLOTS; ++g) {
                if (worn->items[g] > 0) su.gear.push_back(SaveGear{ g, worn->items[g] });
            }
        }
        d.units.push_back(su);
    }

    d.hasItems = true;
    for (Entity e : w.view<GroundItem, Transform>()) {
        const GroundItem* g = w.get<GroundItem>(e);
        const Transform* t = w.get<Transform>(e);
        d.ground.push_back(SaveGround{ t->x, t->y, t->z, g->item, g->count });
    }
    return d;
}

// Puts the camera on the tile under the lowest-numbered unit, so the game starts there
void focusCameraOnFirstUnit(World& w, Camera& cam) {
    Entity first = NULL_ENTITY;
    for (Entity u : w.view<Unit, Transform, Commandable>()) {
        if (first == NULL_ENTITY || u < first) first = u;
    }
    if (first == NULL_ENTITY) return;

    const Transform* t = w.get<Transform>(first);
    cam.focusX = std::floor(t->x) + 0.5f;
    cam.focusY = t->y;
    cam.focusZ = std::floor(t->z) + 0.5f;
    cam.clampState();
}

// Moves the camera's focus onto a unit, wherever it is on the map
void focusCameraOnUnit(World& w, Camera& cam, Entity u) {
    const Transform* t = w.get<Transform>(u);
    if (!t) return;
    cam.focusX = t->x;
    cam.focusY = t->y;
    cam.focusZ = t->z;
    cam.clampState();
}

// Holds the camera's focus on a unit as it moves. False once the unit is gone, so following stops.
bool followUnitCamera(World& w, Camera& cam, Entity u) {
    const Transform* t = w.get<Transform>(u);
    if (!t || !w.has<Unit>(u)) return false;
    cam.focusX = t->x;
    cam.focusZ = t->z;
    cam.clampState();
    return true;
}

// Puts the camera back where it was saved. Saves from before the camera was stored keep
// the default focus set by focusCameraOnFirstUnit.
void applySaveCamera(const SaveCamera& c, Camera& cam) {
    if (!c.present) return;
    cam.focusX = c.fx;
    cam.focusY = c.fy;
    cam.focusZ = c.fz;
    cam.yaw = c.yaw;
    cam.pitch = c.pitch;
    cam.distance = c.distance;
    cam.clampState();
}

// Current time in milliseconds since 1970
long long nowMs() {
    return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

// The name with leading and trailing spaces removed
std::string trimmedName(const std::string& s) {
    size_t a = s.find_first_not_of(' ');
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(' ');
    return s.substr(a, b - a + 1);
}

// Removes the last character. Names can hold UTF-8, so skip its continuation bytes too.
void popUtf8Char(std::string& s) {
    if (s.empty()) return;
    s.pop_back();
    while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0x80) s.pop_back();
}

// The save with exactly this name, if there is one
std::optional<SaveSummary> findSaveByName(const std::vector<SaveSummary>& saves, const std::string& name) {
    for (const SaveSummary& s : saves) {
        if (s.name == name) return s;
    }
    return std::nullopt;
}

std::string toLower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// Sorts the save list. Autosaves always come first, then manual saves. Each group is sorted
// by the chosen column; ties fall back to the other column.
void sortSaves(std::vector<SaveSummary>& saves, SortKey key, bool ascending) {
    std::stable_sort(saves.begin(), saves.end(), [&](const SaveSummary& a, const SaveSummary& b) {
        if (a.autosave != b.autosave) return a.autosave;
        if (key == SortKey::Name) {
            std::string la = toLower(a.name), lb = toLower(b.name);
            if (la != lb) return ascending ? la < lb : lb < la;
            return a.timestamp > b.timestamp;
        }
        if (a.timestamp != b.timestamp) {
            return ascending ? a.timestamp < b.timestamp : a.timestamp > b.timestamp;
        }
        return a.name < b.name;
    });
}

// What a map is, as the list's Type column shows it
std::string mapTypeLabel(const MapChoice& m) {
    if (m.source == "data") return "BUILT-IN";
    return m.source == "world" ? "WORLD" : "MAP FILE";
}

// Sorts the New Game list. The premade map always comes first. The rest sort by name, or by the date
// they were saved, newest first unless ascending.
void sortMaps(std::vector<MapChoice>& maps, bool byName, bool ascending) {
    std::stable_sort(maps.begin(), maps.end(), [&](const MapChoice& a, const MapChoice& b) {
        const bool aPremade = a.source == "data", bPremade = b.source == "data";
        if (aPremade != bPremade) return aPremade;
        if (byName) {
            const std::string la = toLower(a.name), lb = toLower(b.name);
            if (la != lb) return ascending ? la < lb : lb < la;
            return a.created > b.created;
        }
        if (a.created != b.created) return ascending ? a.created < b.created : a.created > b.created;
        return a.name < b.name;
    });
}

// The map a save was made on. Saves made before the premade maps were cut down to text_map name map_01.
std::optional<MapChoice> findMap(const std::vector<MapChoice>& maps, const std::string& source, std::string stem) {
    if (source == "data" && stem == "map_01") stem = "text_map";
    for (const MapChoice& m : maps) {
        if (m.source == source && m.stem == stem) return m;
    }
    return std::nullopt;
}

// How many save rows fit the window height. The rows take whatever space is left under the fixed
// parts of the popout, kept between MIN_SAVE_ROWS and MAX_SAVE_ROWS.
size_t visibleSaveRows(int H) {
    const float s = H / 720.0f;
    const float fixed = 246.0f * s;      // heading, name field, headers, "..." rows, Close, margins
    const float pitch = 32.0f * s;       // one row plus its gap
    const float space = H - 16.0f * s - fixed;
    size_t rows = space > 0.0f ? (size_t)(space / pitch) : 0;
    return std::clamp(rows, MIN_SAVE_ROWS, MAX_SAVE_ROWS);
}

// The save/load popout, in pixels. It is drawn over the screen underneath and leaves it in place.
struct PanelGeometry {
    Rect box;
    float pad;
    Rect nameBox;     // the save name field
    Rect saveBtn;     // Save button, beside the name field
    float headerY;    // column headers
    float headerH;
    Rect aboveBand;   // "..." row for saves hidden above, between the headers and the first row
    float listTop;    // first save row
    Rect belowBand;   // "..." row for saves hidden below, between the last row and Close
    float closeY;     // Close button
    float btnH;       // height of the name field, Save, and Close
    float rowH;       // height of one save row
    float rowPitch;   // distance from one save row to the next
};

PanelGeometry panelGeometry(int W, int H) {
    // Compact sizes so that LOAD_LIST_LIMIT rows fit on a 720p window
    const float s = H / 720.0f;
    const float btnH = 44.0f * s, gap = 8.0f * s, rowGap = 4.0f * s;
    const float pad = 16.0f * s, hdrH = 30.0f * s, band = 24.0f * s;
    const float rowH = 28.0f * s, pitch = rowH + rowGap;
    const float pw = 1100.0f * s, btnW = 180.0f * s;
    // The panel is centerd vertically, and never closer than 8px to the top
    const float panelH = 56.0f * s + btnH + gap + hdrH + band
                       + (float)visibleSaveRows(H) * pitch + band + btnH + pad;
    const float px = (W - pw) * 0.5f, py = std::max(8.0f * s, (H - panelH) * 0.5f);
    const float nameY = py + 56.0f * s;
    const float headerY = nameY + btnH + gap;
    const float aboveY = headerY + hdrH;
    const float listTop = aboveY + band;
    const float belowY = listTop + (float)visibleSaveRows(H) * pitch;
    const float closeY = belowY + band;
    const float bottom = closeY + btnH + pad;
    const float nameW = pw - 2.0f * pad - btnW - gap;
    const float innerX = px + pad, innerW = pw - 2.0f * pad;

    PanelGeometry g;
    g.box = Rect{ px, py, pw, bottom - py };
    g.pad = pad;
    g.nameBox = Rect{ px + pad, nameY, nameW, btnH };
    g.saveBtn = Rect{ px + pad + nameW + gap, nameY, btnW, btnH };
    g.headerY = headerY;
    g.headerH = hdrH;
    g.aboveBand = Rect{ innerX, aboveY, innerW, band };
    g.listTop = listTop;
    g.belowBand = Rect{ innerX, belowY, innerW, band };
    g.closeY = closeY;
    g.btnH = btnH;
    g.rowH = rowH;
    g.rowPitch = pitch;
    return g;
}

// The question shown in the middle of the popout
struct DialogGeometry {
    Rect box;
    Rect yes;
    Rect no;
};

DialogGeometry questionGeometry(int W, int H) {
    const float s = H / 720.0f;
    const float bh = 56.0f * s, bw = 200.0f * s;
    const float dw = 760.0f * s, dh = 190.0f * s;
    const float dx = (W - dw) * 0.5f, dy = H * 0.40f;
    const float by = dy + dh - bh - 28.0f * s;

    DialogGeometry d;
    d.box = Rect{ dx, dy, dw, dh };
    d.yes = Rect{ dx + dw * 0.5f - bw - 10.0f * s, by, bw, bh };
    d.no = Rect{ dx + dw * 0.5f + 10.0f * s, by, bw, bh };
    return d;
}

// Buttons for the popout, in pixels. Drawing and clicking both use this, so they always match.
// While a question is up, only its Yes and No buttons are returned.
// Save rows carry their index in the list as param. Their text is drawn in columns by the renderer code.
std::vector<Button> layoutPanel(int W, int H, bool saveMode, Ask ask,
                                const std::vector<SaveSummary>& saves, size_t listOffset, bool fromTitle) {
    std::vector<Button> out;
    if (ask != Ask::None) {
        DialogGeometry d = questionGeometry(W, H);
        if (ask == Ask::Delete) {
            out.push_back(Button{ "Delete", d.yes, Action::DeleteHold });
            out.push_back(Button{ "Cancel", d.no, Action::DeleteNo });
            return out;
        }
        Action yes = Action::OverwriteYes, no = Action::OverwriteNo;
        if (ask == Ask::LoadUnsaved) { yes = Action::LoadYes; no = Action::LoadNo; }
        out.push_back(Button{ "Yes", d.yes, yes });
        out.push_back(Button{ "No", d.no, no });
        return out;
    }

    PanelGeometry g = panelGeometry(W, H);
    const float x = g.box.x + g.pad, w = g.box.w - 2.0f * g.pad;

    if (saveMode) out.push_back(Button{ "Save", g.saveBtn, Action::ConfirmName });

    // Column headers. Type is not sortable, since autosaves always come first anyway.
    out.push_back(Button{ "Name", Rect{ x, g.headerY, COL_TYPE * w, g.headerH }, Action::SortName });
    out.push_back(Button{ "Time", Rect{ x + COL_TIME * w, g.headerY, (1.0f - COL_TIME) * w, g.headerH }, Action::SortTime });

    const size_t rows = visibleSaveRows(H);
    size_t first = std::min(listOffset, saves.size());
    size_t last = std::min(first + rows, saves.size());

    // "..." rows: clickable, and shown only when saves are hidden above or below
    if (first > 0) out.push_back(Button{ "", g.aboveBand, Action::ScrollUp });
    const float uiScale = H / 720.0f;
    const float delW = 84.0f * uiScale, delGap = 8.0f * uiScale;
    float y = g.listTop;
    for (size_t i = first; i < last; ++i) {
        // Each row: the save itself, then its Delete button in the right-hand column
        out.push_back(Button{ "", Rect{ x, y, w - delW - delGap, g.rowH }, Action::LoadSlot, true, (int)i });
        out.push_back(Button{ "Delete", Rect{ x + w - delW, y, delW, g.rowH }, Action::DeleteSave, true, (int)i });
        y += g.rowPitch;
    }
    if (last < saves.size()) out.push_back(Button{ "", g.belowBand, Action::ScrollDown });

    // Opened from the title screen, the button goes back there. Opened from the Esc menu, it closes to that menu.
    out.push_back(Button{ fromTitle ? "Return to Title" : "Close", Rect{ x, g.closeY, w, g.btnH },
                          Action::CancelName });
    return out;
}

// Buttons for the New Game list, in the same popout as the load list: the column headers, one row
// per visible map, the "..." rows at the ends, and Close.
std::vector<Button> layoutMapPanel(int W, int H, const std::vector<MapChoice>& maps, size_t listOffset) {
    std::vector<Button> out;
    PanelGeometry g = panelGeometry(W, H);
    const float x = g.box.x + g.pad, w = g.box.w - 2.0f * g.pad;

    out.push_back(Button{ "Name", Rect{ x, g.headerY, COL_TYPE * w, g.headerH }, Action::MapSortName });
    out.push_back(Button{ "Created", Rect{ x + COL_TIME * w, g.headerY, (1.0f - COL_TIME) * w, g.headerH },
                          Action::MapSortTime });

    const size_t rows = visibleSaveRows(H);
    const size_t first = std::min(listOffset, maps.size());
    const size_t last = std::min(first + rows, maps.size());
    if (first > 0) out.push_back(Button{ "", g.aboveBand, Action::MapScrollUp });
    float y = g.listTop;
    for (size_t i = first; i < last; ++i) {
        out.push_back(Button{ "", Rect{ x, y, w, g.rowH }, Action::PickMap, true, (int)i });
        y += g.rowPitch;
    }
    if (last < maps.size()) out.push_back(Button{ "", g.belowBand, Action::MapScrollDown });

    out.push_back(Button{ "Return to Title", Rect{ x, g.closeY, w, g.btnH }, Action::ReturnToTitle });
    return out;
}

// Buttons for the main menus, in pixels
std::vector<Button> layoutButtons(Screen screen, int W, int H, const std::vector<SaveSummary>& saves) {
    const float s = H / 720.0f;
    const float bh = 56.0f * s, gap = 14.0f * s;
    const float bw = 380.0f * s;
    std::vector<Button> out;
    auto add = [&](const std::string& label, Action action, float x, float y, bool enabled = true) {
        out.push_back(Button{ label, Rect{ x, y, bw, bh }, action, enabled });
    };

    const bool hasSave = !saves.empty();

    switch (screen) {
    case Screen::Title: {
        // Right-hand side of the screen. The left side is left clear for the logo.
        float x = W * 0.62f, y = H * 0.34f;
        add("Continue", Action::Continue, x, y, hasSave);        y += bh + gap;
        add("New Game", Action::NewGame, x, y);                  y += bh + gap;
        add("Load Game", Action::LoadGame, x, y, hasSave);       y += bh + gap;
        add("Settings", Action::Settings, x, y);                 y += bh + gap;
        add("Quit", Action::Quit, x, y);
        break;
    }
    case Screen::Pause: {
        float x = (W - bw) * 0.5f, y = H * 0.36f;
        add("Resume", Action::Resume, x, y);                     y += bh + gap;
        add("Save Game", Action::Save, x, y);                    y += bh + gap;
        add("Load Game", Action::Load, x, y, hasSave);           y += bh + gap;
        add("Settings", Action::Settings, x, y);                 y += bh + gap;
        add("Return to Title", Action::ReturnToTitle, x, y);
        break;
    }
    case Screen::Playing:
    case Screen::Settings:
    case Screen::MapSelect:
    case Screen::PartySetup:
        break;
    }
    return out;
}

// The tracks a screen plays, as file names in the BGM folder. A null track keeps whatever is playing on its bus,
// so the menus opened from the title (New Game, Load Game, Settings) do not restart it. "" is silence. Both
// tracks are matched to the current screen once per frame.
struct Soundtrack {
    const char* music = nullptr;     // the music bus
    const char* ambient = nullptr;   // the ambient bus
};

Soundtrack soundtrackFor(Screen screen) {
    switch (screen) {
    case Screen::Title:
        return { "title.mp3", "" };
    case Screen::Settings:
    case Screen::MapSelect:
    case Screen::PartySetup:
    case Screen::Pause:
        return {};
    case Screen::Playing:
        return { "bgm_01.mp3", "amb_01.mp3" };
    }
    return {};
}

// Sound effect files, in the SFX folder
const char* const SFX_MENU = "sfx_01.mp3";   // a confirmed click on a menu button or control
const char* const SFX_HIT = "sfx_02.mp3";    // a swing that lands
const char* const SFX_MISS = "sfx_03.mp3";   // a swing that misses or is dodged
const char* const SFX_ITEM = "sfx_04.mp3";   // an item picked up, moved between inventory and gear, or dropped from inventory

} // namespace

int main(int, char**) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    if (!TTF_Init()) {
        SDL_Log("TTF_Init failed: %s", SDL_GetError());
        return 1;
    }
    // Every asset comes from data.pak next to the executable, except the music and sound effects: those are loose
    // files in the BGM and SFX folders beside it, read by the audio module.
    if (!pakOpen("data.pak")) {
        SDL_Log("Could not open data.pak. It must sit next to the game executable.");
        return 1;
    }
    TTF_Font* font = pakFont("data/font/NotoSans_Condensed-Bold.ttf", 64.0f);
    TTF_Font* hudFont = pakFont("data/font/NotoSans_Condensed-Bold.ttf", 22.0f);
    TTF_Font* uiFont = pakFont("data/font/NotoSans_Condensed-Bold.ttf", 30.0f);
    TTF_Font* listFont = pakFont("data/font/NotoSans_Condensed-Bold.ttf", 20.0f);   // save list rows
    if (!font || !hudFont || !uiFont || !listFont) {
        SDL_Log("Could not open font from data.pak: %s", SDL_GetError());
        return 1;
    }
    // The Stats popout uses NotoSans Regular. Until that file is in data/font, it uses Bold.
    TTF_Font* statsTitleFont = pakFont("data/font/NotoSans_Condensed-Regular.ttf", 30.0f);
    TTF_Font* statsRowFont = pakFont("data/font/NotoSans_Condensed-Regular.ttf", 20.0f);
    if (!statsTitleFont || !statsRowFont) {
        SDL_Log("NotoSans_Condensed-Regular.ttf is not in data/font, so the Stats popout uses Bold");
        statsTitleFont = pakFont("data/font/NotoSans_Condensed-Bold.ttf", 30.0f);
        statsRowFont = pakFont("data/font/NotoSans_Condensed-Bold.ttf", 20.0f);
    }
    if (!statsTitleFont || !statsRowFont) {
        SDL_Log("Could not open the Stats popout font from data.pak: %s", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    SDL_Window* window = SDL_CreateWindow("Kobold Game", 1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    SDL_GLContext ctx = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, ctx);

    glewExperimental = GL_TRUE;
    GLenum glewErr = glewInit();
    if (glewErr != GLEW_OK) {
        SDL_Log("GLEW init failed: %s", glewGetErrorString(glewErr));
        return 1;
    }
    glGetError();

    moveOldMapsFolder();
    GameSettings settings = loadSettings(SETTINGS_PATH);
    DisplayInfo displayInfo = queryDisplays(settings.monitor);
    applyDisplay(window, settings, displayInfo);
    SDL_GL_SetSwapInterval(settings.vsync ? 1 : 0);

    // ---- Loading phase: textures and models load before the title screen appears ----
    Renderer renderer;
    if (!renderer.init(font, hudFont, uiFont, [&](float p) { loadingFrame(window, renderer, p); })) return 1;
    renderer.setListFont(listFont);
    renderer.setStatsFonts(statsTitleFont, statsRowFont);

    Terrain terrain;
    World world;
    Rng rng;
    GameState gs;
    Selection sel;
    Camera cam;
    unsigned levelSeed = 0;
    std::vector<MapChoice> maps;          // premade and custom maps, for New Game
    MapChoice currentMap;                 // the map the game in progress is played on
    size_t mapOffset = 0;                 // first map shown in the New Game list
    bool mapSortByName = false;           // New Game list sorted by name, else by date saved
    bool mapSortAsc = false;              // ascending order; the list opens newest first
    SkyTuning sky;                        // atmosphere settings
    int unitPanelRow = 0;                 // first visible row of the bottom unit panel
    // Walk or run for the squad. Run moves faster, and each unit that moves at run pace in an in-game hour pays
    // double hunger at the next hour. Not saved: a game starts walking.
    bool runMode = false;
    // A stack being dragged out of the inventory sidebar. Released outside the sidebar, it drops on the ground.
    bool itemDrag = false;
    Entity dragUnit = NULL_ENTITY;        // the unit whose inventory it came from
    int dragSlot = -1;                    // the inventory slot it came from, or -1 when dragging gear
    int dragGear = -1;                    // the gear slot it came from, or -1 when dragging from the inventory
    // The stats popout for the lone selected unit, opened from the sidebar's Stats button
    bool statsOpen = false;
    // The right-click menu for an inventory item: Use and Drop, opened at the mouse
    struct ItemMenu {
        bool open = false;
        Entity unit = NULL_ENTITY;
        int slot = -1;
        float x = 0.0f, y = 0.0f;
    };
    ItemMenu itemMenu;
    // How far the sidebar is scrolled, in pixels, and the unit that scroll was for. A different unit starts at the top.
    float sidebarScroll = 0.0f;
    Entity sidebarScrollUnit = NULL_ENTITY;
    // The condition editor for one behavior of a unit, opened from the behavior's Condition cell at (x, y)
    struct BehaviorEditor {
        bool open = false;
        Entity unit = NULL_ENTITY;
        size_t row = 0;
        float x = 0.0f, y = 0.0f;
    };
    BehaviorEditor behaviorEditor;

    Screen screen = Screen::Title;
    Screen backScreen = Screen::Title;    // where Back goes from Settings
    SettingsMenuState settingsUi;         // the Settings screen's tab, open dropdown and dragged slider

    // The starting party: the units a new game begins with, edited in the party menu. It is rolled the
    // first time that menu opens, then kept for the session, so edits survive a trip back to the map list.
    Party party{};
    bool partyRolled = false;
    PartyUi partyUi;
    MapChoice pendingMap;                 // the map picked, which starts once the party is confirmed
    Rng partyRng;
    partyRng.gen.seed((unsigned)std::time(nullptr));

    // Whether a game is loaded. Loading over one may need a confirmation first.
    bool gameActive = false;
    // When the current game was last saved. Empty until it's saved, so a new or loaded game counts as unsaved.
    std::optional<Uint64> lastSaveTicks;

    // Save/load popout. It sits over the current screen.
    bool panelOpen = false;
    bool panelSaveMode = false;           // true: Save Game, false: Load Game
    Screen panelFrom = Screen::Title;     // the screen the popout opened over
    Ask ask = Ask::None;                  // the question the popout is asking, if any
    std::optional<SaveSummary> pendingOverwrite;   // the save an overwrite question is about
    std::optional<SaveSummary> pendingLoad;        // the save an unsaved-progress question is about
    std::optional<SaveSummary> pendingDelete;      // the save the delete question is about
    bool deleteHolding = false;           // the Delete button is being held down
    Uint64 deleteHoldStart = 0;           // when that hold began, in SDL ticks
    Rect deleteHoldRect{ 0.0f, 0.0f, 0.0f, 0.0f };   // the Delete button being held
    std::string saveName;                 // text typed in the popout for a manual save
    size_t listOffset = 0;                // first save shown in the popout
    int viewH = 720;                      // window height, for how many save rows fit

    // Sort for the save list. The popout always opens sorted by time, newest first.
    SortKey sortKey = SortKey::Time;
    bool sortAsc = false;

    std::vector<SaveSummary> saves = listSaves();
    sortSaves(saves, sortKey, sortAsc);

    bool leftDown = false, middleDown = false;
    bool running = true;
    bool screenshotPending = false;   // set by F12, and the next frame is saved before it is shown
    float pressX = 0.0f, pressY = 0.0f;
    float lastX = 0.0f, lastY = 0.0f;
    std::string lastTitle;

    // The unit the camera follows, set by a double-click on its panel card. A manual pan clears it.
    Entity followUnit = NULL_ENTITY;

    // The combat log and the damage numbers over the units. Neither is saved, so each new or loaded level starts empty.
    CombatLog combatLog;
    std::vector<FloatingNumber> floatingNumbers;
    int logOffset = 0;      // lines the combat log is scrolled up from its newest line; 0 follows new lines
    size_t logSeen = 0;     // lines the log held last frame, so a view scrolled up stays on the same lines
    auto clearCombatText = [&]() {
        combatLog = CombatLog{};
        floatingNumbers.clear();
        logOffset = 0;
        logSeen = 0;
    };

    // Autosave countdown, in seconds of play
    float autoTimer = 0.0f;

    // Pivot ring under the camera's orbit point: fades in while the middle button is held
    float pivotAlpha = 0.0f;

    // Action crystal: appears where the last right-click order went, then fades out
    float actionAlpha = 0.0f;
    float actionX = 0.0f, actionZ = 0.0f;

    // Short message ("Game saved", and so on)
    std::string notice;
    float noticeSec = 0.0f;

    Uint64 lastTicks = SDL_GetTicks();
    SDL_Event e;

    auto refreshSaves = [&]() {
        saves = listSaves();
        sortSaves(saves, sortKey, sortAsc);
    };

    auto refreshMaps = [&]() {
        maps = listMaps();
        sortMaps(maps, mapSortByName, mapSortAsc);
    };

    auto showNotice = [&](const std::string& text) {
        notice = text;
        noticeSec = 2.5f;
    };

    // Closes the popout and drops any pending question
    auto closePanel = [&]() {
        panelOpen = false;
        ask = Ask::None;
        pendingOverwrite.reset();
        pendingLoad.reset();
        pendingDelete.reset();
        deleteHolding = false;
        SDL_StopTextInput(window);
    };

    // Opens the popout in save mode (with an empty name) or load mode. The sort goes back to the default.
    auto openPanel = [&](bool saveMode) {
        panelOpen = true;
        panelSaveMode = saveMode;
        panelFrom = screen;
        ask = Ask::None;
        pendingOverwrite.reset();
        pendingLoad.reset();
        pendingDelete.reset();
        deleteHolding = false;
        saveName.clear();
        listOffset = 0;
        sortKey = SortKey::Time;
        sortAsc = false;
        refreshSaves();
        if (saveMode) SDL_StartTextInput(window);
        else SDL_StopTextInput(window);
    };

    // True if loading now would throw away progress: a game is running and it hasn't been saved
    // in the last UNSAVED_WARN_MS
    auto wouldLoseProgress = [&]() -> bool {
        if (!gameActive) return false;
        if (!lastSaveTicks) return true;
        return SDL_GetTicks() - *lastSaveTicks > UNSAVED_WARN_MS;
    };

    // Loads a map and starts a new game on it, showing the level-start image
    auto startMapLevel = [&](const MapChoice& choice) {
        std::string err;
        levelSeed = (unsigned)std::time(nullptr);
        // The title music ends as a new game starts, before its level loads
        audio::stopTracks();
        if (!prepareLevel(choice, world, terrain, renderer, rng, levelSeed, true, err,
                          [&](float p) { loadingFrame(window, renderer, p); })) {
            showNotice("Could not load " + choice.name + ": " + err);
            return;
        }
        // The party from the menu replaces the three units that the seed made
        applyParty(world, party);
        clearCombatText();
        closePanel();
        currentMap = choice;
        // The focus is clamped to the map, so the map's size has to be known before it is set
        cam.mapSize = (float)terrain.size();
        focusCameraOnFirstUnit(world, cam);
        primeStreaming(world, terrain, cam);
        gs = GameState{};
        sel = Selection{};
        leftDown = middleDown = false;
        itemDrag = false;
        followUnit = NULL_ENTITY;
        autoTimer = 0.0f;
        gameActive = true;
        lastSaveTicks.reset();
        screen = Screen::Playing;
    };

    // Opens the party menu for a picked map. The party is rolled the first time, then kept.
    auto openParty = [&](const MapChoice& choice) {
        if (!partyRolled) {
            party = randomParty(partyRng);
            partyRolled = true;
        }
        pendingMap = choice;
        partyUi = PartyUi{};
        backScreen = Screen::MapSelect;
        screen = Screen::PartySetup;
    };

    // Stops typing into the name box, if it is taking text
    auto stopNameEdit = [&]() {
        if (partyUi.editingName) SDL_StopTextInput(window);
        partyUi.editingName = false;
    };

    // Closes the open dropdown, if there is one
    auto closeDropdown = [&]() {
        partyUi.dropKind = DropKind::None;
        partyUi.dropScroll = 0;
    };

    // Writes the current game to a file. Used for manual saves and autosaves alike.
    auto writeSave = [&](const std::string& path, const std::string& name, bool autosave) -> bool {
        SaveData d = captureWorld(world, gs, cam, levelSeed, nowMs(), name, currentMap);
        d.autosave = autosave;
        bool ok = writeSaveFile(path, d);
        if (ok) lastSaveTicks = SDL_GetTicks();
        refreshSaves();
        return ok;
    };

    // Loads a save and starts play. The popout closes.
    auto loadSave = [&](std::string path) {
        SaveData d;
        if (!readSaveFile(path, d)) {
            showNotice("Could not read that save");
            refreshSaves();
            return;
        }
        if (d.mapStem.empty()) {
            showNotice("That save is from before maps and can't be loaded");
            refreshSaves();
            return;
        }
        std::optional<MapChoice> found = findMap(listMaps(), d.mapSource, d.mapStem);
        if (!found) {
            showNotice("The map for \"" + d.name + "\" is missing");
            refreshSaves();
            return;
        }
        const MapChoice choice = *found;
        std::string err;
        // The title music ends as a saved game starts, before its level loads
        audio::stopTracks();
        if (!restoreLevel(world, terrain, renderer, rng, d, choice, err)) {
            showNotice("Map \"" + choice.name + "\" could not be loaded: " + err);
            refreshSaves();
            return;
        }
        currentMap = choice;
        levelSeed = d.seed;
        clearCombatText();
        gs = d.gs;
        cam.mapSize = (float)terrain.size();
        focusCameraOnFirstUnit(world, cam);
        applySaveCamera(d.cam, cam);
        primeStreaming(world, terrain, cam);
        sel = Selection{};
        leftDown = middleDown = false;
        itemDrag = false;
        followUnit = NULL_ENTITY;
        autoTimer = 0.0f;
        gameActive = true;
        lastSaveTicks.reset();
        screen = Screen::Playing;
        closePanel();
        showNotice("Loaded " + d.name);
    };

    // Writes an autosave into the next slot. Slots rotate: an empty one first, otherwise the oldest.
    auto autosaveNow = [&]() -> bool {
        std::string path = nextAutosavePath();
        std::string name = std::filesystem::path(path).stem().string();   // autosave0, and so on
        return writeSave(path, name, true);
    };

    // Deletes the save the question is about, then closes the question
    auto performDelete = [&]() {
        if (!pendingDelete) {
            ask = Ask::None;
            return;
        }
        const std::string name = pendingDelete->name;
        std::error_code ec;
        std::filesystem::remove(pendingDelete->path, ec);
        ask = Ask::None;
        pendingDelete.reset();
        deleteHolding = false;
        refreshSaves();
        showNotice(ec ? "Could not delete \"" + name + "\"" : "Deleted \"" + name + "\"");
    };

    // Pans the camera with WASD or the arrow keys. Edge scrolling pans when the mouse is at a window edge.
    auto panWithKeys = [&](float dt, bool edgeScroll) {
        const bool* keys = SDL_GetKeyboardState(nullptr);
        float right = 0.0f, forward = 0.0f;   // edge scrolling adds to these below
        if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) right += 1.0f;
        if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT]) right -= 1.0f;
        if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP]) forward += 1.0f;
        if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]) forward -= 1.0f;

        if (edgeScroll) {
            int winW = 0, winH = 0;
            SDL_GetWindowSizeInPixels(window, &winW, &winH);
            float mx = 0.0f, my = 0.0f;
            SDL_GetMouseState(&mx, &my);
            if (mx < 6.0f) right -= 1.0f;
            if (mx > winW - 6.0f) right += 1.0f;
            if (my < 6.0f) forward += 1.0f;
            if (my > winH - 6.0f) forward -= 1.0f;
        }

        // Holding shift pans twice as fast
        const bool fast = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
        float panSpeed = cam.distance * 0.9f * dt * (fast ? 2.0f : 1.0f);
        // Any manual move takes the camera off the unit it was following
        if (right != 0.0f || forward != 0.0f) followUnit = NULL_ENTITY;
        cam.pan(right * panSpeed, forward * panSpeed);
    };

    // ---- Settings: the audio buses and the window follow the settings as they change ----
    auto applyAudio = [&]() {
        audio::setBus(audio::Bus::Music, settings.musicVolume, settings.musicOn);
        audio::setBus(audio::Bus::Ambient, settings.ambientVolume, settings.ambientOn);
        audio::setBus(audio::Bus::Effects, settings.effectsVolume, settings.effectsOn);
    };
    // Reads the monitors again, then makes the window show the settings, which are normalized to what it can show
    auto applyVideo = [&]() {
        displayInfo = queryDisplays(settings.monitor);
        applyDisplay(window, settings, displayInfo);
        SDL_GL_SetSwapInterval(settings.vsync ? 1 : 0);
    };
    auto saveSettingsFile = [&]() { saveSettings(settings, SETTINGS_PATH); };
    // Applies what a change touched. It is saved when the change is final: a slider saves when released.
    auto settingsCommit = [&](const SettingsChange& c, bool save) {
        if (!c.any) return;
        if (c.display) applyVideo();
        else if (c.vsync) SDL_GL_SetSwapInterval(settings.vsync ? 1 : 0);
        if (c.audio) applyAudio();
        if (save) saveSettingsFile();
    };
    auto leaveSettings = [&]() {
        settingsUi.openRow = -1;
        settingsUi.dragRow = -1;
        screen = backScreen;
    };
    // A left click on the Settings screen: a tab, a dropdown or one of its options, a toggle, a slider or a button
    auto settingsClick = [&](float x, float y, int W, int H) {
        const std::vector<RowSpec> rows = settingsRows(settingsUi.tab, settings, displayInfo);
        const SettingsClick c = hitSettings(layoutSettings(W, H, rows, settingsUi), rows, x, y);
        // Every control that does something clicks. Closing a dropdown, and a click on nothing, do not.
        if (c.kind != ClickKind::None && c.kind != ClickKind::CloseDropdown) audio::playEffect(SFX_MENU);
        switch (c.kind) {
        case ClickKind::Tab:
            settingsSelectTab(settingsUi, static_cast<SettingsTab>(c.index));
            break;
        case ClickKind::OpenDropdown:
            settingsOpenDropdown(settingsUi, rows, c.row);
            break;
        case ClickKind::CloseDropdown:
            settingsCloseDropdown(settingsUi);
            break;
        case ClickKind::PickOption:
            settingsCloseDropdown(settingsUi);
            settingsCommit(pickOption(rows[c.row].id, rows[c.row].options[c.index], settings), true);
            break;
        case ClickKind::ToggleRow:
        case ClickKind::LevelToggle:
            settingsCommit(setRowOn(rows[c.row].id, !rows[c.row].on, settings), true);
            break;
        case ClickKind::LevelDrag:
            settingsUi.dragRow = c.row;
            settingsCommit(setRowLevel(rows[c.row].id, c.index, settings), false);
            break;
        case ClickKind::Defaults:
            settingsCloseDropdown(settingsUi);
            settingsCommit(resetAllSettings(settings), true);
            break;
        case ClickKind::Back:
            leaveSettings();
            break;
        case ClickKind::None:
            break;
        }
    };

    // Runs a button's action
    auto performAction = [&](Action action, int param) {
        audio::playEffect(SFX_MENU);
        switch (action) {
        case Action::Continue:
            if (!saves.empty()) loadSave(saves.front().path);
            return;
        case Action::NewGame:
            refreshMaps();
            mapOffset = 0;
            backScreen = Screen::Title;
            screen = Screen::MapSelect;
            return;
        case Action::PickMap:
            if (param >= 0 && param < (int)maps.size()) openParty(maps[param]);
            return;
        case Action::MapSortName:
        case Action::MapSortTime: {
            // Clicking the active column flips the order. A new column starts ascending.
            const bool byName = action == Action::MapSortName;
            if (mapSortByName == byName) {
                mapSortAsc = !mapSortAsc;
            } else {
                mapSortByName = byName;
                mapSortAsc = true;
            }
            sortMaps(maps, mapSortByName, mapSortAsc);
            mapOffset = 0;
            return;
        }
        case Action::MapScrollUp: {
            const size_t page = visibleSaveRows(viewH);
            mapOffset = (mapOffset > page) ? mapOffset - page : 0;
            return;
        }
        case Action::MapScrollDown: {
            const size_t page = visibleSaveRows(viewH);
            const size_t maxOffset = maps.size() > page ? maps.size() - page : 0;
            mapOffset = std::min(mapOffset + page, maxOffset);
            return;
        }
        case Action::PartyPick:
            partyUi.selected = std::clamp(param, 0, START_UNIT_COUNT - 1);
            stopNameEdit();
            closeDropdown();
            return;
        case Action::PartyTabPick:
            partyUi.tab = (PartyTab)std::clamp(param, 0, PARTY_TAB_COUNT - 1);
            stopNameEdit();
            closeDropdown();
            return;
        case Action::PartyNameField:
            closeDropdown();
            if (!partyUi.editingName) SDL_StartTextInput(window);
            partyUi.editingName = true;
            return;
        case Action::PartySpriteBox:
            // The list opens scrolled so the unit's current sprite is near the middle
            stopNameEdit();
            partyUi.dropKind = DropKind::Sprite;
            partyUi.dropScroll = clampDropScroll(pickedMember(party, partyUi).sprite - 1 - DROP_VISIBLE_ROWS / 2,
                                                 UNIT_SPRITE_COUNT);
            return;
        case Action::PartyRandomUnit: {
            // A new member for the picked unit, with a name that no other unit in the party has
            const int picked = std::clamp(partyUi.selected, 0, START_UNIT_COUNT - 1);
            std::vector<std::string> others;
            for (int i = 0; i < START_UNIT_COUNT; ++i) {
                if (i != picked) others.push_back(party[i].name);
            }
            party[picked] = randomMember(partyRng, others);
            stopNameEdit();
            closeDropdown();
            return;
        }
        case Action::PartyGearSlot:
            stopNameEdit();
            partyUi.dropKind = DropKind::Gear;
            partyUi.dropSlot = std::clamp(param, 0, GEAR_SLOTS - 1);
            partyUi.dropScroll = 0;
            return;
        case Action::PartyStep: {
            // Minus is the even parameter and plus the odd one. Holding Shift moves a level by ten.
            const int stat = param / 2;
            const int step = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0 ? 10 : 1;
            Stats& st = pickedMember(party, partyUi).stats;
            setStatLevel(st, stat, levelOf(st, stat) + ((param % 2) != 0 ? step : -step));
            return;
        }
        case Action::PartyInvSlot:
            stopNameEdit();
            partyUi.stackSlot = std::clamp(param, 0, INVENTORY_SLOTS - 1);
            partyUi.dropKind = DropKind::Item;
            partyUi.dropSlot = partyUi.stackSlot;
            partyUi.dropScroll = 0;
            return;
        case Action::PartyCount: {
            ItemStack& st = pickedMember(party, partyUi).items[partyUi.stackSlot];
            if (st.item > 0) st.count = std::clamp(st.count + param, 1, stackLimit(st.item));
            return;
        }
        case Action::PartyDropPick: {
            const std::vector<DropEntry> entries = dropdownEntries(party, partyUi);
            if (param < 0 || param >= (int)entries.size()) return;
            PartyMember& m = pickedMember(party, partyUi);
            const int value = entries[param].value;
            switch (partyUi.dropKind) {
            case DropKind::Sprite:
                m.sprite = value;
                break;
            case DropKind::Gear:
                m.gear[partyUi.dropSlot] = value;
                break;
            case DropKind::Item: {
                ItemStack& st = m.items[partyUi.dropSlot];
                if (value == 0) {
                    st = ItemStack{};
                } else {
                    // Choosing the same item again keeps its count. Anything else starts at one.
                    const int count = st.item == value ? st.count : 1;
                    st = ItemStack{ value, std::clamp(count, 1, stackLimit(value)) };
                }
                break;
            }
            case DropKind::None:
                break;
            }
            closeDropdown();
            return;
        }
        case Action::PartyDropScroll: {
            const int count = (int)dropdownEntries(party, partyUi).size();
            partyUi.dropScroll = clampDropScroll(partyUi.dropScroll + param, count);
            return;
        }
        case Action::PartyDropBack:
        case Action::PartyHeading:
        case Action::PartyNote:
        case Action::PartyStatLabel:
        case Action::PartyMessage:
            return;
        case Action::PartyRandomAll:
            party = randomParty(partyRng);
            stopNameEdit();
            closeDropdown();
            return;
        case Action::PartyConfirm:
            if (!partyNamesValid(party)) return;
            stopNameEdit();
            closeDropdown();
            startMapLevel(pendingMap);
            return;
        case Action::LoadGame:
        case Action::Load:
            openPanel(false);
            return;
        case Action::Save:
            openPanel(true);
            return;
        case Action::LoadSlot:
            if (param < 0 || param >= (int)saves.size()) return;
            if (panelSaveMode) {
                // Save mode: a click copies that save's name into the name field
                saveName = saves[param].name;
            } else if (wouldLoseProgress()) {
                // Load mode, and the current game would be lost: ask first
                pendingLoad = saves[param];
                ask = Ask::LoadUnsaved;
            } else {
                loadSave(saves[param].path);
            }
            return;
        case Action::LoadYes: {
            if (!pendingLoad) {
                ask = Ask::None;
                return;
            }
            SaveSummary target = *pendingLoad;
            loadSave(target.path);
            return;
        }
        case Action::LoadNo:
            ask = Ask::None;
            pendingLoad.reset();
            return;
        case Action::DeleteSave:
            if (param < 0 || param >= (int)saves.size()) return;
            pendingDelete = saves[param];
            ask = Ask::Delete;
            return;
        case Action::DeleteHold:
            // Handled by the hold timer, not by clicks
            return;
        case Action::DeleteNo:
            ask = Ask::None;
            pendingDelete.reset();
            deleteHolding = false;
            return;
        case Action::ScrollUp: {
            // Jump up one page of saves
            const size_t page = visibleSaveRows(viewH);
            listOffset = (listOffset > page) ? listOffset - page : 0;
            return;
        }
        case Action::ScrollDown: {
            const size_t page = visibleSaveRows(viewH);
            size_t maxOffset = saves.size() > page ? saves.size() - page : 0;
            listOffset = std::min(listOffset + page, maxOffset);
            return;
        }
        case Action::SortName:
        case Action::SortTime: {
            // Clicking the active column flips the direction. A new column starts ascending.
            SortKey k = (action == Action::SortName) ? SortKey::Name : SortKey::Time;
            if (sortKey == k) {
                sortAsc = !sortAsc;
            } else {
                sortKey = k;
                sortAsc = true;
            }
            sortSaves(saves, sortKey, sortAsc);
            listOffset = 0;
            return;
        }
        case Action::ConfirmName: {
            // Save button. A name that matches an existing save asks before overwriting it.
            std::string name = trimmedName(saveName);
            if (name.empty()) return;
            if (std::optional<SaveSummary> existing = findSaveByName(saves, name)) {
                pendingOverwrite = existing;
                ask = Ask::Overwrite;
                return;
            }
            bool ok = writeSave(manualSavePath(nowMs()), name, false);
            closePanel();
            showNotice(ok ? "Game saved" : "Could not save the game");
            return;
        }
        case Action::OverwriteYes: {
            if (!pendingOverwrite) {
                ask = Ask::None;
                return;
            }
            SaveSummary target = *pendingOverwrite;
            // Keep the save's file and its AUTO or MANUAL type; only the contents change
            bool ok = writeSave(target.path, target.name, target.autosave);
            closePanel();
            showNotice(ok ? "Game saved" : "Could not save the game");
            return;
        }
        case Action::OverwriteNo:
            ask = Ask::None;
            pendingOverwrite.reset();
            return;
        case Action::CancelName:
            // Close button
            closePanel();
            return;
        case Action::Settings:
            backScreen = screen;
            screen = Screen::Settings;
            settingsUi.openRow = -1;
            settingsUi.dragRow = -1;
            return;
        case Action::Back:
            stopNameEdit();
            closeDropdown();
            screen = backScreen;
            return;
        case Action::Resume:
            screen = Screen::Playing;
            return;
        case Action::ReturnToTitle:
            screen = Screen::Title;
            gameActive = false;
            refreshSaves();
            return;
        case Action::Quit:
            running = false;
            return;
        }
    };

    // Without an audio output the game runs silent, so this result is not checked
    audio::init();
    applyAudio();

    while (running) {
        // Taken at the top of the frame, for the frame rate limit at its end
        const auto frameStart = std::chrono::steady_clock::now();

        // Music and ambient sound follow the screen. A screen that names a track starts it, nullptr keeps what is
        // playing, and "" is silence. Each request is tried once, so a missing track is not reloaded every frame.
        const Soundtrack sound = soundtrackFor(screen);
        if (sound.music) audio::playTrack(audio::Bus::Music, sound.music);
        if (sound.ambient) audio::playTrack(audio::Bus::Ambient, sound.ambient);

        int winW, winH;
        SDL_GetWindowSizeInPixels(window, &winW, &winH);
        viewH = winH;

        // Own units by entity number, lowest first. The bottom panel shows them in this order, and the
        // camera starts on the first of them, which is also the first party member.
        auto ownUnitList = [&]() {
            std::vector<Entity> units = world.view<Unit, Transform, Commandable>();
            std::sort(units.begin(), units.end());
            return units;
        };
        // The sidebar is up for the lone selected unit, yours or a red one
        auto sidebarUp = [&]() { return sidebarUnitOf(world) != NULL_ENTITY; };
        // The sidebar's layout for a window of W by H, for the unit it shows, scrolled by the given pixels
        auto sidebarFor = [&](int W, int H, float scroll) {
            const Entity shown = sidebarUnitOf(world);
            const Behaviors* b = shown != NULL_ENTITY ? world.get<Behaviors>(shown) : nullptr;
            return layoutInventorySidebar(W, H, b ? (int)b->rules.size() : 0, scroll);
        };
        // Only your own units can be changed from the sidebar. A red unit's sidebar is for looking at.
        auto shownIsYours = [&]() {
            const Entity shown = sidebarUnitOf(world);
            return shown != NULL_ENTITY && world.has<Commandable>(shown);
        };
        // Sorts a unit's inventory by item type
        auto sortInventory = [&](Entity unit) {
            Inventory* inv = world.get<Inventory>(unit);
            if (!inv) return;
            sortInventoryByType(*inv);
            audio::playEffect(SFX_MENU);
        };
        // Adds a behavior with the default rule, unless the list is full
        auto addBehavior = [&](Entity unit) {
            Behaviors* b = world.get<Behaviors>(unit);
            if (!b || (int)b->rules.size() >= BEHAVIOR_LIMIT) return;
            b->rules.push_back(BehaviorRule{});
            b->pending = true;   // the new rule is checked on the next behavior pass
            audio::playEffect(SFX_MENU);
        };
        // A click on one part of a behavior row. Toggles and choices change the rule at once, the priority and delete
        // buttons move or remove it, and the Condition cell opens the editor for it.
        auto behaviorClick = [&](Entity unit, const BehaviorHit& hit, float mx, float my) {
            Behaviors* b = world.get<Behaviors>(unit);
            if (!b || hit.row < 0 || hit.row >= (int)b->rules.size()) return;
            const size_t row = (size_t)hit.row;
            BehaviorRule& r = b->rules[row];
            switch (hit.part) {
            case BehaviorPart::Toggle:
                r.on = !r.on;
                break;
            case BehaviorPart::ConditionTarget:
                r.conditionTarget = (BehaviorTarget)(((int)r.conditionTarget + 1) % (int)BehaviorTarget::Count);
                break;
            case BehaviorPart::Action:
                r.action = (BehaviorAction)(((int)r.action + 1) % (int)BehaviorAction::Count);
                break;
            case BehaviorPart::ActionTarget:
                r.actionTarget = (BehaviorTarget)(((int)r.actionTarget + 1) % (int)BehaviorTarget::Count);
                break;
            case BehaviorPart::Condition:
                itemMenu.open = false;
                behaviorEditor = BehaviorEditor{ true, unit, row, mx, my };
                return;
            case BehaviorPart::Up:
                if (!moveBehavior(b->rules, row, -1)) return;
                behaviorEditor.open = false;   // the rule's row has changed, so the editor is closed
                break;
            case BehaviorPart::Down:
                if (!moveBehavior(b->rules, row, +1)) return;
                behaviorEditor.open = false;
                break;
            case BehaviorPart::Remove:
                if (!removeBehavior(b->rules, row)) return;
                behaviorEditor.open = false;
                break;
            case BehaviorPart::None:
                return;
            }
            b->pending = true;   // every rule is checked again, so the change takes effect at once
            audio::playEffect(SFX_MENU);
        };
        // A click on the open condition editor: a kind, an operator, or a step of the percent
        auto editorClick = [&](const ConditionEditorLayout& ed, float mx, float my) {
            Behaviors* b = world.get<Behaviors>(behaviorEditor.unit);
            if (!b || behaviorEditor.row >= b->rules.size()) {
                behaviorEditor.open = false;
                return;
            }
            BehaviorRule& r = b->rules[behaviorEditor.row];
            const int kind = rectIndexAt(ed.kinds, mx, my);
            const int op = rectIndexAt(ed.ops, mx, my);
            const int step = rectIndexAt(ed.steps, mx, my);
            static const int stepSizes[4] = { -10, -1, 1, 10 };
            if (kind >= 0) r.condition = (BehaviorCondition)kind;
            else if (op >= 0) r.op = (BehaviorOp)op;
            else if (step >= 0) r.percent = std::clamp(r.percent + stepSizes[step], 0, 100);
            else return;
            b->pending = true;
            audio::playEffect(SFX_MENU);
        };
        // Whether the mouse is over the bottom unit panel
        auto unitPanelHovered = [&](int W, int H) {
            float mx = 0.0f, my = 0.0f;
            SDL_GetMouseState(&mx, &my);
            return layoutUnitPanel(W, H, (int)ownUnitList().size(), unitPanelRow).panel.contains(mx, my);
        };
        // Whether a point is over the bottom unit panel or the inventory sidebar. Those belong to the
        // interface: clicks and wheel turns there never reach the world behind them.
        // The buttons of the right-click menu for the item it was opened on: Use for a consumable with an
        // effect, then Drop. Empty when the menu is closed or its item is gone.
        auto menuLabels = [&]() {
            std::vector<std::string> labels;
            const Inventory* inv = world.get<Inventory>(itemMenu.unit);
            if (!itemMenu.open || !inv || itemMenu.slot < 0 || itemMenu.slot >= INVENTORY_SLOTS) return labels;
            const ItemDef* def = findItem(inv->slots[itemMenu.slot].item);
            if (!def) return labels;
            if (def->typeId == "consumable" && !def->onUse.empty()) labels.push_back("Use");
            labels.push_back("Drop");
            return labels;
        };
        // Uses the stack in a slot of a unit, and says what happened
        auto useSlot = [&](Entity unit, int slot) {
            const Inventory* inv = world.get<Inventory>(unit);
            if (!inv || slot < 0 || slot >= INVENTORY_SLOTS) return;
            const ItemDef* def = findItem(inv->slots[slot].item);
            const std::string itemName = def ? def->name : std::string("That");
            switch (useStackInSlot(world, unit, slot)) {
            case UseResult::Used:
                showNotice("Used " + itemName);
                break;
            case UseResult::NotUsable:
                showNotice(itemName + " can't be used");
                break;
            case UseResult::Empty:
                break;
            }
        };
        // The combat log in the bottom left, stopping short of the unit panel. Its offset is kept in range.
        auto combatLogPanel = [&](int W, int H) {
            const float s = H / 720.0f;
            const float unitLeft = layoutUnitPanel(W, H, (int)ownUnitList().size(), unitPanelRow).panel.x;
            return renderer.combatLogLayout(W, H, logOffset, (int)combatLog.lines.size(), unitLeft - 8.0f * s);
        };
        // Whether the mouse is over the combat log
        auto combatLogHovered = [&](int W, int H) {
            float mx = 0.0f, my = 0.0f;
            SDL_GetMouseState(&mx, &my);
            return combatLogPanel(W, H).panel.contains(mx, my);
        };
        auto overPanels = [&](float x, float y, int W, int H) {
            const std::vector<std::string> labels = menuLabels();
            return overGamePanels(layoutUnitPanel(W, H, (int)ownUnitList().size(), unitPanelRow),
                                  sidebarFor(W, H, sidebarScroll), sidebarUp(), x, y)
                || combatLogPanel(W, H).panel.contains(x, y)
                || (statsOpen && renderer.statsPanelLayout(W, H).box.contains(x, y))
                || (behaviorEditor.open
                    && renderer.conditionEditorLayout(behaviorEditor.x, behaviorEditor.y, W, H).box.contains(x, y))
                || (!labels.empty() && renderer.itemMenuLayout(itemMenu.x, itemMenu.y, (int)labels.size(), W, H).box.contains(x, y));
        };
        auto pointerOverPanels = [&](int W, int H) {
            float mx = 0.0f, my = 0.0f;
            SDL_GetMouseState(&mx, &my);
            return overPanels(mx, my, W, H);
        };

        // ---- Input ----
        while (SDL_PollEvent(&e)) {
            // F12 takes a screenshot from any screen, whatever menu or text box has the keys
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_F12 && !e.key.repeat) {
                screenshotPending = true;
                continue;
            }
            switch (e.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;

            case SDL_EVENT_TEXT_INPUT:
                if (screen == Screen::PartySetup) {
                    // Typed text goes into the name being edited, up to the limit
                    std::string& name = pickedMember(party, partyUi).name;
                    const std::string typed = e.text.text;
                    if (partyUi.editingName && name.size() + typed.size() <= PARTY_NAME_LIMIT) name += typed;
                    break;
                }
                if (panelOpen && panelSaveMode && ask == Ask::None && saveName.size() < NAME_LIMIT) {
                    saveName += e.text.text;
                }
                break;

            case SDL_EVENT_KEY_DOWN:
                if (screen == Screen::PartySetup) {
                    // Escape backs out one step at a time: the dropdown, then the name box, then the map list
                    if (e.key.key == SDLK_ESCAPE) {
                        if (partyUi.dropKind != DropKind::None) {
                            closeDropdown();
                        } else if (partyUi.editingName) {
                            stopNameEdit();
                        } else {
                            screen = backScreen;
                        }
                    } else if (partyUi.editingName) {
                        std::string& name = pickedMember(party, partyUi).name;
                        if (e.key.key == SDLK_BACKSPACE) {
                            popUtf8Char(name);
                        } else if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) {
                            stopNameEdit();
                        }
                    }
                    break;
                }
                if (panelOpen) {
                    if (e.key.key == SDLK_ESCAPE) {
                        // Escape backs out of a question first, then closes the popout
                        if (ask != Ask::None) {
                            ask = Ask::None;
                            pendingOverwrite.reset();
                            pendingLoad.reset();
                            pendingDelete.reset();
                            deleteHolding = false;
                        } else {
                            closePanel();
                        }
                    } else if (panelSaveMode && ask == Ask::None) {
                        if (e.key.key == SDLK_BACKSPACE) {
                            popUtf8Char(saveName);
                        } else if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) {
                            performAction(Action::ConfirmName, 0);
                        }
                    }
                    break;
                }
                if (e.key.key == SDLK_ESCAPE) {
                    if (screen == Screen::Playing && (itemMenu.open || statsOpen || behaviorEditor.open)) {
                        // Escape closes the open menu, editor or popout first, and only then pauses the game
                        itemMenu.open = false;
                        statsOpen = false;
                        behaviorEditor.open = false;
                    } else if (screen == Screen::Playing) {
                        leftDown = middleDown = false;
                        sel.dragging = false;
                        itemDrag = false;
                        itemMenu.open = false;
                        statsOpen = false;
                        behaviorEditor.open = false;
                        screen = Screen::Pause;
                        refreshSaves();
                    } else if (screen == Screen::Pause) {
                        screen = Screen::Playing;
                    } else if (screen == Screen::Settings) {
                        // Escape closes an open dropdown first, then leaves the menu
                        if (settingsUi.openRow >= 0) settingsCloseDropdown(settingsUi);
                        else leaveSettings();
                    } else if (screen == Screen::MapSelect) {
                        performAction(Action::ReturnToTitle, 0);
                    }
                }
                break;

            case SDL_EVENT_MOUSE_WHEEL:
                if (panelOpen) {
                    // Scroll the save list, keeping the offset within range
                    int maxOffset = std::max(0, (int)saves.size() - (int)visibleSaveRows(winH));
                    int next = (int)listOffset - (int)e.wheel.y;
                    listOffset = (size_t)std::clamp(next, 0, maxOffset);
                } else if (screen == Screen::MapSelect) {
                    int maxOffset = std::max(0, (int)maps.size() - (int)visibleSaveRows(winH));
                    int next = (int)mapOffset - (int)e.wheel.y;
                    mapOffset = (size_t)std::clamp(next, 0, maxOffset);
                } else if (screen == Screen::PartySetup) {
                    // Scrolls an open dropdown. The rest of the menu does not scroll.
                    if (partyUi.dropKind != DropKind::None) {
                        const int count = (int)dropdownEntries(party, partyUi).size();
                        partyUi.dropScroll = clampDropScroll(partyUi.dropScroll - (int)e.wheel.y, count);
                    }
                } else if (screen == Screen::Settings) {
                    // Scrolls an open dropdown. Nothing else on the menu scrolls.
                    if (settingsUi.openRow >= 0) {
                        const std::vector<RowSpec> rows = settingsRows(settingsUi.tab, settings, displayInfo);
                        settingsScrollDropdown(settingsUi, layoutSettings(winW, winH, rows, settingsUi), (int)e.wheel.y);
                    }
                } else if (screen == Screen::Playing && combatLogHovered(winW, winH)) {
                    // Scrolls the combat log, three lines a notch. Wheel up goes back in time.
                    const int step = e.wheel.y > 0.0f ? 3 : e.wheel.y < 0.0f ? -3 : 0;
                    logOffset = std::clamp(logOffset + step, 0, combatLogPanel(winW, winH).maxOffset());
                } else if (screen == Screen::Playing && unitPanelHovered(winW, winH)) {
                    // Scrolls the unit list by whole rows
                    unitPanelRow = layoutUnitPanel(winW, winH, (int)ownUnitList().size(),
                                                   unitPanelRow - (int)e.wheel.y).scrollRow;
                } else if (screen == Screen::Playing && sidebarUp()
                           && sidebarFor(winW, winH, sidebarScroll).panel.contains(e.wheel.mouse_x, e.wheel.mouse_y)) {
                    // Scrolls the sidebar, 40 pixels a notch. Wheel up goes back toward the top.
                    const float notch = 40.0f * (winH / 720.0f);
                    sidebarScroll = sidebarFor(winW, winH, sidebarScroll - e.wheel.y * notch).scroll;
                } else if (screen == Screen::Playing && !pointerOverPanels(winW, winH)) {
                    // Over the panels the wheel does nothing, so the camera doesn't zoom behind them
                    cam.distance *= (e.wheel.y > 0.0f) ? 0.9f : 1.1f;
                    cam.clampState();
                }
                break;

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (panelOpen) {
                    // The popout takes all clicks while it's open
                    if (e.button.button == SDL_BUTTON_LEFT) {
                        for (const Button& b : layoutPanel(winW, winH, panelSaveMode, ask, saves, listOffset,
                                                           panelFrom == Screen::Title)) {
                            if (b.enabled && b.rect.contains(e.button.x, e.button.y)) {
                                if (b.action == Action::DeleteHold) {
                                    // Held, not clicked: the timer deletes once it has been held long enough
                                    deleteHolding = true;
                                    deleteHoldStart = SDL_GetTicks();
                                    deleteHoldRect = b.rect;
                                } else {
                                    performAction(b.action, b.param);
                                }
                                break;
                            }
                        }
                    }
                    break;
                }

                if (screen == Screen::PartySetup) {
                    if (e.button.button == SDL_BUTTON_LEFT) {
                        const float mx = e.button.x, my = e.button.y;
                        const std::vector<DropEntry> drop = dropdownEntries(party, partyUi);
                        const std::vector<Button> buttons = layoutPartySetup(winW, winH, party, partyUi, drop);
                        // The last enabled button under the pointer is the one on top, so it gets the click
                        const Button* hit = nullptr;
                        for (auto it = buttons.rbegin(); it != buttons.rend(); ++it) {
                            if (it->enabled && it->rect.contains(mx, my)) {
                                hit = &*it;
                                break;
                            }
                        }
                        const bool onDropdown = hit && (hit->action == Action::PartyDropPick
                                                        || hit->action == Action::PartyDropScroll
                                                        || hit->action == Action::PartyDropBack);
                        if (partyUi.dropKind != DropKind::None && !onDropdown) {
                            // A click outside an open dropdown only closes it
                            closeDropdown();
                        } else if (hit) {
                            if (partyUi.editingName && hit->action != Action::PartyNameField) stopNameEdit();
                            performAction(hit->action, hit->param);
                        } else {
                            stopNameEdit();
                        }
                    }
                    break;
                }

                if (screen == Screen::Settings) {
                    if (e.button.button == SDL_BUTTON_LEFT) settingsClick(e.button.x, e.button.y, winW, winH);
                    break;
                }

                if (screen != Screen::Playing) {
                    // Menus: a left click runs the enabled button under the cursor
                    if (e.button.button == SDL_BUTTON_LEFT) {
                        const std::vector<Button> menuButtons = screen == Screen::MapSelect
                            ? layoutMapPanel(winW, winH, maps, mapOffset)
                            : layoutButtons(screen, winW, winH, saves);
                        for (const Button& b : menuButtons) {
                            if (b.enabled && b.rect.contains(e.button.x, e.button.y)) {
                                performAction(b.action, b.param);
                                break;
                            }
                        }
                    }
                    break;
                }

                if (e.button.button == SDL_BUTTON_LEFT && screen == Screen::Playing) {
                    // The unit panel and sidebar take the click, so it never reaches the ground
                    const float mx = e.button.x, my = e.button.y;
                    const std::vector<Entity> own = ownUnitList();
                    const UnitPanelLayout pl = layoutUnitPanel(winW, winH, (int)own.size(), unitPanelRow);

                    // The right-click menu: a click on one of its buttons does that action. A click
                    // anywhere else just closes it, and the click still does what it would have done.
                    if (itemMenu.open) {
                        const std::vector<std::string> labels = menuLabels();
                        if (labels.empty()) {
                            itemMenu.open = false;
                        } else {
                            const ItemMenuLayout ml = renderer.itemMenuLayout(itemMenu.x, itemMenu.y, (int)labels.size(), winW, winH);
                            const int hit = itemMenuButtonAt(ml, mx, my);
                            const bool onMenu = ml.box.contains(mx, my);
                            const Entity unit = itemMenu.unit;
                            const int slot = itemMenu.slot;
                            itemMenu.open = false;
                            if (hit >= 0 && labels[hit] == "Use") {
                                useSlot(unit, slot);
                                audio::playEffect(SFX_MENU);
                            } else if (hit >= 0 && labels[hit] == "Drop") {
                                // A drop from the inventory plays its own sound, in place of the menu click
                                audio::playEffect(dropStack(world, unit, slot) ? SFX_ITEM : SFX_MENU);
                            }
                            if (onMenu) break;
                        }
                    }

                    // The condition editor takes the clicks on it. A click anywhere else closes it, and that click
                    // still does whatever it would have done.
                    if (behaviorEditor.open) {
                        const ConditionEditorLayout ed =
                            renderer.conditionEditorLayout(behaviorEditor.x, behaviorEditor.y, winW, winH);
                        if (ed.box.contains(mx, my)) {
                            editorClick(ed, mx, my);
                            break;
                        }
                        behaviorEditor.open = false;
                    }

                    // The stats popout takes the clicks inside it, and its Close button shuts it. The
                    // Stats button on the profile card opens and closes it.
                    if (statsOpen) {
                        const StatsPanelLayout sp = renderer.statsPanelLayout(winW, winH);
                        if (sp.close.contains(mx, my)) statsOpen = false;
                        if (sp.box.contains(mx, my)) break;
                    }
                    if (sidebarUp() && sidebarFor(winW, winH, sidebarScroll).statsButton.contains(mx, my)) {
                        statsOpen = !statsOpen;
                        break;
                    }

                    if (overPanels(mx, my, winW, winH)) {
                        // The walk and run toggle on the bottom panel
                        if (pl.walkButton.contains(mx, my)) { runMode = false; break; }
                        if (pl.runButton.contains(mx, my)) { runMode = true; break; }

                        const SidebarLayout sl = sidebarFor(winW, winH, sidebarScroll);
                        const bool onSidebar = sidebarUp() && sl.panel.contains(mx, my);
                        if (onSidebar) {
                            // Sort, add and the behavior rows change your unit, when the sidebar shows one. They
                            // come before the drags, which start on the inventory and gear.
                            if (shownIsYours()) {
                                const Entity shown = sidebarUnitOf(world);
                                if (sl.sortButton.contains(mx, my)) {
                                    itemMenu.open = false;
                                    sortInventory(shown);
                                    break;
                                }
                                if (sl.addButton.contains(mx, my)) {
                                    addBehavior(shown);
                                    break;
                                }
                                const BehaviorHit bh = behaviorHitAt(sl, mx, my);
                                if (bh.row >= 0) {
                                    behaviorClick(shown, bh, mx, my);
                                    break;
                                }
                            }

                            // A filled inventory slot starts dragging its stack out of the inventory. A filled
                            // gear slot starts dragging the gear item out of the unit's gear. A red unit's
                            // sidebar has no unit of yours in it, so nothing there can be dragged.
                            const std::vector<Entity> picked = selectedUnits(world);
                            const Inventory* inv = picked.size() == 1 ? world.get<Inventory>(picked[0]) : nullptr;
                            const Equipment* worn = picked.size() == 1 ? world.get<Equipment>(picked[0]) : nullptr;
                            const int slot = sidebarSlotAt(sl, mx, my);
                            const int gear = gearSlotAt(sl, mx, my);
                            if (inv && slot >= 0 && inv->slots[slot].item != 0) {
                                itemDrag = true;
                                dragUnit = picked[0];
                                dragSlot = slot;
                                dragGear = -1;
                            } else if (worn && gear >= 0 && worn->items[gear] != 0) {
                                itemDrag = true;
                                dragUnit = picked[0];
                                dragSlot = -1;
                                dragGear = gear;
                            }
                        }
                        for (const UnitCell& c : pl.cells) {
                            if (!c.rect.contains(mx, my)) continue;
                            const Entity u = own[c.index];
                            // Shift toggles the unit in the selection. Alt adds it and never clears or removes anything.
                            const SDL_Keymod mods = SDL_GetModState();
                            const bool shift = (mods & SDL_KMOD_SHIFT) != 0;
                            const bool additive = (mods & (SDL_KMOD_SHIFT | SDL_KMOD_ALT)) != 0;
                            if (shift && world.has<Selected>(u)) {
                                world.remove<Selected>(u);
                            } else {
                                if (!additive) clearSelection(world);
                                if (!world.has<Selected>(u)) world.add(u, Selected{});
                            }
                            // Double-click moves the camera to the unit, wherever it is, and keeps it on the unit as it moves
                            if (e.button.clicks == 2) {
                                focusCameraOnUnit(world, cam, u);
                                followUnit = u;
                            }
                            break;
                        }
                        break;
                    }

                    // Holding Alt shows the items on the ground. A click on one sends the selected units to pick it up.
                    if ((SDL_GetModState() & SDL_KMOD_ALT) != 0) {
                        const Entity item = renderer.groundItemAtPixel(world, terrain, cam, mx, my, winW, winH);
                        if (item != NULL_ENTITY) {
                            const std::vector<Entity> picked = selectedUnits(world);
                            if (picked.empty()) showNotice("Select units to pick the item up");
                            else issuePickupOrder(world, terrain, picked, item);
                            break;
                        }
                    }
                }

                if (e.button.button == SDL_BUTTON_LEFT) {
                    leftDown = true;
                    pressX = e.button.x;
                    pressY = e.button.y;
                    sel.dragging = false;
                } else if (e.button.button == SDL_BUTTON_MIDDLE) {
                    // Pressing alone changes nothing. Dragging orbits around the current focus.
                    // Pressed over the panels, it orbits nothing.
                    if (!overPanels(e.button.x, e.button.y, winW, winH)) {
                        middleDown = true;
                        lastX = e.button.x;
                        lastY = e.button.y;
                    }
                } else if (e.button.button == SDL_BUTTON_RIGHT) {
                    if (overPanels(e.button.x, e.button.y, winW, winH)) {
                        // Right-click on an inventory item: gear goes straight to its slot, and anything
                        // else opens the menu with Use and Drop. Right-click on a worn gear item takes it off
                        // into the inventory. Elsewhere on the panels, nothing happens. A red unit's sidebar
                        // has no unit of yours in it, so neither of those applies there.
                        itemMenu.open = false;
                        const std::vector<Entity> picked = selectedUnits(world);
                        const SidebarLayout sl = sidebarFor(winW, winH, sidebarScroll);
                        const int slot = sidebarUp() ? sidebarSlotAt(sl, e.button.x, e.button.y) : -1;
                        const int gear = sidebarUp() ? gearSlotAt(sl, e.button.x, e.button.y) : -1;
                        const Inventory* inv = picked.size() == 1 ? world.get<Inventory>(picked[0]) : nullptr;
                        const Equipment* wornHere = picked.size() == 1 ? world.get<Equipment>(picked[0]) : nullptr;
                        if (wornHere && gear >= 0 && wornHere->items[gear] != 0) {
                            std::string why;
                            if (unequipToInventory(world, picked[0], gear, why)) audio::playEffect(SFX_ITEM);
                            else if (!why.empty()) showNotice(why);
                        } else if (inv && slot >= 0 && inv->slots[slot].item != 0) {
                            const int item = inv->slots[slot].item;
                            const ItemDef* def = findItem(item);
                            if (def && !def->equipSlot.empty()) {
                                const Equipment* worn = world.get<Equipment>(picked[0]);
                                const int target = worn ? gearSlotFor(*worn, item) : -1;
                                std::string why;
                                if (target < 0 || !equipFromInventory(world, picked[0], slot, target, why)) {
                                    showNotice(why.empty() ? def->name + " can't be worn" : why);
                                } else {
                                    audio::playEffect(SFX_ITEM);
                                }
                            } else {
                                itemMenu = ItemMenu{ true, picked[0], slot, e.button.x, e.button.y };
                            }
                        }
                    } else {
                        // A red unit under the pointer: each of your selected units fights it. Ground: they walk there.
                        const Entity foe = firstRedUnit(world,
                            renderer.unitsAtPixel(world, cam, e.button.x, e.button.y, winW, winH));
                        if (foe != NULL_ENTITY) {
                            const std::vector<Entity> units = selectedUnits(world);
                            if (!units.empty()) {
                                issueAttackOrder(world, units, foe);
                                if (const Transform* ft = world.get<Transform>(foe)) {
                                    actionX = ft->x;
                                    actionZ = ft->z;
                                    actionAlpha = 1.0f;
                                }
                            }
                        } else {
                            float gx = 0.0f, gz = 0.0f;
                            if (rightClickOrder(world, terrain, cam, e.button.x, e.button.y, winW, winH, gx, gz)) {
                                actionX = gx;
                                actionZ = gz;
                                actionAlpha = 1.0f;
                            }
                        }
                    }
                }
                break;

            case SDL_EVENT_MOUSE_BUTTON_UP:
                // A slider let go of: save its final level
                if (e.button.button == SDL_BUTTON_LEFT && settingsUi.dragRow >= 0) {
                    settingsUi.dragRow = -1;
                    saveSettingsFile();
                }
                if (e.button.button == SDL_BUTTON_LEFT && itemDrag) {
                    itemDrag = false;
                    if (world.has<Unit>(dragUnit)) {
                        const SidebarLayout sl = sidebarFor(winW, winH, sidebarScroll);
                        const float rx = e.button.x, ry = e.button.y;
                        const bool overSidebar = sl.panel.contains(rx, ry);
                        const int gear = gearSlotAt(sl, rx, ry);
                        const bool onGrid = sidebarSlotAt(sl, rx, ry) >= 0;
                        std::string why;
                        if (dragGear < 0) {
                            // An inventory stack: onto a gear slot it equips, out of the sidebar it drops
                            // under the unit. Released on the rest of the sidebar, nothing happens.
                            if (gear >= 0) {
                                if (equipFromInventory(world, dragUnit, dragSlot, gear, why)) audio::playEffect(SFX_ITEM);
                                else if (!why.empty()) showNotice(why);
                            } else if (!overSidebar) {
                                if (dropStack(world, dragUnit, dragSlot)) audio::playEffect(SFX_ITEM);
                            }
                        } else {
                            // A gear item: onto another gear slot it moves, onto the inventory it comes off,
                            // out of the sidebar it drops. Released on the rest of the sidebar, nothing happens.
                            if (gear >= 0 && gear != dragGear) {
                                if (!moveEquipped(world, dragUnit, dragGear, gear, why) && !why.empty()) showNotice(why);
                            } else if (onGrid) {
                                if (unequipToInventory(world, dragUnit, dragGear, why)) audio::playEffect(SFX_ITEM);
                                else if (!why.empty()) showNotice(why);
                            } else if (!overSidebar) {
                                dropEquipped(world, dragUnit, dragGear);
                            }
                        }
                    }
                    dragUnit = NULL_ENTITY;
                    dragSlot = -1;
                    dragGear = -1;
                }
                if (e.button.button == SDL_BUTTON_LEFT && leftDown) {
                    leftDown = false;
                    // Shift adds to the selection. Alt never clears it, so Alt-click on empty ground does nothing.
                    const SDL_Keymod mods = SDL_GetModState();
                    const bool additive = (mods & (SDL_KMOD_SHIFT | SDL_KMOD_ALT)) != 0;
                    if (sel.dragging) {
                        boxSelect(world, cam, sel.x0, sel.y0, sel.x1, sel.y1, winW, winH, additive);
                    } else {
                        clickSelect(world, terrain, cam, e.button.x, e.button.y, winW, winH, additive);
                    }
                    sel.dragging = false;
                }
                if (e.button.button == SDL_BUTTON_MIDDLE) middleDown = false;
                break;

            case SDL_EVENT_MOUSE_MOTION:
                // A slider follows the mouse while its button is held
                if (screen == Screen::Settings && settingsUi.dragRow >= 0) {
                    if (e.motion.state & SDL_BUTTON_LMASK) {
                        const std::vector<RowSpec> rows = settingsRows(settingsUi.tab, settings, displayInfo);
                        const SettingsLayout L = layoutSettings(winW, winH, rows, settingsUi);
                        if (settingsUi.dragRow < (int)L.rows.size()) {
                            const int level = levelAtX(L.rows[settingsUi.dragRow].track, e.motion.x);
                            settingsCommit(setRowLevel(rows[settingsUi.dragRow].id, level, settings), false);
                        }
                    } else {
                        // The button came up outside the window, so no button-up arrived: save here
                        settingsUi.dragRow = -1;
                        saveSettingsFile();
                    }
                }
                if (screen == Screen::Playing) {
                    if (screen == Screen::Playing && leftDown) {
                        float dx = e.motion.x - pressX, dy = e.motion.y - pressY;
                        if (!sel.dragging && dx * dx + dy * dy > 36.0f) sel.dragging = true;
                        if (sel.dragging) {
                            sel.x0 = pressX; sel.y0 = pressY;
                            sel.x1 = e.motion.x; sel.y1 = e.motion.y;
                        }
                    }
                    if (middleDown) {
                        cam.yaw += (e.motion.x - lastX) * 0.005f;
                        cam.pitch += (e.motion.y - lastY) * 0.005f;
                        cam.clampState();
                        lastX = e.motion.x;
                        lastY = e.motion.y;
                    }
                }
                break;
            }
        }

        // ---- Delete hold: the save is deleted once the button has been held for DELETE_HOLD_MS ----
        if (deleteHolding) {
            float hx = 0.0f, hy = 0.0f;
            const bool leftHeld = (SDL_GetMouseState(&hx, &hy) & SDL_BUTTON_LMASK) != 0;
            if (!leftHeld || !deleteHoldRect.contains(hx, hy)) {
                deleteHolding = false;          // let go early, or moved off the button: start over
            } else if (SDL_GetTicks() - deleteHoldStart >= DELETE_HOLD_MS) {
                performDelete();
            }
        }

        // ---- Timing ----
        Uint64 now = SDL_GetTicks();
        float dt = (now - lastTicks) / 1000.0f;
        lastTicks = now;
        if (dt > 0.25f) dt = 0.25f;
        if (noticeSec > 0.0f) noticeSec -= dt;
        // The stats popout and the item menu belong to one lone selected unit. They close when that changes.
        if (screen == Screen::Playing) {
            const Entity shown = sidebarUnitOf(world);
            const bool lone = shown != NULL_ENTITY;
            if (!lone) statsOpen = false;
            if (itemMenu.open) {
                const Inventory* inv = world.get<Inventory>(itemMenu.unit);
                const bool same = lone && shown == itemMenu.unit;
                const bool hasItem = inv && itemMenu.slot >= 0 && itemMenu.slot < INVENTORY_SLOTS
                                  && inv->slots[itemMenu.slot].item != 0;
                if (!same || !hasItem) itemMenu.open = false;
            }
            // The sidebar starts at the top for a unit it was not scrolled for. The editor closes when its rule is gone.
            if (shown != sidebarScrollUnit) {
                sidebarScrollUnit = shown;
                sidebarScroll = 0.0f;
            }
            if (behaviorEditor.open) {
                const Behaviors* b = world.get<Behaviors>(behaviorEditor.unit);
                if (!b || shown != behaviorEditor.unit || behaviorEditor.row >= b->rules.size()) {
                    behaviorEditor.open = false;
                }
            }
        }
        pivotAlpha = std::clamp(pivotAlpha + (middleDown ? 1.0f : -1.0f) * dt * 4.0f, 0.0f, 1.0f);
        actionAlpha = std::max(0.0f, actionAlpha - dt);   // fades out over about a second

        cam.mapSize = (float)terrain.size();

        // ---- Game update: only while playing ----
        if (screen == Screen::Playing) {
            panWithKeys(dt, settings.edgeScroll && !leftDown && !middleDown);
            if (followUnit != NULL_ENTITY && !followUnitCamera(world, cam, followUnit)) followUnit = NULL_ENTITY;
            cam.updateClearance(terrain);

            // Fighting first, so a unit that dies this frame is gone before the others move. One sound covers the
            // swings of the frame: a hit if any landed, otherwise a miss if any were missed or dodged.
            const size_t hitsBefore = combatLog.hits;
            const size_t missesBefore = combatLog.misses;
            combatSystem(world, rng, dt, combatLog, floatingNumbers);
            // Behaviors after the fight, so a unit hit this frame can drink in the same frame
            behaviorSystem(world, combatLog);
            if (combatLog.hits > hitsBefore) audio::playEffect(SFX_HIT);
            else if (combatLog.misses > missesBefore) audio::playEffect(SFX_MISS);
            // A log scrolled up keeps its place as lines arrive. One at its newest line follows them.
            const size_t logCount = combatLog.lines.size();
            if (logCount > logSeen && logOffset > 0) logOffset += (int)(logCount - logSeen);
            logSeen = logCount;
            squadSystem(world, dt, runMode);
            steeringSystem(world, terrain, dt, runMode);
            if (pickupSystem(world) > 0) audio::playEffect(SFX_ITEM);
            cleanupSquads(world);
            // Open worlds: read in the ground near the camera and the units, a few chunks each frame
            if (terrain.streamed()) terrain.stream(streamFocuses(world, cam), STREAM_RADIUS, STREAM_LOADS_PER_FRAME);
            // The in-game clock runs while it is not stopped. Hunger ticks once for each in-game hour it passes,
            // and health comes back by the in-game seconds that passed.
            int hoursPassed = 0;
            double gameSecs = 0.0;
            if (sky.clockRunning) {
                gameSecs = dt * GAME_SECONDS_PER_REAL_SECOND;
                hoursPassed = advanceClock(gs.clockSeconds, dt);
            }
            needsSystem(world, hoursPassed, gameSecs);

            // Autosave every N minutes of play
            autoTimer += dt;
            if (settings.autosaveMinutes > 0 && autoTimer >= settings.autosaveMinutes * 60.0f) {
                autoTimer = 0.0f;
                if (autosaveNow()) showNotice("Autosaved");
            }
        }

        // ---- HUD: shown in the window title, since there's no text rendering there ----
        std::string title = "Kobold Game   " + formatDate(gs.clockSeconds);
        if (title != lastTitle) {
            SDL_SetWindowTitle(window, title.c_str());
            lastTitle = title;
        }

        // ---- Hover label: names of the units under the mouse, up to the limit, then "..." ----
        std::vector<std::string> hud;
        float mouseX = 0.0f, mouseY = 0.0f;
        SDL_GetMouseState(&mouseX, &mouseY);
        if (screen == Screen::Playing) {
            std::vector<Entity> under = renderer.unitsAtPixel(world, cam, mouseX, mouseY, winW, winH);
            for (size_t i = 0; i < under.size() && i < HOVER_NAME_LIMIT; ++i) {
                hud.push_back(world.get<Name>(under[i])->text);
            }
            if (under.size() > HOVER_NAME_LIMIT) hud.push_back("...");
        }

        // ---- Render ----
        renderer.setDayMinutes(timeOfDayMinutes(gs.clockSeconds));
        renderer.setSky(sky);
        auto drawButtons = [&](const std::vector<Button>& buttons) {
            for (const Button& b : buttons) {
                renderer.drawButton(b.rect, b.label, b.enabled, b.rect.contains(mouseX, mouseY));
            }
        };
        // Notices sit at the top middle of the window
        auto drawNotice = [&]() {
            if (noticeSec > 0.0f) renderer.drawNotice(notice, winW, winH);
        };
        auto dim = [&](float alpha) {
            renderer.drawRect(Rect{ 0.0f, 0.0f, (float)winW, (float)winH }, RGBA{ 0.0f, 0.0f, 0.0f, alpha });
        };

        switch (screen) {
        case Screen::Title:
            renderer.beginUi(winW, winH);
            renderer.drawTitleBackground();
            // The build stamp sits in the bottom left corner, in the smaller list font
            renderer.drawListText(BUILD_STAMP, 16.0f, winH - 44.0f, RGBA{ 1.0f, 1.0f, 1.0f, 0.6f });
            drawButtons(layoutButtons(Screen::Title, winW, winH, saves));
            renderer.endUi();
            break;

        case Screen::Settings: {
            renderer.beginUi(winW, winH);
            renderer.drawTitleBackground();
            dim(0.45f);
            const std::vector<RowSpec> rows = settingsRows(settingsUi.tab, settings, displayInfo);
            drawSettingsMenu(renderer, layoutSettings(winW, winH, rows, settingsUi), rows, settingsUi, mouseX, mouseY);
            renderer.endUi();
            break;
        }

        case Screen::Playing:
            renderer.drawFrame(world, terrain, cam, sel, hud, winW, winH);
            renderer.drawPivotRing(cam, terrain, pivotAlpha, winW, winH);
            renderer.drawActionCrystal(cam, terrain, actionX, actionZ, actionAlpha, winW, winH);
            // In-game panels, drawn over the world: item names while Alt is held, the unit list along the
            // bottom, the inventory of a lone selected unit, and the stack being dragged under the mouse
            renderer.beginUi(winW, winH);
            if (settings.showUnitNames) renderer.drawUnitNames(world, cam, winW, winH);
            // Damage numbers rise over the units they hit
            renderer.drawFloatingNumbers(floatingNumbers, cam, winW, winH);
            if ((SDL_GetModState() & SDL_KMOD_ALT) != 0) {
                renderer.drawGroundLabels(world, terrain, cam, winW, winH, mouseX, mouseY);
            }
            {
                const std::vector<Entity> own = ownUnitList();
                const UnitPanelLayout L = layoutUnitPanel(winW, winH, (int)own.size(), unitPanelRow);
                unitPanelRow = L.scrollRow;
                renderer.drawUnitPanel(world, L, own, runMode);
                // The combat log, bottom left. Its offset is written back, so it stays in range.
                const CombatLogLayout cl = combatLogPanel(winW, winH);
                logOffset = cl.offset;
                renderer.drawCombatLog(cl, combatLog);
                // The sidebar shows the lone selected unit, yours or a red one. A red one's gear and inventory
                // are only shown: the mouse handling never starts a drag or a menu on them.
                const Entity shown = sidebarUnitOf(world);
                if (shown != NULL_ENTITY) {
                    // The item being dragged, so the gear slots can show where it fits
                    int draggedId = 0;
                    if (itemDrag && dragGear >= 0) {
                        const Equipment* worn = world.get<Equipment>(dragUnit);
                        if (worn) draggedId = worn->items[dragGear];
                    } else if (itemDrag) {
                        const Inventory* inv = world.get<Inventory>(dragUnit);
                        if (inv && dragSlot >= 0 && dragSlot < INVENTORY_SLOTS) draggedId = inv->slots[dragSlot].item;
                    }
                    const SidebarLayout sl = sidebarFor(winW, winH, sidebarScroll);
                    sidebarScroll = sl.scroll;   // kept within range as the list grows and shrinks
                    renderer.drawInventorySidebar(sl, world, shown, mouseX, mouseY,
                                                  itemDrag && dragGear < 0 ? dragSlot : -1,
                                                  itemDrag && dragGear >= 0 ? dragGear : -1,
                                                  draggedId, shownIsYours());
                    // The full text of the rule under the mouse, when nothing else is open over it
                    if (!itemDrag && !behaviorEditor.open) renderer.drawBehaviorTip(sl, world, shown, mouseX, mouseY);
                }
                if (statsOpen && shown != NULL_ENTITY && world.has<Stats>(shown)) {
                    const Name* nm = world.get<Name>(shown);
                    renderer.drawStatsPanel(renderer.statsPanelLayout(winW, winH),
                                            nm ? nm->text : std::string("Unnamed Unit"),
                                            *world.get<Stats>(shown), mouseX, mouseY);
                }
                if (itemMenu.open) {
                    const std::vector<std::string> labels = menuLabels();
                    if (!labels.empty()) {
                        renderer.drawItemMenu(renderer.itemMenuLayout(itemMenu.x, itemMenu.y, (int)labels.size(), winW, winH),
                                              labels, mouseX, mouseY);
                    }
                }
                if (behaviorEditor.open && shown == behaviorEditor.unit) {
                    const Behaviors* b = world.get<Behaviors>(shown);
                    if (b && behaviorEditor.row < b->rules.size()) {
                        renderer.drawConditionEditor(
                            renderer.conditionEditorLayout(behaviorEditor.x, behaviorEditor.y, winW, winH),
                            b->rules[behaviorEditor.row], mouseX, mouseY);
                    }
                }
                if (itemDrag) {
                    // The item under the mouse: a gear item, or a stack from the inventory
                    if (dragGear >= 0) {
                        const Equipment* worn = world.get<Equipment>(dragUnit);
                        if (worn && worn->items[dragGear] != 0) renderer.drawDraggedStack(mouseX, mouseY, worn->items[dragGear], 1);
                    } else {
                        const Inventory* inv = world.get<Inventory>(dragUnit);
                        if (inv && dragSlot >= 0 && dragSlot < INVENTORY_SLOTS && inv->slots[dragSlot].item != 0) {
                            renderer.drawDraggedStack(mouseX, mouseY, inv->slots[dragSlot].item, inv->slots[dragSlot].count);
                        }
                    }
                }
            }
            // The clock sits in the top right, or just left of the sidebar while the sidebar shows
            {
                const bool sidebar = sidebarUp();
                const float clockRight = sidebar ? sidebarFor(winW, winH, sidebarScroll).panel.x : (float)winW;
                renderer.drawClock(formatClock(gs.clockSeconds), formatDate(gs.clockSeconds), clockRight, winH);
            }
            renderer.endUi();
            break;

        case Screen::MapSelect: {
            // The same popout as the load list: a name and date for each map, sorted, scrolled in a window
            const float s = winH / 720.0f;
            PanelGeometry g = panelGeometry(winW, winH);
            const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f };
            const RGBA gray{ 0.6f, 0.6f, 0.65f, 1.0f };
            const RGBA hover{ 1.0f, 1.0f, 0.6f, 1.0f };
            const RGBA hint{ 0.75f, 0.85f, 1.0f, 1.0f };
            const RGBA builtIn{ 0.6f, 0.85f, 1.0f, 1.0f };
            const float cx = g.box.x + g.box.w * 0.5f;

            renderer.beginUi(winW, winH);
            renderer.drawTitleBackground();
            dim(0.45f);
            renderer.drawRect(g.box, RGBA{ 0.06f, 0.07f, 0.10f, 0.97f });
            renderer.drawUiTextCentered("New Game", cx, g.box.y + g.pad, white);
            renderer.drawUiTextCentered(maps.empty() ? "No maps found. Make one with worldgen.py."
                                                     : "Click a map to start a new game",
                                        cx, g.nameBox.y + g.nameBox.h * 0.22f, gray);

            // Column headers. The active sort column shows its direction.
            const float innerX = g.box.x + g.pad, innerW = g.box.w - 2.0f * g.pad;
            const float textInset = 12.0f * s;
            const float listH = renderer.listTextHeight("Ag");
            const float hy = g.headerY + (g.headerH - listH) * 0.5f;
            auto headerLabel = [&](const std::string& label, bool active) {
                if (!active) return label;
                return label + (mapSortAsc ? "  ^" : "  v");
            };
            renderer.drawListText(headerLabel("Name", mapSortByName), innerX + textInset, hy,
                                  mapSortByName ? white : gray);
            renderer.drawListText("Type", innerX + COL_TYPE * innerW, hy, gray);
            renderer.drawListText(headerLabel("Created", !mapSortByName), innerX + COL_TIME * innerW, hy,
                                  !mapSortByName ? white : gray);

            // "..." rows: how many maps are hidden above and below the visible rows
            const size_t total = maps.size();
            const size_t shownFirst = std::min(mapOffset, total);
            const size_t shownLast = std::min(shownFirst + visibleSaveRows(winH), total);
            if (shownFirst > 0) {
                const bool hot = g.aboveBand.contains(mouseX, mouseY);
                renderer.drawListText("...   " + std::to_string(shownFirst) + " more above",
                                      innerX + textInset, g.aboveBand.y + (g.aboveBand.h - listH) * 0.5f,
                                      hot ? hover : hint);
            }
            if (shownLast < total) {
                const bool hot = g.belowBand.contains(mouseX, mouseY);
                renderer.drawListText("...   " + std::to_string(total - shownLast) + " more below",
                                      innerX + textInset, g.belowBand.y + (g.belowBand.h - listH) * 0.5f,
                                      hot ? hover : hint);
            }

            for (const Button& b : layoutMapPanel(winW, winH, maps, mapOffset)) {
                const bool isHover = b.rect.contains(mouseX, mouseY);
                if (b.action == Action::PickMap) {
                    // A map row: background, then its name, type and date in the columns
                    const MapChoice& m = maps[b.param];
                    renderer.drawButton(b.rect, "", true, isHover);
                    const float ry = b.rect.y + (b.rect.h - listH) * 0.5f;
                    renderer.drawListText(m.name, b.rect.x + textInset, ry, isHover ? hover : white);
                    renderer.drawListText(mapTypeLabel(m), b.rect.x + COL_TYPE * b.rect.w, ry,
                                          m.source == "data" ? builtIn : white);
                    const std::string when = m.source == "data" ? std::string("built in")
                                           : m.created > 0 ? timeString(m.created) : std::string("unknown");
                    renderer.drawListText(when, b.rect.x + COL_TIME * b.rect.w, ry, isHover ? hover : white);
                } else if (b.action == Action::MapScrollUp || b.action == Action::MapScrollDown
                           || b.action == Action::MapSortName || b.action == Action::MapSortTime) {
                    // Drawn above as text: the "..." rows and the headers only need their click areas
                    continue;
                } else {
                    drawButtons({ b });
                }
            }
            renderer.endUi();
            break;
        }

        case Screen::PartySetup: {
            const std::vector<DropEntry> drop = dropdownEntries(party, partyUi);
            renderer.beginUi(winW, winH);
            renderer.drawTitleBackground();
            dim(0.45f);
            renderer.drawPartySetup(layoutPartySetup(winW, winH, party, partyUi, drop), party, partyUi,
                                    partyPanels(winW, winH), pendingMap.name, mouseX, mouseY);
            renderer.endUi();
            break;
        }

        case Screen::Pause:
            renderer.drawFrame(world, terrain, cam, sel, {}, winW, winH);
            renderer.beginUi(winW, winH);
            dim(0.55f);
            renderer.drawUiTextCentered("Paused", winW * 0.5f, winH * 0.22f, RGBA{ 1.0f, 1.0f, 1.0f, 1.0f });
            drawButtons(layoutButtons(Screen::Pause, winW, winH, saves));
            renderer.endUi();
            break;
        }

        // ---- Save/load popout, drawn over whichever screen is showing ----
        if (panelOpen) {
            const float s = winH / 720.0f;
            PanelGeometry g = panelGeometry(winW, winH);
            const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f };
            const RGBA gray{ 0.6f, 0.6f, 0.65f, 1.0f };
            const RGBA hover{ 1.0f, 1.0f, 0.6f, 1.0f };
            const RGBA hint{ 0.75f, 0.85f, 1.0f, 1.0f };
            const float cx = g.box.x + g.box.w * 0.5f;

            renderer.beginUi(winW, winH);
            dim(0.25f);
            renderer.drawRect(g.box, RGBA{ 0.06f, 0.07f, 0.10f, 0.97f });
            renderer.drawUiTextCentered(panelSaveMode ? "Save Game" : "Load Game", cx, g.box.y + g.pad, white);

            if (ask != Ask::None) {
                DialogGeometry d = questionGeometry(winW, winH);
                renderer.drawRect(d.box, RGBA{ 0.13f, 0.07f, 0.07f, 1.0f });
                if (ask == Ask::Overwrite) {
                    std::string who = pendingOverwrite ? pendingOverwrite->name : std::string();
                    renderer.drawUiTextCentered("Overwrite \"" + who + "\"?",
                                                d.box.x + d.box.w * 0.5f, d.box.y + 36.0f * s, white);
                } else if (ask == Ask::LoadUnsaved) {
                    std::string who = pendingLoad ? pendingLoad->name : std::string();
                    renderer.drawUiTextCentered("Load \"" + who + "\"?",
                                                d.box.x + d.box.w * 0.5f, d.box.y + 30.0f * s, white);
                    renderer.drawUiTextCentered("Unsaved progress in this game will be lost.",
                                                d.box.x + d.box.w * 0.5f, d.box.y + 36.0f * s + 48.0f * s, white);
                } else {
                    // Delete: the name is shown so it's clear which save goes
                    const std::string who = pendingDelete ? pendingDelete->name : std::string();
                    renderer.drawUiTextCentered("Delete \"" + who + "\"?",
                                                d.box.x + d.box.w * 0.5f, d.box.y + 30.0f * s, white);
                    renderer.drawUiTextCentered("Hold Delete for 3 seconds to confirm.",
                                                d.box.x + d.box.w * 0.5f, d.box.y + 36.0f * s + 48.0f * s, white);
                }
                for (const Button& b : layoutPanel(winW, winH, panelSaveMode, ask, saves, listOffset,
                                                   panelFrom == Screen::Title)) {
                    if (b.action == Action::DeleteHold) {
                        const float fill = deleteHolding
                            ? std::min(1.0f, (float)(SDL_GetTicks() - deleteHoldStart) / (float)DELETE_HOLD_MS)
                            : 0.0f;
                        renderer.drawHoldButton(b.rect, b.label, fill);
                    } else {
                        drawButtons({ b });
                    }
                }
            } else {
                if (panelSaveMode) {
                    // Name field with a blinking cursor, shown even while the name is empty
                    // so it's clear the field has focus as soon as the popout opens
                    renderer.drawRect(g.nameBox, RGBA{ 0.10f, 0.11f, 0.15f, 1.0f });
                    float textY = g.nameBox.y + g.nameBox.h * 0.22f;
                    bool cursorOn = (SDL_GetTicks() / 500) % 2 == 0;
                    renderer.drawUiText(saveName + (cursorOn ? "|" : ""), g.nameBox.x + 12.0f, textY, white);
                } else {
                    renderer.drawUiTextCentered("Click a save to load it",
                                                cx, g.nameBox.y + g.nameBox.h * 0.22f, gray);
                }

                // Column headers. The active sort column shows its direction.
                const float innerX = g.box.x + g.pad, innerW = g.box.w - 2.0f * g.pad;
                const float textInset = 12.0f * s;
                const float listH = renderer.listTextHeight("Ag");
                const float hy = g.headerY + (g.headerH - listH) * 0.5f;
                auto headerLabel = [&](const std::string& label, bool active) {
                    if (!active) return label;
                    return label + (sortAsc ? "  ^" : "  v");
                };
                renderer.drawListText(headerLabel("Name", sortKey == SortKey::Name),
                                      innerX + textInset, hy, sortKey == SortKey::Name ? white : gray);
                renderer.drawListText("Type", innerX + COL_TYPE * innerW, hy, gray);
                renderer.drawListText(headerLabel("Time", sortKey == SortKey::Time),
                                      innerX + COL_TIME * innerW, hy, sortKey == SortKey::Time ? white : gray);

                if (saves.empty()) {
                    renderer.drawUiTextCentered("No saves yet", cx, g.listTop + g.nameBox.h * 0.22f, gray);
                }

                // "..." rows: how many saves are hidden above and below the visible rows
                const size_t total = saves.size();
                const size_t shownFirst = std::min(listOffset, total);
                const size_t shownLast = std::min(shownFirst + visibleSaveRows(winH), total);
                if (shownFirst > 0) {
                    const bool hot = g.aboveBand.contains(mouseX, mouseY);
                    renderer.drawListText("...   " + std::to_string(shownFirst) + " more above",
                                          innerX + textInset, g.aboveBand.y + (g.aboveBand.h - listH) * 0.5f,
                                          hot ? hover : hint);
                }
                if (shownLast < total) {
                    const bool hot = g.belowBand.contains(mouseX, mouseY);
                    renderer.drawListText("...   " + std::to_string(total - shownLast) + " more below",
                                          innerX + textInset, g.belowBand.y + (g.belowBand.h - listH) * 0.5f,
                                          hot ? hover : hint);
                }

                for (const Button& b : layoutPanel(winW, winH, panelSaveMode, Ask::None, saves, listOffset,
                                                   panelFrom == Screen::Title)) {
                    const bool isHover = b.rect.contains(mouseX, mouseY);
                    if (b.action == Action::LoadSlot) {
                        // A save row: background, then left-aligned name and the type and time columns
                        const SaveSummary& sv = saves[b.param];
                        renderer.drawButton(b.rect, "", true, isHover);
                        const float ry = b.rect.y + (b.rect.h - listH) * 0.5f;
                        renderer.drawListText(sv.name, b.rect.x + textInset, ry, isHover ? hover : white);
                        renderer.drawListText(sv.autosave ? "AUTO" : "MANUAL",
                                              b.rect.x + COL_TYPE * b.rect.w, ry,
                                              sv.autosave ? RGBA{ 0.6f, 0.85f, 1.0f, 1.0f } : white);
                        renderer.drawListText(timeString(sv.timestamp),
                                              b.rect.x + COL_TIME * b.rect.w, ry, isHover ? hover : white);
                    } else if (b.action == Action::ScrollUp || b.action == Action::ScrollDown
                               || b.action == Action::SortName || b.action == Action::SortTime) {
                        // Drawn above as text: the "..." rows and the headers only need their click areas
                        continue;
                    } else if (b.action == Action::DeleteSave) {
                        renderer.drawListButton(b.rect, "Delete", isHover);
                    } else {
                        renderer.drawButton(b.rect, b.label, b.enabled, isHover);
                    }
                }
            }
            renderer.endUi();
        }

        renderer.beginUi(winW, winH);
        drawNotice();
        renderer.endUi();

        // The frame is complete but not yet shown, so it is the one saved
        if (screenshotPending) {
            screenshotPending = false;
            const std::string saved = saveScreenshot(window);
            showNotice(saved.empty() ? std::string("Could not save the screenshot") : "Saved " + saved);
        }

        SDL_GL_SwapWindow(window);
        // Holds the frame to the limit, counted from the frame's start
        limitFrameRate(frameStart, settings.fpsLimit);
    }

    // ---- Shutdown ----
    audio::shutdown();
    renderer.shutdown();
    TTF_CloseFont(statsRowFont);
    TTF_CloseFont(statsTitleFont);
    TTF_CloseFont(listFont);
    TTF_CloseFont(uiFont);
    TTF_CloseFont(hudFont);
    TTF_CloseFont(font);
    TTF_Quit();
    SDL_GL_DestroyContext(ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
