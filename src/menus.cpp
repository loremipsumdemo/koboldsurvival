#include "menus.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include "language.h"
#include "components.h"
#include "ecs.h"
#include "items.h"
#include "inventory.h"
#include "equip.h"
#include "combat.h"
#include "stats.h"
#include "props.h"
#include "systems.h"
#include "textcase.h"
#include "ui_layout.h"
#include "play_state.h"
#include "pak.h"

using namespace play;

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
        // A column down the right of the screen, centered on its height, its right edge on the screen's margin. The
        // Language item is taller: it shows the language in use on a second line (titleItemSub).
        const TitleLayout tl = layoutTitle(W, H);
        const float iw = titleClamp(240.0f * s, W * 0.25f, 380.0f * s);
        const float ih = 44.0f * s, langH = 60.0f * s, ig = titleClamp(5.0f * s, H * 0.008f, 10.0f * s);
        const float total = 5.0f * ih + langH + 5.0f * ig;
        const float x = W - tl.side - iw;
        float y = (H - total) * 0.5f;
        auto item = [&](const std::string& label, Action action, float h, bool enabled = true) {
            out.push_back(Button{ label, Rect{ x, y, iw, h }, action, enabled });
            y += h + ig;
        };
        item(lang("UILABEL_CONTINUE"), Action::Continue, ih, hasSave);
        item(lang("UILABEL_NEW_GAME"), Action::NewGame, ih);
        item(lang("UILABEL_LOAD_GAME"), Action::LoadGame, ih, hasSave);
        item(lang("UILABEL_SETTINGS"), Action::Settings, ih);
        item(lang("UILABEL_LANGUAGE_LABEL"), Action::Language, langH);
        item(lang("UILABEL_QUIT"), Action::Quit, ih);
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
    case Screen::ScenarioSelect:
    case Screen::PartySetup:
        break;
    }
    return out;
}

// The game's name is in the language files, in two words: GAME_TITLE_FIRST (shown green on the title screen) and
// GAME_TITLE_SECOND. gameTitle() is the two together, for the window's title bar.
std::string gameTitle() {
    return lang("GAME_TITLE_FIRST") + " " + lang("GAME_TITLE_SECOND");
}

// The title screen's notice board: the lines of data/motd.txt (the Message of the Day) in the pak, read once. Edit that
// file for update notes. Missing, the board is empty.
const std::vector<std::string>& messageOfTheDay() {
    static std::vector<std::string> lines;
    static bool read = false;
    if (read) return lines;
    read = true;
    const unsigned char* bytes = nullptr;
    size_t size = 0;
    if (!pakFind("data/motd.txt", bytes, size)) return lines;
    std::string line;
    for (size_t i = 0; i <= size; ++i) {
        const char c = i < size ? (char)bytes[i] : '\n';
        if (c == '\r') continue;
        if (c == '\n') {
            lines.push_back(line);
            line.clear();
        } else {
            line += c;
        }
    }
    while (!lines.empty() && lines.back().empty()) lines.pop_back();
    return lines;
}

Soundtrack soundtrackFor(Screen screen) {
    switch (screen) {
    case Screen::Title:
        return { "title.mp3", "" };
    case Screen::Settings:
    case Screen::MapSelect:
    case Screen::ScenarioSelect:
    case Screen::PartySetup:
    case Screen::Pause:
        return {};
    case Screen::Playing:
        return { "bgm_01.mp3", "amb_01.mp3" };
    }
    return {};
}
