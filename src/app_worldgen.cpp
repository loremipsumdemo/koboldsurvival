#include "app.h"
#include <filesystem>
#include <fstream>
#include "spatial.h"
#include "systems_internal.h"

using namespace play;

// ---- Layouts

WorldGenLayout layoutWorldGen(int W, int H, int mapCount, int mapScroll) {
    const float s = uiScaleFor(H);
    const float m = 16.0f * s, rowH = 32.0f * s, gap = 8.0f * s;
    WorldGenLayout L;
    L.panel = Rect{ m, m, 430.0f * s, H - 2.0f * m };
    const float x = L.panel.x + 14.0f * s, w = L.panel.w - 28.0f * s, labelW = 130.0f * s;
    const float tabW = 84.0f * s;
    L.tabTheme = Rect{ x + w - tabW, L.panel.y + 10.0f * s, tabW, rowH };
    L.tabWorld = Rect{ L.tabTheme.x - tabW - gap, L.tabTheme.y, tabW, rowH };
    float y = L.panel.y + 50.0f * s;
    L.nameBox = Rect{ x + labelW, y, w - labelW, rowH };
    y += rowH + gap;
    L.seedRandom = Rect{ x + w - 90.0f * s, y, 90.0f * s, rowH };
    L.seedBox = Rect{ x + labelW, y, w - labelW - 98.0f * s, rowH };
    y += rowH + gap;
    L.sizeDown = Rect{ x + labelW, y, rowH, rowH };
    L.sizeBox = Rect{ x + labelW + rowH, y, w - labelW - 2.0f * rowH, rowH };
    L.sizeUp = Rect{ x + w - rowH, y, rowH, rowH };
    y += rowH + gap;
    L.styleDown = Rect{ x + labelW, y, rowH, rowH };
    L.styleBox = Rect{ x + labelW + rowH, y, w - labelW - 2.0f * rowH, rowH };
    L.styleUp = Rect{ x + w - rowH, y, rowH, rowH };
    y += rowH + gap * 0.5f;
    L.styleAbout = Rect{ x, y, w, 22.0f * s };
    y += 22.0f * s + gap;
    for (int i = 0; i < WORLD_GEN_ROW_COUNT; ++i) {
        L.stepDown.push_back(Rect{ x + labelW, y, rowH, rowH });
        L.bars.push_back(Rect{ x + labelW + rowH, y, w - labelW - 2.0f * rowH, rowH });
        L.stepUp.push_back(Rect{ x + w - rowH, y, rowH, rowH });
        y += rowH + gap;
    }
    const float btnH = 44.0f * s;
    L.back = Rect{ x, L.panel.y + L.panel.h - btnH - 14.0f * s, w, btnH };
    L.themeArea = Rect{ x, L.panel.y + 50.0f * s, w, L.back.y - gap - (L.panel.y + 50.0f * s) };

    const float rightW = 250.0f * s;
    const float areaX = L.panel.x + L.panel.w + m, areaW = W - areaX - m - rightW - m;
    const float side = std::min(areaW, H - 2.0f * m - btnH * 1.0f - 40.0f * s);
    L.preview = Rect{ areaX + (areaW - side) * 0.5f, m + 30.0f * s, side, side };
    L.previewLabel = Rect{ L.preview.x, m, side, 26.0f * s };
    const float halfW = (side - gap) * 0.5f, btnY = L.preview.y + side + 8.0f * s;
    L.generate = Rect{ L.preview.x, btnY, halfW, btnH };
    L.proceed = Rect{ L.preview.x + halfW + gap, btnY, halfW, btnH };

    L.mapsBox = Rect{ W - m - rightW, m, rightW, H - 2.0f * m };
    const float listRow = 30.0f * s;
    const int shown = std::max(0, (int)((L.mapsBox.h - 56.0f * s) / (listRow + 4.0f * s)));
    const int first = std::clamp(mapScroll, 0, std::max(0, mapCount - shown));
    for (int i = first; i < mapCount && i < first + shown; ++i) {
        L.mapRows.push_back(Rect{ L.mapsBox.x + 10.0f * s, L.mapsBox.y + 44.0f * s + (i - first) * (listRow + 4.0f * s), rightW - 20.0f * s, listRow });
    }
    return L;
}

EditorLayout layoutEditor(int W, int H, bool ownUi) {
    const float s = uiScaleFor(H);
    const float m = 10.0f * s, gap = 6.0f * s, rowH = 26.0f * s, btnH = 32.0f * s;
    EditorLayout L;
    L.uiToggle = Rect{ (W - 240.0f * s) * 0.5f, 8.0f * s, 240.0f * s, 30.0f * s };
    if (!ownUi) return L;
    L.panel = Rect{ m, m, 300.0f * s, H - 2.0f * m };
    const float x = L.panel.x + 10.0f * s, w = L.panel.w - 20.0f * s;
    float y = L.panel.y + 40.0f * s;
    L.nameBox = Rect{ x, y, w, btnH };
    y += btnH + gap;
    const float half = (w - gap) * 0.5f;
    L.save = Rect{ x, y, half, btnH };
    L.load = Rect{ x + half + gap, y, half, btnH };
    y += btnH + gap * 2.0f;
    L.spawnTool = Rect{ x, y, half, btnH };
    L.eraseTool = Rect{ x + half + gap, y, half, btnH };
    y += btnH + gap * 2.0f;
    const float third = (w - 2.0f * gap) / 3.0f;
    for (int i = 0; i < 3; ++i) L.tabs[i] = Rect{ x + i * (third + gap), y, third, btnH };
    y += btnH + gap;
    L.tabs[3] = Rect{ x, y, w, btnH };
    y += btnH + gap;
    const float bottom = L.panel.y + L.panel.h - 10.0f * s;
    L.exit = Rect{ x, bottom - btnH, w, btnH };
    L.status = Rect{ x, bottom - btnH - gap - 96.0f * s, w, 96.0f * s };
    L.themeArea = Rect{ x, y, w, L.status.y - gap - y };
    const int shown = std::max(1, (int)((L.status.y - gap - y) / (rowH + 2.0f * s)));
    for (int i = 0; i < shown; ++i) L.rows.push_back(Rect{ x, y + i * (rowH + 2.0f * s), w, rowH });
    return L;
}

ThemeLayout layoutThemePanel(const Rect& area, const ThemeUi& ui, float s) {
    const float rowH = 25.0f * s, gap = 3.0f * s, labelW = area.w * 0.52f;
    ThemeLayout L;
    L.presetDown = Rect{ area.x + labelW, area.y, rowH, rowH };
    L.presetBox = Rect{ area.x + labelW + rowH, area.y, area.w - labelW - 2.0f * rowH, rowH };
    L.presetUp = Rect{ area.x + area.w - rowH, area.y, rowH, rowH };
    struct Entry { int row, channel; };
    std::vector<Entry> entries;
    for (int i = 0; i < themeRowCount(); ++i) {
        entries.push_back({ i, -1 });
        if (i == ui.openColor) {
            for (int c = 0; c < 3; ++c) entries.push_back({ i, c });
        }
    }
    const float top = area.y + rowH + gap * 2.0f;
    L.total = (int)entries.size();
    L.shown = std::max(1, (int)((area.y + area.h - top) / (rowH + gap)));
    L.first = std::clamp(ui.scroll, 0, std::max(0, L.total - L.shown));
    for (int i = L.first; i < L.total && i < L.first + L.shown; ++i) {
        ThemeLine line;
        line.row = entries[(size_t)i].row;
        line.channel = entries[(size_t)i].channel;
        line.box = Rect{ area.x, top + (i - L.first) * (rowH + gap), area.w, rowH };
        line.control = Rect{ area.x + labelW, line.box.y, area.w - labelW, rowH };
        L.lines.push_back(line);
    }
    return L;
}

// ---- The Theme tab

MapTheme& App::themeBeingEdited() {
    return editing ? editor.map.theme : worldGenUi.settings.theme;
}

int& App::themePresetBeingEdited() {
    return editing ? editor.map.themePreset : worldGenUi.settings.themePreset;
}

void App::themeEdited() {
    if (editing) {
        renderer.setMapTheme(editor.map.theme);
        editor.exitWarned = false;
    } else {
        worldGenChanged();
    }
}

void App::drawThemePanel(const Rect& area) {
    const float s = uiScaleFor(winH);
    const ThemeLayout L = layoutThemePanel(area, themeUi, s);
    themeUi.scroll = L.first;
    const MapTheme& theme = themeBeingEdited();
    const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f }, gray{ 0.80f, 0.72f, 0.64f, 1.0f }, gold{ 1.0f, 0.78f, 0.30f, 1.0f };
    const RGBA well{ 0.14f, 0.09f, 0.08f, 1.0f }, fill{ 0.55f, 0.36f, 0.16f, 1.0f };
    const float textH = renderer.listTextHeight("Ag");
    const auto middle = [&](const Rect& r) { return r.y + (r.h - textH) * 0.5f; };
    renderer.drawListText(lang("UILABEL_THEME_PRESET"), area.x, middle(L.presetBox), gray);
    renderer.drawButton(L.presetDown, "<", true, L.presetDown.contains(mouseX, mouseY));
    renderer.drawRect(L.presetBox, well);
    const std::string presetName = lang(themePresets()[themePresetBeingEdited()].labelKey);
    renderer.drawListText(renderer.fitListText(presetName, L.presetBox.w - 8.0f * s), L.presetBox.x + 4.0f * s, middle(L.presetBox), white);
    renderer.drawButton(L.presetUp, ">", true, L.presetUp.contains(mouseX, mouseY));

    for (const ThemeLine& line : L.lines) {
        const ThemeRow& row = themeRows()[line.row];
        const Rect& c = line.control;
        const auto bar = [&](int value, int low, int high) {
            renderer.drawRect(c, well);
            renderer.drawRect(Rect{ c.x, c.y, c.w * (float)(value - low) / (float)std::max(1, high - low), c.h }, fill);
            renderer.drawListText(std::to_string(value), c.x + 6.0f * s, middle(c), white);
        };
        if (line.channel >= 0) {
            static const char* const CHANNELS[3] = { "UILABEL_THEME_RED", "UILABEL_THEME_GREEN", "UILABEL_THEME_BLUE" };
            renderer.drawListText(lang(CHANNELS[line.channel]), line.box.x + 18.0f * s, middle(line.box), gray);
            bar((int)((theme.*row.color >> (16 - line.channel * 8)) & 255u), 0, 255);
            continue;
        }
        if (row.kind == ThemeRowKind::Heading) {
            renderer.drawListText(lang(row.labelKey), line.box.x, middle(line.box), gold);
            continue;
        }
        renderer.drawListText(renderer.fitListText(lang(row.labelKey), c.x - line.box.x - 6.0f * s), line.box.x + 6.0f * s, middle(line.box), gray);
        switch (row.kind) {
        case ThemeRowKind::Number:
            bar(theme.*row.number, row.low, row.high);
            break;
        case ThemeRowKind::Toggle:
            renderer.drawButton(c, lang(theme.*row.number ? "UILABEL_THEME_ON" : "UILABEL_THEME_OFF"), true, c.contains(mouseX, mouseY));
            break;
        case ThemeRowKind::Choice:
            renderer.drawButton(c, lang(skyKindLabelKey(theme.*row.number)), true, c.contains(mouseX, mouseY));
            break;
        case ThemeRowKind::Color: {
            float rgb[3];
            themeRgb(theme.*row.color, rgb);
            renderer.drawRect(c, themeUi.openColor == line.row ? gold : well);
            renderer.drawRect(Rect{ c.x + 2.0f * s, c.y + 2.0f * s, c.w - 4.0f * s, c.h - 4.0f * s }, RGBA{ rgb[0], rgb[1], rgb[2], 1.0f });
            break;
        }
        default:
            break;
        }
    }
}

void App::themeDragAt(float x) {
    const float s = uiScaleFor(winH);
    const Rect area = editing ? layoutEditor(winW, winH, true).themeArea : layoutWorldGen(winW, winH, (int)worldGenUi.maps.size(), worldGenUi.mapScroll).themeArea;
    const float labelW = area.w * 0.52f, controlX = area.x + labelW, controlW = area.w - labelW;
    (void)s;
    const float share = std::clamp((x - controlX) / controlW, 0.0f, 1.0f);
    const ThemeRow& row = themeRows()[themeUi.dragRow];
    MapTheme& theme = themeBeingEdited();
    if (themeUi.dragChannel >= 0) {
        const int shift = 16 - themeUi.dragChannel * 8;
        const ThemeColor value = (ThemeColor)std::lround(share * 255.0f);
        const ThemeColor next = (theme.*row.color & ~(255u << shift)) | (value << shift);
        if (next == theme.*row.color) return;
        theme.*row.color = next;
    } else {
        const int next = row.low + (int)std::lround(share * (float)(row.high - row.low));
        if (next == theme.*row.number) return;
        theme.*row.number = next;
    }
    themeEdited();
}

bool App::themeClick(const Rect& area, float x, float y) {
    const ThemeLayout L = layoutThemePanel(area, themeUi, uiScaleFor(winH));
    if (L.presetDown.contains(x, y) || L.presetUp.contains(x, y)) {
        int& preset = themePresetBeingEdited();
        preset = (preset + (L.presetUp.contains(x, y) ? 1 : themePresetCount() - 1)) % themePresetCount();
        themeBeingEdited() = themePresets()[preset].theme;
        themeEdited();
        playSfx(Sfx::UiClick);
        return true;
    }
    for (const ThemeLine& line : L.lines) {
        if (!line.box.contains(x, y)) continue;
        const ThemeRow& row = themeRows()[line.row];
        MapTheme& theme = themeBeingEdited();
        if (line.channel < 0 && row.kind == ThemeRowKind::Color) {
            themeUi.openColor = themeUi.openColor == line.row ? -1 : line.row;
            playSfx(Sfx::UiClick);
        } else if (line.channel < 0 && (row.kind == ThemeRowKind::Toggle || row.kind == ThemeRowKind::Choice)) {
            theme.*row.number = theme.*row.number >= row.high ? row.low : theme.*row.number + 1;
            themeEdited();
            playSfx(Sfx::UiToggle);
        } else if (line.channel >= 0 || row.kind == ThemeRowKind::Number) {
            themeUi.dragRow = line.row;
            themeUi.dragChannel = line.channel;
            themeDragAt(x);
        }
        return true;
    }
    return false;
}

// ---- World Generation screen

void App::openWorldGen() {
    if (worldGenUi.settings.seed == 1) worldGenUi.settings.seed = (uint32_t)SDL_GetTicksNS() | 1u;
    worldGenUi.maps = listMapFiles();
    worldGenUi.previewDirty = true;
    worldGenUi.dragRow = -1;
    screen = Screen::WorldGen;
}

void App::stopWorldGenName() {
    if (worldGenUi.editingName) SDL_StopTextInput(window);
    worldGenUi.editingName = false;
}

void App::worldGenChanged() {
    worldGenUi.previewDirty = true;
    worldGenUi.generated = false;
}

void App::drawWorldGenScreen() {
    WorldGenUi& ui = worldGenUi;
    const WorldGenLayout L = layoutWorldGen(winW, winH, (int)ui.maps.size(), ui.mapScroll);
    const float s = uiScaleFor(winH);
    // At most ten times a second, so dragging a bar stays smooth
    if (ui.previewDirty && SDL_GetTicks() - ui.previewAt >= 100) {
        renderer.setWorldPreview(WORLD_GEN_PREVIEW_PX, WorldGenerator(ui.settings).preview(WORLD_GEN_PREVIEW_PX));
        ui.previewDirty = false;
        ui.previewAt = SDL_GetTicks();
    }
    const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f }, gray{ 0.80f, 0.72f, 0.64f, 1.0f }, gold{ 1.0f, 0.78f, 0.30f, 1.0f };
    renderer.beginUi(winW, winH);
    drawMenuBackdrop();
    renderer.drawThemedPanel(L.panel);
    renderer.drawListText(lang("UILABEL_WORLD_GENERATION"), L.panel.x + 14.0f * s, L.panel.y + 14.0f * s, gold);
    const float textH = renderer.listTextHeight("Ag");
    const auto label = [&](const std::string& text, const Rect& row) {
        renderer.drawListText(text, L.panel.x + 14.0f * s, row.y + (row.h - textH) * 0.5f, gray);
    };
    const auto field = [&](const Rect& r, const std::string& text, bool active) {
        renderer.drawRect(r, active ? RGBA{ 0.30f, 0.20f, 0.12f, 1.0f } : RGBA{ 0.14f, 0.09f, 0.08f, 1.0f });
        renderer.drawListText(text, r.x + 8.0f * s, r.y + (r.h - textH) * 0.5f, white);
    };
    renderer.drawButton(L.tabWorld, (!ui.themeTab ? "> " : "") + lang("UILABEL_GEN_TAB_WORLD"), true, L.tabWorld.contains(mouseX, mouseY));
    renderer.drawButton(L.tabTheme, (ui.themeTab ? "> " : "") + lang("UILABEL_GEN_TAB_THEME"), true, L.tabTheme.contains(mouseX, mouseY));
    if (ui.themeTab) drawThemePanel(L.themeArea);
    if (!ui.themeTab) {
    label(lang("UILABEL_NAME"), L.nameBox);
    field(L.nameBox, ui.name + (ui.editingName ? "|" : ""), ui.editingName);
    label(lang("UILABEL_GEN_SEED"), L.seedBox);
    field(L.seedBox, std::to_string(ui.settings.seed), false);
    renderer.drawButton(L.seedRandom, lang("UILABEL_GEN_RANDOM"), true, L.seedRandom.contains(mouseX, mouseY));
    label(lang("UILABEL_GEN_SIZE"), L.sizeBox);
    renderer.drawButton(L.sizeDown, "-", true, L.sizeDown.contains(mouseX, mouseY));
    field(L.sizeBox, std::to_string(ui.settings.size) + " x " + std::to_string(ui.settings.size), false);
    renderer.drawButton(L.sizeUp, "+", true, L.sizeUp.contains(mouseX, mouseY));
    const MapStyleInfo& style = mapStyleInfo(ui.settings.style);
    label(lang("UILABEL_GEN_STYLE"), L.styleBox);
    renderer.drawButton(L.styleDown, "<", true, L.styleDown.contains(mouseX, mouseY));
    field(L.styleBox, renderer.fitListText(lang(style.labelKey), L.styleBox.w - 16.0f * s), false);
    renderer.drawButton(L.styleUp, ">", true, L.styleUp.contains(mouseX, mouseY));
    renderer.drawListText(renderer.fitListText(lang(style.aboutKey), L.styleAbout.w), L.styleAbout.x, L.styleAbout.y, gray);
    for (int i = 0; i < WORLD_GEN_ROW_COUNT; ++i) {
        const int value = ui.settings.*WORLD_GEN_ROWS[i].value;
        label(lang(WORLD_GEN_ROWS[i].labelKey), L.bars[(size_t)i]);
        renderer.drawButton(L.stepDown[(size_t)i], "-", true, L.stepDown[(size_t)i].contains(mouseX, mouseY));
        const Rect& bar = L.bars[(size_t)i];
        renderer.drawRect(bar, RGBA{ 0.14f, 0.09f, 0.08f, 1.0f });
        renderer.drawRect(Rect{ bar.x, bar.y, bar.w * (float)value / 100.0f, bar.h }, RGBA{ 0.55f, 0.36f, 0.16f, 1.0f });
        renderer.drawListText(std::to_string(value), bar.x + 8.0f * s, bar.y + (bar.h - textH) * 0.5f, white);
        renderer.drawButton(L.stepUp[(size_t)i], "+", true, L.stepUp[(size_t)i].contains(mouseX, mouseY));
    }
    }
    renderer.drawButton(L.back, lang("UILABEL_RETURN_TO_TITLE"), true, L.back.contains(mouseX, mouseY));

    renderer.drawListText(ui.generated ? langf("UILABEL_GEN_DONE", { ui.generatedName, std::to_string(mapFileText(ui.settings).size()) })
                                       : lang("UILABEL_GEN_PREVIEW"), L.previewLabel.x, L.previewLabel.y, gray);
    renderer.drawWorldPreview(L.preview);
    renderer.drawButton(L.generate, lang("UILABEL_GEN_GENERATE"), !safeFileName(ui.name).empty(), L.generate.contains(mouseX, mouseY));
    renderer.drawButton(L.proceed, lang("UILABEL_GEN_CONTINUE"), ui.generated, L.proceed.contains(mouseX, mouseY));

    renderer.drawThemedPanel(L.mapsBox);
    renderer.drawListText(lang("UILABEL_GEN_MAPS"), L.mapsBox.x + 10.0f * s, L.mapsBox.y + 12.0f * s, gray);
    const int first = std::clamp(ui.mapScroll, 0, std::max(0, (int)ui.maps.size() - (int)L.mapRows.size()));
    for (size_t i = 0; i < L.mapRows.size(); ++i) {
        renderer.drawListButton(L.mapRows[i], renderer.fitListText(ui.maps[(size_t)first + i], L.mapRows[i].w - 12.0f * s), L.mapRows[i].contains(mouseX, mouseY));
    }
    renderer.endUi();
}

void App::worldGenBarAt(int row, float x) {
    const WorldGenLayout L = layoutWorldGen(winW, winH, (int)worldGenUi.maps.size(), worldGenUi.mapScroll);
    const Rect& bar = L.bars[(size_t)row];
    const int value = std::clamp((int)std::lround((x - bar.x) / bar.w * 100.0f), 0, 100);
    int& held = worldGenUi.settings.*WORLD_GEN_ROWS[row].value;
    if (held == value) return;
    held = value;
    worldGenChanged();
}

void App::worldGenClick(float x, float y) {
    WorldGenUi& ui = worldGenUi;
    const WorldGenLayout L = layoutWorldGen(winW, winH, (int)ui.maps.size(), ui.mapScroll);
    if (L.tabWorld.contains(x, y) || L.tabTheme.contains(x, y)) {
        ui.themeTab = L.tabTheme.contains(x, y);
        stopWorldGenName();
        playSfx(Sfx::UiTab);
        return;
    }
    if (ui.themeTab && L.themeArea.contains(x, y)) {
        themeClick(L.themeArea, x, y);
        return;
    }
    if (!ui.themeTab && L.nameBox.contains(x, y)) {
        if (!ui.editingName) SDL_StartTextInput(window);
        ui.editingName = true;
        playSfx(Sfx::UiClick);
        return;
    }
    stopWorldGenName();
    const auto stepSize = [&](int by) {
        const int count = (int)std::size(WORLD_GEN_SIZES);
        int at = 0;
        for (int i = 0; i < count; ++i) {
            if (WORLD_GEN_SIZES[i] == ui.settings.size) at = i;
        }
        ui.settings.size = WORLD_GEN_SIZES[std::clamp(at + by, 0, count - 1)];
        worldGenChanged();
        playSfx(Sfx::UiClick);
    };
    const auto stepStyle = [&](int by) {
        const int count = (int)MapStyle::Count;
        ui.settings.style = (ui.settings.style + by + count) % count;
        ui.settings.themePreset = mapStyleInfo(ui.settings.style).themePreset;
        ui.settings.theme = themePresets()[ui.settings.themePreset].theme;
        worldGenChanged();
        playSfx(Sfx::UiClick);
    };
    if (!ui.themeTab && L.styleDown.contains(x, y)) {
        stepStyle(-1);
    } else if (!ui.themeTab && L.styleUp.contains(x, y)) {
        stepStyle(1);
    } else if (ui.themeTab && !L.back.contains(x, y) && !L.generate.contains(x, y) && !L.proceed.contains(x, y) && L.panel.contains(x, y)) {
        return;
    } else if (L.seedRandom.contains(x, y)) {
        ui.settings.seed = partyRng.gen();
        worldGenChanged();
        playSfx(Sfx::UiClick);
    } else if (L.sizeDown.contains(x, y)) {
        stepSize(-1);
    } else if (L.sizeUp.contains(x, y)) {
        stepSize(1);
    } else if (L.back.contains(x, y)) {
        playSfx(Sfx::UiBack);
        screen = Screen::Title;
    } else if (L.generate.contains(x, y)) {
        generateWorld();
    } else if (L.proceed.contains(x, y) && ui.generated) {
        playSfx(Sfx::UiClick);
        openLevelEditor(ui.generatedName);
    } else {
        for (int i = 0; i < WORLD_GEN_ROW_COUNT && !ui.themeTab; ++i) {
            int& value = ui.settings.*WORLD_GEN_ROWS[i].value;
            if (L.stepDown[(size_t)i].contains(x, y) || L.stepUp[(size_t)i].contains(x, y)) {
                value = std::clamp(value + (L.stepUp[(size_t)i].contains(x, y) ? 1 : -1), 0, 100);
                worldGenChanged();
                playSfx(Sfx::UiClick);
                return;
            }
            if (L.bars[(size_t)i].contains(x, y)) {
                ui.dragRow = i;
                worldGenBarAt(i, x);
                return;
            }
        }
        const int first = std::clamp(ui.mapScroll, 0, std::max(0, (int)ui.maps.size() - (int)L.mapRows.size()));
        for (size_t i = 0; i < L.mapRows.size(); ++i) {
            if (!L.mapRows[i].contains(x, y)) continue;
            const std::string name = ui.maps[(size_t)first + i];
            std::string err;
            WorldGenSettings opened;
            if (!readMapFile(mapFilePath(name), opened, err)) {
                refuse(err);
                return;
            }
            ui.settings = opened;
            ui.name = name;
            ui.previewDirty = true;
            ui.generated = true;
            ui.generatedName = name;
            playSfx(Sfx::UiClick);
            return;
        }
    }
}

bool App::generateWorld() {
    WorldGenUi& ui = worldGenUi;
    const std::string name = safeFileName(ui.name);
    if (name.empty()) {
        refuse(lang("MISC_GEN_NEEDS_NAME"));
        return false;
    }
    ui.name = name;
    std::string err;
    if (!writeMapFile(mapFilePath(name), ui.settings, err)) {
        refuse(err);
        return false;
    }
    ui.generated = true;
    ui.generatedName = name;
    ui.maps = listMapFiles();
    playSfx(Sfx::UiSaved);
    return true;
}

// ---- Level editor

void App::openLevelEditor(const std::string& mapName) {
    const std::optional<MapChoice> map = findMap(listMaps(), MAP_SOURCE_FILE, mapName);
    if (!map) {
        refuse(langf("MISC_MAP_MISSING", { mapName }));
        return;
    }
    std::string err;
    const LoadProgress progress = [&](float p, const std::string& what) { loadingFrame(window, renderer, p, std::string(), what); };
    if (!loadLevelGround(*map, terrain, renderer, err, progress)) {
        refuse(langf("MISC_LOAD_FAILED", { map->name, err }));
        return;
    }
    audio::stopTracks();
    resetPlayState();
    rng.gen.seed((unsigned)std::time(nullptr));
    world = World{};
    currentMap = *map;
    gs = GameState{};
    gs.clockSeconds = 12.0 * SECONDS_PER_HOUR;   // midday, to see the ground by
    enterLevel();
    editing = true;
    editor = LevelEditorUi{};
    editor.name = mapName;
    if (readMapFile(map->path, editor.map, err)) editor.savedMapText = mapFileText(editor.map);
    themeUi = ThemeUi{};
    world.resource<LevelInfo>();
    const std::string path = levelFilePath(editor.name);
    LevelData loaded;
    if (std::filesystem::exists(std::filesystem::u8path(path)) && readLevelFile(path, loaded, err) && loaded.map == mapName) {
        applyLevel(world, terrain, rng, loaded);
    }
    editor.savedText = levelToText(captureLevel(world, mapName));
    editorFocusCamera();
    primeStreaming(world, terrain, cam);
}

void App::editorFocusCamera() {
    const LevelInfo& info = world.resource<LevelInfo>();
    Vec2 at{ info.spawnX, info.spawnZ };
    if (!info.spawnSet) at = systems_detail::findSpawn(terrain);
    cam.focusX = at.x;
    cam.focusZ = at.z;
    cam.focusY = terrain.heightAt(at.x, at.z);
}

void App::editorEdited() {
    editor.exitWarned = false;
}

void App::editorRemember() {
    editor.undo.push_back(levelToText(captureLevel(world, currentMap.stem)));
    constexpr size_t MOST_UNDO = 100;
    if (editor.undo.size() > MOST_UNDO) editor.undo.erase(editor.undo.begin());
}

void App::editorUndo() {
    if (editor.undo.empty()) return;
    LevelData d;
    std::string err;
    if (levelFromText(editor.undo.back(), d, err)) applyLevel(world, terrain, rng, d);
    editor.undo.pop_back();
    hoverProp = NULL_ENTITY;
    playSfx(Sfx::UiBack);
}

void App::stopEditorName() {
    if (editor.editingName) SDL_StopTextInput(window);
    editor.editingName = false;
}

void App::setEditorTool(EditorTool tool, int picked) {
    editor.tool = tool;
    editor.picked = picked;
    session.buildPick = tool == EditorTool::Prop ? BuildPick::place((PropKind)picked) : BuildPick{};
}

std::vector<App::EditorEntry> App::editorEntries() {
    std::vector<EditorEntry> out;
    switch (editor.tab) {
    case EditorTab::Props:
        for (PropKind k : propKindsListed()) out.push_back(EditorEntry{ lang(propInfo(k).nameKey), idx(k) });
        break;
    case EditorTab::Mobs:
        for (int i = 1; i <= mobKindCount(); ++i) out.push_back(EditorEntry{ lang(mobInfo((MobKind)i).nameKey), i });
        break;
    case EditorTab::Items:
        for (const ItemDef& d : itemDatabase()) out.push_back(EditorEntry{ itemDisplayName(d), d.id });
        break;
    case EditorTab::Theme:
        break;
    }
    return out;
}

void App::drawEditorPanel() {
    const EditorLayout L = layoutEditor(winW, winH, editor.useOwnUi);
    const float s = uiScaleFor(winH);
    const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f }, gray{ 0.80f, 0.72f, 0.64f, 1.0f }, gold{ 1.0f, 0.78f, 0.30f, 1.0f };
    const float textH = renderer.listTextHeight("Ag");
    renderer.drawButton(L.uiToggle, lang(editor.useOwnUi ? "UILABEL_EDITOR_USE_GAME_UI" : "UILABEL_EDITOR_USE_OWN_UI"), true, L.uiToggle.contains(mouseX, mouseY));
    if (!editor.useOwnUi) return;
    renderer.drawThemedPanel(L.panel);
    renderer.drawListText(langf("UILABEL_EDITOR_TITLE", { currentMap.stem }), L.panel.x + 10.0f * s, L.panel.y + 12.0f * s, gold);
    renderer.drawRect(L.nameBox, editor.editingName ? RGBA{ 0.30f, 0.20f, 0.12f, 1.0f } : RGBA{ 0.14f, 0.09f, 0.08f, 1.0f });
    renderer.drawListText(editor.name + (editor.editingName ? "|" : ""), L.nameBox.x + 8.0f * s, L.nameBox.y + (L.nameBox.h - textH) * 0.5f, white);
    const auto button = [&](const Rect& r, const std::string& text, bool on) {
        renderer.drawButton(r, (on ? "> " : "") + text, true, r.contains(mouseX, mouseY));
    };
    button(L.save, lang("UILABEL_SAVE"), false);
    button(L.load, lang("UILABEL_EDITOR_RELOAD"), false);
    button(L.spawnTool, lang("UILABEL_EDITOR_SPAWN"), editor.tool == EditorTool::Spawn);
    button(L.eraseTool, lang("UILABEL_EDITOR_ERASE"), editor.tool == EditorTool::Erase);
    button(L.tabs[0], lang("UILABEL_EDITOR_PROPS"), editor.tab == EditorTab::Props);
    button(L.tabs[1], lang("UILABEL_EDITOR_MOBS"), editor.tab == EditorTab::Mobs);
    button(L.tabs[2], lang("UILABEL_EDITOR_ITEMS"), editor.tab == EditorTab::Items);
    button(L.tabs[3], lang("UILABEL_GEN_TAB_THEME"), editor.tab == EditorTab::Theme);
    if (editor.tab == EditorTab::Theme) drawThemePanel(L.themeArea);
    const std::vector<EditorEntry> entries = editorEntries();
    const EditorTool tabTool = editor.tab == EditorTab::Props ? EditorTool::Prop : editor.tab == EditorTab::Mobs ? EditorTool::Mob : EditorTool::Item;
    editor.listScroll = std::clamp(editor.listScroll, 0, std::max(0, (int)entries.size() - (int)L.rows.size()));
    for (size_t i = 0; i < L.rows.size() && (size_t)editor.listScroll + i < entries.size(); ++i) {
        const EditorEntry& entry = entries[(size_t)editor.listScroll + i];
        const bool on = editor.tool == tabTool && editor.picked == entry.value;
        const bool hot = L.rows[i].contains(mouseX, mouseY);
        if (on || hot) renderer.drawRect(L.rows[i], on ? RGBA{ 0.45f, 0.30f, 0.14f, 1.0f } : RGBA{ 0.28f, 0.18f, 0.12f, 1.0f });
        renderer.drawListText(renderer.fitListText(entry.label, L.rows[i].w - 12.0f * s), L.rows[i].x + 6.0f * s, L.rows[i].y + (L.rows[i].h - textH) * 0.5f, on ? gold : white);
    }
    const std::string text = levelToText(captureLevel(world, currentMap.stem));
    const bool dirty = text != editor.savedText || mapFileText(editor.map) != editor.savedMapText;
    float y = L.status.y;
    const auto line = [&](const std::string& t, RGBA tint) {
        renderer.drawListText(renderer.fitListText(t, L.status.w), L.status.x, y, tint);
        y += textH + 2.0f * s;
    };
    line(langf("UILABEL_EDITOR_COUNTS", { std::to_string(world.count<Prop>()), std::to_string(world.count<Mob>()), std::to_string(world.count<GroundItem>()) }), gray);
    line(langf("UILABEL_EDITOR_FILE", { std::to_string(text.size()) }) + (dirty ? " *" : ""), dirty ? gold : gray);
    line(lang("UILABEL_EDITOR_HELP1"), gray);
    line(lang("UILABEL_EDITOR_HELP2"), gray);
    button(L.exit, lang(editor.exitWarned ? "UILABEL_EDITOR_EXIT_SURE" : "UILABEL_EDITOR_EXIT"), false);
}

bool App::editorMouseDown() {
    const float x = e.button.x, y = e.button.y;
    const EditorLayout L = layoutEditor(winW, winH, editor.useOwnUi);
    if (e.button.button == SDL_BUTTON_LEFT && L.uiToggle.contains(x, y)) {
        editor.useOwnUi = !editor.useOwnUi;
        setEditorTool(EditorTool::None, 0);
        stopEditorName();
        playSfx(Sfx::UiToggle);
        return true;
    }
    if (!editor.useOwnUi) return false;
    if (L.panel.contains(x, y)) {
        if (e.button.button != SDL_BUTTON_LEFT) return true;
        if (L.nameBox.contains(x, y)) {
            if (!editor.editingName) SDL_StartTextInput(window);
            editor.editingName = true;
            playSfx(Sfx::UiClick);
            return true;
        }
        stopEditorName();
        if (L.save.contains(x, y)) {
            editorSave();
        } else if (L.load.contains(x, y)) {
            editorLoad();
        } else if (L.exit.contains(x, y)) {
            editorExit();
        } else if (L.spawnTool.contains(x, y)) {
            setEditorTool(editor.tool == EditorTool::Spawn ? EditorTool::None : EditorTool::Spawn, 0);
            playSfx(Sfx::UiClick);
        } else if (L.eraseTool.contains(x, y)) {
            setEditorTool(editor.tool == EditorTool::Erase ? EditorTool::None : EditorTool::Erase, 0);
            playSfx(Sfx::UiClick);
        } else {
            if (editor.tab == EditorTab::Theme && L.themeArea.contains(x, y)) {
                themeClick(L.themeArea, x, y);
                return true;
            }
            for (int i = 0; i < 4; ++i) {
                if (!L.tabs[i].contains(x, y)) continue;
                editor.tab = (EditorTab)i;
                editor.listScroll = 0;
                setEditorTool(EditorTool::None, 0);
                playSfx(Sfx::UiTab);
                return true;
            }
            const std::vector<EditorEntry> entries = editorEntries();
            for (size_t i = 0; i < L.rows.size() && (size_t)editor.listScroll + i < entries.size(); ++i) {
                if (!L.rows[i].contains(x, y)) continue;
                const EditorTool tool = editor.tab == EditorTab::Props ? EditorTool::Prop : editor.tab == EditorTab::Mobs ? EditorTool::Mob : EditorTool::Item;
                setEditorTool(tool, entries[(size_t)editor.listScroll + i].value);
                playSfx(Sfx::UiSelect);
                return true;
            }
        }
        return true;
    }
    stopEditorName();
    if (e.button.button == SDL_BUTTON_RIGHT) {
        editorEraseAt(x, y);
        return true;
    }
    if (e.button.button != SDL_BUTTON_LEFT) return false;
    if (editor.tool == EditorTool::Erase) {
        editorEraseAt(x, y);
        return true;
    }
    if (editor.tool != EditorTool::None) {
        editorPlaceAt(x, y);
        return true;
    }
    return true;
}

void App::editorPlaceAt(float mx, float my) {
    const auto hit = pickGround(cam, terrain, mx, my, winW, winH);
    if (!hit) return;
    float x = hit->first, z = hit->second;
    if (editor.tool == EditorTool::Spawn) {
        editorRemember();
        LevelInfo& info = world.resource<LevelInfo>();
        info.spawnSet = true;
        info.spawnX = x;
        info.spawnZ = z;
        playSfx(Sfx::UiOrder);
        editorEdited();
        return;
    }
    LevelObject o;
    o.yaw = editor.yaw;
    o.kind = editor.picked;
    if (editor.tool == EditorTool::Prop) {
        snapBuildSpot((PropKind)editor.picked, hit->first, hit->second, x, z);
        o.type = LevelObject::Type::Prop;
        if (propInfo((PropKind)editor.picked).built) o.owner = FACTION_PLAYER;
    } else if (editor.tool == EditorTool::Mob) {
        o.type = LevelObject::Type::Mob;
    } else {
        o.type = LevelObject::Type::Item;
    }
    o.x = x;
    o.z = z;
    editorRemember();
    LevelInfo& info = world.resource<LevelInfo>();
    o.id = info.nextId++;
    placeLevelObject(world, terrain, rng, o);
    playSfx(Sfx::Drop);
    editorEdited();
}

void App::editorEraseAt(float mx, float my) {
    Entity target = NULL_ENTITY;
    const std::vector<Entity> units = renderer.unitsAtPixel(world, cam, mx, my, winW, winH);
    const std::vector<Entity> items = renderer.groundItemsAtPixel(world, terrain, cam, mx, my, winW, winH);
    const std::vector<Entity> props = renderer.propsAtPixel(world, cam, mx, my, winW, winH);
    if (!units.empty()) target = units.front();
    else if (!items.empty()) target = items.front();
    else if (!props.empty()) target = props.front();
    if (target == NULL_ENTITY) return;
    editorRemember();
    world.destroy(target);
    hoverProp = NULL_ENTITY;
    playSfx(Sfx::Take);
    editorEdited();
}

void App::editorSave() {
    const std::string name = safeFileName(editor.name);
    if (name.empty()) {
        refuse(lang("MISC_GEN_NEEDS_NAME"));
        return;
    }
    editor.name = name;
    std::string err;
    const LevelData d = captureLevel(world, currentMap.stem);
    if (!writeLevelFile(levelFilePath(name), d, err)) {
        refuse(err);
        return;
    }
    editor.savedText = levelToText(d);
    editor.exitWarned = false;
    // The theme belongs to the map, so a level and its map are saved together
    if (mapFileText(editor.map) != editor.savedMapText) {
        if (!writeMapFile(mapFilePath(currentMap.stem), editor.map, err)) {
            refuse(err);
            return;
        }
        editor.savedMapText = mapFileText(editor.map);
    }
    showNotice(langf("MISC_LEVEL_SAVED", { name }));
    playSfx(Sfx::UiSaved);
}

void App::editorLoad() {
    LevelData d;
    std::string err;
    if (!readLevelFile(levelFilePath(safeFileName(editor.name)), d, err)) {
        refuse(err);
        return;
    }
    if (d.map != currentMap.stem) {
        refuse(langf("MISC_LEVEL_OTHER_MAP", { d.map }));
        return;
    }
    editorRemember();
    applyLevel(world, terrain, rng, d);
    editor.savedText = levelToText(captureLevel(world, currentMap.stem));
    hoverProp = NULL_ENTITY;
    playSfx(Sfx::UiLoaded);
}

void App::editorExit() {
    const bool dirty = levelToText(captureLevel(world, currentMap.stem)) != editor.savedText || mapFileText(editor.map) != editor.savedMapText;
    if (dirty && !editor.exitWarned) {
        editor.exitWarned = true;
        playSfx(Sfx::UiError);
        return;
    }
    setEditorTool(EditorTool::None, 0);
    stopEditorName();
    editing = false;
    gameActive = false;
    audio::stopTracks();
    openWorldGen();
    playSfx(Sfx::UiBack);
}

bool App::editorKeyDown() {
    const SDL_Keycode k = e.key.key;
    if (editor.editingName) {
        if (k == SDLK_BACKSPACE) popUtf8Char(editor.name);
        else if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_ESCAPE) stopEditorName();
        return true;
    }
    if (e.key.repeat) return k == SDLK_ESCAPE || k == SDLK_SPACE || k == SDLK_TAB;
    if (k == SDLK_TAB) {
        editor.useOwnUi = !editor.useOwnUi;
        setEditorTool(EditorTool::None, 0);
        playSfx(Sfx::UiToggle);
        return true;
    }
    if (k == SDLK_ESCAPE) {
        if (mapOpen) {
            mapOpen = false;
        } else if (editor.tool != EditorTool::None) {
            setEditorTool(EditorTool::None, 0);
            playSfx(Sfx::UiBack);
        }
        return true;
    }
    if (k == SDLK_SPACE) return true;
    if (!editor.useOwnUi) return false;
    if (k == SDLK_Z && (SDL_GetModState() & SDL_KMOD_CTRL) != 0) {
        editorUndo();
        return true;
    }
    if (k == SDLK_S && (SDL_GetModState() & SDL_KMOD_CTRL) != 0) {
        editorSave();
        return true;
    }
    if (k == SDLK_Q || k == SDLK_E) {
        constexpr float TURN = 3.14159265f / 12.0f;
        editor.yaw += k == SDLK_E ? TURN : -TURN;
        return true;
    }
    return false;
}
