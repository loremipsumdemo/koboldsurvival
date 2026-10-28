#include <SDL3/SDL.h>
#include "language.h"
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
#include "fonts.h"
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
// The neutral unit being talked to, or NULL_ENTITY when no talk box is open
Entity dialogueUnit = NULL_ENTITY;
Entity dialoguePlayer = NULL_ENTITY;      // the player unit a talk is with, who speaks the player's lines
std::vector<SpokenLine> dialogueConvo;    // the lines of the talk so far
bool tradeOpen = false;                   // the trade popout is up, in place of the talk box
constexpr float TALK_COOLDOWN = 1.0f;     // seconds after a talk ends before another one can start
float talkCooldown = 0.0f;                // seconds of that cooldown left
bool paused = false;                      // paused with Space, until Space is pressed again
std::vector<TradeGhost> tradeNpc;         // items shown on the NPC's side of the trade
std::vector<TradeGhost> tradePlayer;      // items shown on your side of the trade
int tradeNpcDrag = -1;                    // the NPC's inventory slot being dragged into the trade, or -1

// A stack's item being chosen for the trade: how many, out of how many the inventory holds that are not in the trade yet
struct QuantityPrompt {
    bool open = false;
    bool npcSide = false;
    Entity owner = NULL_ENTITY;
    int slot = -1;
    int item = 0;
    std::string name;
    int chosen = 1;
    int available = 0;
};
QuantityPrompt quantity;

// Shows a stack on one side of a trade, in the next free cell. Only a picture: the inventory is not changed.
// How many of an item an inventory holds that are not already in one side of a trade
int tradeAvailable(const Inventory& inv, const std::vector<TradeGhost>& side, int item) {
    int total = 0;
    for (int i = 0; i < INVENTORY_SLOTS; ++i) {
        if (inv.slots[i].item == item) total += inv.slots[i].count;
    }
    for (const TradeGhost& g : side) {
        if (g.stack.item == item) total -= g.stack.count;
    }
    return std::max(0, total);
}

// Puts up to `want` of the clicked stack's item in a side of the trade: the clicked stack first, then the other stacks
// of that item in the inventory. Stacks already in the trade only give what is still free. Returns how many went in.
int tradeAddItems(std::vector<TradeGhost>& side, const Inventory& inv, int clickedSlot, int want) {
    if (clickedSlot < 0 || clickedSlot >= INVENTORY_SLOTS || inv.slots[clickedSlot].item == 0 || want <= 0) return 0;
    const int item = inv.slots[clickedSlot].item;
    std::vector<int> order{ clickedSlot };
    for (int i = 0; i < INVENTORY_SLOTS; ++i) {
        if (i != clickedSlot && inv.slots[i].item == item) order.push_back(i);
    }
    int added = 0;
    for (int slot : order) {
        if (added >= want) break;
        TradeGhost* ghost = nullptr;
        for (TradeGhost& g : side) {
            if (g.slot == slot) ghost = &g;
        }
        const int inTrade = ghost ? ghost->stack.count : 0;
        const int take = std::min(inv.slots[slot].count - inTrade, want - added);
        if (take <= 0) continue;
        if (ghost) {
            ghost->stack.count += take;
        } else {
            if ((int)side.size() >= TRADE_SLOTS) break;
            side.push_back(TradeGhost{ slot, ItemStack{ item, take } });
        }
        added += take;
    }
    return added;
}

// Takes some of a stack's items out of an inventory; the slot empties when nothing is left in it
void takeFromSlot(Inventory& inv, int slot, int count) {
    if (slot < 0 || slot >= INVENTORY_SLOTS) return;
    ItemStack& st = inv.slots[slot];
    st.count -= count;
    if (st.count <= 0) st = ItemStack{};
}

// Whether an inventory still holds everything a side of the trade says it gives
bool tradeStillThere(const Inventory* inv, const std::vector<TradeGhost>& side) {
    if (!inv) return side.empty();
    for (const TradeGhost& g : side) {
        if (g.slot < 0 || g.slot >= INVENTORY_SLOTS) return false;
        int needed = 0;
        for (const TradeGhost& other : side) {
            if (other.slot == g.slot) needed += other.stack.count;
        }
        const ItemStack& st = inv->slots[g.slot];
        if (st.item != g.stack.item || st.count < needed) return false;
    }
    return true;
}

bool tradeItemsPresent(World& w, Entity npc, Entity player) {
    return tradeStillThere(w.get<Inventory>(npc), tradeNpc) && tradeStillThere(w.get<Inventory>(player), tradePlayer);
}

// Takes back out the ghost shown in one of the cells. True if one was there.
bool takeTradeGhostAt(std::vector<TradeGhost>& side, const std::vector<Rect>& cells, float x, float y) {
    for (size_t i = 0; i < side.size() && i < cells.size(); ++i) {
        if (cells[i].contains(x, y)) {
            side.erase(side.begin() + (int)i);
            return true;
        }
    }
    return false;
}

// The distance in tiles between two units on the ground
float gapBetween(World& w, Entity a, Entity b) {
    const Transform* ta = w.get<Transform>(a);
    const Transform* tb = w.get<Transform>(b);
    if (!ta || !tb) return 1e9f;
    const float dx = ta->x - tb->x, dz = ta->z - tb->z;
    return std::sqrt(dx * dx + dz * dz);
}

// Whether a side's inventory has room for what it receives, once what it gives is taken out of it
bool tradeFits(Inventory inv, const std::vector<TradeGhost>& gives, const std::vector<TradeGhost>& receives) {
    for (const TradeGhost& g : gives) {
        if (g.slot >= 0 && g.slot < INVENTORY_SLOTS) takeFromSlot(inv, g.slot, g.stack.count);
    }
    for (const TradeGhost& g : receives) {
        if (addToInventory(inv, g.stack.item, g.stack.count) != 0) return false;
    }
    return true;
}

// Whether the trade on the table can be confirmed: both sides have items, each inventory has room for what it
// receives, and the offer covers the NPC's price with its fee. Also how close the offer is, for the value bar.
TradeStatus checkTrade(World& w, Entity npc, Entity player) {
    TradeStatus st;
    const Inventory* ni = npc != NULL_ENTITY ? w.get<Inventory>(npc) : nullptr;
    const Inventory* pi = player != NULL_ENTITY ? w.get<Inventory>(player) : nullptr;
    if (!ni || !pi) {
        st.text = lang("MISC_NO_UNIT_TO_TRADE");
        return st;
    }
    const int npcValue = tradeValueOf(tradeNpc);
    const int offer = tradeValueOf(tradePlayer);
    const int needed = requiredOffer(npcValue, levelOf(statsOf(w, player), STAT_CHARISMA));
    st.progress = needed > 0 ? (float)offer / (float)needed : 1.0f;
    if (tradeNpc.empty() || tradePlayer.empty()) st.text = lang("MISC_TRADE_ADD_BOTH");
    else if (!tradeFits(*pi, tradePlayer, tradeNpc)) st.text = lang("MISC_TRADE_NO_ROOM_YOURS");
    else if (!tradeFits(*ni, tradeNpc, tradePlayer)) st.text = lang("MISC_TRADE_NO_ROOM_THEIRS");
    else if (offer < needed) st.text = langf("MISC_NEED_MORE_VALUE", { std::to_string(needed - offer) });
    else {
        st.ok = true;
        st.text = langf("MISC_TRADE_READY_FEE", { std::to_string(needed - npcValue) });
    }
    return st;
}

// Makes the trade: each side's items move to the other, and the player's Charisma gains XP for the fee paid
void completeTrade(World& w, Entity npc, Entity player) {
    Inventory* ni = w.get<Inventory>(npc);
    Inventory* pi = w.get<Inventory>(player);
    const int npcValue = tradeValueOf(tradeNpc);
    const int fee = requiredOffer(npcValue, levelOf(statsOf(w, player), STAT_CHARISMA)) - npcValue;
    for (const TradeGhost& g : tradeNpc) takeFromSlot(*ni, g.slot, g.stack.count);
    for (const TradeGhost& g : tradePlayer) takeFromSlot(*pi, g.slot, g.stack.count);
    for (const TradeGhost& g : tradeNpc) addToInventory(*pi, g.stack.item, g.stack.count);
    for (const TradeGhost& g : tradePlayer) addToInventory(*ni, g.stack.item, g.stack.count);
    if (fee > 0) grantXp(w, player, STAT_CHARISMA, fee, lang("MISC_REASON_FEES"));
    tradeOpen = false;
    tradeNpc.clear();
    tradePlayer.clear();
    tradeNpcDrag = -1;
}

// The unit the Unit Panel shows: the talking unit during a trade, otherwise the lone selected unit
Entity sidebarUnitOf(World& w) {
    if (tradeOpen && dialoguePlayer != NULL_ENTITY && w.has<Unit>(dialoguePlayer)) return dialoguePlayer;
    const std::vector<Entity> mine = selectedUnits(w);
    if (mine.size() == 1) return mine[0];
    if (!mine.empty()) return NULL_ENTITY;
    const std::vector<Entity> any = w.view<Unit, Selected>();
    return any.size() == 1 ? any[0] : NULL_ENTITY;
}

// The units the Unit Panel acts on: the talking unit during a trade, otherwise the selected units
std::vector<Entity> panelUnits(World& w) {
    if (tradeOpen && dialoguePlayer != NULL_ENTITY && w.has<Unit>(dialoguePlayer)) return { dialoguePlayer };
    return selectedUnits(w);
}

// The first neutral unit in a list, or NULL_ENTITY if there is none
Entity firstNeutral(World& w, const std::vector<Entity>& units) {
    for (Entity u : units) {
        if (w.has<Neutral>(u)) return u;
    }
    return NULL_ENTITY;
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
        if (!withinDetail(cam, tr->x, tr->y, tr->z)) continue;
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
    if (selectedUnits(w).empty()) return false;
    auto hit = pickGround(cam, t, mx, my, W, H);
    if (!hit) return false;
    gx = hit->first;
    gz = hit->second;
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

// The main map: a world folder packed into data.pak at data/map/main. New Game always starts on it.
constexpr const char* MAIN_MAP_STEM = "main";

bool mainMapPacked() {
    const unsigned char* bytes = nullptr;
    size_t size = 0;
    return pakFind(std::string("data/map/") + MAIN_MAP_STEM + "/world.txt", bytes, size);
}

// Every map the New Game list can start, not yet sorted: the main map, the premade maps, then worlds and .map files
std::vector<MapChoice> listMaps() {
    std::vector<MapChoice> out;
    if (mainMapPacked()) {
        out.push_back(MapChoice{ "packed", MAIN_MAP_STEM, MAIN_MAP_STEM, 0,
                                 std::string(PAK_WORLD_PREFIX) + "data/map/" + MAIN_MAP_STEM });
    }
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

// Tiles across the minimap
constexpr float MINIMAP_TILES = 160.0f;

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
    dialogueUnit = NULL_ENTITY;   // entity ids restart with the new world, so a talk box can't keep pointing at one
    dialogueConvo.clear();
    discardQueuedLog();
    tradeOpen = false;
    tradeNpc.clear();
    tradePlayer.clear();
    tradeNpcDrag = -1;
    paused = false;
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
        if (u.hostile || u.neutral) unit.speed = HOSTILE_WALK_SPEED;
        world.add(e, unit);
        world.add(e, Combat{});
        if (u.hostile) {
            world.add(e, Hostile{});
        } else if (u.neutral) {
            world.add(e, Neutral{});
        } else {
            world.add(e, Commandable{});
        }
        if (u.hostile || u.neutral) world.add(e, Dialogue{});
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
        world.add(e, Body{ u.hasBody ? u.body : bodyForSavedName(u.name) });
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
        su.neutral = w.has<Neutral>(u);
        su.sprite = us ? us->id : 1;
        su.name = n ? n->text : lang("UILABEL_UNNAMED_UNIT");
        if (const Body* bd = w.get<Body>(u)) {
            su.hasBody = true;
            su.body = bd->type;
        }
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
    if (m.source == "data" || m.source == "packed") return lang("UILABEL_BUILT_IN");
    return m.source == "world" ? lang("UILABEL_WORLD") : lang("UILABEL_MAP_FILE");
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
            out.push_back(Button{ lang("UILABEL_DELETE"), d.yes, Action::DeleteHold });
            out.push_back(Button{ lang("UILABEL_CANCEL"), d.no, Action::DeleteNo });
            return out;
        }
        Action yes = Action::OverwriteYes, no = Action::OverwriteNo;
        if (ask == Ask::LoadUnsaved) { yes = Action::LoadYes; no = Action::LoadNo; }
        out.push_back(Button{ lang("UILABEL_YES"), d.yes, yes });
        out.push_back(Button{ lang("UILABEL_NO"), d.no, no });
        return out;
    }

    PanelGeometry g = panelGeometry(W, H);
    const float x = g.box.x + g.pad, w = g.box.w - 2.0f * g.pad;

    if (saveMode) out.push_back(Button{ lang("UILABEL_SAVE"), g.saveBtn, Action::ConfirmName });

    // Column headers. Type is not sortable, since autosaves always come first anyway.
    out.push_back(Button{ lang("UILABEL_NAME"), Rect{ x, g.headerY, COL_TYPE * w, g.headerH }, Action::SortName });
    out.push_back(Button{ lang("UILABEL_TIME"), Rect{ x + COL_TIME * w, g.headerY, (1.0f - COL_TIME) * w, g.headerH }, Action::SortTime });

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
        out.push_back(Button{ lang("UILABEL_DELETE"), Rect{ x + w - delW, y, delW, g.rowH }, Action::DeleteSave, true, (int)i });
        y += g.rowPitch;
    }
    if (last < saves.size()) out.push_back(Button{ "", g.belowBand, Action::ScrollDown });

    // Opened from the title screen, the button goes back there. Opened from the Esc menu, it closes to that menu.
    out.push_back(Button{ fromTitle ? lang("UILABEL_RETURN_TO_TITLE") : lang("UILABEL_CLOSE"), Rect{ x, g.closeY, w, g.btnH },
                          Action::CancelName });
    return out;
}

// Buttons for the New Game list, in the same popout as the load list: the column headers, one row
// per visible map, the "..." rows at the ends, and Close.
std::vector<Button> layoutMapPanel(int W, int H, const std::vector<MapChoice>& maps, size_t listOffset) {
    std::vector<Button> out;
    PanelGeometry g = panelGeometry(W, H);
    const float x = g.box.x + g.pad, w = g.box.w - 2.0f * g.pad;

    out.push_back(Button{ lang("UILABEL_NAME"), Rect{ x, g.headerY, COL_TYPE * w, g.headerH }, Action::MapSortName });
    out.push_back(Button{ lang("UILABEL_CREATED"), Rect{ x + COL_TIME * w, g.headerY, (1.0f - COL_TIME) * w, g.headerH },
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

    out.push_back(Button{ lang("UILABEL_RETURN_TO_TITLE"), Rect{ x, g.closeY, w, g.btnH }, Action::ReturnToTitle });
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
        add(lang("UILABEL_CONTINUE"), Action::Continue, x, y, hasSave);        y += bh + gap;
        add(lang("UILABEL_NEW_GAME"), Action::NewGame, x, y);                  y += bh + gap;
        add(lang("UILABEL_LOAD_GAME"), Action::LoadGame, x, y, hasSave);       y += bh + gap;
        add(lang("UILABEL_SETTINGS"), Action::Settings, x, y);                 y += bh + gap;
        add(langf("UILABEL_LANGUAGE_BUTTON", { languageDisplayName() }), Action::Language, x, y); y += bh + gap;
        add(lang("UILABEL_QUIT"), Action::Quit, x, y);
        break;
    }
    case Screen::Pause: {
        float x = (W - bw) * 0.5f, y = H * 0.36f;
        add(lang("UILABEL_RESUME"), Action::Resume, x, y);                     y += bh + gap;
        add(lang("UILABEL_SAVE_GAME"), Action::Save, x, y);                    y += bh + gap;
        add(lang("UILABEL_LOAD_GAME"), Action::Load, x, y, hasSave);           y += bh + gap;
        add(lang("UILABEL_SETTINGS"), Action::Settings, x, y);                 y += bh + gap;
        add(lang("UILABEL_RETURN_TO_TITLE"), Action::ReturnToTitle, x, y);
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

// The text fonts, opened at the Font size setting and for the selected language. Each main font has the other fonts
// as fallbacks (see fonts.h). owned holds every font opened, so they can all be closed.
struct UiFonts {
    TTF_Font* hud = nullptr;
    TTF_Font* ui = nullptr;
    TTF_Font* list = nullptr;
    TTF_Font* statsTitle = nullptr;
    TTF_Font* statsRow = nullptr;
    std::vector<TTF_Font*> owned;
    bool valid() const { return hud && ui && list && statsTitle && statsRow; }
};

void closeUiFonts(UiFonts& f);

UiFonts openUiFonts(int pct, const std::string& fontPath, const std::string& languageId) {
    // 100% is the default size: 20 points at 100%
    const float size = 20.0f * pct / 100.0f;
    UiFonts f;
    // The HUD, the UI, the lists and the title are bold. The Stats popout is regular.
    f.hud = openFontChain(size, true, fontPath, languageId, f.owned);
    f.ui = openFontChain(size, true, fontPath, languageId, f.owned);
    f.list = openFontChain(size, true, fontPath, languageId, f.owned);
    f.statsTitle = openFontChain(size, false, fontPath, languageId, f.owned);
    f.statsRow = openFontChain(size, false, fontPath, languageId, f.owned);
    return f;
}

void closeUiFonts(UiFonts& f) {
    closeFonts(f.owned);
    f.hud = f.ui = f.list = f.statsTitle = f.statsRow = nullptr;
}

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
    // Settings come first: the saved text size decides the fonts
    GameSettings settings = loadSettings(SETTINGS_PATH);
    UiFonts fonts = openUiFonts(settings.fontScale, settings.fontPath, settings.language);
    int appliedFontScale = settings.fontScale;
    std::string appliedFontPath = settings.fontPath;
    std::string appliedLanguage = settings.language;
    {
        // A saved language whose file can't be found goes back to the default one, and the setting is saved
        std::string langErr;
        if (!loadLanguage(settings.language, langErr)) {
            SDL_Log("Language: %s. Using the default language.", langErr.c_str());
            settings.language = LANGUAGE_DEFAULT_ID;
            loadLanguage(settings.language, langErr);
            saveSettings(settings, SETTINGS_PATH);
        }
    }
    // The large title font, for the selected language, with the same fallbacks. It keeps the language it started with.
    std::vector<TTF_Font*> titleFontOwned;
    TTF_Font* font = openFontChain(64.0f, true, settings.fontPath, settings.language, titleFontOwned);
    if (!font || !fonts.valid()) {
        SDL_Log("Could not open font from data.pak: %s", SDL_GetError());
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
    DisplayInfo displayInfo = queryDisplays(settings.monitor);
    applyDisplay(window, settings, displayInfo);
    SDL_GL_SetSwapInterval(settings.vsync ? 1 : 0);

    // ---- Loading phase: textures and models load before the title screen appears ----
    Renderer renderer;
    if (!renderer.init(font, fonts.hud, fonts.ui, [&](float p) { loadingFrame(window, renderer, p); })) return 1;
    renderer.setListFont(fonts.list);
    renderer.setStatsFonts(fonts.statsTitle, fonts.statsRow);

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
    bool combatCollapsed = false, squadCollapsed = false;   // the title bars' [-] buttons
    bool unitOpen = false;          // the Unit Panel: open shows its tabs, closed shows its bar alone
    int unitTab = 0;                // the tab shown: Inventory, Equipment, Profile, Stats or Behavior
    Entity lastLone = NULL_ENTITY;  // the lone unit the Unit Panel showed last frame
    // Where the Squad Panel, the Unit Panel and the Combat Log were dragged. 0 is the default size.
    PanelPlace squadPlace, unitPlace, logPlace;
    // The panel being dragged (0 squad, 1 Unit Panel, 2 combat log) and what the drag does: 1 its top edge (height),
    // 2 its right edge (width), 3 its title bar (move). The mouse, and the panel's rectangle, where the drag began.
    int grabWhich = -1, grabKind = 0;
    float grabMouseX = 0.0f, grabMouseY = 0.0f;
    float grabX = 0.0f, grabY = 0.0f, grabW = 0.0f, grabH = 0.0f;
    float mapCx = 0.0f, mapCz = 0.0f, mapSpan = 0.0f;   // the world map's view: its centre and width in tiles, 0 while closed
    bool mapOpen = false;          // the world map covers the ground: M or the View Map button
    bool rightHold = false;        // the right button is held over a point for the selected units
    float holdX = 0.0f, holdZ = 0.0f;
    // A stack being dragged out of the inventory sidebar. Released outside the sidebar, it drops on the ground.
    bool itemDrag = false;
    Entity dragUnit = NULL_ENTITY;        // the unit whose inventory it came from
    int dragSlot = -1;                    // the inventory slot it came from, or -1 when dragging gear
    int dragGear = -1;                    // the gear slot it came from, or -1 when dragging from the inventory
    // The stats popout for the lone selected unit, opened from the sidebar's Stats button
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
    std::vector<TradeLabel> tradeLabels;   // trade values shown over the traders for a second
    int infoTab = 0;        // the Information Panel's tab: 0 Message, 1 Game
    LogFilter logFilter;    // which game message kinds the Game tab shows
    bool logFilterOpen = false;
    int logOffset = 0;      // lines the combat log is scrolled up from its newest line; 0 follows new lines
    size_t logSeen = 0;     // lines the log held last frame, so a view scrolled up stays on the same lines
    auto clearCombatText = [&]() {
        combatLog = CombatLog{};
        floatingNumbers.clear();
        tradeLabels.clear();
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

    // Short message (lang("MISC_GAME_SAVED"), and so on)
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

    auto showNotice = [&](const std::string& text, float seconds = 2.5f) {
        notice = text;
        noticeSec = seconds;
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
            showNotice(langf("MISC_LOAD_FAILED", { choice.name, err }));
            return;
        }
        // The party from the menu replaces the three units that the seed made
        ensurePlayerUnits(world, terrain, rng, (int)party.size());
        applyParty(world, party);
        clearCombatText();
        closePanel();
        currentMap = choice;
        // A new game starts with the default camera, not the zoom and angle the last game was left at. The focus is
        // clamped to the map, so the map's size has to be known before it is set.
        cam = Camera{};
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

    // Opens the party menu for a picked map. A new party is rolled every time, so its names come from the language
    // in use now.
    auto openParty = [&](const MapChoice& choice) {
        party = randomParty(partyRng);
        pendingMap = choice;
        partyUi = PartyUi{};
        backScreen = Screen::Title;
        screen = Screen::PartySetup;
    };

    // Stops typing into the name box, if it is taking text
    auto stopNameEdit = [&]() {
        if (partyUi.editingName) SDL_StopTextInput(window);
        partyUi.editingName = false;
        partyUi.composition.clear();
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
            showNotice(lang("MISC_READ_FAILED"));
            refreshSaves();
            return;
        }
        if (d.mapStem.empty()) {
            showNotice(lang("MISC_SAVE_OLD"));
            refreshSaves();
            return;
        }
        std::optional<MapChoice> found = findMap(listMaps(), d.mapSource, d.mapStem);
        if (!found) {
            showNotice(langf("MISC_MAP_MISSING", { d.name }));
            refreshSaves();
            return;
        }
        const MapChoice choice = *found;
        std::string err;
        // The title music ends as a saved game starts, before its level loads
        audio::stopTracks();
        if (!restoreLevel(world, terrain, renderer, rng, d, choice, err)) {
            showNotice(langf("MISC_MAP_LOAD_FAILED", { choice.name, err }));
            refreshSaves();
            return;
        }
        currentMap = choice;
        levelSeed = d.seed;
        clearCombatText();
        gs = d.gs;
        // Older saves have no camera, so they start from the default one, not the last game's view
        cam = Camera{};
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
        showNotice(langf("MISC_LOADED", { d.name }));
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
        showNotice(ec ? langf("MISC_DELETE_FAILED", { name }) : langf("MISC_DELETED", { name }));
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
    // The Font size setting: every text font is reopened at the new size, and the text drawn with the old ones is dropped
    auto applyFontScale = [&]() {
        if (settings.fontScale == appliedFontScale && settings.fontPath == appliedFontPath
            && settings.language == appliedLanguage) return;
        UiFonts next = openUiFonts(settings.fontScale, settings.fontPath, settings.language);
        if (!next.valid()) {
            closeUiFonts(next);
            return;
        }
        renderer.setFonts(next.hud, next.ui, next.list, next.statsTitle, next.statsRow);
        closeUiFonts(fonts);
        fonts = next;
        appliedFontScale = settings.fontScale;
        appliedFontPath = settings.fontPath;
        appliedLanguage = settings.language;
    };
    // The Language setting: its text file is reloaded. A file that can't be found goes back to the default language,
    // and that choice is saved.
    auto applyLanguage = [&]() {
        std::string err;
        if (loadLanguage(settings.language, err)) return;
        showNotice(err + ". Using the default language.");
        settings.language = LANGUAGE_DEFAULT_ID;
        loadLanguage(settings.language, err);
        saveSettingsFile();
    };
    auto settingsCommit = [&](const SettingsChange& c, bool save) {
        if (!c.any) return;
        if (c.display) applyVideo();
        else if (c.vsync) SDL_GL_SetSwapInterval(settings.vsync ? 1 : 0);
        if (c.audio) applyAudio();
        if (c.language) applyLanguage();
        if (c.fonts || c.language) applyFontScale();
        if (save) saveSettingsFile();
    };
    auto leaveSettings = [&]() {
        settingsUi.openRow = -1;
        settingsUi.dragRow = -1;
        screen = backScreen;
    };
    // A left click on the Settings screen: a tab, a dropdown or one of its options, a toggle, a slider or a button
    // Puts the Squad Panel, the Combat Log and the Unit Panel back in their default places and sizes
    auto resetPanels = [&]() {
        squadPlace = PanelPlace{};
        logPlace = PanelPlace{};
        unitPlace = PanelPlace{};
    };
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
        case ClickKind::Action:
            if (rows[c.row].id == RowId::ResetPanels) resetPanels();
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
        case Action::NewGame: {
            // New Game always starts on the main map, so there is no map to pick
            const std::optional<MapChoice> mainMap = findMap(listMaps(), "packed", MAIN_MAP_STEM);
            if (!mainMap) {
                showNotice("The main map is missing from data.pak");
                return;
            }
            openParty(*mainMap);
            return;
        }
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
            partyUi.selected = std::clamp(param, 0, (int)party.size() - 1);
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
        case Action::PartyBodyBox:
            stopNameEdit();
            partyUi.dropKind = DropKind::Body;
            partyUi.dropScroll = 0;
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
            const int picked = std::clamp(partyUi.selected, 0, (int)party.size() - 1);
            std::vector<std::string> others;
            for (int i = 0; i < (int)party.size(); ++i) {
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
            case DropKind::Body:
                m.body = value;
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
        case Action::PartyAddUnit:
            // A random unit at the end of the party. The list is scrolled to the end, and the layout clamps it.
            if ((int)party.size() < PARTY_MAX_UNITS) {
                addRandomMember(party, partyRng);
                partyUi.selected = (int)party.size() - 1;
                partyUi.listScroll = (int)party.size();
                stopNameEdit();
                closeDropdown();
            }
            return;
        case Action::PartyRemoveUnit:
            // The picked unit leaves the party, but the starting units stay
            if ((int)party.size() > START_UNIT_COUNT) {
                const int picked = std::clamp(partyUi.selected, 0, (int)party.size() - 1);
                party.erase(party.begin() + picked);
                partyUi.selected = std::clamp(picked, 0, (int)party.size() - 1);
                stopNameEdit();
                closeDropdown();
            }
            return;
        case Action::PartyRandomAll:
            party = randomParty(partyRng, (int)party.size());
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
            showNotice(ok ? lang("MISC_GAME_SAVED") : lang("MISC_SAVE_FAILED"));
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
            showNotice(ok ? lang("MISC_GAME_SAVED") : lang("MISC_SAVE_FAILED"));
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
            rescanLanguages();
            return;
        case Action::Language:
            // The start menu's Language button: the same Settings screen, opened on its Language tab
            backScreen = screen;
            screen = Screen::Settings;
            settingsUi.tab = SettingsTab::Language;
            settingsUi.openRow = -1;
            settingsUi.dragRow = -1;
            rescanLanguages();
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
            return layoutInventorySidebar(W, H, b ? (int)b->rules.size() : 0, scroll, unitOpen, unitTab, unitPlace);
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
            return layoutUnitPanel(W, H, (int)ownUnitList().size(), unitPanelRow, squadCollapsed, squadPlace).panel.contains(mx, my);
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
            if (def->typeId == "consumable" && !def->onUse.empty()) labels.push_back(lang("UILABEL_USE"));
            labels.push_back(lang("UILABEL_DROP"));
            return labels;
        };
        // Uses the stack in a slot of a unit, and says what happened
        auto useSlot = [&](Entity unit, int slot) {
            const Inventory* inv = world.get<Inventory>(unit);
            if (!inv || slot < 0 || slot >= INVENTORY_SLOTS) return;
            const ItemDef* def = findItem(inv->slots[slot].item);
            const std::string itemName = def ? itemDisplayName(*def) : std::string(lang("MISC_THAT_ITEM"));
            switch (useStackInSlot(world, unit, slot)) {
            case UseResult::Used:
                showNotice(langf("MISC_USED", { itemName }));
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
            const float unitLeft = layoutUnitPanel(W, H, (int)ownUnitList().size(), unitPanelRow, squadCollapsed, squadPlace).panel.x;
            return renderer.combatLogLayout(W, H, infoTab, logFilterOpen, logOffset,
                                            (int)infoLines(combatLog, infoTab, logFilter).size(),
                                            unitLeft - 8.0f * s, combatCollapsed, logPlace);
        };
        // The talk: each line said goes into the talk's lines and the Message tab. The player's side speaks as the
        // first player unit that is selected, or else the first one there is.
        auto sayAs = [&](Entity who, const std::string& text, bool player) {
            SpokenLine ln;
            ln.text = text;
            ln.player = player;
            const bool known = who != NULL_ENTITY && world.has<Unit>(who);
            ln.speaker = known ? nameOf(world, who) : std::string(player ? lang("MISC_YOU") : lang("UILABEL_UNNAMED_UNIT"));
            const UnitSprite* us = known ? world.get<UnitSprite>(who) : nullptr;
            ln.sprite = us ? us->id : 1;
            dialogueConvo.push_back(ln);
            combatLog.say(ln.speaker + ": " + text);
        };
        // Ends the talk and the trade with it
        auto endTalk = [&]() {
            talkCooldown = TALK_COOLDOWN;
            tradeOpen = false;
            tradeNpc.clear();
            tradePlayer.clear();
            tradeNpcDrag = -1;
            quantity.open = false;
            dialogueUnit = NULL_ENTITY;
            dialogueConvo.clear();
        };
        // An item put in the trade from a stack: a single item goes straight in, a stack asks how many first
        auto requestTrade = [&](bool npcSide, Entity owner, int slot) {
            const Inventory* inv = world.get<Inventory>(owner);
            if (!inv || slot < 0 || slot >= INVENTORY_SLOTS || inv->slots[slot].item == 0) return;
            std::vector<TradeGhost>& side = npcSide ? tradeNpc : tradePlayer;
            const int item = inv->slots[slot].item;
            if (inv->slots[slot].count <= 1) {
                if (tradeAddItems(side, *inv, slot, 1) == 0) showNotice(lang("MISC_TRADE_ALREADY"));
                return;
            }
            const int avail = tradeAvailable(*inv, side, item);
            if (avail <= 0) {
                showNotice(lang("MISC_TRADE_NO_MORE"));
                return;
            }
            const ItemDef* def = findItem(item);
            quantity = QuantityPrompt{ true, npcSide, owner, slot, item, def ? itemDisplayName(*def) : std::string(lang("MISC_ITEM_FALLBACK")), 1, avail };
        };
        auto confirmQuantity = [&]() {
            const Inventory* inv = world.get<Inventory>(quantity.owner);
            std::vector<TradeGhost>& side = quantity.npcSide ? tradeNpc : tradePlayer;
            const int added = inv ? tradeAddItems(side, *inv, quantity.slot, quantity.chosen) : 0;
            if (added == 0) showNotice(lang("MISC_TRADE_FULL"));
            quantity.open = false;
        };
        auto startTalk = [&](Entity npc, Entity talker) {
            // Only the talker starts the dialogue. The other selected units still walk to the NPC's spot.
            std::vector<Entity> others;
            for (Entity u : selectedUnits(world)) {
                if (u != talker) others.push_back(u);
            }
            clearTalkOrders(world);
            if (const Transform* nt = world.get<Transform>(npc); nt && !others.empty()) {
                issueMoveOrder(world, terrain, others, Vec2{ nt->x, nt->z });
            }
            dialogueUnit = npc;
            dialogueConvo.clear();
            dialoguePlayer = talker;
            const Dialogue* dg = world.get<Dialogue>(npc);
            sayAs(npc, dg ? dg->text : std::string(lang("NPC_GREETING")), false);
        };
        // Picking an option: the player says its prompt, and the unit answers with its reply
        // Starts a talk, unless one ended too recently. Then no dialogue starts, but the units still walk to the NPC.
        auto tryStartTalk = [&](Entity npc, Entity talker, std::vector<Entity> units) {
            if (talkCooldown <= 0.0f) {
                startTalk(npc, talker);
                return;
            }
            for (Entity u : world.view<TalkOrder>()) units.push_back(u);
            std::sort(units.begin(), units.end());
            units.erase(std::unique(units.begin(), units.end()), units.end());
            const Transform* nt = world.get<Transform>(npc);
            if (nt && !units.empty()) issueMoveOrder(world, terrain, units, Vec2{ nt->x, nt->z });
        };
        auto chooseOption = [&](size_t i) {
            const Dialogue* dg = world.get<Dialogue>(dialogueUnit);
            if (!dg || i >= dg->options.size()) return;
            const std::string prompt = dg->options[i].prompt, reply = dg->options[i].reply;
            const bool trade = dg->options[i].opensTrade;
            sayAs(dialoguePlayer, prompt, true);
            sayAs(dialogueUnit, reply, false);
            if (trade) {
                tradeOpen = true;
                tradeNpc.clear();
                tradePlayer.clear();
                tradeNpcDrag = -1;
            }
        };
        // The talk box for the lines so far and the options of the unit it is with
        auto dialogueBox = [&](int W, int H) {
            const Dialogue* dg = world.get<Dialogue>(dialogueUnit);
            const int lines = (int)std::min<size_t>(2, dialogueConvo.size());
            return layoutDialogue(W, H, lines, dg ? (int)dg->options.size() : 0);
        };
        // A click on the Information Panel's tabs, its Filter button or its menu. True if the click was used.
        auto infoPanelClick = [&](int W, int H, float tx, float ty) {
            const CombatLogLayout cl = combatLogPanel(W, H);
            if (cl.collapsed) return false;
            if (cl.tabMessage.contains(tx, ty)) {
                if (infoTab != 0) { infoTab = 0; logOffset = 0; }
                logFilterOpen = false;
                return true;
            }
            if (cl.tabGame.contains(tx, ty)) {
                if (infoTab != 1) { infoTab = 1; logOffset = 0; }
                logFilterOpen = false;
                return true;
            }
            if (infoTab == 1 && cl.filterBtn.contains(tx, ty)) {
                logFilterOpen = !logFilterOpen;
                return true;
            }
            if (cl.filterOpen) {
                for (const CombatLogLayout::FilterRow& fr : cl.filterRows) {
                    if (fr.heading || !fr.r.contains(tx, ty)) continue;
                    logFilter.on[(int)fr.kind] = !logFilter.on[(int)fr.kind];
                    return true;
                }
                if (cl.filterMenu.contains(tx, ty)) return true;
                logFilterOpen = false;   // a click elsewhere closes the menu, and still does its own job
            }
            return false;
        };
        // The rectangle of each bottom panel: 0 squad, 1 Unit Panel, 2 combat log
        auto resizablePanel = [&](int which, int W, int H) -> Rect {
            if (which == 0) return layoutUnitPanel(W, H, (int)ownUnitList().size(), unitPanelRow, squadCollapsed, squadPlace).panel;
            if (which == 1) return sidebarFor(W, H, sidebarScroll).panel;
            return combatLogPanel(W, H).panel;
        };
        // What a press or the mouse over a point would grab: the panel, and the kind of grab. Kind 1 is the top edge
        // (height), 2 the right edge (width), 3 the title bar (move). A folded panel can only be moved. Panel -1 means none.
        auto grabAt = [&](int W, int H, float x, float y) -> std::pair<int, int> {
            const float barH = 22.0f * H / 720.0f;
            for (int which = 0; which < 3; ++which) {
                const bool folded = which == 0 ? squadCollapsed : which == 1 ? !unitOpen : combatCollapsed;
                const Rect r = resizablePanel(which, W, H);
                const bool inX = x >= r.x && x < r.x + r.w;
                if (inX && !folded && y >= r.y - 5.0f && y <= r.y + 3.0f) return { which, 1 };
                if (!folded && y >= r.y && y < r.y + r.h && x >= r.x + r.w - 4.0f && x <= r.x + r.w + 4.0f) return { which, 2 };
                if (inX && y >= r.y + 3.0f && y < r.y + barH) return { which, 3 };
            }
            return { -1, 0 };
        };
        // The up-and-down or left-and-right arrow cursor shows over a top or right edge
        auto setGrabCursor = [&](int kind) {
            static SDL_Cursor* upDown = nullptr;
            static SDL_Cursor* leftRight = nullptr;
            static int shown = 0;
            const int want = kind == 1 || kind == 2 ? kind : 0;
            if (want == shown) return;
            shown = want;
            if (want == 1 && !upDown) upDown = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NS_RESIZE);
            if (want == 2 && !leftRight) leftRight = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_EW_RESIZE);
            SDL_Cursor* c = want == 1 ? upDown : want == 2 ? leftRight : nullptr;
            SDL_SetCursor(c ? c : SDL_GetDefaultCursor());
        };
        // The place of a panel by its number: 0 squad, 1 Unit Panel, 2 combat log
        auto placeOf = [&](int which) -> PanelPlace& {
            return which == 0 ? squadPlace : which == 1 ? unitPlace : logPlace;
        };
        // Moves the grabbed panel to follow the mouse, from where the press began. It stays on the screen.
        auto applyGrab = [&](int W, int H, float x, float y) {
            const float s = H / 720.0f;
            PanelPlace& p = placeOf(grabWhich);
            if (grabKind == 1) {
                p.h = std::clamp(grabH + (grabMouseY - y), 0.0f, (float)H);
            } else if (grabKind == 2) {
                const float minW = grabWhich == 0 ? squadMinWidth(H) : grabWhich == 1 ? 200.0f * s : 120.0f * s;
                p.w = std::clamp(grabW + (x - grabMouseX), minW, std::max(minW, W - grabX));
            } else if (grabKind == 3) {
                const float left = std::clamp(grabX + (x - grabMouseX), 0.0f, std::max(0.0f, W - grabW));
                const float top = std::clamp(grabY + (y - grabMouseY), 0.0f, std::max(0.0f, H - grabH));
                const float defaultLeft = grabWhich == 0 ? squadDefaultX(W, H) : grabWhich == 1 ? sidebarDefaultX(W, H) : 8.0f * s;
                p.dx = left - defaultLeft;
                p.dy = top + grabH - (H - 8.0f * s);
            }
        };
        // Lets go of the grab. A panel whose top edge was dragged under 64 pixels folds and goes back to its default size.
        auto endGrab = [&](int W, int H) {
            const int which = grabWhich, kind = grabKind;
            grabWhich = -1;
            grabKind = 0;
            if (kind != 1 || which < 0 || resizablePanel(which, W, H).h >= 64.0f) return;
            placeOf(which).w = placeOf(which).h = 0.0f;
            if (which == 0) squadCollapsed = true;
            else if (which == 1) { unitOpen = false; unitTab = 0; }
            else combatCollapsed = true;
        };
        // Whether the mouse is over the combat log
        auto combatLogHovered = [&](int W, int H) {
            float mx = 0.0f, my = 0.0f;
            SDL_GetMouseState(&mx, &my);
            return combatLogPanel(W, H).panel.contains(mx, my);
        };
        // The minimap sits in the top right corner, left of the sidebar while the sidebar shows
        auto minimapFor = [&](int W, int H) {
            return layoutMinimap(H, (float)W);
        };
        // Whether a panel is over a point, so that a click there goes to the panel rather than the minimap under it
        auto panelsCover = [&](int W, int H, float x, float y) {
            return layoutUnitPanel(W, H, (int)ownUnitList().size(), unitPanelRow, squadCollapsed, squadPlace).panel.contains(x, y)
                || sidebarFor(W, H, sidebarScroll).panel.contains(x, y) || combatLogPanel(W, H).panel.contains(x, y);
        };
        // The world map's area: the window above the unit panel
        auto worldMapArea = [&](int W, int H) {
            return expandedMapRect(H, (float)W);
        };
        // The ground point under the mouse on the minimap, or on the world map while it is open
        auto mapGroundAt = [&](int W, int H, float x, float y, float& gx, float& gz) {
            const float size = (float)terrain.size();
            if (mapOpen) {
                return minimapGround(WorldMapBox(worldMapArea(W, H), H / 720.0f), mapCx, mapCz, mapSpan, size, x, y, gx, gz);
            }
            // The sidebar covers the minimap, so a click on the sidebar is never a click on the minimap
            if (sidebarFor(W, H, sidebarScroll).panel.contains(x, y)) return false;
            return minimapGround(minimapFor(W, H).map, cam.focusX, cam.focusZ, MINIMAP_TILES, size, x, y, gx, gz);
        };
        // A move order to a ground point for the selected units, as a right click on the ground gives
        // The right button pressed on a point: the selected units walk straight at it while it is held, with the
        // crystal showing it. The route is planned when the button is let go.
        auto beginRightHold = [&](float x, float z) {
            const std::vector<Entity> units = selectedUnits(world);
            if (units.empty()) return;
            rightHold = true;
            holdX = x;
            holdZ = z;
            actionX = x;
            actionZ = z;
            actionAlpha = 1.0f;
            steerUnits(world, units, Vec2{ x, z });
        };
        auto moveSelectedTo = [&](float x, float z) {
            const std::vector<Entity> units = selectedUnits(world);
            if (units.empty()) return;
            issueMoveOrder(world, terrain, units, Vec2{ x, z });
            actionX = x;
            actionZ = z;
            actionAlpha = 1.0f;
        };
        auto overPanels = [&](float x, float y, int W, int H) {
            const std::vector<std::string> labels = menuLabels();
            return overGamePanels(layoutUnitPanel(W, H, (int)ownUnitList().size(), unitPanelRow, squadCollapsed, squadPlace),
                                  sidebarFor(W, H, sidebarScroll), true, x, y)
                || combatLogPanel(W, H).panel.contains(x, y)
                || (!mapOpen && (minimapFor(W, H).map.contains(x, y) || minimapFor(W, H).compass.contains(x, y)))
                || (behaviorEditor.open
                    && renderer.conditionEditorLayout(behaviorEditor.x, behaviorEditor.y, W, H).box.contains(x, y))
                || (!labels.empty() && renderer.itemMenuLayout(itemMenu.x, itemMenu.y, (int)labels.size(), W, H).box.contains(x, y));
        };
        // The point a held right button is over: the minimap or full map where the mouse is on it, or else the ground
        // under the mouse. Over the panels, or over the full map away from its ground, there is none.
        auto holdTarget = [&](int W, int H, float x, float y, float& gx, float& gz) {
            if (mapGroundAt(W, H, x, y, gx, gz)) return true;
            if (mapOpen || overPanels(x, y, W, H)) return false;
            auto hit = pickGround(cam, terrain, x, y, W, H);
            if (!hit) return false;
            gx = hit->first;
            gz = hit->second;
            return true;
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
                    // Typed text goes into the name being edited, up to the limit in characters. Any text an input
                    // method was composing is now committed, so it leaves the name box.
                    partyUi.composition.clear();
                    if (partyUi.editingName) appendUtf8Limited(pickedMember(party, partyUi).name, e.text.text, PARTY_NAME_CHARS);
                    break;
                }
                if (panelOpen && panelSaveMode && ask == Ask::None && saveName.size() < NAME_LIMIT) {
                    saveName += e.text.text;
                }
                break;

            case SDL_EVENT_TEXT_EDITING:
                // An input method is composing text (for example Chinese or Japanese): it shows in the name box
                if (screen == Screen::PartySetup && partyUi.editingName) partyUi.composition = e.edit.text;
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
                if (screen == Screen::Playing && !e.key.repeat && e.key.key == SDLK_M) {
                    mapOpen = !mapOpen;   // M opens and closes the world map
                    break;
                }
                // Space pauses and resumes the game, but not while a talk or a trade is open
                if (screen == Screen::Playing && e.key.key == SDLK_SPACE && !e.key.repeat && dialogueUnit == NULL_ENTITY) {
                    paused = !paused;
                }
                if (e.key.key == SDLK_ESCAPE) {
                    // A talk only ends with its End Dialogue button, so Escape does nothing while one is open
                    if (screen == Screen::Playing && dialogueUnit != NULL_ENTITY) break;
                    if (screen == Screen::Playing && mapOpen) {
                        mapOpen = false;  // Escape closes the world map first
                    } else if (screen == Screen::Playing && (itemMenu.open || behaviorEditor.open)) {
                        // Escape closes the open menu, editor or popout first, and only then pauses the game
                        itemMenu.open = false;
                        behaviorEditor.open = false;
                    } else if (screen == Screen::Playing) {
                        leftDown = middleDown = false;
                        sel.dragging = false;
                        itemDrag = false;
                        itemMenu.open = false;
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

            case SDL_EVENT_MOUSE_WHEEL: {
                // The world map popout takes the wheel over everything under it
                const bool wheelOverMap = mapOpen && screen == Screen::Playing
                                          && worldMapArea(winW, winH).contains(e.wheel.mouse_x, e.wheel.mouse_y);
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
                    } else if (partyPanels(winW, winH).left.contains(e.wheel.mouse_x, e.wheel.mouse_y)) {
                        // The unit list scrolls, a unit at a time
                        const int maxScroll = partyListMaxScroll((int)party.size(), winH);
                        partyUi.listScroll = std::clamp(std::min(partyUi.listScroll, maxScroll) - (int)e.wheel.y, 0, maxScroll);
                    }
                } else if (screen == Screen::Settings) {
                    // Scrolls an open dropdown. Nothing else on the menu scrolls.
                    if (settingsUi.openRow >= 0) {
                        const std::vector<RowSpec> rows = settingsRows(settingsUi.tab, settings, displayInfo);
                        settingsScrollDropdown(settingsUi, layoutSettings(winW, winH, rows, settingsUi), (int)e.wheel.y);
                    }
                } else if (screen == Screen::Playing && !wheelOverMap && combatLogHovered(winW, winH)) {
                    // Scrolls the combat log, three lines a notch. Wheel up goes back in time.
                    const int step = e.wheel.y > 0.0f ? 3 : e.wheel.y < 0.0f ? -3 : 0;
                    logOffset = std::clamp(logOffset + step, 0, combatLogPanel(winW, winH).maxOffset());
                } else if (wheelOverMap) {
                    // Zooms the world map about the point under the mouse: wheel up zooms in, down zooms out
                    const Rect box = WorldMapBox(worldMapArea(winW, winH), winH / 720.0f);
                    const float fx = std::clamp((e.wheel.mouse_x - box.x) / box.w, 0.0f, 1.0f);
                    const float fz = std::clamp((e.wheel.mouse_y - box.y) / box.h, 0.0f, 1.0f);
                    const float oldSpan = mapSpan;
                    const float gx = mapCx + oldSpan * 0.5f - fx * oldSpan;   // the ground under the mouse
                    const float gz = mapCz + oldSpan * 0.5f - fz * oldSpan;
                    const float size = (float)terrain.size();
                    mapSpan = std::clamp(e.wheel.y > 0.0f ? oldSpan * 0.8f : oldSpan * 1.25f, std::min(64.0f, size), size);
                    mapCx = gx - mapSpan * 0.5f + fx * mapSpan;
                    mapCz = gz - mapSpan * 0.5f + fz * mapSpan;
                } else if (screen == Screen::Playing && unitPanelHovered(winW, winH)) {
                    // Scrolls the unit list by whole rows
                    unitPanelRow = layoutUnitPanel(winW, winH, (int)ownUnitList().size(),
                                                   unitPanelRow - (int)e.wheel.y, squadCollapsed, squadPlace).scrollRow;
                } else if (screen == Screen::Playing
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
            }

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

                // With the world map open, only the panels take clicks; the ground and its units do not
                if (screen == Screen::Playing && mapOpen && !overPanels(e.button.x, e.button.y, winW, winH)) {
                    // With the world map open, a right click on the map is a move order; the rest is dropped
                    float mgx = 0.0f, mgz = 0.0f;
                    if (e.button.button == SDL_BUTTON_RIGHT && mapGroundAt(winW, winH, e.button.x, e.button.y, mgx, mgz)) {
                        beginRightHold(mgx, mgz);
                    }
                    break;
                }

                // The talk owns the mouse while it is open. During a trade the Unit Panel still works, so a click there
                // passes on to the panel; everything else is the talk's or the trade's.
                if (dialogueUnit != NULL_ENTITY && screen == Screen::Playing) {
                    // The quantity prompt is modal over the trade: only its buttons answer
                    if (quantity.open && tradeOpen) {
                        if (e.button.button == SDL_BUTTON_LEFT) {
                            const QuantityLayout ql = layoutQuantity(winW, winH);
                            const float qx = e.button.x, qy = e.button.y;
                            const int cap = std::max(1, quantity.available);
                            auto nudge = [&](int by) { quantity.chosen = std::clamp(quantity.chosen + by, 1, cap); };
                            if (ql.minus100.contains(qx, qy)) nudge(-100);
                            else if (ql.minus10.contains(qx, qy)) nudge(-10);
                            else if (ql.minus1.contains(qx, qy)) nudge(-1);
                            else if (ql.plus1.contains(qx, qy)) nudge(1);
                            else if (ql.plus10.contains(qx, qy)) nudge(10);
                            else if (ql.plus100.contains(qx, qy)) nudge(100);
                            else if (ql.single.contains(qx, qy)) quantity.chosen = 1;
                            else if (ql.max.contains(qx, qy)) quantity.chosen = cap;
                            else if (ql.confirm.contains(qx, qy)) confirmQuantity();
                            else if (ql.cancel.contains(qx, qy)) quantity.open = false;
                        }
                        break;
                    }
                    const float x = e.button.x, y = e.button.y;
                    const bool unitPanelClick = tradeOpen && sidebarFor(winW, winH, sidebarScroll).panel.contains(x, y);
                    if (!unitPanelClick) {
                        if (tradeOpen) {
                            const TradeLayout tl = layoutTrade(winW, winH);
                            const Inventory* npcInv = world.get<Inventory>(dialogueUnit);
                            const bool leftButton = e.button.button == SDL_BUTTON_LEFT;
                            if (leftButton && tl.cancelBtn.contains(x, y)) {
                                tradeOpen = false;
                                tradeNpc.clear();
                                tradePlayer.clear();
                                tradeNpcDrag = -1;
                            } else if (leftButton && tl.tradeBtn.contains(x, y)) {
                                if (!tradeItemsPresent(world, dialogueUnit, dialoguePlayer)) {
                                    // Something offered is no longer in its inventory: the trade fails and the talk ends
                                    tradeLabels.push_back(TradeLabel{ dialogueUnit, lang("MISC_TRADE_FAILED"), 0.0f });
                                    tradeLabels.push_back(TradeLabel{ dialoguePlayer, lang("MISC_TRADE_FAILED"), 0.0f });
                                    endTalk();
                                } else if (checkTrade(world, dialogueUnit, dialoguePlayer).ok) {
                                    const std::string label = "[Trade Value: " + std::to_string(tradeValueOf(tradePlayer)) + "]";
                                    tradeLabels.push_back(TradeLabel{ dialogueUnit, label, 0.0f });
                                    tradeLabels.push_back(TradeLabel{ dialoguePlayer, label, 0.0f });
                                    completeTrade(world, dialogueUnit, dialoguePlayer);
                                    endTalk();
                                    showNotice(lang("MISC_TRADE_COMPLETE"), TRADE_LABEL_LIFE);
                                }
                            } else if (takeTradeGhostAt(tradeNpc, tl.theirCells, x, y)
                                       || takeTradeGhostAt(tradePlayer, tl.yourCells, x, y)) {
                                // A ghost was taken back out of its cell
                            } else {
                                // An NPC item: left button drags it to the left box, right button puts it there
                                for (size_t i = 0; npcInv && i < tl.npcSlots.size(); ++i) {
                                    if (!tl.npcSlots[i].contains(x, y) || npcInv->slots[i].item == 0) continue;
                                    if (leftButton) {
                                        tradeNpcDrag = (int)i;
                                    } else if (e.button.button == SDL_BUTTON_RIGHT) {
                                        requestTrade(true, dialogueUnit, (int)i);
                                    }
                                    break;
                                }
                            }
                        } else if (e.button.button == SDL_BUTTON_LEFT) {
                            const DialogueLayout dl = dialogueBox(winW, winH);
                            if (dl.end.contains(x, y)) {
                                talkCooldown = TALK_COOLDOWN;
                                dialogueUnit = NULL_ENTITY;
                                dialogueConvo.clear();
                            } else {
                                for (size_t i = 0; i < dl.options.size(); ++i) {
                                    if (dl.options[i].contains(x, y)) {
                                        chooseOption(i);
                                        break;
                                    }
                                }
                            }
                        }
                        break;
                    }
                }
                // The expand button, on the minimap or on the full map it expands into
                if (screen == Screen::Playing && e.button.button == SDL_BUTTON_LEFT) {
                    const float x = e.button.x, y = e.button.y;
                    const bool hit = mapOpen ? mapToggleOf(worldMapArea(winW, winH), winH / 720.0f).contains(x, y)
                                             : (minimapFor(winW, winH).toggle.contains(x, y) && !panelsCover(winW, winH, x, y));
                    if (hit) {
                        mapOpen = !mapOpen;
                        break;
                    }
                }
                // The world map popout is over everything: its top and right edges resize it, and a click inside it
                // never reaches the panels or the ground under it
                if (screen == Screen::Playing && mapOpen) {
                    const float x = e.button.x, y = e.button.y;
                    const Rect m = worldMapArea(winW, winH);
                    if (m.contains(x, y)) {
                        float mgx = 0.0f, mgz = 0.0f;
                        if (e.button.button == SDL_BUTTON_RIGHT && mapGroundAt(winW, winH, x, y, mgx, mgz)) beginRightHold(mgx, mgz);
                        break;
                    }
                }
                // The [-] and [+] buttons on the title bars fold their panels up or down
                if (screen == Screen::Playing && e.button.button == SDL_BUTTON_LEFT) {
                    const float tx = e.button.x, ty = e.button.y;
                    const SidebarLayout sbl = sidebarFor(winW, winH, sidebarScroll);
                    if (sbl.plus.contains(tx, ty)) {
                        unitOpen = true;
                        unitPlace.w = unitPlace.h = 0.0f;
                        break;
                    }
                    if (sbl.minus.contains(tx, ty)) {
                        unitOpen = false;
                        unitTab = 0;
                        unitPlace.w = unitPlace.h = 0.0f;
                        break;
                    }
                    if (infoPanelClick(winW, winH, tx, ty)) break;
                    if (layoutUnitPanel(winW, winH, (int)ownUnitList().size(), unitPanelRow, squadCollapsed, squadPlace).toggle.contains(tx, ty)) {
                        squadCollapsed = !squadCollapsed;
                        squadPlace.w = squadPlace.h = 0.0f;
                        break;
                    }
                    if (combatLogPanel(winW, winH).toggle.contains(tx, ty)) {
                        combatCollapsed = !combatCollapsed;
                        logPlace.w = logPlace.h = 0.0f;
                        break;
                    }
                }
                // The top edge, right edge or title bar of a bottom panel grabs it, to resize or move it
                if (screen == Screen::Playing && e.button.button == SDL_BUTTON_LEFT) {
                    const auto g = grabAt(winW, winH, e.button.x, e.button.y);
                    if (g.first >= 0) {
                        const Rect r = resizablePanel(g.first, winW, winH);
                        grabWhich = g.first;
                        grabKind = g.second;
                        grabMouseX = e.button.x;
                        grabMouseY = e.button.y;
                        grabX = r.x;
                        grabY = r.y;
                        grabW = r.w;
                        grabH = r.h;
                        break;
                    }
                }
                if (e.button.button == SDL_BUTTON_LEFT && screen == Screen::Playing) {
                    // The unit panel and sidebar take the click, so it never reaches the ground
                    const float mx = e.button.x, my = e.button.y;
                    const std::vector<Entity> own = ownUnitList();
                    const UnitPanelLayout pl = layoutUnitPanel(winW, winH, (int)own.size(), unitPanelRow, squadCollapsed, squadPlace);

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
                            if (hit >= 0 && labels[hit] == lang("UILABEL_USE")) {
                                useSlot(unit, slot);
                                audio::playEffect(SFX_MENU);
                            } else if (hit >= 0 && labels[hit] == lang("UILABEL_DROP")) {
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

                    // The Unit Panel's tabs pick what it shows
                    {
                        const SidebarLayout st = sidebarFor(winW, winH, sidebarScroll);
                        bool tabHit = false;
                        for (int t = 0; t < UNIT_TAB_COUNT && st.open; ++t) {
                            if (st.tabs[t].contains(mx, my)) {
                                unitTab = t;
                                sidebarScroll = 0.0f;
                                tabHit = true;
                                break;
                            }
                        }
                        if (tabHit) break;
                    }

                    if (overPanels(mx, my, winW, winH)) {
                        // The pace toggle and the View Map button on the bottom panel
                        if (pl.paceButton.contains(mx, my)) { runMode = !runMode; break; }

                        const SidebarLayout sl = sidebarFor(winW, winH, sidebarScroll);
                        const bool onSidebar = sl.panel.contains(mx, my);
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
                            const std::vector<Entity> picked = panelUnits(world);
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
                            if (picked.empty()) showNotice(lang("MISC_PICKUP_SELECT"));
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
                    float mgx = 0.0f, mgz = 0.0f;
                    if (screen == Screen::Playing && mapGroundAt(winW, winH, e.button.x, e.button.y, mgx, mgz)) {
                        // The minimap: the selected units walk to the point clicked on it
                        beginRightHold(mgx, mgz);
                    } else if (overPanels(e.button.x, e.button.y, winW, winH)) {
                        // Right-click on an inventory item: gear goes straight to its slot, and anything
                        // else opens the menu with Use and Drop. Right-click on a worn gear item takes it off
                        // into the inventory. Elsewhere on the panels, nothing happens. A red unit's sidebar
                        // has no unit of yours in it, so neither of those applies there.
                        itemMenu.open = false;
                        const std::vector<Entity> picked = panelUnits(world);
                        const SidebarLayout sl = sidebarFor(winW, winH, sidebarScroll);
                        const int slot = sidebarUp() ? sidebarSlotAt(sl, e.button.x, e.button.y) : -1;
                        const int gear = sidebarUp() ? gearSlotAt(sl, e.button.x, e.button.y) : -1;
                        const Inventory* inv = picked.size() == 1 ? world.get<Inventory>(picked[0]) : nullptr;
                        const Equipment* wornHere = picked.size() == 1 ? world.get<Equipment>(picked[0]) : nullptr;
                        if (wornHere && gear >= 0 && wornHere->items[gear] != 0) {
                            std::string why;
                            if (unequipToInventory(world, picked[0], gear, why)) audio::playEffect(SFX_ITEM);
                            else if (!why.empty()) showNotice(why);
                        } else if (inv && slot >= 0 && inv->slots[slot].item != 0 && tradeOpen) {
                            // During a trade a right-click puts the item in the trade, whatever it is
                            requestTrade(false, picked[0], slot);
                        } else if (inv && slot >= 0 && inv->slots[slot].item != 0) {
                            const int item = inv->slots[slot].item;
                            const ItemDef* def = findItem(item);
                            if (def && !def->equipSlot.empty()) {
                                const Equipment* worn = world.get<Equipment>(picked[0]);
                                const int target = worn ? gearSlotFor(*worn, item) : -1;
                                std::string why;
                                if (target < 0 || !equipFromInventory(world, picked[0], slot, target, why)) {
                                    showNotice(why.empty() ? langf("MISC_CANT_BE_WORN", { itemDisplayName(*def) }) : why);
                                } else {
                                    audio::playEffect(SFX_ITEM);
                                }
                            } else {
                                itemMenu = ItemMenu{ true, picked[0], slot, e.button.x, e.button.y };
                            }
                        }
                    } else {
                        // A red unit under the pointer: each of your selected units fights it. Ground: they walk there.
                        // A neutral unit under the pointer: right-clicking it starts a talk. Nothing else happens.
                        const Entity npc = firstNeutral(world,
                            renderer.unitsAtPixel(world, cam, e.button.x, e.button.y, winW, winH));
                        if (npc != NULL_ENTITY) {
                            rightHold = false;
                            // A selected unit already in talk range talks now; otherwise the selected units walk up
                            // to the NPC, and the first to arrive starts the talk
                            const std::vector<Entity> units = selectedUnits(world);
                            Entity talker = NULL_ENTITY;
                            for (Entity u : units) {
                                if (gapBetween(world, u, npc) <= TALK_RANGE) {
                                    talker = u;
                                    break;
                                }
                            }
                            if (talker != NULL_ENTITY) tryStartTalk(npc, talker, units);
                            else if (units.empty()) showNotice(lang("MISC_TALK_SELECT"));
                            else issueTalkOrder(world, units, npc);
                            break;
                        }
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
                            if (rightClickOrder(world, terrain, cam, e.button.x, e.button.y, winW, winH, gx, gz)) beginRightHold(gx, gz);
                        }
                    }
                }
                break;

            case SDL_EVENT_MOUSE_BUTTON_UP:
                // Letting go of the right button plans the route to where it was held
                if (rightHold && e.button.button == SDL_BUTTON_RIGHT) {
                    rightHold = false;
                    moveSelectedTo(holdX, holdZ);
                    break;
                }
                if (grabWhich >= 0 && e.button.button == SDL_BUTTON_LEFT) {
                    endGrab(winW, winH);
                    setGrabCursor(grabAt(winW, winH, e.button.x, e.button.y).second);
                    break;
                }
                // A slider let go of: save its final level
                if (e.button.button == SDL_BUTTON_LEFT && settingsUi.dragRow >= 0) {
                    settingsUi.dragRow = -1;
                    saveSettingsFile();
                }
                // The trade: an NPC item let go over its side becomes a ghost there. A unit's item let go over your side
                // does too, if it is the unit being traded with. Either way the drag ends with nothing moved.
                if (e.button.button == SDL_BUTTON_LEFT && tradeOpen && dialogueUnit != NULL_ENTITY) {
                    const float x = e.button.x, y = e.button.y;
                    const TradeLayout tl = layoutTrade(winW, winH);
                    if (tradeNpcDrag >= 0) {
                        if (tl.theirBox.contains(x, y)) requestTrade(true, dialogueUnit, tradeNpcDrag);
                        tradeNpcDrag = -1;
                    }
                    if (itemDrag && !sidebarFor(winW, winH, sidebarScroll).panel.contains(x, y)) {
                        if (tl.yourBox.contains(x, y) && dragGear < 0) {
                            if (dragUnit == dialoguePlayer) requestTrade(false, dragUnit, dragSlot);
                            else showNotice(lang("MISC_TRADE_ONLY_TALKING"));
                        }
                        itemDrag = false;
                        dragUnit = NULL_ENTITY;
                        dragSlot = -1;
                        dragGear = -1;
                    }
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
                if (grabWhich >= 0) {
                    applyGrab(winW, winH, e.motion.x, e.motion.y);
                    break;
                }
                if (screen == Screen::Playing) {
                    const bool onMap = mapOpen && worldMapArea(winW, winH).contains(e.motion.x, e.motion.y);
                    setGrabCursor(onMap ? 0 : grabAt(winW, winH, e.motion.x, e.motion.y).second);
                }
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
            // The Unit Panel opens when one unit is selected on its own, and folds when there is none
            if (!lone) {
                unitOpen = false;
                unitTab = 0;
                unitPlace.w = unitPlace.h = 0.0f;
            } else if (shown != lastLone) {
                unitOpen = true;
                unitPlace.w = unitPlace.h = 0.0f;
                sidebarScroll = 0.0f;
            }
            lastLone = shown;
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
        if (rightHold && screen == Screen::Playing) {
            // The hold's point follows the mouse, and the units walk straight at it: no route is planned until release
            float hx = holdX, hz = holdZ, mhx = 0.0f, mhy = 0.0f;
            SDL_GetMouseState(&mhx, &mhy);
            if (holdTarget(winW, winH, mhx, mhy, hx, hz)) {
                holdX = hx;
                holdZ = hz;
            }
            steerUnits(world, selectedUnits(world), Vec2{ holdX, holdZ });
            actionX = holdX;
            actionZ = holdZ;
            actionAlpha = 1.0f;
        } else {
            rightHold = false;
            actionAlpha = std::max(0.0f, actionAlpha - dt);   // fades out over about a second
        }

        // Game messages written outside a fight (XP, pickups, drops) join the log every frame
        flushQueuedLog(combatLog);
        cam.mapSize = (float)terrain.size();

        // ---- Game update: only while playing ----
        // The game stands still while a talk is open, and only End Dialogue starts it again
        talkCooldown = std::max(0.0f, talkCooldown - dt);
        // A talk order ends the first time one of its units is in talk range of its NPC
        if (screen == Screen::Playing && dialogueUnit == NULL_ENTITY && !paused) {
            Entity talkNpc = NULL_ENTITY, talker = NULL_ENTITY;
            for (Entity u : world.view<TalkOrder>()) {
                const Entity npcOf = world.get<TalkOrder>(u)->npc;
                if (!world.has<Unit>(npcOf)) {
                    clearTalkOrders(world);
                    break;
                }
                if (gapBetween(world, u, npcOf) <= TALK_RANGE) {
                    talkNpc = npcOf;
                    talker = u;
                    break;
                }
            }
            if (talker != NULL_ENTITY) tryStartTalk(talkNpc, talker, {});
        }
        if (screen == Screen::Playing && dialogueUnit == NULL_ENTITY && !paused) {
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
                if (autosaveNow()) showNotice(lang("MISC_AUTOSAVED"));
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
        renderer.setMouse(mouseX, mouseY);
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

        case Screen::Playing: {
            // The world map's view starts out showing the whole level, and starts over each time it opens
            if (!mapOpen) {
                mapSpan = 0.0f;
            } else {
                const float size = (float)terrain.size();
                if (mapSpan <= 0.0f) {
                    mapSpan = size;
                    mapCx = size * 0.5f;
                    mapCz = size * 0.5f;
                }
                // From one chunk across to the whole level
                mapSpan = std::clamp(mapSpan, std::min(64.0f, size), size);
                const float half = mapSpan * 0.5f;
                mapCx = std::clamp(mapCx, half, size - half);
                mapCz = std::clamp(mapCz, half, size - half);
            }
            renderer.drawFrame(world, terrain, cam, sel, hud, winW, winH);
            renderer.drawPivotRing(cam, terrain, pivotAlpha, winW, winH);
            renderer.drawActionCrystal(cam, terrain, actionX, actionZ, actionAlpha, winW, winH);
            // In-game panels, drawn over the world: item names while Alt is held, the unit list along the
            // bottom, the inventory of a lone selected unit, and the stack being dragged under the mouse
            renderer.beginUi(winW, winH);
            // Every player unit is a green dot on the minimap and the world map
            std::vector<MapDot> mapDots;
            for (Entity e : world.view<Unit, Transform>()) {
                const Transform* t = world.get<Transform>(e);
                MapDotKind kind = MapDotKind::Player;
                if (world.has<Hostile>(e)) kind = MapDotKind::Hostile;
                else if (world.has<Neutral>(e)) kind = MapDotKind::Neutral;
                else if (!world.has<Commandable>(e)) continue;
                mapDots.push_back(MapDot{ Vec2{ t->x, t->z }, kind });
            }
            if (settings.showUnitNames) renderer.drawUnitNames(world, cam, winW, winH);
            // Damage numbers rise over the units they hit
            renderer.drawFloatingNumbers(floatingNumbers, cam, winW, winH);
            for (TradeLabel& l : tradeLabels) l.age += dt;
            tradeLabels.erase(std::remove_if(tradeLabels.begin(), tradeLabels.end(),
                                             [](const TradeLabel& l) { return l.age >= TRADE_LABEL_LIFE; }),
                              tradeLabels.end());
            renderer.drawTradeLabels(tradeLabels, world, cam, winW, winH);
            if ((SDL_GetModState() & SDL_KMOD_ALT) != 0) {
                renderer.drawGroundLabels(world, terrain, cam, winW, winH, mouseX, mouseY);
            }
            // The minimap and clock draw over the names and labels
            if (!mapOpen) {
                const MinimapLayout mm = minimapFor(winW, winH);
                renderer.drawMinimap(mm.map, mapDots, cam.focusX, cam.focusZ, MINIMAP_TILES);
                renderer.drawCompass(mm.compass, cam.bearingDegrees());
                renderer.drawMinimapToggle(mm.toggle, false);
            }
            {
                const MinimapLayout mm = minimapFor(winW, winH);
                renderer.drawClock(formatTime(gs.clockSeconds), formatYmd(gs.clockSeconds),
                                   mm.map.x + mm.map.w, mm.clockTop, winH);
            }
            {
                const std::vector<Entity> own = ownUnitList();
                const UnitPanelLayout L = layoutUnitPanel(winW, winH, (int)own.size(), unitPanelRow, squadCollapsed, squadPlace);
                unitPanelRow = L.scrollRow;
                renderer.drawUnitPanel(world, L, own, runMode);
                // The combat log, bottom left. Its offset is written back, so it stays in range.
                const CombatLogLayout cl = combatLogPanel(winW, winH);
                logOffset = cl.offset;
                renderer.drawCombatLog(cl, infoLines(combatLog, infoTab, logFilter), logFilter, mouseX, mouseY);
                // The sidebar shows the lone selected unit, yours or a red one. A red one's gear and inventory
                // are only shown: the mouse handling never starts a drag or a menu on them.
                const Entity shown = sidebarUnitOf(world);
                {
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
                    if (shown != NULL_ENTITY && !itemDrag && !behaviorEditor.open) renderer.drawBehaviorTip(sl, world, shown, mouseX, mouseY);
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
            // The world map popout is over everything, the panels included
            if (mapOpen) {
                const Rect area = worldMapArea(winW, winH);
                renderer.drawWorldMap(area, mapDots, mapCx, mapCz, mapSpan);
                renderer.drawMinimapToggle(mapToggleOf(area, winH / 720.0f), true);
            }
            // The talk box, over everything. It ends if the unit it is with is gone.
            if (dialogueUnit != NULL_ENTITY) {
                if (!world.has<Unit>(dialogueUnit)) {
                    dialogueUnit = NULL_ENTITY;
                    dialogueConvo.clear();
                    tradeOpen = false;
                } else {
                    float mhx = 0.0f, mhy = 0.0f;
                    SDL_GetMouseState(&mhx, &mhy);
                    if (dialogueUnit == NULL_ENTITY) quantity.open = false;
                    if (tradeOpen) {
                        const Inventory* npcInv = world.get<Inventory>(dialogueUnit);
                        renderer.drawTrade(layoutTrade(winW, winH), nameOf(world, dialogueUnit), npcInv, tradeNpc,
                                           tradePlayer, tradeNpcDrag, checkTrade(world, dialogueUnit, dialoguePlayer), mhx, mhy);
                        if (npcInv && tradeNpcDrag >= 0 && npcInv->slots[tradeNpcDrag].item != 0) {
                            renderer.drawDraggedStack(mhx, mhy, npcInv->slots[tradeNpcDrag].item, npcInv->slots[tradeNpcDrag].count);
                        }
                        if (quantity.open) {
                            renderer.drawQuantityPrompt(layoutQuantity(winW, winH), quantity.name, quantity.chosen,
                                                        quantity.available, mhx, mhy);
                        }
                    } else {
                        const Dialogue* dg = world.get<Dialogue>(dialogueUnit);
                        renderer.drawDialogue(dialogueBox(winW, winH), dialogueConvo,
                                              dg ? dg->options : std::vector<DialogueOption>{}, mhx, mhy);
                    }
                }
            }
            // The PAUSED bar, over everything in the game and its popouts, while the game stands still
            if (paused || dialogueUnit != NULL_ENTITY) renderer.drawPauseBar(paused && dialogueUnit == NULL_ENTITY);
            renderer.endUi();
            }
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
            renderer.drawUiTextCentered(lang("UILABEL_NEW_GAME"), cx, g.box.y + g.pad, white);
            renderer.drawUiTextCentered(maps.empty() ? lang("UILABEL_NO_MAPS")
                                                     : lang("UILABEL_CLICK_MAP"),
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
            renderer.drawListText(headerLabel(lang("UILABEL_NAME"), mapSortByName), innerX + textInset, hy,
                                  mapSortByName ? white : gray);
            renderer.drawListText(lang("UILABEL_TYPE"), innerX + COL_TYPE * innerW, hy, gray);
            renderer.drawListText(headerLabel(lang("UILABEL_CREATED"), !mapSortByName), innerX + COL_TIME * innerW, hy,
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
                                          (m.source == "data" || m.source == "packed") ? builtIn : white);
                    const std::string when = (m.source == "data" || m.source == "packed") ? std::string(lang("UILABEL_BUILT_IN_WHEN"))
                                           : m.created > 0 ? timeString(m.created) : std::string(lang("UILABEL_UNKNOWN"));
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
            renderer.drawPauseBar(false);
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
            renderer.drawUiTextCentered(panelSaveMode ? lang("UILABEL_SAVE_GAME") : lang("UILABEL_LOAD_GAME"), cx, g.box.y + g.pad, white);

            if (ask != Ask::None) {
                DialogGeometry d = questionGeometry(winW, winH);
                renderer.drawRect(d.box, RGBA{ 0.13f, 0.07f, 0.07f, 1.0f });
                if (ask == Ask::Overwrite) {
                    std::string who = pendingOverwrite ? pendingOverwrite->name : std::string();
                    renderer.drawUiTextCentered(langf("MISC_OVERWRITE_Q", { who }),
                                                d.box.x + d.box.w * 0.5f, d.box.y + 36.0f * s, white);
                } else if (ask == Ask::LoadUnsaved) {
                    std::string who = pendingLoad ? pendingLoad->name : std::string();
                    renderer.drawUiTextCentered(langf("MISC_LOAD_Q", { who }),
                                                d.box.x + d.box.w * 0.5f, d.box.y + 30.0f * s, white);
                    renderer.drawUiTextCentered(lang("UILABEL_UNSAVED_WARNING"),
                                                d.box.x + d.box.w * 0.5f, d.box.y + 36.0f * s + 48.0f * s, white);
                } else {
                    // Delete: the name is shown so it's clear which save goes
                    const std::string who = pendingDelete ? pendingDelete->name : std::string();
                    renderer.drawUiTextCentered(langf("MISC_DELETE_Q", { who }),
                                                d.box.x + d.box.w * 0.5f, d.box.y + 30.0f * s, white);
                    renderer.drawUiTextCentered(lang("UILABEL_HOLD_DELETE"),
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
                    renderer.drawUiTextCentered(lang("UILABEL_CLICK_SAVE"),
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
                renderer.drawListText(headerLabel(lang("UILABEL_NAME"), sortKey == SortKey::Name),
                                      innerX + textInset, hy, sortKey == SortKey::Name ? white : gray);
                renderer.drawListText(lang("UILABEL_TYPE"), innerX + COL_TYPE * innerW, hy, gray);
                renderer.drawListText(headerLabel(lang("UILABEL_TIME"), sortKey == SortKey::Time),
                                      innerX + COL_TIME * innerW, hy, sortKey == SortKey::Time ? white : gray);

                if (saves.empty()) {
                    renderer.drawUiTextCentered(lang("UILABEL_NO_SAVES"), cx, g.listTop + g.nameBox.h * 0.22f, gray);
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
                        renderer.drawListText(sv.autosave ? lang("UILABEL_AUTO") : lang("UILABEL_MANUAL"),
                                              b.rect.x + COL_TYPE * b.rect.w, ry,
                                              sv.autosave ? RGBA{ 0.6f, 0.85f, 1.0f, 1.0f } : white);
                        renderer.drawListText(timeString(sv.timestamp),
                                              b.rect.x + COL_TIME * b.rect.w, ry, isHover ? hover : white);
                    } else if (b.action == Action::ScrollUp || b.action == Action::ScrollDown
                               || b.action == Action::SortName || b.action == Action::SortTime) {
                        // Drawn above as text: the "..." rows and the headers only need their click areas
                        continue;
                    } else if (b.action == Action::DeleteSave) {
                        renderer.drawListButton(b.rect, lang("UILABEL_DELETE"), isHover);
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
            showNotice(saved.empty() ? std::string(lang("MISC_SCREENSHOT_FAILED")) : langf("MISC_SCREENSHOT_SAVED", { saved }));
        }

        SDL_GL_SwapWindow(window);
        // Holds the frame to the limit, counted from the frame's start
        limitFrameRate(frameStart, settings.fpsLimit);
    }

    // ---- Shutdown ----
    audio::shutdown();
    renderer.shutdown();
    closeUiFonts(fonts);
    closeFonts(titleFontOwned);
    TTF_Quit();
    SDL_GL_DestroyContext(ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
