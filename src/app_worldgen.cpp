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

    const float rightW = 280.0f * s;   // a map's row has a pencil and an X beside it
    const float areaX = L.panel.x + L.panel.w + m, areaW = W - areaX - m - rightW - m;
    const float side = std::min(areaW, H - 2.0f * m - btnH * 1.0f - 40.0f * s);
    L.preview = Rect{ areaX + (areaW - side) * 0.5f, m + 30.0f * s, side, side };
    L.previewLabel = Rect{ L.preview.x, m, side, 26.0f * s };
    const float halfW = (side - gap) * 0.5f, btnY = L.preview.y + side + 8.0f * s;
    L.generate = Rect{ L.preview.x, btnY, halfW, btnH };
    L.proceed = Rect{ L.preview.x + halfW + gap, btnY, halfW, btnH };
    L.mapEditor = Rect{ L.preview.x, btnY + btnH + gap, side, btnH };

    L.mapsBox = Rect{ W - m - rightW, m, rightW, H - 2.0f * m };
    const float listRow = 30.0f * s;
    const int shown = std::max(0, (int)((L.mapsBox.h - 56.0f * s) / (listRow + 4.0f * s)));
    const int first = std::clamp(mapScroll, 0, std::max(0, mapCount - shown));
    const RowBar bar = layoutRowBar(L.mapsBox.x + rightW - 10.0f * s - 8.0f * s, L.mapsBox.y + 44.0f * s,
                                    std::max(0.0f, shown * (listRow + 4.0f * s) - 4.0f * s), s, mapCount, shown, first);
    L.mapBar = bar;
    for (int i = first; i < mapCount && i < first + shown; ++i) {
        const float rowY = L.mapsBox.y + 44.0f * s + (i - first) * (listRow + 4.0f * s);
        const float rowW = rightW - 20.0f * s - 2.0f * (listRow + 4.0f * s) - (bar.on() ? 12.0f * s : 0.0f);
        L.mapRows.push_back(Rect{ L.mapsBox.x + 10.0f * s, rowY, rowW, listRow });
        L.mapNick.push_back(Rect{ L.mapsBox.x + 10.0f * s + rowW + 4.0f * s, rowY, listRow, listRow });
        L.mapDelete.push_back(Rect{ L.mapsBox.x + 10.0f * s + rowW + 4.0f * s + listRow + 4.0f * s, rowY, listRow, listRow });
    }
    return L;
}

// The lines stack up from the buttons, so the box grows upwards with them
MapQuestionLayout layoutMapQuestion(int W, int H, int lineCount) {
    const float s = uiScaleFor(H);
    const DialogGeometry d = questionGeometry(W, H);
    const float lineH = 44.0f * s;
    const float bottom = d.yes.y - 10.0f * s, top = bottom - lineCount * lineH;
    MapQuestionLayout L;
    L.box = Rect{ d.box.x, top - 22.0f * s, d.box.w, d.box.y + d.box.h - (top - 22.0f * s) };
    L.confirm = d.yes;
    L.cancel = d.no;
    for (int i = 0; i < lineCount; ++i) L.lines.push_back(Rect{ d.box.x + 24.0f * s, top + i * lineH, d.box.w - 48.0f * s, lineH });
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
    L.themeTab = Rect{ x, y, w, btnH };
    y += btnH + gap;
    L.clean = Rect{ x, y, w, btnH };
    y += btnH + gap;
    const float bottom = L.panel.y + L.panel.h - 10.0f * s;
    L.exit = Rect{ x, bottom - btnH, w, btnH };
    L.status = Rect{ x, bottom - btnH - gap - 96.0f * s, w, 96.0f * s };
    // The spawn list, as the Developer panel has it; the theme panel takes the list's place
    L.picker = layoutSpawnPicker(Rect{ x, y, w, L.status.y - gap - y }, s);
    L.themeArea = L.picker.listArea;
    (void)rowH;
    return L;
}

ThemeLayout layoutThemePanel(const Rect& area, const ThemeUi& ui, float scale) {
    const float s = scale;
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
    const float listH = L.shown * (rowH + gap) - gap;
    L.bar = layoutRowBar(area.x + area.w - 8.0f * scale, top, listH, scale, L.total, L.shown, L.first);
    const float lineW = L.bar.on() ? area.w - 8.0f * scale - 4.0f * scale : area.w;
    for (int i = L.first; i < L.total && i < L.first + L.shown; ++i) {
        ThemeLine line;
        line.row = entries[(size_t)i].row;
        line.channel = entries[(size_t)i].channel;
        line.box = Rect{ area.x, top + (i - L.first) * (rowH + gap), lineW, rowH };
        line.control = Rect{ area.x + labelW, line.box.y, lineW - labelW, rowH };
        L.lines.push_back(line);
    }
    return L;
}

// ---- The Theme tab

MapTheme& App::themeBeingEdited() {
    if (devThemeShown()) return devTheme;
    return editing ? editor.map.theme : worldGenUi.settings.theme;
}

int& App::themePresetBeingEdited() {
    if (devThemeShown()) return devThemePreset;
    return editing ? editor.map.themePreset : worldGenUi.settings.themePreset;
}

// The Developer panel's Theme tab in a game: the world's theme, changed temporarily
bool App::devThemeShown() const {
    return screen == Screen::Playing && !editing && devSpawn.open && devSpawn.tab == EditorTab::Theme;
}

// Loads what the world has saved, once for a game; changes made since stay until made permanent or the game ends
void App::openDevTheme() {
    if (!devThemeLoaded) {
        WorldGenSettings saved;
        std::string err;
        if (readMapFile(mapFilePath(currentMap.stem), saved, err)) {
            devSavedTheme = saved.theme;
            devSavedPreset = saved.themePreset;
        }
        devTheme = devSavedTheme;
        devThemePreset = devSavedPreset;
        devThemeLoaded = true;
    }
    devThemeConfirm = false;
    renderer.setMapTheme(devTheme);
}

void App::devThemeRevert() {
    devTheme = devSavedTheme;
    devThemePreset = devSavedPreset;
    devThemeConfirm = false;
    renderer.setMapTheme(devTheme);
    playSfx(Sfx::UiBack);
}

// Pressed once asks; pressed again writes the theme into the world's map file
void App::devThemeMakePermanent() {
    if (!devThemeConfirm) {
        devThemeConfirm = true;
        playSfx(Sfx::UiClick);
        return;
    }
    devThemeConfirm = false;
    WorldGenSettings settings;
    std::string err;
    if (!readMapFile(mapFilePath(currentMap.stem), settings, err)) {
        refuse(err);
        return;
    }
    settings.theme = devTheme;
    settings.themePreset = devThemePreset;
    if (!writeMapFile(mapFilePath(currentMap.stem), settings, err)) {
        refuse(err);
        return;
    }
    devSavedTheme = devTheme;
    devSavedPreset = devThemePreset;
    showNotice(lang("UILABEL_DEV_THEME_SAVED"));
    playSfx(Sfx::UiSaved);
}

void App::themeEdited() {
    if (devThemeShown()) {
        renderer.setMapTheme(devTheme);
        return;
    }
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
    renderer.drawButton(L.presetDown, "<", true);
    renderer.drawRect(L.presetBox, well);
    const std::string presetName = lang(themePresets()[themePresetBeingEdited()].labelKey);
    renderer.drawListText(renderer.fitListText(presetName, L.presetBox.w - 8.0f * s), L.presetBox.x + 4.0f * s, middle(L.presetBox), white);
    renderer.drawButton(L.presetUp, ">", true);
    if (L.bar.on()) renderer.drawScrollBar(L.bar.track, L.bar.thumb, barDrag.kind == BarDrag::Theme || renderer.mouseOver(L.bar.track));

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
            renderer.drawButton(c, lang(theme.*row.number ? "UILABEL_THEME_ON" : "UILABEL_THEME_OFF"), true);
            break;
        case ThemeRowKind::Choice:
            renderer.drawButton(c, lang(themeChoiceLabelKey(row, theme.*row.number)), true);
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

Rect App::themeAreaNow() {
    return editing ? layoutEditor(winW, winH, true).themeArea
         : devThemeShown() ? layoutDeveloper(winW, winH, EditorTab::Theme).themeArea
         : layoutWorldGen(winW, winH, (int)worldGenUi.maps.size(), worldGenUi.mapScroll).themeArea;
}

// Taking a list's scrollbar: the thumb is held where it was taken, a click on the track outside it takes it by its middle
void App::startBarDrag(BarDrag::Kind kind, const RowBar& bar, float y, SpawnPicker* picker) {
    barDrag.kind = kind;
    barDrag.picker = picker;
    barDrag.grab = bar.thumb.contains(bar.thumb.x, y) ? y - bar.thumb.y : bar.thumb.h * 0.5f;
    playSfx(Sfx::UiClick);
    moveBarDrag(y);
}

bool App::pressListBar(const RowBar& bar, float x, float y, std::function<void(float)> apply) {
    if (!bar.on() || !bar.track.contains(x, y)) return false;
    startBarDrag(BarDrag::Custom, bar, y, nullptr);
    barDrag.track = bar.track;
    barDrag.apply = std::move(apply);
    moveBarDrag(y);
    return true;
}

void App::drawListBar(const RowBar& bar) {
    if (!bar.on()) return;
    const bool held = barDrag.kind == BarDrag::Custom && barDrag.track.x == bar.track.x && barDrag.track.y == bar.track.y;
    renderer.drawScrollBar(bar.track, bar.thumb, held || renderer.mouseOver(bar.track));
}

void App::moveBarDrag(float y) {
    const float top = y - barDrag.grab;
    switch (barDrag.kind) {
    case BarDrag::Custom:
        if (barDrag.apply) barDrag.apply(top);
        break;
    case BarDrag::Theme: {
        const ThemeLayout L = layoutThemePanel(themeAreaNow(), themeUi, uiScaleFor(winH));
        if (L.bar.on()) themeUi.scroll = rowBarFirst(L.bar, L.total, L.shown, top);
        break;
    }
    case BarDrag::Maps: {
        const WorldGenLayout L = layoutWorldGen(winW, winH, (int)worldGenUi.maps.size(), worldGenUi.mapScroll);
        if (L.mapBar.on()) worldGenUi.mapScroll = rowBarFirst(L.mapBar, (int)worldGenUi.maps.size(), (int)L.mapRows.size(), top);
        break;
    }
    case BarDrag::Picker: {
        SpawnPicker* p = barDrag.picker;
        if (!p) break;
        const SpawnPickerLayout base = p == &devSpawn ? layoutDeveloper(winW, winH, devSpawn.tab).picker : layoutEditor(winW, winH, true).picker;
        const SpawnPickerLayout L = pickerView(*p, base);
        if (L.bar.on()) p->listScroll = rowBarFirst(L.bar, (int)spawnEntries(p->tab).size(), (int)L.rows.size(), top);
        break;
    }
    case BarDrag::Saves: {
        const RowBar bar = saveListBar(winW, winH, saves.size(), listOffset);
        if (bar.on()) listOffset = (size_t)std::max(0, rowBarFirst(bar, (int)saves.size(), (int)visibleSaveRows(winH), top));
        break;
    }
    case BarDrag::Storage: {
        const StorageLayout L = storageLayout(world, winW, winH);
        const RowBar bar = storageBar(L, session.storageScroll);
        const int shown = ((int)L.slots.size() + STORAGE_COLUMNS - 1) / STORAGE_COLUMNS;
        if (bar.on()) session.storageScroll = std::clamp(rowBarFirst(bar, shown + L.scrollMax, shown, top), 0, L.scrollMax);
        break;
    }
    default:
        break;
    }
}

void App::themeDragAt(float x) {
    const float share = std::clamp((x - themeUi.dragX) / std::max(1.0f, themeUi.dragW), 0.0f, 1.0f);
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
    if (L.bar.on() && L.bar.track.contains(x, y)) {
        startBarDrag(BarDrag::Theme, L.bar, y, nullptr);
        return true;
    }
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
            themeUi.dragX = line.control.x;
            themeUi.dragW = line.control.w;
            themeDragAt(x);
        }
        return true;
    }
    return false;
}

// ---- World Generation screen

namespace {
std::string lowered(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    }
    return s;
}

// The size a step up (by > 0) or down from this one: the next of WORLD_GEN_SIZES above or below it, or the size itself if there is none. A
// size that is not one of them (a hand-made map file) goes to the nearer one on that side.
int steppedSize(int size, int by) {
    int next = size;
    if (by > 0) {
        for (int i = (int)std::size(WORLD_GEN_SIZES) - 1; i >= 0; --i) {
            if (WORLD_GEN_SIZES[i] > size) next = WORLD_GEN_SIZES[i];
        }
    } else {
        for (const int candidate : WORLD_GEN_SIZES) {
            if (candidate < size) next = candidate;
        }
    }
    return next;
}

// The end of a map's name that says how many times 512 tiles across it is: "_1x" for 512, "_2x" for 1024, "_4x" for 2048, and so on
std::string sizeSuffix(int size) {
    return "_" + std::to_string(std::max(1, (size + 256) / 512)) + "x";
}

// The name with its size on the end, in place of any size it already ends with (an underscore, a number and an x), so that maps of one seed
// and different sizes have different names. A name too long to take it loses the end of its own.
std::string nameWithSize(std::string name, int size) {
    const size_t last = name.size();
    if (last > 0 && name[last - 1] == 'x') {
        size_t digits = last - 1;
        while (digits > 0 && name[digits - 1] >= '0' && name[digits - 1] <= '9') --digits;
        if (digits < last - 1 && digits > 0 && name[digits - 1] == '_') name.erase(digits - 1);
    }
    const std::string suffix = sizeSuffix(size);
    while (!name.empty() && utf8Length(name) + suffix.size() > (size_t)WORLD_GEN_NAME_CHARS) popUtf8Char(name);
    return name + suffix;
}

// The Size was changed: the name's end says so (a name that is empty is left for what is typed)
void putSizeOnName(WorldGenUi& ui) {
    if (!ui.name.empty()) ui.name = nameWithSize(ui.name, ui.settings.size);
}
}

// "world", the seed and the size, until a name is typed or a map picked
void App::defaultWorldGenName() {
    if (!worldGenUi.nameEdited || worldGenUi.name.empty()) {
        worldGenUi.name = nameWithSize(lang("UILABEL_GEN_DEFAULT_NAME") + std::to_string(worldGenUi.settings.seed), worldGenUi.settings.size);
        worldGenUi.nameEdited = false;
    }
}

// True if a game save is played on the map of this name. The ground of such a map isn't written over, so the save's ground
// stays as it was. Its theme can be: a save takes the theme from the map file each time it loads.
bool App::worldHasSaves(const std::string& name) const {
    return worldSaveCount(name) > 0;
}

int App::worldSaveCount(const std::string& name) const {
    const std::vector<std::string>& played = worldGenUi.playedMaps;
    return (int)std::count(played.begin(), played.end(), lowered(name));
}

// The lines of the question asked before the map is deleted, with their colours
std::vector<std::pair<std::string, RGBA>> App::mapDeleteLines() {
    const std::string& name = worldGenUi.deleteMap;
    std::vector<std::pair<std::string, RGBA>> lines;
    lines.push_back({ langf("MISC_DELETE_MAP_Q", { mapDisplayName(name) }), theme::TEXT });
    const int saves = worldSaveCount(name);
    if (saves > 0) lines.push_back({ langf("UILABEL_DELETE_MAP_SAVES", { std::to_string(saves) }), RGBA{ 1.0f, 0.45f, 0.38f, 1.0f } });
    const int levels = worldGenUi.deleteLevels;
    if (levels > 0) lines.push_back({ langf("UILABEL_DELETE_MAP_LEVELS", { std::to_string(levels) }), theme::TEXT_DIM });
    lines.push_back({ lang("UILABEL_HOLD_DELETE"), theme::TEXT });
    return lines;
}

void App::cancelMapDelete() {
    worldGenUi.deleteMap.clear();
    deleteHolding = false;
    playSfx(Sfx::UiBack);
}

// The lines of the question asked before a map is written over, with their colours
std::vector<std::pair<std::string, RGBA>> App::mapOverwriteLines() {
    const std::string& name = worldGenUi.overwriteMap;
    std::vector<std::pair<std::string, RGBA>> lines;
    lines.push_back({ langf(worldGenUi.overwriteThemeOnly ? "MISC_GEN_OVERWRITE_THEME_Q" : "MISC_GEN_OVERWRITE_Q", { mapDisplayName(name) }), theme::TEXT });
    const int saves = worldSaveCount(name);
    if (saves > 0) lines.push_back({ langf("UILABEL_GEN_OVERWRITE_SAVES", { std::to_string(saves) }), RGBA{ 1.0f, 0.45f, 0.38f, 1.0f } });
    return lines;
}

void App::cancelMapOverwrite() {
    worldGenUi.overwriteMap.clear();
    playSfx(Sfx::UiBack);
}

// The lines of the warning asked before the Size goes over what the game is made for, with their colours. Its button is held down.
std::vector<std::pair<std::string, RGBA>> App::bigSizeLines() {
    std::vector<std::pair<std::string, RGBA>> lines;
    lines.push_back({ langf("MISC_GEN_BIG_SIZE_Q", { std::to_string(WORLD_GEN_SAFE_SIZE) }), theme::TEXT });
    lines.push_back({ lang("UILABEL_GEN_BIG_SIZE_RISK"), RGBA{ 1.0f, 0.45f, 0.38f, 1.0f } });
    lines.push_back({ lang("UILABEL_GEN_BIG_SIZE_HOLD"), theme::TEXT });
    return lines;
}

void App::cancelBigSize() {
    worldGenUi.sizeAsk = false;
    deleteHolding = false;
    playSfx(Sfx::UiBack);
}

// The button was held down: the bigger sizes are open from now on, and the Size goes up the step it was asked for
void App::confirmBigSize() {
    WorldGenUi& ui = worldGenUi;
    ui.sizeAsk = false;
    deleteHolding = false;
    ui.sizesUnlocked = true;
    ui.settings.size = steppedSize(ui.settings.size, 1);
    putSizeOnName(ui);
    worldGenChanged();
    playSfx(Sfx::UiClick);
}

// ---- A map's nickname

void App::openNickname(const std::string& stem) {
    nicknameUi.open = true;
    nicknameUi.stem = stem;
    nicknameUi.text = mapNickname(stem);
    SDL_StartTextInput(window);
    playSfx(Sfx::UiClick);
}

// keep: write what is typed down as the map's nickname (nothing typed takes it off)
void App::closeNickname(bool keep) {
    if (!nicknameUi.open) return;
    nicknameUi.open = false;
    SDL_StopTextInput(window);
    if (!keep) {
        playSfx(Sfx::UiBack);
        return;
    }
    if (!setMapNickname(nicknameUi.stem, nicknameUi.text)) {
        refuse(lang("MISC_MAP_NICKNAME_FAILED"));
        return;
    }
    // New Game's list and its pick were made before, with the names as they were
    for (MapChoice& m : mapPick.maps) {
        if (m.source == MAP_SOURCE_FILE && m.stem == nicknameUi.stem) m.name = mapDisplayName(m.stem);
    }
    if (pendingMap.source == MAP_SOURCE_FILE && pendingMap.stem == nicknameUi.stem) pendingMap.name = mapDisplayName(pendingMap.stem);
    playSfx(Sfx::UiSaved);
}

// The question takes every click while it is open: Save and Cancel answer it, and so does a click outside it (as Cancel)
bool App::nicknameClick(float x, float y) {
    if (!nicknameUi.open) return false;
    const MapQuestionLayout D = layoutMapQuestion(winW, winH, 4);
    if (D.confirm.contains(x, y)) closeNickname(true);
    else if (D.cancel.contains(x, y) || !D.box.contains(x, y)) closeNickname(false);
    return true;
}

void App::nicknameKey() {
    const SDL_Keycode k = e.key.key;
    if (k == SDLK_BACKSPACE) popUtf8Char(nicknameUi.text);
    else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) closeNickname(true);
    else if (k == SDLK_ESCAPE) closeNickname(false);
}

void App::drawNicknameDialog() {
    if (!nicknameUi.open) return;
    const float s = uiScaleFor(winH);
    dimScreen(0.45f);
    const MapQuestionLayout D = layoutMapQuestion(winW, winH, 4);
    renderer.drawThemedPanel(D.box);
    renderer.drawUiTextInBox(langf("MISC_MAP_NICKNAME_Q", { nicknameUi.stem }), D.lines[0], theme::TEXT);
    // What is typed, and where the next letter goes
    const Rect field{ D.lines[1].x, D.lines[1].y + 4.0f * s, D.lines[1].w, D.lines[1].h - 8.0f * s };
    const float textH = renderer.listTextHeight("Ag");
    renderer.drawField(field, true);
    renderer.drawListText(nicknameUi.text + "|", field.x + 8.0f * s, field.y + (field.h - textH) * 0.5f, RGBA{ 1.0f, 1.0f, 1.0f, 1.0f });
    renderer.drawUiTextInBox(lang("UILABEL_MAP_NICKNAME_HINT1"), D.lines[2], theme::TEXT_DIM);
    renderer.drawUiTextInBox(lang("UILABEL_MAP_NICKNAME_HINT2"), D.lines[3], theme::TEXT_DIM);
    drawScreenButtons({ Button{ lang("UILABEL_SAVE"), D.confirm, Action::ConfirmName },
                        Button{ lang("UILABEL_CANCEL"), D.cancel, Action::CancelName } });
}

// A pencil in square pixels, the tip down at the left and the eraser up at the right, in the middle of a button
void App::drawPencil(const Rect& r) {
    const float cell = std::max(1.0f, std::round(r.h * 0.6f / 10.0f));
    const float ox = std::floor(r.x + (r.w - cell * 10.0f) * 0.5f), oy = std::floor(r.y + (r.h - cell * 10.0f) * 0.5f);
    const RGBA eraser{ 0.98f, 0.70f, 0.72f, 1.0f }, metal{ 0.82f, 0.82f, 0.86f, 1.0f }, light{ 1.0f, 0.92f, 0.55f, 1.0f };
    const RGBA body{ 1.0f, 0.80f, 0.25f, 1.0f }, shade{ 0.85f, 0.58f, 0.12f, 1.0f };
    const RGBA wood{ 0.93f, 0.78f, 0.58f, 1.0f }, lead{ 0.25f, 0.22f, 0.22f, 1.0f };
    for (int y = 0; y < 10; ++y) {
        for (int x = 0; x < 10; ++x) {
            // Three cells wide along the diagonal: the upper edge (-1), the middle, and the lower edge (1)
            const int band = x + y - 9;
            if (band < -1 || band > 1) continue;
            // From the tip (-9) to the eraser (9)
            const int along = x - y;
            RGBA c = band < 0 ? light : band > 0 ? shade : body;
            if (along <= -8) c = lead;
            else if (along <= -5) c = wood;
            else if (along >= 7) c = eraser;
            else if (along >= 5) c = metal;
            renderer.drawRect(Rect{ ox + (float)x * cell, oy + (float)y * cell, cell, cell }, c);
        }
    }
}

// Deletes the map the question was asked about, with the levels made on it and their pictures
void App::deleteWorldMap() {
    WorldGenUi& ui = worldGenUi;
    const std::string name = ui.deleteMap;
    ui.deleteMap.clear();
    deleteHolding = false;
    std::error_code ec;
    for (const std::string& level : listLevelFiles(name)) {
        std::error_code levelEc;
        std::filesystem::remove(std::filesystem::u8path(levelFilePath(level)), levelEc);
        std::filesystem::remove(std::filesystem::u8path(savePreviewPath(levelFilePath(level))), levelEc);
    }
    std::filesystem::remove(std::filesystem::u8path(mapFilePath(name)), ec);
    std::filesystem::remove(std::filesystem::u8path(mapEditsPath(name)), ec);
    if (!ec && !mapNickname(name).empty()) setMapNickname(name, "");   // a map made later under this name starts with none
    if (ui.generated && ui.generatedName == name) ui.generated = false;
    ui.maps = listMapFiles();
    ui.mapScroll = std::min(ui.mapScroll, std::max(0, (int)ui.maps.size() - 1));
    showNotice(ec ? langf("MISC_DELETE_FAILED", { name }) : langf("MISC_DELETED", { name }));
    playSfx(Sfx::UiClick);
}

// Reads which maps the game saves on disk are played on
void App::refreshPlayedMaps() {
    worldGenUi.playedMaps.clear();
    for (const SaveSummary& s : listSaves()) {
        if (s.mapSource == MAP_SOURCE_FILE) worldGenUi.playedMaps.push_back(lowered(s.mapStem));
    }
}

// New Game with no world yet: the same world World Generation would make from its first page, left as it is
void App::makeDefaultWorld() {
    WorldGenSettings settings;
    settings.seed = (uint32_t)SDL_GetTicksNS() | 1u;
    settings.theme = themePresets()[mapStyleInfo(settings.style).themePreset].theme;
    const std::string name = safeFileName(nameWithSize(lang("UILABEL_GEN_DEFAULT_NAME") + std::to_string(settings.seed), settings.size));
    std::string err;
    if (!writeMapFile(mapFilePath(name), settings, err)) {
        refuse(err);
        screen = Screen::Title;
        return;
    }
    playSfx(Sfx::UiSaved);
    openMapPick();
}

void App::drawDefaultWorldAsk() {
    const float s = uiScaleFor(winH);
    const DialogGeometry d = questionGeometry(winW, winH);
    const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f };
    const RGBA gray{ 0.80f, 0.72f, 0.64f, 1.0f };
    renderer.beginUi(winW, winH);
    drawMenuBackdrop();
    renderer.drawThemedPanel(d.box);
    renderer.drawUiTextCentered(lang("MISC_NO_WORLD_Q"), d.box.x + d.box.w * 0.5f, d.box.y + 30.0f * s, white);
    renderer.drawUiTextCentered(lang("UILABEL_NO_WORLD_HINT"), d.box.x + d.box.w * 0.5f, d.box.y + 36.0f * s + 48.0f * s, gray);
    drawScreenButtons(layoutButtons(Screen::DefaultWorldAsk, winW, winH, saves));
    renderer.endUi();
}

void App::openWorldGen() {
    if (worldGenUi.settings.seed == 1) worldGenUi.settings.seed = (uint32_t)SDL_GetTicksNS() | 1u;
    defaultWorldGenName();
    refreshPlayedMaps();
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
    const bool deleting = !ui.deleteMap.empty();
    const bool holding = deleting || ui.sizeAsk;   // the questions answered by holding a button down for 3 seconds
    const bool questioning = holding || !ui.overwriteMap.empty();
    const bool asking = questioning || nicknameUi.open;   // with a question over the screen, nothing under it lights up or gives a tip
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
        renderer.drawField(r, active);
        renderer.drawListText(text, r.x + 8.0f * s, r.y + (r.h - textH) * 0.5f, white);
    };
    renderer.drawToggleButton(L.tabWorld, lang("UILABEL_GEN_TAB_WORLD"), !ui.themeTab);
    renderer.drawToggleButton(L.tabTheme, lang("UILABEL_GEN_TAB_THEME"), ui.themeTab);
    if (ui.themeTab) drawThemePanel(L.themeArea);
    if (!ui.themeTab) {
    label(lang("UILABEL_NAME"), L.nameBox);
    field(L.nameBox, ui.name + (ui.editingName ? "|" : ""), ui.editingName);
    label(lang("UILABEL_GEN_SEED"), L.seedBox);
    field(L.seedBox, std::to_string(ui.settings.seed), false);
    renderer.drawButton(L.seedRandom, lang("UILABEL_GEN_RANDOM"), true);
    label(lang("UILABEL_GEN_SIZE"), L.sizeBox);
    renderer.drawButton(L.sizeDown, "-", true);
    // A size over what the game is made for shows in a warning colour, and says why when pointed at
    const bool bigSize = ui.settings.size > WORLD_GEN_SAFE_SIZE;
    renderer.drawField(L.sizeBox, false);
    renderer.drawListText(std::to_string(ui.settings.size) + " x " + std::to_string(ui.settings.size), L.sizeBox.x + 8.0f * s,
                          L.sizeBox.y + (L.sizeBox.h - textH) * 0.5f, bigSize ? RGBA{ 1.0f, 0.62f, 0.35f, 1.0f } : white);
    if (bigSize && !asking) renderer.noteTip(langf("MISC_GEN_BIG_SIZE_Q", { std::to_string(WORLD_GEN_SAFE_SIZE) }), L.sizeBox);
    renderer.drawButton(L.sizeUp, "+", true);
    const MapStyleInfo& style = mapStyleInfo(ui.settings.style);
    label(lang("UILABEL_GEN_STYLE"), L.styleBox);
    renderer.drawButton(L.styleDown, "<", true);
    field(L.styleBox, renderer.fitListText(lang(style.labelKey), L.styleBox.w - 16.0f * s), false);
    renderer.drawButton(L.styleUp, ">", true);
    renderer.drawListText(renderer.fitListText(lang(style.aboutKey), L.styleAbout.w), L.styleAbout.x, L.styleAbout.y, gray);
    for (int i = 0; i < WORLD_GEN_ROW_COUNT; ++i) {
        const int value = ui.settings.*WORLD_GEN_ROWS[i].value;
        label(lang(WORLD_GEN_ROWS[i].labelKey), L.bars[(size_t)i]);
        renderer.drawButton(L.stepDown[(size_t)i], "-", true);
        const Rect& bar = L.bars[(size_t)i];
        renderer.drawField(bar, false);
        renderer.drawRect(Rect{ bar.x, bar.y, bar.w * (float)value / 100.0f, bar.h }, RGBA{ 0.55f, 0.36f, 0.16f, 1.0f });
        renderer.drawListText(std::to_string(value), bar.x + 8.0f * s, bar.y + (bar.h - textH) * 0.5f, white);
        renderer.drawButton(L.stepUp[(size_t)i], "+", true);
    }
    }
    renderer.drawButton(L.back, lang("UILABEL_RETURN_TO_TITLE"), true);

    renderer.drawListText(ui.generated ? langf("UILABEL_GEN_DONE", { ui.generatedName, std::to_string(mapFileText(ui.settings).size()) })
                                       : lang("UILABEL_GEN_PREVIEW"), L.previewLabel.x, L.previewLabel.y, gray);
    renderer.drawWorldPreview(L.preview);
    renderer.drawButton(L.generate, lang("UILABEL_GEN_GENERATE"), !safeFileName(ui.name).empty());
    renderer.drawButton(L.proceed, lang("UILABEL_GEN_CONTINUE"), ui.generated);
    renderer.drawButton(L.mapEditor, lang("UILABEL_MAP_EDITOR"), ui.generated);

    renderer.drawThemedPanel(L.mapsBox);
    renderer.drawListText(lang("UILABEL_GEN_MAPS"), L.mapsBox.x + 10.0f * s, L.mapsBox.y + 12.0f * s, gray);
    const int first = std::clamp(ui.mapScroll, 0, std::max(0, (int)ui.maps.size() - (int)L.mapRows.size()));
    if (L.mapBar.on()) renderer.drawScrollBar(L.mapBar.track, L.mapBar.thumb, barDrag.kind == BarDrag::Maps || renderer.mouseOver(L.mapBar.track));
    const float hoverX = asking ? -1.0f : mouseX, hoverY = asking ? -1.0f : mouseY;
    for (size_t i = 0; i < L.mapRows.size(); ++i) {
        const Rect& row = L.mapRows[i];
        const std::string& stem = ui.maps[(size_t)first + i];
        const std::string name = mapDisplayName(stem);   // with its nickname before it, if it has one
        renderer.drawListButton(row, "", row.contains(hoverX, hoverY));
        // A floppy disk and the number of game saves played on the map, in a slot every row keeps so the names line up
        const Rect slot{ row.x + 8.0f * s, row.y, 34.0f * s, row.h };
        const int saves = worldSaveCount(stem);
        if (saves > 0) {
            const float d = row.h * 0.56f, dx = slot.x, dy = row.y + (row.h - d) * 0.5f;
            const RGBA body{ 0.62f, 0.46f, 0.20f, 1.0f }, metal{ 0.86f, 0.84f, 0.80f, 1.0f }, paper{ 0.96f, 0.92f, 0.82f, 1.0f };
            renderer.drawRect(Rect{ dx, dy, d, d }, body);
            renderer.drawRect(Rect{ dx + d * 0.22f, dy, d * 0.52f, d * 0.34f }, metal);
            renderer.drawRect(Rect{ dx + d * 0.54f, dy + d * 0.05f, d * 0.12f, d * 0.24f }, body);
            renderer.drawRect(Rect{ dx + d * 0.16f, dy + d * 0.52f, d * 0.68f, d * 0.40f }, paper);
            renderer.drawListText(std::to_string(saves), dx + d + 3.0f * s, row.y + (row.h - textH) * 0.5f, gold);
            if (!asking) renderer.noteTip(langf("UILABEL_MAP_SAVES_TIP", { std::to_string(saves) }), slot);
        }
        const float nameX = slot.x + slot.w + 4.0f * s, nameW = row.x + row.w - 8.0f * s - nameX;
        const Rect nameArea{ nameX, row.y, nameW, row.h };
        const std::string shown = renderer.fitListText(name, nameW);
        if (!asking && shown != name) renderer.noteTip(name, nameArea);
        renderer.drawListText(shown,
                              nameX, row.y + (row.h - textH) * 0.5f, white);
        // The pencil that sets the map's nickname, and the X that deletes it
        renderer.drawListButton(L.mapNick[i], "", L.mapNick[i].contains(hoverX, hoverY));
        drawPencil(L.mapNick[i]);
        if (!asking) renderer.noteTip(lang("UILABEL_MAP_NICKNAME_TIP"), L.mapNick[i]);
        renderer.drawListButton(L.mapDelete[i], "X", L.mapDelete[i].contains(hoverX, hoverY));
    }

    if (questioning) {
        dimScreen(0.45f);
        const std::vector<std::pair<std::string, RGBA>> lines = deleting ? mapDeleteLines() : ui.sizeAsk ? bigSizeLines() : mapOverwriteLines();
        const MapQuestionLayout D = layoutMapQuestion(winW, winH, (int)lines.size());
        renderer.drawThemedPanel(D.box);
        for (size_t i = 0; i < lines.size(); ++i) renderer.drawUiTextInBox(lines[i].first, D.lines[i], lines[i].second);
        if (holding) {
            const float fill = deleteHolding ? std::min(1.0f, (float)(SDL_GetTicks() - deleteHoldStart) / (float)DELETE_HOLD_MS) : 0.0f;
            renderer.drawHoldButton(D.confirm, lang(deleting ? "UILABEL_DELETE" : "UILABEL_CONTINUE"), fill);
            drawScreenButtons({ Button{ lang("UILABEL_CANCEL"), D.cancel, Action::DeleteNo } });
        } else {
            drawScreenButtons({ Button{ lang("UILABEL_GEN_OVERWRITE"), D.confirm, Action::OverwriteYes },
                                Button{ lang("UILABEL_CANCEL"), D.cancel, Action::OverwriteNo } });
        }
    }
    drawNicknameDialog();
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
    if (nicknameClick(x, y)) return;
    if (ui.sizeAsk) {
        const MapQuestionLayout D = layoutMapQuestion(winW, winH, (int)bigSizeLines().size());
        if (D.confirm.contains(x, y)) {
            deleteHolding = true;
            deleteHoldStart = SDL_GetTicks();
            deleteHoldRect = D.confirm;
        } else if (D.cancel.contains(x, y) || !D.box.contains(x, y)) {
            cancelBigSize();
        }
        return;
    }
    if (!ui.deleteMap.empty()) {
        const MapQuestionLayout D = layoutMapQuestion(winW, winH, (int)mapDeleteLines().size());
        if (D.confirm.contains(x, y)) {
            deleteHolding = true;
            deleteHoldStart = SDL_GetTicks();
            deleteHoldRect = D.confirm;
        } else if (D.cancel.contains(x, y) || !D.box.contains(x, y)) {
            cancelMapDelete();
        }
        return;
    }
    if (!ui.overwriteMap.empty()) {
        const MapQuestionLayout D = layoutMapQuestion(winW, winH, (int)mapOverwriteLines().size());
        if (D.confirm.contains(x, y)) {
            ui.overwriteMap.clear();
            generateWorld(true);
        } else if (D.cancel.contains(x, y) || !D.box.contains(x, y)) {
            cancelMapOverwrite();
        }
        return;
    }
    if (L.mapBar.on() && L.mapBar.track.contains(x, y)) {
        startBarDrag(BarDrag::Maps, L.mapBar, y, nullptr);
        return;
    }
    {
        const int top = std::clamp(ui.mapScroll, 0, std::max(0, (int)ui.maps.size() - (int)L.mapRows.size()));
        for (size_t i = 0; i < L.mapNick.size(); ++i) {
            if (!L.mapNick[i].contains(x, y)) continue;
            stopWorldGenName();
            openNickname(ui.maps[(size_t)top + i]);
            return;
        }
        for (size_t i = 0; i < L.mapDelete.size(); ++i) {
            if (!L.mapDelete[i].contains(x, y)) continue;
            stopWorldGenName();
            refreshPlayedMaps();
            ui.deleteMap = ui.maps[(size_t)top + i];
            ui.deleteLevels = (int)listLevelFiles(ui.deleteMap).size();
            deleteHolding = false;
            playSfx(Sfx::UiClick);
            return;
        }
    }
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
        const int to = steppedSize(ui.settings.size, by);
        if (to == ui.settings.size) {
            playSfx(Sfx::UiClick);   // already the smallest or the biggest
            return;
        }
        // A size over what the game is made for is asked about first, once: the warning is held through, and then the bigger sizes are open
        if (to > WORLD_GEN_SAFE_SIZE && !ui.sizesUnlocked) {
            ui.sizeAsk = true;
            deleteHolding = false;
            playSfx(Sfx::UiClick);
            return;
        }
        ui.settings.size = to;
        putSizeOnName(ui);
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
        ui.nameEdited = false;   // a new seed is a new world, with a new name whatever the last one was
        defaultWorldGenName();
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
    } else if (L.mapEditor.contains(x, y) && ui.generated) {
        playSfx(Sfx::UiClick);
        fadeOutThen([this, name = ui.generatedName] { openMapEditor(name); });
    } else if (L.proceed.contains(x, y) && ui.generated) {
        playSfx(Sfx::UiClick);
        fadeOutThen([this, name = ui.generatedName] { openLevelEditor(name); });
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
            ui.nameEdited = true;
            ui.previewDirty = true;
            ui.generated = true;
            ui.generatedName = name;
            playSfx(Sfx::UiClick);
            return;
        }
    }
}

// Writes the map. A map of that name that is already there is only written over after a question, which this asks (confirmed: it was
// answered). The ground of a map with game saves stays as it was, so all that may change on it is its theme.
bool App::generateWorld(bool confirmed) {
    WorldGenUi& ui = worldGenUi;
    const std::string name = safeFileName(ui.name);
    if (name.empty()) {
        refuse(lang("MISC_GEN_NEEDS_NAME"));
        return false;
    }
    ui.name = name;
    // Read the saves again right before writing, so the ground of a map with game saves is never written over
    refreshPlayedMaps();
    const std::string path = mapFilePath(name);
    std::error_code existsEc;
    const bool exists = std::filesystem::exists(std::filesystem::u8path(path), existsEc);
    WorldGenSettings onDisk;
    std::string err;
    const bool readable = exists && readMapFile(path, onDisk, err);
    if (readable && mapFileText(onDisk) == mapFileText(ui.settings)) {
        // The map is already as it is on screen, so there is nothing to write over
        ui.generated = true;
        ui.generatedName = name;
        playSfx(Sfx::UiSaved);
        return true;
    }
    const bool themeOnly = readable && sameGround(onDisk, ui.settings);
    if (worldHasSaves(name) && !themeOnly) {
        refuse(langf("MISC_GEN_HAS_SAVES", { name }));
        return false;
    }
    if (exists && !confirmed) {
        ui.overwriteMap = name;
        ui.overwriteThemeOnly = themeOnly;
        playSfx(Sfx::UiClick);
        return false;
    }
    if (!writeMapFile(path, ui.settings, err)) {
        refuse(err);
        return false;
    }
    // New ground: edits made to the old ground no longer fit it
    if (!themeOnly) {
        std::error_code editsEc;
        std::filesystem::remove(std::filesystem::u8path(mapEditsPath(name)), editsEc);
    }
    ui.generated = true;
    ui.generatedName = name;
    ui.maps = listMapFiles();
    playSfx(Sfx::UiSaved);
    return true;
}

// ---- Level editor

void App::openLevelEditor(const std::string& mapName) {
    mapEditing = false;
    const std::optional<MapChoice> map = findMap(listMaps(), MAP_SOURCE_FILE, mapName);
    if (!map) {
        refuse(langf("MISC_MAP_MISSING", { mapName }));
        return;
    }
    std::string err;
    renderer.pickLoadingArt();
    LoadingScreen loading(window, renderer);
    const LoadProgress progress = [&](float p, const std::string& what) { loading.show(p, what); };
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
    } else {
        // A new level starts with what a regular map has (its camps, resource nodes and wild animals, in the same amounts); Clean Level
        // empties it. Your own units are not part of a level: its start point says where yours go.
        buildLevel(world, terrain, rng, (unsigned)std::time(nullptr), outpostScenario());
        settleFactionHomes(world, terrain);
        for (ObjectId u : world.view<Entity, Faction>()) {
            if (world.get<Faction>(u)->id == FACTION_PLAYER && !world.has<Prop>(u)) world.destroy(u);
        }
        world.resource<LevelInfo>();
    }
    editor.savedText = levelToText(captureLevel(world, mapName));
    editorFocusCamera();
    primeStreaming(world, terrain, cam);
    loading.fadeOut();
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
    hoverProp = NO_OBJECT;
    playSfx(Sfx::UiBack);
}

void App::stopEditorName() {
    if (editor.editingName) SDL_StopTextInput(window);
    editor.editingName = false;
}

void App::setEditorTool(EditorTool tool, int picked) {
    editor.spawn.tool = tool;
    editor.spawn.picked = picked;
    session.buildPick = tool == EditorTool::Prop ? BuildPick::place((PropKind)picked) : BuildPick{};
}

void App::drawEditorPanel() {
    if (mapEditing) {
        drawMapEditorPanel();
        return;
    }
    const EditorLayout L = layoutEditor(winW, winH, editor.useOwnUi);
    const float s = uiScaleFor(winH);
    const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f }, gray{ 0.80f, 0.72f, 0.64f, 1.0f }, gold{ 1.0f, 0.78f, 0.30f, 1.0f };
    const float textH = renderer.listTextHeight("Ag");
    renderer.drawButton(L.uiToggle, lang(editor.useOwnUi ? "UILABEL_EDITOR_USE_GAME_UI" : "UILABEL_EDITOR_USE_OWN_UI"), true);
    if (!editor.useOwnUi) return;
    renderer.drawThemedPanel(L.panel);
    renderer.drawListText(langf("UILABEL_EDITOR_TITLE", { currentMap.stem }), L.panel.x + 10.0f * s, L.panel.y + 12.0f * s, gold);
    renderer.drawField(L.nameBox, editor.editingName);
    renderer.drawListText(editor.name + (editor.editingName ? "|" : ""), L.nameBox.x + 8.0f * s, L.nameBox.y + (L.nameBox.h - textH) * 0.5f, white);
    const auto button = [&](const Rect& r, const std::string& text, bool on) {
        renderer.drawToggleButton(r, text, on);
    };
    button(L.save, lang("UILABEL_SAVE"), false);
    button(L.load, lang("UILABEL_LOAD"), false);
    button(L.spawnTool, lang("UILABEL_EDITOR_SPAWN"), editor.spawn.tool == EditorTool::Spawn);
    button(L.eraseTool, lang("UILABEL_EDITOR_ERASE"), editor.spawn.tool == EditorTool::Erase);
    button(L.themeTab, lang("UILABEL_GEN_TAB_THEME"), editor.spawn.tab == EditorTab::Theme);
    button(L.clean, lang("UILABEL_EDITOR_CLEAN"), editor.cleanAsk);
    drawSpawnPicker(editor.spawn, L.picker);
    if (editor.spawn.tab == EditorTab::Theme) drawThemePanel(L.themeArea);
    const std::string text = levelToText(captureLevel(world, currentMap.stem));
    const bool dirty = text != editor.savedText || mapFileText(editor.map) != editor.savedMapText;
    float y = L.status.y;
    const auto line = [&](const std::string& t, RGBA tint) {
        renderer.drawListText(renderer.fitListText(t, L.status.w), L.status.x, y, tint);
        y += textH + 2.0f * s;
    };
    line(langf("UILABEL_EDITOR_COUNTS", { std::to_string(world.count<Entity>()), std::to_string(world.count<Prop>()), std::to_string(world.count<GroundItem>()) }), gray);
    line(langf("UILABEL_EDITOR_FILE", { std::to_string(text.size()) }) + (dirty ? " *" : ""), dirty ? gold : gray);
    line(lang("UILABEL_EDITOR_HELP1"), gray);
    line(lang("UILABEL_EDITOR_HELP2"), gray);
    button(L.exit, lang(editor.exitWarned ? "UILABEL_EDITOR_EXIT_SURE" : "UILABEL_EDITOR_EXIT"), false);
    if (editor.cleanAsk) {
        // Held down for 3 seconds, the button erases everything placed on the level
        dimScreen(0.45f);
        const MapQuestionLayout D = layoutMapQuestion(winW, winH, 2);
        renderer.drawThemedPanel(D.box);
        renderer.drawUiTextInBox(lang("MISC_EDITOR_CLEAN_Q"), D.lines[0], white);
        renderer.drawUiTextInBox(lang("MISC_EDITOR_CLEAN_Q2"), D.lines[1], RGBA{ 1.0f, 0.62f, 0.35f, 1.0f });
        const float fill = deleteHolding ? std::min(1.0f, (float)(SDL_GetTicks() - deleteHoldStart) / (float)DELETE_HOLD_MS) : 0.0f;
        renderer.drawHoldButton(D.confirm, lang("UILABEL_EDITOR_CLEAN"), fill);
        renderer.drawButton(D.cancel, lang("UILABEL_CANCEL"), true);
    }
}

// Everything placed on the level is erased (the map, and where you start, stay), for an empty level to start from
void App::cleanLevel() {
    editorRemember();
    LevelData d = captureLevel(world, currentMap.stem);
    d.objects.clear();
    applyLevel(world, terrain, rng, d);
    editor.cleanAsk = false;
    deleteHolding = false;
    hoverProp = NO_OBJECT;
    editorEdited();
    showNotice(lang("MISC_LEVEL_CLEANED"));
    playSfx(Sfx::Take);
}

bool App::editorMouseDown() {
    if (mapEditing) return mapEditorMouseDown();
    const float x = e.button.x, y = e.button.y;
    const EditorLayout L = layoutEditor(winW, winH, editor.useOwnUi);
    if (e.button.button == SDL_BUTTON_LEFT && L.uiToggle.contains(x, y)) {
        editor.useOwnUi = !editor.useOwnUi;
        setEditorTool(EditorTool::None, 0);
        stopEditorName();
        playSfx(Sfx::UiToggle);
        return true;
    }
    if (editor.cleanAsk) {
        const MapQuestionLayout D = layoutMapQuestion(winW, winH, 2);
        if (e.button.button == SDL_BUTTON_LEFT && D.confirm.contains(x, y)) {
            deleteHolding = true;
            deleteHoldStart = SDL_GetTicks();
            deleteHoldRect = D.confirm;
        } else if (D.cancel.contains(x, y) || !D.box.contains(x, y)) {
            editor.cleanAsk = false;
            deleteHolding = false;
            playSfx(Sfx::UiBack);
        }
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
            openPanel(true, true);
            playSfx(Sfx::UiClick);
        } else if (L.load.contains(x, y)) {
            openPanel(false, true);
            playSfx(Sfx::UiClick);
        } else if (L.exit.contains(x, y)) {
            editorExit();
        } else if (L.spawnTool.contains(x, y)) {
            setEditorTool(editor.spawn.tool == EditorTool::Spawn ? EditorTool::None : EditorTool::Spawn, 0);
            playSfx(Sfx::UiClick);
        } else if (L.eraseTool.contains(x, y)) {
            setEditorTool(editor.spawn.tool == EditorTool::Erase ? EditorTool::None : EditorTool::Erase, 0);
            playSfx(Sfx::UiClick);
        } else if (L.clean.contains(x, y)) {
            editor.cleanAsk = true;
            deleteHolding = false;
            setEditorTool(EditorTool::None, 0);
            playSfx(Sfx::UiClick);
        } else if (L.themeTab.contains(x, y)) {
            editor.spawn.tab = EditorTab::Theme;
            setEditorTool(EditorTool::None, 0);
            playSfx(Sfx::UiTab);
        } else {
            if (editor.spawn.tab == EditorTab::Theme && L.themeArea.contains(x, y)) {
                themeClick(L.themeArea, x, y);
                return true;
            }
            if (spawnPickerClick(editor.spawn, L.picker, x, y)) setEditorTool(editor.spawn.tool, editor.spawn.picked);
        }
        return true;
    }
    stopEditorName();
    if (e.button.button == SDL_BUTTON_RIGHT) {
        editorEraseAt(x, y);
        return true;
    }
    if (e.button.button != SDL_BUTTON_LEFT) return false;
    if (editor.spawn.tool == EditorTool::Erase) {
        editorEraseAt(x, y);
        return true;
    }
    if (editor.spawn.tool != EditorTool::None) {
        editorPlaceAt(x, y);
        return true;
    }
    return true;
}

void App::editorPlaceAt(float mx, float my) {
    const auto hit = pickGround(cam, terrain, mx, my, winW, winH);
    if (!hit) return;
    const float x = hit->first, z = hit->second;
    if (editor.spawn.tool == EditorTool::Spawn) {
        editorRemember();
        LevelInfo& info = world.resource<LevelInfo>();
        info.spawnSet = true;
        info.spawnX = x;
        info.spawnZ = z;
        playSfx(Sfx::UiOrder);
        editorEdited();
        return;
    }
    editorRemember();
    if (spawnAt(editor.spawn, x, z, true) > 0) editorEdited();
    else editor.undo.pop_back();
}

void App::editorEraseAt(float mx, float my) {
    ObjectId target = NO_OBJECT;
    const std::vector<ObjectId> units = renderer.entitiesAtPixel(world, cam, mx, my, winW, winH);
    const std::vector<ObjectId> items = renderer.groundItemsAtPixel(world, terrain, cam, mx, my, winW, winH);
    const std::vector<ObjectId> props = renderer.propsAtPixel(world, cam, mx, my, winW, winH);
    if (!units.empty()) target = units.front();
    else if (!items.empty()) target = items.front();
    else if (!props.empty()) target = props.front();
    if (target == NO_OBJECT) return;
    editorRemember();
    world.destroy(target);
    hoverProp = NO_OBJECT;
    playSfx(Sfx::Take);
    editorEdited();
}

bool App::editorDirty() {
    return levelToText(captureLevel(world, currentMap.stem)) != editor.savedText || mapFileText(editor.map) != editor.savedMapText;
}

bool App::editorSave() {
    const std::string name = safeFileName(editor.name);
    if (name.empty()) {
        refuse(lang("MISC_GEN_NEEDS_NAME"));
        return false;
    }
    editor.name = name;
    std::string err;
    const LevelData d = captureLevel(world, currentMap.stem);
    if (!writeLevelFile(levelFilePath(name), d, err)) {
        refuse(err);
        return false;
    }
    writePreview(levelFilePath(name));
    editor.savedText = levelToText(d);
    editor.exitWarned = false;
    // The theme belongs to the map, so a level and its map are saved together
    if (mapFileText(editor.map) != editor.savedMapText) {
        if (!writeMapFile(mapFilePath(currentMap.stem), editor.map, err)) {
            refuse(err);
            return false;
        }
        editor.savedMapText = mapFileText(editor.map);
    }
    showNotice(langf("MISC_LEVEL_SAVED", { name }));
    playSfx(Sfx::UiSaved);
    return true;
}

bool App::editorLoad() {
    LevelData d;
    std::string err;
    if (!readLevelFile(levelFilePath(safeFileName(editor.name)), d, err)) {
        refuse(err);
        return false;
    }
    if (d.map != currentMap.stem) {
        refuse(langf("MISC_LEVEL_OTHER_MAP", { d.map }));
        return false;
    }
    editorRemember();
    applyLevel(world, terrain, rng, d);
    editor.savedText = levelToText(captureLevel(world, currentMap.stem));
    hoverProp = NO_OBJECT;
    showNotice(langf("MISC_LOADED", { editor.name }));
    playSfx(Sfx::UiLoaded);
    return true;
}

void App::editorExit() {
    if (editorDirty() && !editor.exitWarned) {
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
    if (mapEditing) return mapEditorKeyDown();
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
    if (editor.cleanAsk) {
        if (k == SDLK_ESCAPE) {
            editor.cleanAsk = false;
            deleteHolding = false;
            playSfx(Sfx::UiBack);
        }
        return true;
    }
    if (k == SDLK_ESCAPE) {
        if (mapOpen) {
            mapOpen = false;
        } else if (editor.spawn.tool != EditorTool::None) {
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
        openPanel(true, true);
        return true;
    }
    if (k == SDLK_Q || k == SDLK_E) {
        constexpr float TURN = 3.14159265f / 12.0f;
        editor.spawn.yaw += k == SDLK_E ? TURN : -TURN;
        return true;
    }
    return false;
}
