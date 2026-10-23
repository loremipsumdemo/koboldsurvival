// Runs the world tool inside the map editor. The frame loop mirrors the tool's own main loop,
// without its benchmark, performance panel, screenshot key and command-line variants.
#include "worldmaker_host.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "grid.h"
#include "map.h"
#include "timefmt.h"
#include "world_format.h"

#include "app/console.hpp"
#include "app/mapview.hpp"
#include "app/menu.hpp"
#include "app/session.hpp"
#include "core/debug_options.hpp"
#include "core/math.hpp"
#include "core/render_options.hpp"
#include "core/tunables.hpp"
#include "player/walker.hpp"
#include "render/renderer.hpp"
#include "theme/theme.hpp"
#include "world/world.hpp"

// Render options and per-frame stats for the world tool.
namespace wm {
RenderOptions gOpt{};   // the baseline rendering
RenderStats gStats{};   // counted during each frame
}  // namespace wm

using namespace wm;

namespace wmhost {

namespace {

// Map biome for each tool biome. Water is not generated, so there is no entry for it.
uint8_t GameBiome(wm::Biome b) {
    switch (b) {
    case wm::Biome::Sand:       return BIOME_SAND;
    case wm::Biome::Grass:      return BIOME_GRASS;
    case wm::Biome::Forest:     return BIOME_FOREST;
    case wm::Biome::Desert:     return BIOME_DESERT;
    case wm::Biome::Rock:       return BIOME_ROCK;
    case wm::Biome::Snow:       return BIOME_SNOW;
    case wm::Biome::BeachSand:  return BIOME_BEACH;
    case wm::Biome::Silt:       return BIOME_SILT;
    case wm::Biome::FrozenClay: return BIOME_FROZEN_CLAY;
    case wm::Biome::Clay:       return BIOME_CLAY;
    case wm::Biome::Gravel:     return BIOME_GRAVEL;
    case wm::Biome::AridSoil:   return BIOME_ARID_SOIL;
    case wm::Biome::Scree:      return BIOME_SCREE;
    case wm::Biome::Floodplain: return BIOME_FLOODPLAIN;
    case wm::Biome::CliffRock:  return BIOME_CLIFF_ROCK;
    }
    return BIOME_NONE;
}

// The game's heights are a tenth of the tool's world units. A tile is one unit wide, so this keeps
// the slopes the game walks on gentle.
constexpr float kGameHeightScale = 0.1f;
// A tile is blocked when its corners differ in height by more than this, in game units
constexpr float kMaxTileStep = 0.5f;

// Height of a corner in game units: the average of the four tiles around it, measured from the sea
float GameCornerHeight(const World& w, int gx, int gz) {
    const int n = w.size;
    auto tile = [&](int x, int z) {
        x = std::clamp(x, 0, n - 1);
        z = std::clamp(z, 0, n - 1);
        return w.height[static_cast<size_t>(z) * n + x];
    };
    const float avg = 0.25f * (tile(gx - 1, gz - 1) + tile(gx, gz - 1) + tile(gx - 1, gz) + tile(gx, gz));
    return std::clamp((avg - w.seaLevel) * kGameHeightScale, -MAX_HEIGHT, MAX_HEIGHT);
}

// Writes the loaded world as a game world: a folder holding world.txt and region files, built
// straight from the tool's heights. Land under the sea and bare rock are blocked, and so is any tile
// whose corners differ in height by more than kMaxTileStep. outPath gets the folder's full path.
// A world already saved under this name is replaced in its own folder, as the game's saves are.
bool ExportGameWorld(const World& w, const std::string& name, const std::string& mapDir,
                     std::string& outPath, std::string& err) {
    namespace fs = std::filesystem;
    const int n = w.size;
    if (n < 1 || w.height.size() != static_cast<size_t>(n) * static_cast<size_t>(n) ||
        w.biome.size() != w.height.size()) {
        err = "no world to export";
        return false;
    }
    if (name.empty()) {
        err = "the world needs a name";
        return false;
    }

    // A world already saved under this name is replaced in its own folder. Otherwise a new folder is
    // made, named by the time it was saved.
    const long long now = (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::string folder;
    for (const WorldEntry& e : listWorlds(mapDir)) {
        if (e.name == name) {
            folder = e.folder;
            break;
        }
    }
    const bool replacing = !folder.empty();
    if (!replacing) folder = mapDir + "/world_" + std::to_string(now);

    // The world is written beside the old one and moved into place only when complete, so a save
    // that fails leaves the old world as it was
    const std::string temp = folder + ".saving";
    std::error_code ec;
    fs::remove_all(temp, ec);

    WorldInfo info;
    info.name = name;
    info.created = now;
    info.size = n;
    info.heightScale = kGameHeightScale;
    const bool ok = writeWorld(temp, info, [&](int cx, int cz, ChunkData& c) {
        for (int j = 0; j < CHUNK_CORNERS; ++j) {
            for (int i = 0; i < CHUNK_CORNERS; ++i) {
                const int gx = std::min(cx * CHUNK_TILES + i, n);
                const int gz = std::min(cz * CHUNK_TILES + j, n);
                c.heights[static_cast<size_t>(j) * CHUNK_CORNERS + i] = GameCornerHeight(w, gx, gz);
            }
        }
        for (int j = 0; j < CHUNK_TILES; ++j) {
            for (int i = 0; i < CHUNK_TILES; ++i) {
                const int tx = cx * CHUNK_TILES + i, tz = cz * CHUNK_TILES + j;
                if (tx >= n || tz >= n) continue;   // past the edge: no ground
                const size_t k = static_cast<size_t>(tz) * n + tx;
                const size_t t = static_cast<size_t>(j) * CHUNK_TILES + i;
                const wm::Biome b = static_cast<wm::Biome>(w.biome[k]);
                c.biome[t] = GameBiome(b);

                const size_t c00 = static_cast<size_t>(j) * CHUNK_CORNERS + i;
                const float corners[4] = { c.heights[c00], c.heights[c00 + 1],
                                           c.heights[c00 + CHUNK_CORNERS], c.heights[c00 + CHUNK_CORNERS + 1] };
                const float lo = std::min(std::min(corners[0], corners[1]), std::min(corners[2], corners[3]));
                const float hi = std::max(std::max(corners[0], corners[1]), std::max(corners[2], corners[3]));
                const bool land = w.height[k] >= w.seaLevel;
                const bool flat = hi - lo <= kMaxTileStep;
                const bool bare = b == wm::Biome::Rock || b == wm::Biome::CliffRock;
                c.walk[t] = (land && flat && !bare) ? 1 : 0;
            }
        }
    }, err);
    if (!ok) {
        fs::remove_all(temp, ec);
        return false;
    }
    if (replacing) fs::remove_all(folder, ec);
    fs::rename(temp, folder, ec);
    if (ec) {
        err = "could not move the world into place";
        return false;
    }
    outPath = folder;
    return true;
}

// Camera for the walker's eye, as the tool's own loop builds it.
Camera MakeCamera(const Player& p, int w, int h) {
    Camera c{};
    c.eye = p.pos + V3{0.0f, T.eyeHeight, 0.0f};
    c.fwd = LookDirection(p.yaw, p.pitch);
    const V3 worldUp{0.0f, 1.0f, 0.0f};
    c.right = Normalize(Cross(c.fwd, worldUp));
    c.up = Cross(c.right, c.fwd);
    c.fovy = T.fov;
    c.aspect = static_cast<float>(w) / static_cast<float>(h);

    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
    if (gOpt.dynamicFar) farPlane = std::clamp(T.viewDistance * 1.1f + 256.0f, 300.0f, 16000.0f);
    c.farPlane = farPlane;

    const Mat4 view = LookAt(c.eye, c.eye + c.fwd, worldUp);
    const Mat4 proj = Perspective(c.fovy, c.aspect, nearPlane, farPlane);
    c.viewProj = Multiply(proj, view);
    return c;
}

// Progress bar across the top, shown while a world generates and the menu is closed.
void DrawProgress(Renderer& r, int w, int percent) {
    const float fw = static_cast<float>(w);
    const float barW = fw * 0.4f;
    const float x = (fw - barW) * 0.5f;
    const float fill = barW * static_cast<float>(percent) / 100.0f;

    r.Rect(x, 24.0f, barW, 14.0f, V3{0.05f, 0.05f, 0.08f}, 0.85f);
    r.Rect(x, 24.0f, fill, 14.0f, V3{0.0f, 0.86f, 1.0f}, 1.0f);
    r.Text(x, 44.0f, "generating world " + std::to_string(percent) + "%", V3{0.80f, 0.85f, 0.90f});
}

// Generation log, top-left, while a world builds.
void DrawGenLog(Renderer& r, Session& s, int w, int h) {
    const GenStatus st = SnapshotStatus(s);
    const float lh = r.LineHeight();
    const float x = 12.0f;
    const float top = 12.0f;
    const float maxTextW = static_cast<float>(w) * 0.5f;
    const int pct = s.progress.load();

    const std::string where = st.stage == 0 ? std::string("Starting...")
        : "Stage " + std::to_string(st.stage) + " of " + std::to_string(st.stages) + ": " + st.name;
    const std::string timing = std::to_string(pct) + "%   elapsed " + FormatDuration(st.elapsed) + "   ETA " +
                               (st.eta < 0 ? std::string("estimating") : FormatDuration(st.eta));
    const int left = st.stages - st.stage;

    std::vector<std::string> rows{where, timing, "Stages left after this: " + std::to_string(left > 0 ? left : 0), ""};
    const float room = (static_cast<float>(h) - top - 24.0f) / lh;
    const size_t fit = room > static_cast<float>(rows.size()) ? static_cast<size_t>(room) - rows.size() : 0;
    const size_t first = st.log.size() > fit ? st.log.size() - fit : 0;
    for (size_t i = st.log.size(); i-- > first;) rows.push_back(st.log[i]);

    float panelW = 0.0f;
    for (std::string& row : rows) {
        while (!row.empty() && r.MeasureText(row) > maxTextW) row.pop_back();
        panelW = std::max(panelW, r.MeasureText(row));
    }
    const float panelH = static_cast<float>(rows.size()) * lh + 12.0f;
    r.Rect(x - 8.0f, top - 6.0f, panelW + 16.0f, panelH, V3{0.04f, 0.04f, 0.06f}, 0.80f);
    for (size_t i = 0; i < rows.size(); ++i) {
        const V3 color = i == 0 ? V3{0.0f, 0.86f, 1.0f} : V3{0.80f, 0.85f, 0.90f};
        r.Text(x, top + static_cast<float>(i) * lh, rows[i], color);
    }
}

V3 Rgb3(const float* c) { return {c[0], c[1], c[2]}; }

}  // namespace

struct Tool::Impl {
    SDL_Window* win = nullptr;
    Renderer renderer;
    Session session;
    Look look;
    Player player;
    Console con;
    MenuState menu;
    MapView mapView;
    uint64_t mapTexKey = 0;
    Mouse mouse;
    bool running = true;
    std::vector<LineVertex> lines;
    Uint64 lastNs = 0;
    double updateAccum = 0.0;
    static constexpr double kUpdateStep = 1.0 / 60.0;
    static constexpr int kMaxUpdatesPerFrame = 5;

    // Mouse look and the cursor follow whichever overlay is open.
    void ApplyInputMode() {
        const bool overlay = con.open || menu.open;
        if (overlay) SDL_StartTextInput(win);
        else         SDL_StopTextInput(win);
        SDL_SetWindowRelativeMouseMode(win, !overlay);
    }
    void OpenMenu() {
        con.open = false;
        MenuOpen(menu, session);
        ApplyInputMode();
    }
    void CloseMenu() {
        menu.open = false;
        MenuStopEditing(menu);
        ApplyInputMode();
    }
    void ToggleConsole() {
        con.open = !con.open;
        if (con.open) menu.open = false;
        ApplyInputMode();
    }
    void SubmitConsole() {
        if (!con.input.empty()) con.history.push_back(con.input);
        RunConsoleLine(con.input, con, running, look, session);
        con.input.clear();
        con.histPos = -1;
    }
};

Tool::Tool() : impl_(std::make_unique<Impl>()) {}
Tool::~Tool() = default;

bool Tool::Init(SDL_Window* win, const std::vector<unsigned char>& font, const std::string& dataDir,
                const std::string& mapDir, std::string& err) {
    Impl& s = *impl_;
    s.win = win;
    ResetWalkerVars();
    if (!s.renderer.Init(font, err)) return false;

    s.session.worldsDir = dataDir + "/worlds";
    s.session.themesDir = dataDir + "/themes";
    s.session.exportMap = [mapDir](const World& w, const std::string& name, std::string& path, std::string& e) {
        return ExportGameWorld(w, name, mapDir, path, e);
    };
    s.session.listGameWorlds = [mapDir](bool byName, bool ascending) {
        std::vector<WorldEntry> found = listWorlds(mapDir);
        sortWorldEntries(found, byName, ascending);
        std::vector<GameWorldEntry> out;
        for (const WorldEntry& e : found) {
            out.push_back(GameWorldEntry{ e.name, e.created,
                                          e.created > 0 ? timeString(e.created) : std::string("unknown") });
        }
        return out;
    };
    s.con.print("F1 opens the console. Esc opens the menu, where you can generate, export and import.");
    s.con.print("Type: gen   to generate a world with a random seed.");
    s.lastNs = SDL_GetTicksNS();
    return true;
}

void Tool::Begin() {
    Impl& s = *impl_;
    JoinGenerate(s.session);
    s.session.world = World{};
    s.session.hasWorld = false;
    s.session.terrainDirty = false;
    s.renderer.ClearTerrain();
    s.player = Player{};
    s.mapTexKey = 0;
    s.updateAccum = 0.0;
    s.running = true;
    s.con.open = false;
    s.menu.open = false;
    s.lastNs = SDL_GetTicksNS();
    s.ApplyInputMode();
}

void Tool::End() {
    Impl& s = *impl_;
    s.con.open = false;
    s.menu.open = false;
    SDL_StopTextInput(s.win);
    SDL_SetWindowRelativeMouseMode(s.win, false);
}

void Tool::Shutdown() {
    Impl& s = *impl_;
    JoinGenerate(s.session);
    s.renderer.Shutdown();
}

bool Tool::wantsBack() const { return !impl_->running; }

void Tool::Event(const SDL_Event& e) {
    Impl& s = *impl_;
    switch (e.type) {
    case SDL_EVENT_KEY_DOWN: {
        const SDL_Keycode k = e.key.key;
        if (k == SDLK_F1 && !e.key.repeat) {
            s.ToggleConsole();
            break;
        }
        if (s.con.open) {
            if (k == SDLK_ESCAPE) {
                s.con.open = false;
                s.ApplyInputMode();
            } else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
                s.SubmitConsole();
            } else if (k == SDLK_BACKSPACE) {
                if (!s.con.input.empty()) s.con.input.pop_back();
            } else if (k == SDLK_UP && !s.con.history.empty()) {
                if (s.con.histPos + 1 < static_cast<int>(s.con.history.size())) ++s.con.histPos;
                s.con.input = s.con.history[s.con.history.size() - 1 - static_cast<std::size_t>(s.con.histPos)];
            } else if (k == SDLK_DOWN) {
                if (s.con.histPos > 0) {
                    --s.con.histPos;
                    s.con.input = s.con.history[s.con.history.size() - 1 - static_cast<std::size_t>(s.con.histPos)];
                } else if (s.con.histPos == 0) {
                    s.con.histPos = -1;
                    s.con.input.clear();
                }
            }
        } else if (s.menu.open) {
            if (k == SDLK_ESCAPE && !e.key.repeat && !s.menu.activeField && !s.menu.picker.open) {
                s.CloseMenu();
            } else if (k == SDLK_ESCAPE && s.menu.picker.open && !s.menu.activeField) {
                s.menu.picker.open = false;
            } else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
                MenuCommitSlider(s.menu);
                MenuStopEditing(s.menu);
            } else if (k == SDLK_ESCAPE) {
                MenuStopEditing(s.menu);
            } else if (k == SDLK_BACKSPACE) {
                MenuBackspace(s.menu);
            } else if (!s.menu.activeField && k == SDLK_UP) {
                MenuScrollBy(s.menu, -48.0f);
            } else if (!s.menu.activeField && k == SDLK_DOWN) {
                MenuScrollBy(s.menu, 48.0f);
            } else if (!s.menu.activeField && k == SDLK_PAGEUP) {
                MenuScrollBy(s.menu, -400.0f);
            } else if (!s.menu.activeField && k == SDLK_PAGEDOWN) {
                MenuScrollBy(s.menu, 400.0f);
            } else if (!s.menu.activeField && k == SDLK_HOME) {
                MenuScrollTo(s.menu, false);
            } else if (!s.menu.activeField && k == SDLK_END) {
                MenuScrollTo(s.menu, true);
            }
        } else if (k == SDLK_ESCAPE && !e.key.repeat) {
            s.OpenMenu();
        }
        break;
    }
    case SDL_EVENT_TEXT_INPUT:
        if (s.con.open) {
            if (s.con.input.size() < 200) s.con.input += e.text.text;
        } else if (s.menu.open) {
            MenuTextInput(s.menu, e.text.text);
        }
        break;
    case SDL_EVENT_MOUSE_MOTION:
        s.mouse.x = e.motion.x;
        s.mouse.y = e.motion.y;
        if (!s.con.open && !s.menu.open) {
            s.player.yaw   -= e.motion.xrel * T.mouseSens;
            s.player.pitch -= e.motion.yrel * T.mouseSens;
            s.player.pitch  = std::clamp(s.player.pitch, -89.0f, 89.0f);
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (e.button.button == SDL_BUTTON_LEFT) {
            s.mouse.x = e.button.x;
            s.mouse.y = e.button.y;
            s.mouse.down = true;
            s.mouse.pressed = true;
        }
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        s.mouse.wheel += e.wheel.y;
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (e.button.button == SDL_BUTTON_LEFT) {
            s.mouse.x = e.button.x;
            s.mouse.y = e.button.y;
            s.mouse.down = false;
        }
        break;
    default:
        break;
    }
}

void Tool::Frame() {
    Impl& s = *impl_;
    int w = 1, h = 1;
    SDL_GetWindowSize(s.win, &w, &h);
    if (h < 1) h = 1;
    if (w < 1) w = 1;

    // A finished generation or a loaded world: build the terrain and stand the walker on it.
    if (PollGenerate(s.session)) {
        s.con.print("world ready: seed " + std::to_string(s.session.world.seed) + ", " +
                    std::to_string(s.session.world.size) + " x " + std::to_string(s.session.world.size) + " tiles");
    }
    if (s.session.terrainDirty) {
        s.session.terrainDirty = false;
        s.renderer.BuildTerrain(s.session.world);
        s.player.pos = {0.0f, SampleHeight(s.session.world, 0.0f, 0.0f), 0.0f};
        s.player.vel = {0.0f, 0.0f, 0.0f};
        s.player.grounded = true;
    }

    const Uint64 nowNs = SDL_GetTicksNS();
    float dt = std::min(static_cast<float>(nowNs - s.lastNs) / 1e9f, 0.05f);
    s.lastNs = nowNs;
    const double now = static_cast<double>(nowNs) / 1e9;

    // Fixed-step updates, so movement and the day cycle do not depend on the frame rate.
    s.updateAccum += dt;
    int steps = 0;
    while (s.updateAccum >= Impl::kUpdateStep - 1e-9 && steps < Impl::kMaxUpdatesPerFrame) {
        const float groundY = s.session.hasWorld ? SampleHeight(s.session.world, s.player.pos.x, s.player.pos.z) : 0.0f;
        AdvanceClock(s.look, static_cast<float>(Impl::kUpdateStep));
        UpdatePlayer(s.player, SDL_GetKeyboardState(nullptr), !s.con.open && !s.menu.open,
                     static_cast<float>(Impl::kUpdateStep), now, groundY);
        s.updateAccum -= Impl::kUpdateStep;
        ++steps;
    }
    if (steps == Impl::kMaxUpdatesPerFrame) s.updateAccum = 0.0;

    gStats = RenderStats{};
    SkyState sky = ComputeSky(s.look);
    if (s.renderer.UsingSkybox()) sky.horizon = s.renderer.SkyboxHorizon();
    const Camera cam = MakeCamera(s.player, w, h);

    // 3D pass: sky, terrain, floor, sea, then the reference lines when there is no world.
    s.renderer.BeginFrame(w, h, sky);
    if (!gOpt.skyLast && !gDebug.hideSky) s.renderer.DrawSky(cam, sky);
    if (s.session.hasWorld) {
        if (!gDebug.hideTerrain) s.renderer.DrawTerrain(cam, sky, T.viewDistance, T.lodDistance);
        if (!gDebug.hideFloor) s.renderer.DrawFloor(cam, sky);
        if (!gDebug.hideWater) s.renderer.DrawWater(cam, sky, s.session.world.seaLevel, T.viewDistance);
    }
    if (gOpt.skyLast && !gDebug.hideSky) s.renderer.DrawSky(cam, sky);

    s.lines.clear();
    if (!s.session.hasWorld) {
        const int major = std::max(1, static_cast<int>(T.majorEvery));
        if (gOpt.shaderGrid) {
            s.renderer.DrawGrid(cam, sky, T.gridRadius, major, Rgb3(s.look.gridMinor), Rgb3(s.look.gridMajor));
        } else {
            AppendGrid(s.lines, s.player.pos, T.gridRadius, major, Rgb3(s.look.gridMinor), Rgb3(s.look.gridMajor));
        }
    }
    s.renderer.DrawLines(cam, sky, s.lines);

    // 2D pass: menu, console or progress bar.
    if (s.menu.open) {
        s.renderer.BeginUI(w, h);
        const MenuAction action = DrawMenu(s.renderer, s.menu, s.session, s.look, s.mouse, w, h);
        s.renderer.EndUI();
        if (action == MenuAction::Resume) {
            s.CloseMenu();
        } else if (action == MenuAction::OpenConsole) {
            s.ToggleConsole();
        } else if (action == MenuAction::Quit) {
            s.running = false;
        }
    } else if (s.con.open) {
        s.renderer.BeginUI(w, h);
        DrawConsole(s.renderer, s.con, w, h);
        s.renderer.EndUI();
    } else if (s.session.generating) {
        s.renderer.BeginUI(w, h);
        DrawProgress(s.renderer, w, s.session.progress.load());
        s.renderer.EndUI();
    }

    // Map and compass: the compass always, the minimap, or the full map while M is held.
    if (s.session.hasWorld && !s.menu.open) {
        const uint64_t key = (static_cast<uint64_t>(s.session.world.size) << 32) ^ s.session.world.seed ^
                             static_cast<uint64_t>(reinterpret_cast<uintptr_t>(s.session.world.height.data()));
        if (key != s.mapTexKey) {
            const MapImage img = BuildMapImage(s.session.world, 1024);
            s.renderer.UploadMap(img.rgba.data(), img.side);
            s.mapTexKey = key;
        }
        const bool holdM = !s.con.open && SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_M];
        if (holdM) DrawFullMap(s.renderer, s.session.world, s.player, s.mapView, s.mouse, w, h);
        else       DrawMinimap(s.renderer, s.session.world, s.player, w, h);
        DrawCompass(s.renderer, s.player, w, h);
    }

    if (s.session.generating) {
        s.renderer.BeginUI(w, h);
        DrawGenLog(s.renderer, s.session, w, h);
        s.renderer.EndUI();
    }

    s.mouse.pressed = false;
    s.mouse.wheel = 0.0f;
}

}  // namespace wmhost
