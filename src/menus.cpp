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

std::string trimmedName(const std::string& s) {
    size_t a = s.find_first_not_of(' ');
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(' ');
    return s.substr(a, b - a + 1);
}

void popUtf8Char(std::string& s) {
    if (s.empty()) return;
    // Drop the trailing bytes of the last character, then its first byte, so no half character is left
    while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0x80) s.pop_back();
    if (!s.empty()) s.pop_back();
}

std::optional<SaveSummary> findSaveByName(const std::vector<SaveSummary>& saves, const std::string& name) {
    for (const SaveSummary& s : saves) {
        if (s.name == name) return s;
    }
    return std::nullopt;
}

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

std::optional<MapChoice> findMap(const std::vector<MapChoice>& maps, const std::string& source, const std::string& stem) {
    for (const MapChoice& m : maps) {
        if (m.source == source && m.stem == stem) return m;
    }
    return std::nullopt;
}

size_t visibleSaveRows(int H) {
    const float s = uiScaleFor(H);
    const float fixed = 246.0f * s;
    const float pitch = 32.0f * s;
    const float space = H - 16.0f * s - fixed;
    size_t rows = space > 0.0f ? (size_t)(space / pitch) : 0;
    return std::clamp(rows, MIN_SAVE_ROWS, MAX_SAVE_ROWS);
}

PanelGeometry panelGeometry(int W, int H) {
    const float s = uiScaleFor(H);
    const float btnH = 44.0f * s, gap = 8.0f * s, rowGap = 4.0f * s;
    const float pad = 16.0f * s, hdrH = 30.0f * s, band = 24.0f * s;
    const float rowH = 28.0f * s, pitch = rowH + rowGap;
    const float pw = 1100.0f * s, btnW = 180.0f * s;
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
    const float s = uiScaleFor(H);
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
        if (ask == Ask::LeaveUnsaved) { yes = Action::LeaveYes; no = Action::LeaveNo; }
        out.push_back(Button{ lang("UILABEL_YES"), d.yes, yes });
        out.push_back(Button{ lang("UILABEL_NO"), d.no, no });
        return out;
    }

    PanelGeometry g = panelGeometry(W, H);
    const float x = g.box.x + g.pad, w = g.box.w - 2.0f * g.pad;

    if (saveMode) out.push_back(Button{ lang("UILABEL_SAVE"), g.saveBtn, Action::ConfirmName });

    out.push_back(Button{ lang("UILABEL_NAME"), Rect{ x, g.headerY, COL_TYPE * w, g.headerH }, Action::SortName });
    out.push_back(Button{ lang("UILABEL_TIME"), Rect{ x + COL_TIME * w, g.headerY, (1.0f - COL_TIME) * w, g.headerH }, Action::SortTime });

    const size_t rows = visibleSaveRows(H);
    size_t first = std::min(listOffset, saves.size());
    size_t last = std::min(first + rows, saves.size());

    if (first > 0) out.push_back(Button{ "", g.aboveBand, Action::ScrollUp });
    const float uiScale = H / 720.0f;
    const float delW = 84.0f * uiScale, delGap = 8.0f * uiScale;
    float y = g.listTop;
    for (size_t i = first; i < last; ++i) {
        out.push_back(Button{ "", Rect{ x, y, w - delW - delGap, g.rowH }, Action::LoadSlot, true, (int)i });
        out.push_back(Button{ lang("UILABEL_DELETE"), Rect{ x + w - delW, y, delW, g.rowH }, Action::DeleteSave, true, (int)i });
        y += g.rowPitch;
    }
    if (last < saves.size()) out.push_back(Button{ "", g.belowBand, Action::ScrollDown });

    out.push_back(Button{ fromTitle ? lang("UILABEL_RETURN_TO_TITLE") : lang("UILABEL_CLOSE"), Rect{ x, g.closeY, w, g.btnH },
                          Action::CancelName });
    return out;
}

std::vector<Button> layoutButtons(Screen screen, int W, int H, const std::vector<SaveSummary>& saves) {
    const float s = uiScaleFor(H);
    const float bh = 56.0f * s, gap = 14.0f * s;
    const float bw = 380.0f * s;
    std::vector<Button> out;
    auto add = [&](const std::string& label, Action action, float x, float y, bool enabled = true) {
        out.push_back(Button{ label, Rect{ x, y, bw, bh }, action, enabled });
    };

    const bool hasSave = !saves.empty();

    switch (screen) {
    case Screen::Title: {
        const TitleLayout tl = layoutTitle(W, H);
        const float iw = titleClamp(240.0f * s, W * 0.25f, 380.0f * s);
        const float ih = 44.0f * s, langH = 60.0f * s, ig = titleClamp(5.0f * s, H * 0.008f, 10.0f * s);
        const float total = 6.0f * (ih + ig) + langH;
        const float x = W - tl.side - iw;
        float y = (H - total) * 0.5f;
        auto item = [&](const std::string& label, Action action, float h, bool enabled = true) {
            out.push_back(Button{ label, Rect{ x, y, iw, h }, action, enabled });
            y += h + ig;
        };
        item(lang("UILABEL_CONTINUE"), Action::Continue, ih, hasSave);
        item(lang("UILABEL_NEW_GAME"), Action::NewGame, ih);
        item(lang("UILABEL_LOAD_GAME"), Action::LoadGame, ih, hasSave);
        item(lang("UILABEL_WORLD_GENERATION"), Action::WorldGeneration, ih);
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
    case Screen::WorldGen:
        break;
    }
    return out;
}

std::string gameTitle() {
    return lang("GAME_TITLE_FIRST") + " " + lang("GAME_TITLE_SECOND");
}

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
    case Screen::Title: return soundtrackNamed("Title");
    case Screen::Settings: return soundtrackNamed("Settings");
    case Screen::MapSelect:
    case Screen::ScenarioSelect: return soundtrackNamed("ScenarioSelect");
    case Screen::PartySetup: return soundtrackNamed("PartySetup");
    case Screen::Pause: return soundtrackNamed("Pause");
    case Screen::Playing: return soundtrackNamed("Playing");
    case Screen::WorldGen: return soundtrackNamed("Settings");
    }
    return {};
}
