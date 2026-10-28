#include "settings_menu.h"
#include "fontlist.h"
#include "language.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

// The panel is laid out in units u, where u is the panel's height over PANEL_H. Every size below is in u, so the
// menu keeps its proportions at any window size.
constexpr float PANEL_W = 900.0f;      // widest the panel is, at a 720-pixel-high window
constexpr float PANEL_H = 640.0f;      // tallest the panel is, at a 720-pixel-high window
constexpr float PAD = 28.0f;           // inset of the contents from the panel's left and right edges
constexpr float TITLE_H = 64.0f;       // room above the tabs for the heading
constexpr float TAB_H = 40.0f;
constexpr float TAB_GAP = 8.0f;
constexpr float CONTENT_GAP = 18.0f;   // between the tabs and the first row
constexpr float ROW_STEP = 44.0f;      // from one row to the next
constexpr float ROW_H = 40.0f;         // a row's height
constexpr float ROW_VPAD = 3.0f;       // a control's inset from the top and bottom of its row
constexpr float LABEL_INSET = 14.0f;   // the label's inset from the left of the row
constexpr float COL_SPLIT = 0.44f;     // the controls column starts this far across the row
constexpr float COL_RIGHT = 14.0f;     // and stops this far from the row's right edge
constexpr float TOGGLE_W = 180.0f;     // an on/off button's width
constexpr float LEVEL_BTN_W = 96.0f;   // a level row's on/off button
constexpr float LEVEL_TEXT_W = 60.0f;  // a level row's percentage
constexpr float LEVEL_GAP = 8.0f;      // between a level row's parts
constexpr float TRACK_H = 8.0f;        // a slider's track
constexpr float FOOT_H = 44.0f;
constexpr float FOOT_BOTTOM = 16.0f;   // the footer's distance from the panel's bottom edge
constexpr float DEFAULTS_W = 220.0f;
constexpr float BACK_W = 200.0f;
constexpr float DROP_EDGE = 8.0f;      // a dropdown list keeps this far from the window's edge
constexpr int DROP_MIN_ROWS = 4;       // a list opens downward when this many of its rows fit there

// A resolution option, labeled "W x H" with an optional note
DropOption sizeOption(int w, int h, const std::string& note = std::string()) {
    return DropOption{ std::to_string(w) + " x " + std::to_string(h) + note, 0, w, h, true };
}

// The index of the option with this value, or 0 when none has it
int indexOfValue(const std::vector<DropOption>& opts, int value) {
    for (size_t i = 0; i < opts.size(); ++i) {
        if (opts[i].value == value) return (int)i;
    }
    return 0;
}

// The index of the option whose size is nearest to (w, h). An exact match is nearest of all.
int indexOfSize(const std::vector<DropOption>& opts, int w, int h) {
    int best = 0;
    long long bestD = -1;
    for (size_t i = 0; i < opts.size(); ++i) {
        const long long dw = opts[i].w - w, dh = opts[i].h - h;
        const long long d = dw * dw + dh * dh;
        if (bestD < 0 || d < bestD) {
            bestD = d;
            best = (int)i;
        }
    }
    return best;
}

// A dropdown row. The current index is clamped, and an empty list shows one greyed "None available" entry.
RowSpec makeDropdown(RowId id, const std::string& label, std::vector<DropOption> options, int current, bool enabled) {
    RowSpec r;
    r.id = id;
    r.kind = RowKind::Dropdown;
    r.label = label;
    if (options.empty()) {
        options.push_back(DropOption{ lang("UILABEL_NONE_AVAILABLE"), 0, 0, 0, false });
        current = 0;
        enabled = false;
    }
    r.current = std::clamp(current, 0, (int)options.size() - 1);
    r.text = options[r.current].label;
    r.options = std::move(options);
    r.enabled = enabled;
    return r;
}

RowSpec makeToggle(RowId id, const std::string& label, bool on) {
    RowSpec r;
    r.id = id;
    r.kind = RowKind::Toggle;
    r.label = label;
    r.on = on;
    r.text = on ? lang("UILABEL_ON") : lang("UILABEL_OFF");
    return r;
}

// A button row: its control is a button with the text, and the row does what its id says
RowSpec makeAction(RowId id, const std::string& label, const std::string& text) {
    RowSpec r;
    r.id = id;
    r.kind = RowKind::Action;
    r.label = label;
    r.text = text;
    return r;
}

// A slider from 100% to 200%, in steps of 5: the level 0 to 100 is the percentage above 100
// A scale slider: the percentage runs from UI_SCALE_MIN to UI_SCALE_MAX, and the track shows where it sits.
RowSpec makeScale(RowId id, const std::string& label, int percent) {
    RowSpec r;
    r.id = id;
    r.kind = RowKind::Scale;
    r.label = label;
    const int span = UI_SCALE_MAX - UI_SCALE_MIN;
    r.current = std::clamp(((percent - UI_SCALE_MIN) * 100 + span / 2) / span, 0, 100);
    r.text = std::to_string(percent) + "%";
    return r;
}

RowSpec makeLevel(RowId id, const std::string& label, int level, bool on) {
    RowSpec r;
    r.id = id;
    r.kind = RowKind::Level;
    r.label = label;
    r.current = std::clamp(level, 0, 100);
    r.on = on;
    r.text = std::to_string(r.current) + "%";
    return r;
}

RowSpec makeText(const std::string& keys, const std::string& does) {
    RowSpec r;
    r.id = RowId::Reference;
    r.kind = RowKind::Text;
    r.label = keys;
    r.text = does;
    return r;
}

std::vector<RowSpec> graphicsRows(const GameSettings& s, const DisplayInfo& d) {
    std::vector<RowSpec> rows;

    std::vector<DropOption> modes;
    for (int i = 0; i < DISPLAY_MODE_COUNT; ++i) modes.push_back(DropOption{ lang(DISPLAY_MODE_NAMES[i]), i, 0, 0, true });
    rows.push_back(makeDropdown(RowId::Display, lang("UILABEL_DISPLAY_MODE"), modes, s.displayMode, true));

    std::vector<DropOption> monitors;
    for (size_t i = 0; i < d.monitors.size(); ++i) {
        const MonitorInfo& m = d.monitors[i];
        const std::string size = m.w > 0 && m.h > 0
            ? " (" + std::to_string(m.w) + " x " + std::to_string(m.h) + ")" : std::string();
        monitors.push_back(DropOption{ std::to_string(i + 1) + ". " + m.name + size, (int)i, 0, 0, true });
    }
    rows.push_back(makeDropdown(RowId::Monitor, lang("UILABEL_MONITOR"), monitors, d.chosen, true));

    // Windowed offers every standard size that fits the monitor, full screen every mode it has, and borderless
    // always covers the desktop, so its one size is shown but cannot be changed.
    std::vector<DropOption> sizes;
    bool sizeEnabled = true;
    if (s.displayMode == DISPLAY_WINDOWED) {
        for (const Resolution& r : windowedSizes(d.desktopW, d.desktopH)) sizes.push_back(sizeOption(r.w, r.h));
    } else if (s.displayMode == DISPLAY_FULLSCREEN) {
        for (const Resolution& r : fullscreenSizes(d.modes)) sizes.push_back(sizeOption(r.w, r.h));
    } else {
        sizes.push_back(sizeOption(d.desktopW, d.desktopH, " (desktop)"));
        sizeEnabled = false;
    }
    rows.push_back(makeDropdown(RowId::Resolution, lang("UILABEL_RESOLUTION"), sizes,
                                indexOfSize(sizes, s.width, s.height), sizeEnabled));

    // Refresh rates only apply to full screen, at the size chosen. Default leaves it to the monitor.
    std::vector<DropOption> rates{ DropOption{ lang("UILABEL_DEFAULT"), 0, 0, 0, true } };
    if (s.displayMode == DISPLAY_FULLSCREEN) {
        for (int hz : refreshRates(d.modes, s.width, s.height)) {
            rates.push_back(DropOption{ std::to_string(hz) + " Hz", hz, 0, 0, true });
        }
    }
    rows.push_back(makeDropdown(RowId::Refresh, lang("UILABEL_REFRESH_RATE"), rates,
                                indexOfValue(rates, s.refreshHz), rates.size() > 1));

    rows.push_back(makeToggle(RowId::VSync, lang("UILABEL_VSYNC"), s.vsync));

    std::vector<DropOption> fps;
    for (int i = 0; i < FPS_CHOICE_COUNT; ++i) {
        const int v = FPS_CHOICES[i];
        fps.push_back(DropOption{ v == 0 ? std::string(lang("UILABEL_UNLIMITED")) : langf("UILABEL_FPS", { std::to_string(v) }), v, 0, 0, true });
    }
    rows.push_back(makeDropdown(RowId::FpsLimit, lang("UILABEL_FRAME_LIMIT"), fps, indexOfValue(fps, s.fpsLimit), true));
    return rows;
}

std::vector<RowSpec> interfaceRows(const GameSettings& s) {
    std::vector<RowSpec> rows;
    std::vector<DropOption> saves;
    for (int i = 0; i < AUTOSAVE_COUNT; ++i) {
        const int m = AUTOSAVE_CHOICES[i];
        std::string label;
        if (m == 0) label = lang("UILABEL_OFF");
        else if (m == 1) label = lang("UILABEL_MINUTE");
        else label = langf("UILABEL_MINUTES", { std::to_string(m) });
        saves.push_back(DropOption{ label, m, 0, 0, true });
    }
    rows.push_back(makeDropdown(RowId::Autosave, lang("UILABEL_AUTOSAVE"), saves, indexOfValue(saves, s.autosaveMinutes), true));
    rows.push_back(makeToggle(RowId::UnitNames, lang("UILABEL_UNIT_NAMES"), s.showUnitNames));
    rows.push_back(makeScale(RowId::FontScale, lang("UILABEL_UI_SCALE"), s.fontScale));
    rows.push_back(makeAction(RowId::ResetPanels, lang("UILABEL_RESET_PANELS"), lang("UILABEL_RESET")));
    return rows;
}

// The Language tab: the language, then the font. The start menu's Language button opens this same tab.
std::vector<RowSpec> languageRows(const GameSettings& s) {
    std::vector<RowSpec> rows;
    // Each option's value is its place in the list of languages, which pickOption maps back to the id
    std::vector<DropOption> langs;
    int currentLanguage = 0;
    const std::vector<LanguageChoice>& choices = languageChoices();
    for (size_t i = 0; i < choices.size(); ++i) {
        langs.push_back(DropOption{ choices[i].label, (int)i, 0, 0, true });
        if (choices[i].id == s.language) currentLanguage = (int)i;
    }
    rows.push_back(makeDropdown(RowId::Language, lang("UILABEL_LANGUAGE_TAB"), langs, currentLanguage, true));
    // The font: Automatic (the built-in fonts, the default), or any font installed on this computer
    std::vector<DropOption> fonts{ DropOption{ lang("UILABEL_FONT_AUTOMATIC"), 0, 0, 0, true } };
    int currentFont = 0;
    const std::vector<std::string>& installed = installedFontPaths();
    for (size_t i = 0; i < installed.size(); ++i) {
        fonts.push_back(DropOption{ fontDisplayName(installed[i]), (int)i + 1, 0, 0, true });
        if (installed[i] == s.fontPath) currentFont = (int)i + 1;
    }
    rows.push_back(makeDropdown(RowId::Font, lang("UILABEL_FONT"), fonts, currentFont, true));
    return rows;
}

std::vector<RowSpec> controlsRows(const GameSettings& s) {
    std::vector<RowSpec> rows;
    rows.push_back(makeToggle(RowId::EdgeScroll, lang("UILABEL_EDGE_SCROLL"), s.edgeScroll));
    for (int i = 0; i < CONTROL_NOTE_COUNT; ++i) {
        rows.push_back(makeText(lang(CONTROL_NOTES[i].keys), lang(CONTROL_NOTES[i].does)));
    }
    return rows;
}

std::vector<RowSpec> audioRows(const GameSettings& s) {
    std::vector<RowSpec> rows;
    rows.push_back(makeLevel(RowId::MusicLevel, lang("UILABEL_MUSIC"), s.musicVolume, s.musicOn));
    rows.push_back(makeLevel(RowId::AmbientLevel, lang("UILABEL_AMBIENT"), s.ambientVolume, s.ambientOn));
    rows.push_back(makeLevel(RowId::EffectsLevel, lang("UILABEL_SOUND_EFFECTS"), s.effectsVolume, s.effectsOn));
    return rows;
}

} // namespace

std::vector<RowSpec> settingsRows(SettingsTab tab, const GameSettings& s, const DisplayInfo& d) {
    switch (tab) {
    case SettingsTab::Graphics:  return graphicsRows(s, d);
    case SettingsTab::Interface: return interfaceRows(s);
    case SettingsTab::Language:  return languageRows(s);
    case SettingsTab::Controls:  return controlsRows(s);
    case SettingsTab::Audio:     return audioRows(s);
    }
    return {};
}

SettingsLayout layoutSettings(int W, int H, const std::vector<RowSpec>& rows, const SettingsMenuState& st) {
    SettingsLayout L;
    const float s = H / 720.0f;
    const float pw = std::min(W - 32.0f, PANEL_W * s);
    const float ph = std::min(H - 32.0f, PANEL_H * s);
    const float px = (W - pw) * 0.5f, py = (H - ph) * 0.5f;
    L.panel = Rect{ px, py, pw, ph };

    // Sizes come from the panel's own height, the same way drawing measures them
    const float u = ph / PANEL_H;
    const float pad = PAD * u;
    const float tabY = py + TITLE_H * u, tabH = TAB_H * u, tabGap = TAB_GAP * u;
    const float tabW = (pw - 2.0f * pad - (SETTINGS_TAB_COUNT - 1) * tabGap) / SETTINGS_TAB_COUNT;
    for (int i = 0; i < SETTINGS_TAB_COUNT; ++i) {
        L.tabs.push_back(Rect{ px + pad + i * (tabW + tabGap), tabY, tabW, tabH });
    }

    const float footH = FOOT_H * u;
    const float footY = py + ph - FOOT_BOTTOM * u - footH;
    L.defaults = Rect{ px + pad, footY, DEFAULTS_W * u, footH };
    L.back = Rect{ px + pw - pad - BACK_W * u, footY, BACK_W * u, footH };

    const float cx = px + pad, cw = pw - 2.0f * pad;
    const float top = tabY + tabH + CONTENT_GAP * u;
    const float rowStep = ROW_STEP * u, rowH = ROW_H * u, vpad = ROW_VPAD * u;
    const float colX = cx + cw * COL_SPLIT, colW = cw * (1.0f - COL_SPLIT) - COL_RIGHT * u;

    L.rows.resize(rows.size());
    for (size_t i = 0; i < rows.size(); ++i) {
        RowLayout& R = L.rows[i];
        const float by = top + static_cast<float>(i) * rowStep;
        const float cy = by + vpad, ch = rowH - 2.0f * vpad;
        R.band = Rect{ cx, by, cw, rowH };
        R.text = Rect{ colX, by, colW, rowH };
        switch (rows[i].kind) {
        case RowKind::Dropdown:
            R.control = Rect{ colX, cy, colW, ch };
            break;
        case RowKind::Action:
        case RowKind::Toggle:
            R.control = Rect{ colX, cy, std::min(colW, TOGGLE_W * u), ch };
            break;
        case RowKind::Level:
        case RowKind::Scale: {
            const float btnW = LEVEL_BTN_W * u, txtW = LEVEL_TEXT_W * u, gap = LEVEL_GAP * u;
            R.toggle = Rect{ colX + colW - btnW, cy, btnW, ch };
            R.value = Rect{ R.toggle.x - gap - txtW, cy, txtW, ch };
            const float trackW = std::max(0.0f, R.value.x - gap - colX);
            R.track = Rect{ colX, by + rowH * 0.5f - TRACK_H * u * 0.5f, trackW, TRACK_H * u };
            R.control = Rect{ colX, by, trackW, rowH };
            break;
        }
        case RowKind::Text:
            break;
        }
    }

    // An open dropdown lists its options under its box, or above it when there is more room there
    if (st.openRow >= 0 && st.openRow < (int)rows.size() && rows[st.openRow].kind == RowKind::Dropdown
        && rows[st.openRow].enabled && !rows[st.openRow].options.empty()) {
        const RowSpec& row = rows[st.openRow];
        const Rect ctrl = L.rows[st.openRow].control;
        const float itemH = ctrl.h;
        const int n = (int)row.options.size();
        const int want = std::min(n, SETTINGS_DROP_ROWS);
        const int maxDown = (int)std::floor((H - DROP_EDGE * u - (ctrl.y + ctrl.h)) / itemH);
        const int maxUp = (int)std::floor((ctrl.y - DROP_EDGE * u) / itemH);
        const bool down = std::min(want, maxDown) >= std::min(want, DROP_MIN_ROWS) || maxDown >= maxUp;
        const int visible = std::max(1, std::min(want, down ? maxDown : maxUp));
        const float y0 = down ? ctrl.y + ctrl.h : ctrl.y - visible * itemH;

        L.drop.open = true;
        L.drop.row = st.openRow;
        L.drop.count = n;
        L.drop.first = std::clamp(st.dropFirst, 0, n - visible);
        L.drop.box = Rect{ ctrl.x, y0, ctrl.w, visible * itemH };
        for (int i = 0; i < visible; ++i) {
            L.drop.items.push_back(Rect{ ctrl.x, y0 + i * itemH, ctrl.w, itemH });
        }
    }
    return L;
}

SettingsClick hitSettings(const SettingsLayout& L, const std::vector<RowSpec>& rows, float x, float y) {
    if (L.drop.open) {
        for (size_t i = 0; i < L.drop.items.size(); ++i) {
            if (!L.drop.items[i].contains(x, y)) continue;
            const int index = L.drop.first + (int)i;
            if (rows[L.drop.row].options[index].enabled) return { ClickKind::PickOption, L.drop.row, index };
            return { ClickKind::None, L.drop.row, index };
        }
        return { ClickKind::CloseDropdown, L.drop.row, -1 };
    }

    for (size_t i = 0; i < L.tabs.size(); ++i) {
        if (L.tabs[i].contains(x, y)) return { ClickKind::Tab, -1, (int)i };
    }
    if (L.defaults.contains(x, y)) return { ClickKind::Defaults, -1, -1 };
    if (L.back.contains(x, y)) return { ClickKind::Back, -1, -1 };

    for (size_t i = 0; i < rows.size() && i < L.rows.size(); ++i) {
        const RowSpec& r = rows[i];
        const RowLayout& R = L.rows[i];
        const int row = (int)i;
        switch (r.kind) {
        case RowKind::Dropdown:
            if (r.enabled && R.control.contains(x, y)) return { ClickKind::OpenDropdown, row, -1 };
            break;
        case RowKind::Action:
            if (r.enabled && R.control.contains(x, y)) return { ClickKind::Action, row, -1 };
            break;
        case RowKind::Toggle:
            if (r.enabled && R.control.contains(x, y)) return { ClickKind::ToggleRow, row, -1 };
            break;
        case RowKind::Scale:
            if (r.enabled && R.control.contains(x, y)) return { ClickKind::LevelDrag, row, levelAtX(R.track, x) };
            break;
        case RowKind::Level:
            if (R.toggle.contains(x, y)) return { ClickKind::LevelToggle, row, -1 };
            if (R.control.contains(x, y)) return { ClickKind::LevelDrag, row, levelAtX(R.track, x) };
            break;
        case RowKind::Text:
            break;
        }
    }
    return {};
}

int levelAtX(const Rect& track, float x) {
    if (track.w <= 0.0f) return 0;
    const float t = std::clamp((x - track.x) / track.w, 0.0f, 1.0f);
    return (int)std::lround(t * 100.0f);
}

void settingsSelectTab(SettingsMenuState& st, SettingsTab tab) {
    st.tab = tab;
    st.openRow = -1;
    st.dropFirst = 0;
    st.dragRow = -1;
}

void settingsOpenDropdown(SettingsMenuState& st, const std::vector<RowSpec>& rows, int row) {
    if (row < 0 || row >= (int)rows.size()) return;
    const RowSpec& r = rows[row];
    if (r.kind != RowKind::Dropdown || !r.enabled || r.options.empty()) return;
    st.openRow = row;
    st.dropFirst = r.current;     // the layout clamps this, so the current option is in view
    st.dragRow = -1;
}

void settingsCloseDropdown(SettingsMenuState& st) {
    st.openRow = -1;
    st.dropFirst = 0;
}

void settingsScrollDropdown(SettingsMenuState& st, const SettingsLayout& L, int notches) {
    if (!L.drop.open) return;
    const int maxFirst = std::max(0, L.drop.count - (int)L.drop.items.size());
    st.dropFirst = std::clamp(L.drop.first - notches, 0, maxFirst);
}

SettingsChange pickOption(RowId id, const DropOption& option, GameSettings& s) {
    SettingsChange c;
    if (!option.enabled) return c;
    switch (id) {
    case RowId::Display:
        s.displayMode = option.value;
        c.display = true;
        break;
    case RowId::Monitor:
        s.monitor = option.value;
        c.display = true;
        break;
    case RowId::Resolution:
        // A new size resets the refresh rate, since the rates a size has differ
        s.width = option.w;
        s.height = option.h;
        s.refreshHz = 0;
        c.display = true;
        break;
    case RowId::Refresh:
        s.refreshHz = option.value;
        c.display = true;
        break;
    case RowId::FpsLimit:
        s.fpsLimit = option.value;
        break;
    case RowId::Autosave:
        s.autosaveMinutes = option.value;
        break;
    case RowId::Language: {
        const std::vector<LanguageChoice>& choices = languageChoices();
        if (option.value >= 0 && option.value < (int)choices.size()) s.language = choices[option.value].id;
        c.language = true;
        break;
    }
    case RowId::Font: {
        const std::vector<std::string>& installed = installedFontPaths();
        s.fontPath = option.value > 0 && option.value <= (int)installed.size() ? installed[option.value - 1] : std::string();
        c.fonts = true;
        break;
    }
    default:
        return c;
    }
    c.any = true;
    return c;
}

SettingsChange setRowOn(RowId id, bool on, GameSettings& s) {
    SettingsChange c;
    switch (id) {
    case RowId::VSync:
        s.vsync = on;
        c.vsync = true;
        break;
    case RowId::UnitNames:
        s.showUnitNames = on;
        break;
    case RowId::EdgeScroll:
        s.edgeScroll = on;
        break;
    case RowId::MusicLevel:
        s.musicOn = on;
        c.audio = true;
        break;
    case RowId::AmbientLevel:
        s.ambientOn = on;
        c.audio = true;
        break;
    case RowId::EffectsLevel:
        s.effectsOn = on;
        c.audio = true;
        break;
    default:
        return c;
    }
    c.any = true;
    return c;
}

SettingsChange setRowLevel(RowId id, int level, GameSettings& s) {
    SettingsChange c;
    if (id == RowId::FontScale) {
        // Steps of 5 percent from 50 to 200: the level 0 to 100 spans that range and snaps to the nearest step
        const int span = UI_SCALE_MAX - UI_SCALE_MIN;
        const int offset = std::clamp(level, 0, 100) * span / 100;
        const int pct = UI_SCALE_MIN + UI_SCALE_STEP * ((offset + UI_SCALE_STEP / 2) / UI_SCALE_STEP);
        if (s.fontScale != pct) {
            s.fontScale = pct;
            c.fonts = true;
            c.any = true;
        }
        return c;
    }
    const int v = std::clamp(level, 0, 100);
    switch (id) {
    case RowId::MusicLevel:
        s.musicVolume = v;
        c.audio = true;
        break;
    case RowId::AmbientLevel:
        s.ambientVolume = v;
        c.audio = true;
        break;
    case RowId::EffectsLevel:
        s.effectsVolume = v;
        c.audio = true;
        break;
    default:
        return c;
    }
    c.any = true;
    return c;
}

SettingsChange resetAllSettings(GameSettings& s) {
    s = GameSettings{};
    SettingsChange c;
    c.display = true;
    c.vsync = true;
    c.fonts = true;
    c.language = true;
    c.audio = true;
    c.any = true;
    return c;
}
