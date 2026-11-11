#include "app.h"
#include "chunk_source.h"
#include "factions.h"
#include "level_file.h"
#include "world_gen.h"

using namespace play;

void LayoutPlanner::start(std::shared_ptr<const ChunkSource> source) {
    stop();
    cancel_ = false;
    {
        std::lock_guard<std::mutex> hold(lock_);
        done_ = false;
        plan_ = LayoutPlan{};
    }
    // A streamed map only has the ground near the camera to hand, so there is nothing to plan from
    if (!source || source->streamed()) return;
    worker_ = std::thread([this, source]() {
        Terrain ground(source);
        LayoutPlan plan = planRandomLayout(ground, false, &cancel_);
        if (cancel_) return;
        std::lock_guard<std::mutex> hold(lock_);
        plan_ = std::move(plan);
        done_ = true;
    });
}

void LayoutPlanner::stop() {
    cancel_ = true;
    if (worker_.joinable()) worker_.join();
}

bool LayoutPlanner::take(LayoutPlan& out) {
    std::lock_guard<std::mutex> hold(lock_);
    if (!done_) return false;
    done_ = false;
    out = plan_;
    return true;
}

// New Game's first page: the map and the level to start on, with a picture of the map and where the level puts things
MapPickLayout layoutMapPick(int W, int H, int maps, int levels, int mapScroll, int levelScroll) {
    const float s = uiScaleFor((float)H);
    const float pad = 20.0f * s, gap = 16.0f * s, head = 26.0f * s, rowH = 30.0f * s, rowGap = 4.0f * s;
    const float btnH = 36.0f * s, btnW = 170.0f * s;
    MapPickLayout L;
    const float boxW = std::min((float)W - 48.0f * s, 1000.0f * s), boxH = (float)H - 64.0f * s;
    L.box = Rect{ std::floor((W - boxW) * 0.5f), 32.0f * s, boxW, boxH };
    L.title = Rect{ L.box.x + pad, L.box.y + pad, boxW - 2.0f * pad, head };
    L.back = Rect{ L.box.x + pad, L.box.y + boxH - pad - btnH, btnW, btnH };
    L.next = Rect{ L.box.x + boxW - pad - btnW, L.back.y, btnW, btnH };

    const float top = L.title.y + head + 10.0f * s, bottom = L.back.y - gap;
    const float listW = std::min(320.0f * s, boxW * 0.38f);
    const float listX = L.box.x + pad;
    // Maps take the top half of the column and levels the bottom half
    const float half = (bottom - top - gap) * 0.5f;
    auto column = [&](float y, float h, int count, int scroll, Rect& heading, std::vector<Rect>& rows, int& first) {
        heading = Rect{ listX, y, listW, head };
        const float listTop = y + head + 4.0f * s;
        const int visible = std::max(1, (int)std::floor((h - head - 4.0f * s + rowGap) / (rowH + rowGap)));
        first = std::clamp(scroll, 0, std::max(0, count - visible));
        for (int i = first; i < count && i < first + visible; ++i) {
            rows.push_back(Rect{ listX, listTop + (i - first) * (rowH + rowGap), listW, rowH });
        }
    };
    column(top, half, maps, mapScroll, L.mapsHead, L.mapRows, L.mapFirst);
    column(top + half + gap, half, levels, levelScroll, L.levelsHead, L.levelRows, L.levelFirst);
    L.mapsArea = Rect{ listX, top, listW, half };
    L.levelsArea = Rect{ listX, top + half + gap, listW, half };

    const float px = listX + listW + gap + 8.0f * s, pw = L.box.x + boxW - pad - px;
    const float side = std::max(0.0f, std::min(pw, bottom - top - head - 28.0f * s));
    L.previewHead = Rect{ px, top, pw, head };
    L.preview = Rect{ std::floor(px + (pw - side) * 0.5f), top + head + 4.0f * s, side, side };
    L.note = Rect{ px, L.preview.y + side + 4.0f * s, pw, 22.0f * s };
    return L;
}

MapPickLayout App::mapPickLayout() {
    return layoutMapPick(winW, winH, (int)mapPick.maps.size(), (int)mapPick.levels.size(), mapPick.mapScroll, mapPick.levelScroll);
}

void App::openMapPick() {
    mapPick = MapPickUi{};
    mapPick.maps = listMaps();
    pendingLevel.clear();
    screen = Screen::MapSelect;
    if (!mapPick.maps.empty()) pickMap(0);
}

void App::pickMap(int i) {
    if (i < 0 || i >= (int)mapPick.maps.size()) return;
    mapPick.map = i;
    pendingMap = mapPick.maps[(size_t)i];
    mapPick.levels = { std::string() };
    for (const std::string& name : listLevelFiles(pendingMap.stem)) mapPick.levels.push_back(name);
    mapPick.levelScroll = 0;
    pickLevel(0);

    // The picture is made away from the frame loop, from the map's own source of ground
    mapPick.previewReady = false;
    std::string err;
    std::shared_ptr<const ChunkSource> source;
    if (pendingMap.source == MAP_SOURCE_FILE) {
        WorldGenSettings settings;
        if (readMapFile(pendingMap.path, settings, err)) source = generatedChunkSource(settings);
    } else {
        source = worldFolderSource(pendingMap.path, err);
    }
    if (!mapPreview) mapPreview = std::make_unique<OverviewBuilder>();
    if (!layoutPlanner) layoutPlanner = std::make_unique<LayoutPlanner>();
    mapPick.planReady = false;
    mapPick.plan = LayoutPlan{};
    layoutPlanner->start(source);
    if (!source) {
        mapPreview->stop();
        return;
    }
    mapPick.previewSize = source->size();
    mapPreview->start(source->size(), [source](int cx, int cz, ChunkData& c) { return source->fill(cx, cz, c); },
                      [source](int px, std::vector<uint8_t>& rgba, const std::atomic<bool>& cancel) { return source->picture(px, rgba, cancel); },
                      true);
}

void App::pickLevel(int i) {
    if (i < 0 || i >= (int)mapPick.levels.size()) return;
    mapPick.level = i;
    pendingLevel = mapPick.levels[(size_t)i];
    mapPick.levelData = LevelData{};
    mapPick.levelRead = false;
    if (pendingLevel.empty()) return;
    std::string err;
    mapPick.levelRead = readLevelFile(levelFilePath(pendingLevel), mapPick.levelData, err);
}

void App::updateMapPick() {
    if (layoutPlanner && layoutPlanner->take(mapPick.plan)) mapPick.planReady = true;
    OverviewPicture pic;
    if (!mapPreview || !mapPreview->take(pic) || pic.px <= 0) return;
    renderer.setWorldPreview(pic.px, pic.rgba);
    mapPick.previewExtent = pic.extent();
    mapPick.previewReady = true;
}

bool App::mapPickMouseDown() {
    if (screen != Screen::MapSelect || e.button.button != SDL_BUTTON_LEFT) return false;
    const float mx = e.button.x, my = e.button.y;
    const MapPickLayout L = mapPickLayout();
    for (size_t i = 0; i < L.mapRows.size(); ++i) {
        if (!L.mapRows[i].contains(mx, my)) continue;
        if (L.mapFirst + (int)i != mapPick.map) pickMap(L.mapFirst + (int)i);
        playSfx(Sfx::UiClick);
        return true;
    }
    for (size_t i = 0; i < L.levelRows.size(); ++i) {
        if (!L.levelRows[i].contains(mx, my)) continue;
        pickLevel(L.levelFirst + (int)i);
        playSfx(Sfx::UiClick);
        return true;
    }
    if (L.back.contains(mx, my)) {
        if (mapPreview) mapPreview->stop();
        if (layoutPlanner) layoutPlanner->stop();
        performAction(Action::ReturnToTitle, 0);
        return true;
    }
    if (L.next.contains(mx, my) && !mapPick.maps.empty()) {
        if (mapPreview) mapPreview->stop();
        if (layoutPlanner) layoutPlanner->stop();
        playSfx(Sfx::UiClick);
        openParty(pendingMap);
        return true;
    }
    return true;
}

void App::mapPickWheel(float x, float y, float notches) {
    const MapPickLayout L = mapPickLayout();
    const int step = notches > 0.0f ? -1 : notches < 0.0f ? 1 : 0;
    if (L.mapsArea.contains(x, y)) mapPick.mapScroll = std::max(0, L.mapFirst + step);
    if (L.levelsArea.contains(x, y)) mapPick.levelScroll = std::max(0, L.levelFirst + step);
}

void App::drawMapPickScreen() {
    const float s = uiScaleFor(winH);
    const MapPickLayout L = mapPickLayout();
    mapPick.mapScroll = L.mapFirst;
    mapPick.levelScroll = L.levelFirst;
    const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f };
    const RGBA gray{ 0.80f, 0.72f, 0.64f, 1.0f };
    const RGBA accent{ 1.0f, 0.78f, 0.30f, 1.0f };
    renderer.beginUi(winW, winH);
    drawMenuBackdrop();
    renderer.drawThemedPanel(L.box);
    renderer.drawListText(lang("UILABEL_MAP_PICK_TITLE"), L.title.x, L.title.y, accent);
    renderer.drawListText(lang("UILABEL_MAP_PICK_MAPS"), L.mapsHead.x, L.mapsHead.y, gray);
    for (size_t i = 0; i < L.mapRows.size(); ++i) {
        const int at = L.mapFirst + (int)i;
        const bool picked = at == mapPick.map;
        const Rect& r = L.mapRows[i];
        renderer.drawButton(r, (picked ? "> " : "") + mapPick.maps[(size_t)at].name, true, picked || r.contains(mouseX, mouseY));
    }
    renderer.drawListText(lang("UILABEL_MAP_PICK_LEVELS"), L.levelsHead.x, L.levelsHead.y, gray);
    for (size_t i = 0; i < L.levelRows.size(); ++i) {
        const int at = L.levelFirst + (int)i;
        const bool picked = at == mapPick.level;
        const Rect& r = L.levelRows[i];
        const std::string& name = mapPick.levels[(size_t)at];
        renderer.drawButton(r, (picked ? "> " : "") + (name.empty() ? lang("UILABEL_LEVEL_RANDOM") : name), true, picked || r.contains(mouseX, mouseY));
    }

    renderer.drawListText(lang("UILABEL_GEN_PREVIEW"), L.previewHead.x, L.previewHead.y, gray);
    renderer.drawRect(L.preview, RGBA{ 0.0f, 0.0f, 0.0f, 0.35f });
    const float th = renderer.listTextHeight("Ag");
    if (!mapPick.previewReady) {
        const std::string wait = lang("UILABEL_MAP_PICK_LOADING");
        renderer.drawListText(wait, L.preview.x + (L.preview.w - renderer.listTextWidth(wait)) * 0.5f, L.preview.y + (L.preview.h - th) * 0.5f, gray);
    } else {
        renderer.drawWorldPreview(L.preview);
        // What the level places, over the picture: your units, other units, mobs, props, and where you start
        const float k = L.preview.w / std::max(1.0f, mapPick.previewExtent);
        auto dot = [&](float x, float z, float side, RGBA c) {
            const float sx = L.preview.x + x * k, sy = L.preview.y + z * k;
            if (sx < L.preview.x || sy < L.preview.y || sx > L.preview.x + L.preview.w || sy > L.preview.y + L.preview.h) return;
            renderer.drawRect(Rect{ sx - side * 0.5f - 1.0f, sy - side * 0.5f - 1.0f, side + 2.0f, side + 2.0f }, RGBA{ 0.0f, 0.0f, 0.0f, 0.8f });
            renderer.drawRect(Rect{ sx - side * 0.5f, sy - side * 0.5f, side, side }, c);
        };
        if (mapPick.levelRead) {
            const float d = std::max(3.0f, 4.0f * s);
            for (const LevelObject& o : mapPick.levelData.objects) {
                if (o.type == LevelObject::Type::Prop) dot(o.x, o.z, d * 0.8f, RGBA{ 0.70f, 0.45f, 0.95f, 1.0f });
            }
            for (const LevelObject& o : mapPick.levelData.objects) {
                if (o.type == LevelObject::Type::Mob) dot(o.x, o.z, d, RGBA{ 0.75f, 0.75f, 0.75f, 1.0f });
                if (o.type == LevelObject::Type::Unit) {
                    dot(o.x, o.z, d, o.kind == FACTION_PLAYER ? RGBA{ 0.3f, 1.0f, 0.3f, 1.0f } : RGBA{ 1.0f, 0.4f, 0.3f, 1.0f });
                }
            }
            if (mapPick.levelData.hasSpawn) dot(mapPick.levelData.spawnX, mapPick.levelData.spawnZ, 9.0f * s, accent);
        }
        // A random set-up: each faction's camp, with the room it has round it, and where you start
        if (pendingLevel.empty() && mapPick.planReady) {
            for (const CampPlan& c : mapPick.plan.camps) {
                const FactionColor fc = factionColor(c.faction);
                const float side = std::max(6.0f, 2.0f * CAMP_ROOM * k);
                const float sx = L.preview.x + c.x * k, sy = L.preview.y + c.z * k;
                renderer.drawRect(Rect{ sx - side * 0.5f, sy - side * 0.5f, side, side }, RGBA{ fc.r, fc.g, fc.b, c.roomy ? 0.35f : 0.18f });
                dot(c.x, c.z, std::max(4.0f, 6.0f * s), RGBA{ fc.r, fc.g, fc.b, 1.0f });
            }
            dot(mapPick.plan.spawnX, mapPick.plan.spawnZ, 9.0f * s, accent);
        }
    }
    const std::string note = pendingLevel.empty() ? (mapPick.planReady ? lang("UILABEL_MAP_PICK_CAMPS_NOTE") : lang("UILABEL_MAP_PICK_RANDOM_NOTE"))
                           : mapPick.levelRead ? lang("UILABEL_MAP_PICK_LEVEL_NOTE") : lang("UILABEL_MAP_PICK_LEVEL_BAD");
    float noteY = L.note.y;
    for (const std::string& line : renderer.wrapListText(note, L.note.w)) {
        renderer.drawListText(line, L.note.x, noteY, gray);
        noteY += th;
    }

    renderer.drawButton(L.back, lang("UILABEL_BACK"), true, L.back.contains(mouseX, mouseY));
    renderer.drawButton(L.next, lang("UILABEL_NEXT"), !mapPick.maps.empty(), L.next.contains(mouseX, mouseY));
    (void)white;
    renderer.endUi();
}
