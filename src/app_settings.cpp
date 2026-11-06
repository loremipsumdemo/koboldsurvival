// Applying and saving settings, and clicks on the Settings screen.
#include "user_files.h"
#include "app.h"

using namespace play;

void App::applyAudio() {
    audio::setGlobal(settings.globalVolume, settings.globalOn);
    audio::setBus(audio::Bus::Music, settings.musicVolume, settings.musicOn);
    audio::setBus(audio::Bus::Ambient, settings.ambientVolume, settings.ambientOn);
    audio::setBus(audio::Bus::Effects, settings.effectsVolume, settings.effectsOn);
    audio::setBus(audio::Bus::Interface, settings.interfaceVolume, settings.interfaceOn);
}

void App::applyVideo() {
    displayInfo = queryDisplays(settings.monitor);
    applyDisplay(window, settings, displayInfo);
    SDL_GL_SetSwapInterval(settings.vsync ? 1 : 0);
}

void App::saveSettingsFile() { saveSettings(settings, settingsFilePath()); }

void App::applyFontScale() {
    if (settings.fontScale == appliedFontScale && settings.fontPath == appliedFontPath
        && settings.language == appliedLanguage) return;
    UiFonts next = openUiFonts(settings.fontScale, settings.fontPath, settings.language);
    if (!next.valid()) {
        closeUiFonts(next);
        return;
    }
    renderer.setFonts(next.hud, next.ui, next.list);
    closeUiFonts(fonts);
    fonts = next;
    appliedMenuPercent = 0;   // the menu fonts follow at the top of the next frame
    // The name's font follows the language and the font picked, too
    std::vector<TTF_Font*> nextName;
    if (TTF_Font* f = openFontChain(TITLE_NAME_POINTS, true, settings.fontPath, settings.language, nextName)) {
        renderer.setTitleFont(f);
        closeFonts(nameFontOwned);
        nameFontOwned = nextName;
    }
    appliedFontScale = settings.fontScale;
    appliedFontPath = settings.fontPath;
    appliedLanguage = settings.language;
}

void App::applyLanguage() {
    std::string err;
    if (loadLanguage(settings.language, err)) return;
    showNotice(err + ". Using the default language.");
    settings.language = LANGUAGE_DEFAULT_ID;
    loadLanguage(settings.language, err);
    saveSettingsFile();
}

void App::settingsCommit(const SettingsChange& c, bool save) {
    if (!c.any) return;
    if (c.display) applyVideo();
    else if (c.vsync) SDL_GL_SetSwapInterval(settings.vsync ? 1 : 0);
    if (c.audio) applyAudio();
    if (c.language) {
        applyLanguage();
        // The party's names came from the old language, so the party is rolled again in the new one
        party = scenarioParty(scenarioPick, partyRng, partyPeople(party));
    }
    if (c.fonts || c.language) applyFontScale();
    if (save) saveSettingsFile();
}

void App::leaveSettings() {
    settingsUi.openRow = -1;
    settingsUi.dragRow = -1;
    screen = backScreen;
}

void App::resetPanels() {
    squadPlace = defaultPanelPlace(PanelId::Squad);
    logPlace = defaultPanelPlace(PanelId::Info);
    unitPlace = defaultPanelPlace(PanelId::Unit);
    for (int i = 0; i < PANEL_COUNT; ++i) {
        panelLocked[i] = true;
        settings.panels[(size_t)i] = {};
    }
    saveSettingsFile();
}

void App::settingsClick(float x, float y, int W, int H) {
    const std::vector<RowSpec> rows = settingsRows(settingsUi.tab, settings, displayInfo);
    const SettingsClick c = hitSettings(layoutSettings(W, H, rows, settingsUi), rows, x, y);
    // Every control that does something clicks. Closing a dropdown, and a click on nothing, do not.
    // A tab, a switch, a dropdown and Back each have their own sound. Closing a dropdown, a slider drag (it sounds as
    // it is let go) and a click on nothing are silent.
    switch (c.kind) {
    case ClickKind::None: case ClickKind::CloseDropdown: case ClickKind::LevelDrag: break;
    case ClickKind::Tab: playSfx(Sfx::UiTab); break;
    case ClickKind::ToggleRow: case ClickKind::LevelToggle: playSfx(Sfx::UiToggle); break;
    case ClickKind::OpenDropdown: playSfx(Sfx::UiMenuOpen); break;
    case ClickKind::Back: playSfx(Sfx::UiBack); break;
    default: playSfx(Sfx::UiClick); break;
    }
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
}
