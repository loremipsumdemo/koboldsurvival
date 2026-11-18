#include "app.h"
#include "spatial.h"
#include "systems_internal.h"

using namespace play;

// ---- The Map Editor: the Level Editor's screen with the terrain's brushes in its panel

namespace {
constexpr unsigned long long APPLY_EVERY_MS = 50;
constexpr float PI_F = 3.14159265f;

const char* const BIOME_KEYS[] = { "GRASS", "SAND", "FOREST", "DESERT", "SNOW", "ROCK", "WATER", "SHELF", "BEACH", "SILT", "FROZEN_CLAY", "CLAY",
                                   "GRAVEL", "ARID_SOIL", "SCREE", "FLOODPLAIN", "CLIFF_ROCK" };
static_assert(sizeof(BIOME_KEYS) / sizeof(BIOME_KEYS[0]) == (size_t)Biome::Count, "a name for every ground type");

// About how each ground type looks, for its swatch in the list
RGBA biomeSwatch(int b) {
    static const RGBA C[] = { { 0.47f, 0.38f, 0.27f, 1 }, { 0.69f, 0.64f, 0.47f, 1 }, { 0.38f, 0.33f, 0.23f, 1 }, { 0.84f, 0.75f, 0.53f, 1 },
                              { 0.92f, 0.94f, 0.97f, 1 }, { 0.45f, 0.45f, 0.47f, 1 }, { 0.15f, 0.35f, 0.60f, 1 }, { 0.25f, 0.50f, 0.65f, 1 },
                              { 0.80f, 0.75f, 0.58f, 1 }, { 0.55f, 0.50f, 0.42f, 1 }, { 0.62f, 0.66f, 0.70f, 1 }, { 0.66f, 0.45f, 0.33f, 1 },
                              { 0.55f, 0.55f, 0.52f, 1 }, { 0.72f, 0.52f, 0.30f, 1 }, { 0.50f, 0.48f, 0.46f, 1 }, { 0.40f, 0.42f, 0.28f, 1 },
                              { 0.32f, 0.31f, 0.33f, 1 } };
    return C[std::clamp(b, 0, (int)Biome::Count - 1)];
}

// How strongly the brush acts at (dx, dz) from its middle: 1 at the middle, 0 at and past its edge
float brushWeight(BrushShape shape, BrushFall fall, float dx, float dz, float r) {
    float d = 0.0f;
    switch (shape) {
    case BrushShape::Circle: d = std::sqrt(dx * dx + dz * dz) / r; break;
    case BrushShape::Square: d = std::max(std::fabs(dx), std::fabs(dz)) / r; break;
    case BrushShape::Diamond: d = (std::fabs(dx) + std::fabs(dz)) / r; break;
    case BrushShape::Count: break;
    }
    if (d >= 1.0f) return 0.0f;
    switch (fall) {
    case BrushFall::Soft: return 0.5f * (1.0f + std::cos(PI_F * d));
    case BrushFall::Linear: return 1.0f - d;
    default: return 1.0f;
    }
}

float noiseAt(int x, int z, unsigned salt) {
    unsigned h = (unsigned)x * 73856093u ^ (unsigned)z * 19349663u ^ salt * 83492791u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return (float)(h & 0xFFFFu) / 32767.5f - 1.0f;
}
}

MapEditorLayout layoutMapEditor(int W, int H) {
    (void)W;
    const float s = uiScaleFor(H);
    const float m = 10.0f * s, gap = 4.0f * s, rowH = 24.0f * s;
    MapEditorLayout L;
    L.panel = Rect{ m, m, 300.0f * s, H - 2.0f * m };
    const float x = L.panel.x + 10.0f * s, w = L.panel.w - 20.0f * s;
    float y = L.panel.y + 34.0f * s;
    L.title = Rect{ x, L.panel.y + 8.0f * s, w, 22.0f * s };
    const float half = (w - gap) * 0.5f;
    for (int i = 0; i < (int)MapTool::Count; ++i) L.tools[i] = Rect{ x + (float)(i % 2) * (half + gap), y + (float)(i / 2) * (rowH + gap), half, rowH };
    y += 4.0f * (rowH + gap) + gap;
    const float third = (w - 2.0f * gap) / 3.0f;
    for (int i = 0; i < (int)BrushShape::Count; ++i) L.shapes[i] = Rect{ x + (float)i * (third + gap), y, third, rowH };
    y += rowH + gap;
    for (int i = 0; i < (int)BrushFall::Count; ++i) L.falls[i] = Rect{ x + (float)i * (third + gap), y, third, rowH };
    y += rowH + gap * 2.0f;
    const float labelW = 78.0f * s;
    L.sizeDown = Rect{ x + labelW, y, rowH, rowH };
    L.sizeBar = Rect{ x + labelW + rowH, y, w - labelW - 2.0f * rowH, rowH };
    L.sizeUp = Rect{ x + w - rowH, y, rowH, rowH };
    y += rowH + gap;
    L.strengthDown = Rect{ x + labelW, y, rowH, rowH };
    L.strengthBar = Rect{ x + labelW + rowH, y, w - labelW - 2.0f * rowH, rowH };
    L.strengthUp = Rect{ x + w - rowH, y, rowH, rowH };
    y += rowH + gap * 3.0f + 16.0f * s;   // the Textures heading
    const float btnH = 30.0f * s;
    const float bottom = L.panel.y + L.panel.h - m;
    L.back = Rect{ x, bottom - btnH, w, btnH };
    L.levels = Rect{ x, L.back.y - gap - btnH, w, btnH };
    L.save = Rect{ x + half + gap, L.levels.y - gap - btnH, half, btnH };
    L.undo = Rect{ x, L.save.y, half, btnH };
    L.status = Rect{ x, L.undo.y - gap - 2.0f * 16.0f * s, w, 2.0f * 16.0f * s };
    const float cellH = std::min(rowH, std::max(14.0f * s, (L.status.y - gap - y) / 9.0f - gap));
    for (int i = 0; i < (int)Biome::Count; ++i) L.biomes.push_back(Rect{ x + (float)(i % 2) * (half + gap), y + (float)(i / 2) * (cellH + gap), half, cellH });
    return L;
}

void App::openMapEditor(const std::string& mapName) {
    const std::optional<MapChoice> map = findMap(listMaps(), MAP_SOURCE_FILE, mapName);
    if (!map) {
        refuse(langf("MISC_MAP_MISSING", { mapName }));
        return;
    }
    refreshPlayedMaps();
    if (worldHasSaves(mapName)) {
        refuse(langf("MISC_MAPED_HAS_SAVES", { mapName }));
        return;
    }
    std::string err;
    renderer.pickLoadingArt();
    LoadingScreen loading(window, renderer);
    const LoadProgress progress = [&](float p, const std::string& what) { loading.show(p, what); };
    std::shared_ptr<MapEdits> edits;
    if (!loadLevelGround(*map, terrain, renderer, err, progress, &edits)) {
        refuse(langf("MISC_LOAD_FAILED", { map->name, err }));
        return;
    }
    audio::stopTracks();
    resetPlayState();
    world = World{};
    currentMap = *map;
    gs = GameState{};
    gs.clockSeconds = 12.0 * SECONDS_PER_HOUR;
    enterLevel();
    editing = true;
    editor = LevelEditorUi{};
    editor.name = mapName;
    mapEditing = true;
    mapEdit = MapEditorUi{};
    mapEdit.edits = edits;
    mapEdit.stem = mapName;
    const Vec2 at = systems_detail::findSpawn(terrain);
    cam.focusX = at.x;
    cam.focusZ = at.z;
    cam.focusY = terrain.heightAt(at.x, at.z);
    primeStreaming(world, terrain, cam);
    loading.fadeOut();
}

// Every chunk that has corners or tiles in this box is made again from the edited source
void App::mapEditorReload(int x0, int z0, int x1, int z1) {
    for (int cz = std::max(0, (z0 - 1) / CHUNK_TILES); cz <= std::min(terrain.chunksPerSide() - 1, z1 / CHUNK_TILES); ++cz) {
        for (int cx = std::max(0, (x0 - 1) / CHUNK_TILES); cx <= std::min(terrain.chunksPerSide() - 1, x1 / CHUNK_TILES); ++cx) terrain.reloadChunk(cx, cz);
    }
}

void App::mapEditorApply(float cx, float cz) {
    MapEditorUi& u = mapEdit;
    const float r = (float)u.radius;
    const int size = terrain.size();
    const int x0 = std::max(0, (int)std::floor(cx - r)), x1 = std::min(size, (int)std::ceil(cx + r));
    const int z0 = std::max(0, (int)std::floor(cz - r)), z1 = std::min(size, (int)std::ceil(cz + r));
    ++u.applyCount;
    const float strength = (float)u.strength * 0.01f;
    if (u.tool == MapTool::Paint || u.tool == MapTool::Unpaint) {
        // Whole tiles: Paint lays its ground type on those the brush reaches, a softer brush reaching in less at its edge; Unpaint gives the
        // generated ground type back
        const float need = u.tool == MapTool::Paint ? 1.0f - strength : 0.0f;
        for (int tz = z0; tz < z1; ++tz) {
            for (int tx = x0; tx < x1; ++tx) {
                const float w = brushWeight(u.shape, u.fall, (float)tx + 0.5f - cx, (float)tz + 0.5f - cz, r);
                if (w <= 0.0f || w < need) continue;
                uint8_t was = 0;
                const bool had = u.edits->tileEdit(tx, tz, was);
                if (u.tool == MapTool::Paint ? (had && was == (uint8_t)u.biome) : !had) continue;
                if (u.seenTiles.insert((long long)tz * 1000003LL + tx).second) u.current.tiles.push_back({ tx, tz, had, was });
                if (u.tool == MapTool::Paint) u.edits->setTile(tx, tz, (uint8_t)u.biome);
                else u.edits->clearTile(tx, tz);
            }
        }
    } else {
        // Corners, read all at once so that a stroke's smoothing does not feed on itself
        const int bw = x1 - x0 + 3;
        std::vector<float> height((size_t)bw * (size_t)(z1 - z0 + 3));
        for (int z = z0 - 1; z <= z1 + 1; ++z) {
            for (int x = x0 - 1; x <= x1 + 1; ++x) {
                height[(size_t)(z - z0 + 1) * bw + (size_t)(x - x0 + 1)] = terrain.heightAt((float)std::clamp(x, 0, size), (float)std::clamp(z, 0, size));
            }
        }
        const auto at = [&](int x, int z) { return height[(size_t)(z - z0 + 1) * bw + (size_t)(x - x0 + 1)]; };
        for (int z = z0; z <= z1; ++z) {
            for (int x = x0; x <= x1; ++x) {
                const float w = brushWeight(u.shape, u.fall, (float)x - cx, (float)z - cz, r);
                if (w <= 0.0f) continue;
                const float h = at(x, z);
                float to = h;
                switch (u.tool) {
                case MapTool::Raise: to = h + strength * 0.25f * w; break;
                case MapTool::Lower: to = h - strength * 0.25f * w; break;
                case MapTool::Smooth: to = h + ((at(x - 1, z) + at(x + 1, z) + at(x, z - 1) + at(x, z + 1)) * 0.25f - h) * std::min(1.0f, strength * w * 0.7f); break;
                case MapTool::Flatten: to = h + (u.flatHeight - h) * std::min(1.0f, strength * w * 1.5f); break;
                case MapTool::Roughen: to = h + noiseAt(x, z, u.applyCount) * strength * 0.3f * w; break;
                case MapTool::Restore: break;
                default: break;
                }
                to = std::clamp(to, MAP_HEIGHT_LEAST, MAP_HEIGHT_MOST);
                float was = 0.0f;
                const bool had = u.edits->cornerEdit(x, z, was);
                if (u.tool == MapTool::Restore) {
                    if (!had || w < 0.2f) continue;
                    if (u.seenCorners.insert((long long)z * 1000003LL + x).second) u.current.corners.push_back({ x, z, true, was });
                    u.edits->clearCorner(x, z);
                    continue;
                }
                if (std::fabs(to - h) < 1e-4f) continue;
                if (u.seenCorners.insert((long long)z * 1000003LL + x).second) u.current.corners.push_back({ x, z, had, was });
                u.edits->setCorner(x, z, to);
            }
        }
    }
    u.dirty = true;
    u.exitWarned = false;
    mapEditorReload(x0, z0, x1, z1);
}

void App::mapEditorEndStroke() {
    MapEditorUi& u = mapEdit;
    u.dragging = false;
    if (!u.current.empty()) {
        u.undo.push_back(std::move(u.current));
        if (u.undo.size() > 60) u.undo.erase(u.undo.begin());
    }
    u.current = MapStroke{};
    u.seenCorners.clear();
    u.seenTiles.clear();
}

void App::mapEditorUndo() {
    MapEditorUi& u = mapEdit;
    if (u.undo.empty()) return;
    const MapStroke s = std::move(u.undo.back());
    u.undo.pop_back();
    int x0 = terrain.size(), z0 = terrain.size(), x1 = 0, z1 = 0;
    const auto grow = [&](int x, int z) {
        x0 = std::min(x0, x);
        z0 = std::min(z0, z);
        x1 = std::max(x1, x);
        z1 = std::max(z1, z);
    };
    for (const MapStroke::Corner& c : s.corners) {
        if (c.had) u.edits->setCorner(c.x, c.z, c.old);
        else u.edits->clearCorner(c.x, c.z);
        grow(c.x, c.z);
    }
    for (const MapStroke::Tile& t : s.tiles) {
        if (t.had) u.edits->setTile(t.x, t.z, t.old);
        else u.edits->clearTile(t.x, t.z);
        grow(t.x, t.z);
    }
    u.dirty = true;
    mapEditorReload(x0, z0, x1 + 1, z1 + 1);
    playSfx(Sfx::UiBack);
}

bool App::mapEditorSave() {
    std::string err;
    if (!writeMapEdits(mapEditsPath(mapEdit.stem), *mapEdit.edits, err)) {
        refuse(err);
        return false;
    }
    mapEdit.dirty = false;
    mapEdit.exitWarned = false;
    showNotice(langf("MISC_MAPED_SAVED", { mapEdit.stem }));
    playSfx(Sfx::UiSaved);
    return true;
}

// Each frame: a stroke under way goes on while the button is held, and a slider being dragged follows the pointer
void App::mapEditorUpdate() {
    MapEditorUi& u = mapEdit;
    float mx = 0.0f, my = 0.0f;
    const bool held = (SDL_GetMouseState(&mx, &my) & SDL_BUTTON_LMASK) != 0;
    const MapEditorLayout L = layoutMapEditor(winW, winH);
    if (u.dragBar >= 0) {
        if (!held) {
            u.dragBar = -1;
            return;
        }
        const Rect& bar = u.dragBar == 0 ? L.sizeBar : L.strengthBar;
        const float t = std::clamp((mx - bar.x) / bar.w, 0.0f, 1.0f);
        if (u.dragBar == 0) u.radius = std::clamp(1 + (int)std::lround(t * (float)(BRUSH_MOST_RADIUS - 1)), 1, BRUSH_MOST_RADIUS);
        else u.strength = std::clamp(1 + (int)std::lround(t * 99.0f), 1, 100);
        return;
    }
    if (!u.dragging) return;
    if (!held) {
        mapEditorEndStroke();
        return;
    }
    if (SDL_GetTicks() - u.lastApply < APPLY_EVERY_MS) return;
    if (const auto hit = pickGround(cam, terrain, mx, my, winW, winH)) {
        u.lastApply = SDL_GetTicks();
        mapEditorApply(hit->first, hit->second);
    }
}

// The brush's outline on the ground, a mark on each tile along its edge
void App::mapEditorPreview(BuildPreview& bp) {
    const MapEditorUi& u = mapEdit;
    if (mouseOverPanels()) return;
    const auto hit = pickGround(cam, terrain, mouseX, mouseY, winW, winH);
    if (!hit) return;
    const float r = (float)u.radius;
    const int x0 = (int)std::floor(hit->first - r), x1 = (int)std::ceil(hit->first + r), z0 = (int)std::floor(hit->second - r), z1 = (int)std::ceil(hit->second + r);
    for (int tz = z0; tz < z1; ++tz) {
        for (int tx = x0; tx < x1; ++tx) {
            const auto inside = [&](int ax, int az) { return brushWeight(u.shape, BrushFall::Hard, (float)ax + 0.5f - hit->first, (float)az + 0.5f - hit->second, r) > 0.0f; };
            if (!inside(tx, tz) || (inside(tx - 1, tz) && inside(tx + 1, tz) && inside(tx, tz - 1) && inside(tx, tz + 1))) continue;
            bp.marks.push_back({ tx, tz, terrain.heightAt((float)tx + 0.5f, (float)tz + 0.5f) + 0.14f, 1 });
        }
    }
}

// ---- The panel

void App::drawMapEditorPanel() {
    MapEditorUi& u = mapEdit;
    const MapEditorLayout L = layoutMapEditor(winW, winH);
    const float s = uiScaleFor(winH), textH = renderer.listTextHeight("Ag");
    const RGBA white{ 1, 1, 1, 1 }, gray{ 0.80f, 0.72f, 0.64f, 1 }, gold{ 1.0f, 0.78f, 0.30f, 1 };
    renderer.drawThemedPanel(L.panel);
    renderer.drawListText(langf("UILABEL_MAPED_TITLE", { u.stem }), L.title.x, L.title.y, gold);
    static const char* const TOOLS[] = { "UILABEL_MAPED_RAISE", "UILABEL_MAPED_LOWER", "UILABEL_MAPED_SMOOTH", "UILABEL_MAPED_FLATTEN", "UILABEL_MAPED_ROUGHEN",
                                         "UILABEL_MAPED_PAINT", "UILABEL_MAPED_UNPAINT", "UILABEL_MAPED_RESTORE" };
    static const char* const SHAPES[] = { "UILABEL_MAPED_CIRCLE", "UILABEL_MAPED_SQUARE", "UILABEL_MAPED_DIAMOND" };
    static const char* const FALLS[] = { "UILABEL_MAPED_SOFT", "UILABEL_MAPED_LINEAR", "UILABEL_MAPED_HARD" };
    for (int i = 0; i < (int)MapTool::Count; ++i) renderer.drawToggleButton(L.tools[i], lang(TOOLS[i]), u.tool == (MapTool)i);
    for (int i = 0; i < (int)BrushShape::Count; ++i) renderer.drawToggleButton(L.shapes[i], lang(SHAPES[i]), u.shape == (BrushShape)i);
    for (int i = 0; i < (int)BrushFall::Count; ++i) renderer.drawToggleButton(L.falls[i], lang(FALLS[i]), u.fall == (BrushFall)i);
    const auto slider = [&](const char* label, const Rect& down, const Rect& bar, const Rect& up, int value, int least, int most) {
        renderer.drawListText(lang(label), down.x - 74.0f * s, down.y + (down.h - textH) * 0.5f, gray);
        renderer.drawButton(down, "-", true);
        renderer.drawField(bar, false);
        renderer.drawRect(Rect{ bar.x, bar.y, bar.w * (float)(value - least) / (float)(most - least), bar.h }, RGBA{ 0.55f, 0.36f, 0.16f, 1.0f });
        renderer.drawListText(std::to_string(value), bar.x + 6.0f * s, bar.y + (bar.h - textH) * 0.5f, white);
        renderer.drawButton(up, "+", true);
    };
    slider("UILABEL_MAPED_SIZE", L.sizeDown, L.sizeBar, L.sizeUp, u.radius, 1, BRUSH_MOST_RADIUS);
    slider("UILABEL_MAPED_STRENGTH", L.strengthDown, L.strengthBar, L.strengthUp, u.strength, 1, 100);
    renderer.drawListText(lang("UILABEL_MAPED_TEXTURES"), L.biomes.front().x, L.biomes.front().y - 16.0f * s, gray);
    for (size_t i = 0; i < L.biomes.size(); ++i) {
        const Rect& r = L.biomes[i];
        const bool on = (int)i == u.biome;
        renderer.drawToggleButton(r, "", on && (u.tool == MapTool::Paint));
        const float sw = r.h * 0.7f;
        renderer.drawRect(Rect{ r.x + 4.0f * s, r.y + (r.h - sw) * 0.5f, sw, sw }, biomeSwatch((int)i));
        renderer.drawListText(renderer.fitListText(lang(std::string("UILABEL_BIOME_") + BIOME_KEYS[i]), r.w - sw - 14.0f * s), r.x + sw + 10.0f * s, r.y + (r.h - textH) * 0.5f,
                              on ? gold : white);
    }
    float y = L.status.y;
    const auto line = [&](const std::string& t, RGBA tint) {
        renderer.drawListText(renderer.fitListText(t, L.status.w), L.status.x, y, tint);
        y += textH + 2.0f * s;
    };
    line(lang("UILABEL_MAPED_HELP"), gray);
    line(u.dirty ? lang("UILABEL_MAPED_UNSAVED") : lang("UILABEL_MAPED_SAVED"), u.dirty ? gold : gray);
    renderer.drawButton(L.undo, lang("UILABEL_MAPED_UNDO"), !u.undo.empty());
    renderer.drawButton(L.save, lang("UILABEL_SAVE"), true);
    renderer.drawButton(L.levels, lang("UILABEL_MAPED_TO_LEVELS"), true);
    renderer.drawButton(L.back, lang(u.exitWarned ? "UILABEL_MAPED_BACK_SURE" : "UILABEL_MAPED_BACK"), true);
}

bool App::mapEditorMouseDown() {
    MapEditorUi& u = mapEdit;
    const float x = e.button.x, y = e.button.y;
    const MapEditorLayout L = layoutMapEditor(winW, winH);
    if (!L.panel.contains(x, y)) {
        if (e.button.button != SDL_BUTTON_LEFT) return false;
        const auto hit = pickGround(cam, terrain, x, y, winW, winH);
        if (!hit) return true;
        u.dragging = true;
        u.flatHeight = terrain.heightAt(hit->first, hit->second);
        u.lastApply = SDL_GetTicks();
        mapEditorApply(hit->first, hit->second);
        return true;
    }
    if (e.button.button != SDL_BUTTON_LEFT) return true;
    for (int i = 0; i < (int)MapTool::Count; ++i) {
        if (L.tools[i].contains(x, y)) {
            u.tool = (MapTool)i;
            playSfx(Sfx::UiTab);
            return true;
        }
    }
    for (int i = 0; i < (int)BrushShape::Count; ++i) {
        if (L.shapes[i].contains(x, y)) {
            u.shape = (BrushShape)i;
            playSfx(Sfx::UiClick);
            return true;
        }
    }
    for (int i = 0; i < (int)BrushFall::Count; ++i) {
        if (L.falls[i].contains(x, y)) {
            u.fall = (BrushFall)i;
            playSfx(Sfx::UiClick);
            return true;
        }
    }
    if (L.sizeBar.contains(x, y)) u.dragBar = 0;
    else if (L.strengthBar.contains(x, y)) u.dragBar = 1;
    else if (L.sizeDown.contains(x, y) || L.sizeUp.contains(x, y)) u.radius = std::clamp(u.radius + (L.sizeUp.contains(x, y) ? 1 : -1), 1, BRUSH_MOST_RADIUS);
    else if (L.strengthDown.contains(x, y) || L.strengthUp.contains(x, y)) u.strength = std::clamp(u.strength + (L.strengthUp.contains(x, y) ? 5 : -5), 1, 100);
    else if (L.undo.contains(x, y)) mapEditorUndo();
    else if (L.save.contains(x, y)) mapEditorSave();
    else if (L.levels.contains(x, y)) {
        if (u.dirty && !mapEditorSave()) return true;
        playSfx(Sfx::UiClick);
        const std::string stem = u.stem;
        fadeOutThen([this, stem] { openLevelEditor(stem); });
    } else if (L.back.contains(x, y)) {
        if (u.dirty && !u.exitWarned) {
            u.exitWarned = true;
            playSfx(Sfx::UiError);
            return true;
        }
        if (u.dirty) mapEditorSave();
        mapEditing = false;
        editing = false;
        gameActive = false;
        audio::stopTracks();
        openWorldGen();
        playSfx(Sfx::UiBack);
    } else {
        for (size_t i = 0; i < L.biomes.size(); ++i) {
            if (!L.biomes[i].contains(x, y)) continue;
            u.biome = (int)i;
            u.tool = MapTool::Paint;
            playSfx(Sfx::UiClick);
            break;
        }
    }
    return true;
}

bool App::mapEditorKeyDown() {
    const SDL_Keycode k = e.key.key;
    if (e.key.repeat) return k == SDLK_ESCAPE || k == SDLK_SPACE;
    if (k == SDLK_Z && (SDL_GetModState() & SDL_KMOD_CTRL) != 0) {
        mapEditorUndo();
        return true;
    }
    if (k == SDLK_S && (SDL_GetModState() & SDL_KMOD_CTRL) != 0) {
        mapEditorSave();
        return true;
    }
    if (k == SDLK_LEFTBRACKET || k == SDLK_RIGHTBRACKET) {
        mapEdit.radius = std::clamp(mapEdit.radius + (k == SDLK_RIGHTBRACKET ? 2 : -2), 1, BRUSH_MOST_RADIUS);
        return true;
    }
    return k == SDLK_ESCAPE || k == SDLK_SPACE;
}
