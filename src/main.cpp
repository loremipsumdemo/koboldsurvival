#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <GL/glew.h>
#include <SDL3/SDL_opengl.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <ctime>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "camera.h"
#include "components.h"
#include "ecs.h"
#include "renderer.h"
#include "resources.h"
#include "savegame.h"
#include "settings.h"
#include "systems.h"
#include "terrain.h"
#include "ui.h"

namespace {

constexpr float OVERLAY_HOLD_MS = 1000.0f;   // level-start image stays fully visible
constexpr float OVERLAY_FADE_MS = 500.0f;    // then fades out over this long
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

    if (hx < 0.0f || hz < 0.0f || hx >= MAP_SIZE || hz >= MAP_SIZE) return std::nullopt;
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

// Right click: order the selected units to a tile, or into the door if the building was clicked
// Returns true, with the ground point in gx and gz, if an order was given.
bool rightClickOrder(World& w, Terrain& t, const Camera& cam, float mx, float my, int W, int H,
                     float& gx, float& gz) {
    std::vector<Entity> units = selectedUnits(w);
    if (units.empty()) return false;
    auto hit = pickGround(cam, t, mx, my, W, H);
    if (!hit) return false;
    gx = hit->first;
    gz = hit->second;

    GridPos tile{ (int)std::floor(hit->first), (int)std::floor(hit->second) };
    Door* door = findDoor(w);
    bool onDoor = door
               && tile.x >= door->origin.x && tile.x < door->origin.x + 3
               && tile.y >= door->origin.y && tile.y < door->origin.y + 3;

    issueMoveOrder(w, t, units, tile, onDoor);
    return true;
}

// Draws one loading frame and keeps the window responsive while things load
void loadingFrame(SDL_Window* window, Renderer& renderer, float progress) {
    SDL_PumpEvents();
    int winW, winH;
    SDL_GetWindowSizeInPixels(window, &winW, &winH);
    renderer.drawLoadingScreen(progress, winW, winH);
    SDL_GL_SwapWindow(window);
}

// Builds a fresh level: new terrain, new entities. Used by New Game and on restart.
// progress, if given, is called as the level loads (0..1), for the loading screen.
void startLevel(World& world, Terrain& terrain, Renderer& renderer, Rng& rng, unsigned seed,
                const std::function<void(float)>& progress = {}) {
    auto report = [&](float p) { if (progress) progress(p); };
    terrain.generate(seed);
    report(0.35f);
    renderer.uploadTerrain(terrain);
    report(0.7f);
    world = World{};
    rng.gen.seed(seed);
    setupWorld(world, terrain, rng);
    report(0.9f);
}

// Rebuilds a level from a save. The terrain and building come from the seed. The units and keys
// are then swapped for the saved ones, and the door is opened if it was open when saved.
void restoreLevel(World& world, Terrain& terrain, Renderer& renderer, Rng& rng, const SaveData& d) {
    terrain.generate(d.seed);
    renderer.uploadTerrain(terrain);
    world = World{};
    rng.gen.seed(d.seed);
    setupWorld(world, terrain, rng);

    // Drop the units and keys that setupWorld generated
    for (Entity e : world.view<Unit>()) world.destroy(e);
    for (Entity e : world.view<Key>()) world.destroy(e);

    for (const SaveUnit& u : d.units) {
        Entity e = world.create();
        world.add(e, Transform{ u.x, u.y, u.z });
        world.add(e, Unit{});
        if (u.hostile) world.add(e, Hostile{});
        else world.add(e, Commandable{});
        world.add(e, Name{ u.name });
        world.add(e, UnitSprite{ u.sprite });
    }
    for (const SaveKey& k : d.keys) {
        Entity e = world.create();
        world.add(e, Transform{ k.x, k.y, k.z });
        world.add(e, Key{});
    }

    if (Door* door = findDoor(world)) {
        door->unlocked = d.doorUnlocked;
        if (d.doorUnlocked) terrain.setWalkable(door->entrance.x, door->entrance.y, true);
    }
}

// Captures what a save needs from the running world, including the camera
SaveData captureWorld(World& w, const GameState& gs, const Camera& cam, unsigned seed,
                      long long ts, const std::string& name) {
    SaveData d;
    d.name = name;
    d.timestamp = ts;
    d.seed = seed;
    d.gs = gs;

    d.cam.present = true;
    d.cam.fx = cam.focusX;
    d.cam.fy = cam.focusY;
    d.cam.fz = cam.focusZ;
    d.cam.yaw = cam.yaw;
    d.cam.pitch = cam.pitch;
    d.cam.distance = cam.distance;

    Door* door = findDoor(w);
    d.doorUnlocked = door && door->unlocked;

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
        d.units.push_back(su);
    }
    for (Entity k : w.view<Key, Transform>()) {
        Transform* t = w.get<Transform>(k);
        d.keys.push_back(SaveKey{ t->x, t->y, t->z });
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
    // The panel is centred vertically, and never closer than 8px to the top
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
                                const std::vector<SaveSummary>& saves, size_t listOffset) {
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

    out.push_back(Button{ "Close", Rect{ x, g.closeY, w, g.btnH }, Action::CancelName });
    return out;
}

// Buttons for the main menus, in pixels
std::vector<Button> layoutButtons(Screen screen, int W, int H, const GameSettings& st,
                                  const std::vector<SaveSummary>& saves) {
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
        float x = W * 0.62f, y = H * 0.42f;
        add("Continue", Action::Continue, x, y, hasSave);        y += bh + gap;
        add("New Game", Action::NewGame, x, y);                  y += bh + gap;
        add("Load Game", Action::LoadGame, x, y, hasSave);       y += bh + gap;
        add("Settings", Action::Settings, x, y);                 y += bh + gap;
        add("Quit", Action::Quit, x, y);
        break;
    }
    case Screen::Settings: {
        float x = (W - bw) * 0.5f, y = H * 0.30f;
        add(std::string("Display: ") + DISPLAY_MODE_NAMES[st.displayMode], Action::CycleDisplay, x, y); y += bh + gap;
        add("Resolution: " + std::to_string(RESOLUTIONS[st.resolutionIndex].w) + "x"
                           + std::to_string(RESOLUTIONS[st.resolutionIndex].h),
            Action::CycleResolution, x, y, st.displayMode != DISPLAY_BORDERLESS); y += bh + gap;
        add(std::string("Edge scrolling: ") + (st.edgeScroll ? "On" : "Off"), Action::ToggleEdge, x, y); y += bh + gap;
        add("Autosave: " + std::to_string(st.autosaveMinutes) + " min", Action::CycleAutosave, x, y); y += bh + gap;
        add("Back", Action::Back, x, y);
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
        break;
    }
    return out;
}

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
    TTF_Font* font = TTF_OpenFont("assets/font/NotoSans_Condensed-Bold.ttf", 64.0f);
    TTF_Font* hudFont = TTF_OpenFont("assets/font/NotoSans_Condensed-Bold.ttf", 22.0f);
    TTF_Font* uiFont = TTF_OpenFont("assets/font/NotoSans_Condensed-Bold.ttf", 30.0f);
    TTF_Font* listFont = TTF_OpenFont("assets/font/NotoSans_Condensed-Bold.ttf", 20.0f);   // save list rows
    if (!font || !hudFont || !uiFont) {
        SDL_Log("Could not open font: %s", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    SDL_Window* window = SDL_CreateWindow("RTS", 1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    SDL_GLContext ctx = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, ctx);
    SDL_GL_SetSwapInterval(1);

    glewExperimental = GL_TRUE;
    GLenum glewErr = glewInit();
    if (glewErr != GLEW_OK) {
        SDL_Log("GLEW init failed: %s", glewGetErrorString(glewErr));
        return 1;
    }
    glGetError();

    GameSettings settings = loadSettings(SETTINGS_PATH);
    applyDisplay(window, settings);

    // ---- Loading phase: textures and models load before the title screen appears ----
    Renderer renderer;
    if (!renderer.init(font, hudFont, uiFont, [&](float p) { loadingFrame(window, renderer, p); })) return 1;
    renderer.setListFont(listFont);

    Terrain terrain;
    World world;
    Rng rng;
    GameState gs;
    Selection sel;
    Camera cam;
    unsigned levelSeed = 0;

    Screen screen = Screen::Title;
    Screen backScreen = Screen::Title;    // where Back goes from Settings

    // Whether a game is loaded. Loading over one may need a confirmation first.
    bool gameActive = false;
    // When the current game was last saved. Empty until it's saved, so a new or loaded game counts as unsaved.
    std::optional<Uint64> lastSaveTicks;

    // Save/load popout. It sits over the current screen.
    bool panelOpen = false;
    bool panelSaveMode = false;           // true: Save Game, false: Load Game
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
    float pressX = 0.0f, pressY = 0.0f;
    float lastX = 0.0f, lastY = 0.0f;
    std::string lastTitle;

    // Level-start image: visible for OVERLAY_HOLD_MS, then fades out
    float overlayMs = 0.0f;
    bool overlayOn = false;

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

    // Builds a new level and starts play, showing the level-start image
    auto startNewGame = [&]() {
        closePanel();
        levelSeed = (unsigned)std::time(nullptr);
        startLevel(world, terrain, renderer, rng, levelSeed,
                   [&](float p) { loadingFrame(window, renderer, p); });
        focusCameraOnFirstUnit(world, cam);
        gs = GameState{};
        sel = Selection{};
        leftDown = middleDown = false;
        overlayMs = 0.0f;
        overlayOn = true;
        autoTimer = 0.0f;
        gameActive = true;
        lastSaveTicks.reset();
        screen = Screen::Playing;
    };

    // Writes the current game to a file. Used for manual saves and autosaves alike.
    auto writeSave = [&](const std::string& path, const std::string& name, bool autosave) -> bool {
        SaveData d = captureWorld(world, gs, cam, levelSeed, nowMs(), name);
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
        restoreLevel(world, terrain, renderer, rng, d);
        levelSeed = d.seed;
        gs = d.gs;
        focusCameraOnFirstUnit(world, cam);
        applySaveCamera(d.cam, cam);
        sel = Selection{};
        leftDown = middleDown = false;
        overlayOn = false;
        overlayMs = 0.0f;
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

    // Runs a button's action
    auto performAction = [&](Action action, int param) {
        switch (action) {
        case Action::Continue:
            if (!saves.empty()) loadSave(saves.front().path);
            return;
        case Action::NewGame:
            startNewGame();
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
            return;
        case Action::Back:
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
        case Action::CycleDisplay:
            settings.displayMode = (settings.displayMode + 1) % DISPLAY_MODE_COUNT;
            applyDisplay(window, settings);
            break;
        case Action::CycleResolution:
            settings.resolutionIndex = (settings.resolutionIndex + 1) % RESOLUTION_COUNT;
            applyDisplay(window, settings);
            break;
        case Action::ToggleEdge:
            settings.edgeScroll = !settings.edgeScroll;
            break;
        case Action::CycleAutosave: {
            int idx = 0;
            for (int i = 0; i < AUTOSAVE_COUNT; ++i) {
                if (AUTOSAVE_CHOICES[i] == settings.autosaveMinutes) idx = i;
            }
            settings.autosaveMinutes = AUTOSAVE_CHOICES[(idx + 1) % AUTOSAVE_COUNT];
            break;
        }
        }
        saveSettings(settings, SETTINGS_PATH);
    };

    while (running) {
        int winW, winH;
        SDL_GetWindowSizeInPixels(window, &winW, &winH);
        viewH = winH;

        // ---- Input ----
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;

            case SDL_EVENT_TEXT_INPUT:
                if (panelOpen && panelSaveMode && ask == Ask::None && saveName.size() < NAME_LIMIT) {
                    saveName += e.text.text;
                }
                break;

            case SDL_EVENT_KEY_DOWN:
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
                    if (screen == Screen::Playing) {
                        leftDown = middleDown = false;
                        sel.dragging = false;
                        screen = Screen::Pause;
                        refreshSaves();
                    } else if (screen == Screen::Pause) {
                        screen = Screen::Playing;
                    } else if (screen == Screen::Settings) {
                        screen = backScreen;
                    }
                }
                break;

            case SDL_EVENT_MOUSE_WHEEL:
                if (panelOpen) {
                    // Scroll the save list, keeping the offset within range
                    int maxOffset = std::max(0, (int)saves.size() - (int)visibleSaveRows(winH));
                    int next = (int)listOffset - (int)e.wheel.y;
                    listOffset = (size_t)std::clamp(next, 0, maxOffset);
                } else if (screen == Screen::Playing) {
                    cam.distance *= (e.wheel.y > 0.0f) ? 0.9f : 1.1f;
                    cam.clampState();
                }
                break;

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (panelOpen) {
                    // The popout takes all clicks while it's open
                    if (e.button.button == SDL_BUTTON_LEFT) {
                        for (const Button& b : layoutPanel(winW, winH, panelSaveMode, ask, saves, listOffset)) {
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

                if (screen != Screen::Playing) {
                    // Menus: a left click runs the enabled button under the cursor
                    if (e.button.button == SDL_BUTTON_LEFT) {
                        for (const Button& b : layoutButtons(screen, winW, winH, settings, saves)) {
                            if (b.enabled && b.rect.contains(e.button.x, e.button.y)) {
                                performAction(b.action, b.param);
                                break;
                            }
                        }
                    }
                    break;
                }

                if (e.button.button == SDL_BUTTON_LEFT) {
                    leftDown = true;
                    pressX = e.button.x;
                    pressY = e.button.y;
                    sel.dragging = false;
                } else if (e.button.button == SDL_BUTTON_MIDDLE) {
                    // Pressing alone changes nothing. Dragging orbits around the current focus.
                    middleDown = true;
                    lastX = e.button.x;
                    lastY = e.button.y;
                } else if (e.button.button == SDL_BUTTON_RIGHT) {
                    float gx = 0.0f, gz = 0.0f;
                    if (rightClickOrder(world, terrain, cam, e.button.x, e.button.y, winW, winH, gx, gz)) {
                        actionX = gx;
                        actionZ = gz;
                        actionAlpha = 1.0f;
                    }
                }
                break;

            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (e.button.button == SDL_BUTTON_LEFT && leftDown) {
                    leftDown = false;
                    bool additive = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
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
                if (screen == Screen::Playing) {
                    if (leftDown) {
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
        pivotAlpha = std::clamp(pivotAlpha + (middleDown ? 1.0f : -1.0f) * dt * 4.0f, 0.0f, 1.0f);
        actionAlpha = std::max(0.0f, actionAlpha - dt);   // fades out over about a second

        // ---- Game update: only while playing ----
        float overlayAlpha = 0.0f;
        if (screen == Screen::Playing) {
            // Keyboard panning and (optional) edge scrolling
            const bool* keys = SDL_GetKeyboardState(nullptr);
            float right = 0.0f, forward = 0.0f;
            if (keys[SDL_SCANCODE_D]) right += 1.0f;
            if (keys[SDL_SCANCODE_A]) right -= 1.0f;
            if (keys[SDL_SCANCODE_W]) forward += 1.0f;
            if (keys[SDL_SCANCODE_S]) forward -= 1.0f;

            if (settings.edgeScroll && !leftDown && !middleDown) {
                float mx, my;
                SDL_GetMouseState(&mx, &my);
                if (mx < 6.0f) right -= 1.0f;
                if (mx > winW - 6.0f) right += 1.0f;
                if (my < 6.0f) forward += 1.0f;
                if (my > winH - 6.0f) forward -= 1.0f;
            }

            float panSpeed = cam.distance * 0.9f * dt;
            cam.pan(right * panSpeed, forward * panSpeed);
            cam.updateClearance(terrain);

            squadSystem(world, dt);
            steeringSystem(world, terrain, gs, dt);
            pickupSystem(world, terrain, gs);
            cleanupSquads(world);
            updateWinState(world, gs);

            // Every unit has passed through the door, so start a new level
            if (gs.won) {
                levelSeed = (unsigned)rng.gen();
                startLevel(world, terrain, renderer, rng, levelSeed,
                           [&](float p) { loadingFrame(window, renderer, p); });
                focusCameraOnFirstUnit(world, cam);
                gs = GameState{};
                sel = Selection{};
                leftDown = false;
                overlayMs = 0.0f;
                overlayOn = true;
                autoTimer = 0.0f;
            }

            // Autosave every N minutes of play
            autoTimer += dt;
            if (autoTimer >= settings.autosaveMinutes * 60.0f) {
                autoTimer = 0.0f;
                if (autosaveNow()) showNotice("Autosaved");
            }

            // Level-start image timing
            if (overlayOn) {
                overlayMs += dt * 1000.0f;
                if (overlayMs >= OVERLAY_HOLD_MS + OVERLAY_FADE_MS) {
                    overlayOn = false;
                } else if (overlayMs < OVERLAY_HOLD_MS) {
                    overlayAlpha = 1.0f;
                } else {
                    overlayAlpha = 1.0f - (overlayMs - OVERLAY_HOLD_MS) / OVERLAY_FADE_MS;
                }
            }
        }

        // ---- HUD: shown in the window title, since there's no text rendering there ----
        std::string title = "RTS   Keys " + std::to_string(gs.keysCollected) + "/" + std::to_string(KEY_COUNT)
                          + "   Entered " + std::to_string(gs.unitsEntered) + "/" + std::to_string(UNIT_COUNT);
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
        auto drawButtons = [&](const std::vector<Button>& buttons) {
            for (const Button& b : buttons) {
                renderer.drawButton(b.rect, b.label, b.enabled, b.rect.contains(mouseX, mouseY));
            }
        };
        auto drawNotice = [&]() {
            if (noticeSec > 0.0f) {
                renderer.drawUiTextCentered(notice, winW * 0.5f, winH - 70.0f * (winH / 720.0f),
                                            RGBA{ 1.0f, 1.0f, 0.6f, 1.0f });
            }
        };
        auto dim = [&](float alpha) {
            renderer.drawRect(Rect{ 0.0f, 0.0f, (float)winW, (float)winH }, RGBA{ 0.0f, 0.0f, 0.0f, alpha });
        };

        switch (screen) {
        case Screen::Title:
            renderer.beginUi(winW, winH);
            renderer.drawTitleBackground();
            renderer.drawUiText(std::string("Version ") + GAME_VERSION, 16.0f, winH - 44.0f,
                                RGBA{ 1.0f, 1.0f, 1.0f, 0.8f });
            drawButtons(layoutButtons(Screen::Title, winW, winH, settings, saves));
            renderer.endUi();
            break;

        case Screen::Settings:
            renderer.beginUi(winW, winH);
            renderer.drawTitleBackground();
            dim(0.45f);
            renderer.drawUiTextCentered("Settings", winW * 0.5f, winH * 0.18f, RGBA{ 1.0f, 1.0f, 1.0f, 1.0f });
            drawButtons(layoutButtons(Screen::Settings, winW, winH, settings, saves));
            renderer.endUi();
            break;

        case Screen::Playing:
            renderer.drawFrame(world, terrain, cam, sel, overlayAlpha, hud, winW, winH);
            renderer.drawPivotRing(cam, terrain, pivotAlpha, winW, winH);
            renderer.drawActionCrystal(cam, terrain, actionX, actionZ, actionAlpha, winW, winH);
            break;

        case Screen::Pause:
            renderer.drawFrame(world, terrain, cam, sel, 0.0f, {}, winW, winH);
            renderer.beginUi(winW, winH);
            dim(0.55f);
            renderer.drawUiTextCentered("Paused", winW * 0.5f, winH * 0.22f, RGBA{ 1.0f, 1.0f, 1.0f, 1.0f });
            drawButtons(layoutButtons(Screen::Pause, winW, winH, settings, saves));
            renderer.endUi();
            break;
        }

        // ---- Save/load popout, drawn over whichever screen is showing ----
        if (panelOpen) {
            const float s = winH / 720.0f;
            PanelGeometry g = panelGeometry(winW, winH);
            const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f };
            const RGBA grey{ 0.6f, 0.6f, 0.65f, 1.0f };
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
                for (const Button& b : layoutPanel(winW, winH, panelSaveMode, ask, saves, listOffset)) {
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
                                                cx, g.nameBox.y + g.nameBox.h * 0.22f, grey);
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
                                      innerX + textInset, hy, sortKey == SortKey::Name ? white : grey);
                renderer.drawListText("Type", innerX + COL_TYPE * innerW, hy, grey);
                renderer.drawListText(headerLabel("Time", sortKey == SortKey::Time),
                                      innerX + COL_TIME * innerW, hy, sortKey == SortKey::Time ? white : grey);

                if (saves.empty()) {
                    renderer.drawUiTextCentered("No saves yet", cx, g.listTop + g.nameBox.h * 0.22f, grey);
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

                for (const Button& b : layoutPanel(winW, winH, panelSaveMode, Ask::None, saves, listOffset)) {
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

        SDL_GL_SwapWindow(window);
    }

    // ---- Shutdown ----
    renderer.shutdown();
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
