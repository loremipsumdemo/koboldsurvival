// Saving and loading: the save list, the save and load panel, and deleting a save.
#include "app.h"

using namespace play;

void App::refreshSaves() {
    saves = listSaves();
    sortSaves(saves, sortKey, sortAsc);
}

void App::closePanel() {
    panelOpen = false;
    clearQuestion();
    SDL_StopTextInput(window);
}

void App::openPanel(bool saveMode) {
    panelOpen = true;
    panelSaveMode = saveMode;
    panelFrom = screen;
    clearQuestion();
    saveName.clear();
    listOffset = 0;
    sortKey = SortKey::Time;
    sortAsc = false;
    refreshSaves();
    if (saveMode) SDL_StartTextInput(window);
    else SDL_StopTextInput(window);
}

bool App::wouldLoseProgress() {
    if (!gameActive) return false;
    if (!lastSaveTicks) return true;
    return SDL_GetTicks() - *lastSaveTicks > UNSAVED_WARN_MS;
}

bool App::writeSave(const std::string& path, const std::string& name, bool autosave) {
    SaveData d = captureWorld(world, gs, cam, levelSeed, nowMs(), name, currentMap);
    d.autosave = autosave;
    bool ok = writeSaveFile(path, d);
    if (ok) lastSaveTicks = SDL_GetTicks();
    refreshSaves();
    return ok;
}

void App::loadSave(std::string path) {
    SaveData d;
    if (!readSaveFile(path, d)) {
        refuse(lang("MISC_READ_FAILED"));
        refreshSaves();
        return;
    }
    if (d.mapStem.empty()) {
        showNotice(lang("MISC_SAVE_OLD"));
        refreshSaves();
        return;
    }
    std::optional<MapChoice> found = findMap(listMaps(), d.mapSource, d.mapStem);
    if (!found) {
        refuse(langf("MISC_MAP_MISSING", { d.name }));
        refreshSaves();
        return;
    }
    const MapChoice choice = *found;
    std::string err;
    // The title music ends as a saved game starts, before its level loads
    audio::stopTracks();
    if (!restoreLevel(world, terrain, renderer, rng, d, choice, err)) {
        refuse(langf("MISC_MAP_LOAD_FAILED", { choice.name, err }));
        refreshSaves();
        return;
    }
    currentMap = choice;
    levelSeed = d.seed;
    clearCombatText();
    gs = d.gs;
    setPlayerReputation(d.reputation);   // older saves have none, so every standing is its default
    // Older saves have no camera, so they start from the default one, not the last game's view
    cam = Camera{};
    cam.mapSize = (float)terrain.size();
    focusCameraOnFirstUnit(world, cam);
    applySaveCamera(d.cam, cam);
    primeStreaming(world, terrain, cam);
    sel = Selection{};
    leftDown = middleDown = false;
    itemDrag = false;
    followUnit = NULL_ENTITY;
    autoTimer = 0.0f;
    gameActive = true;
    lastSaveTicks.reset();
    screen = Screen::Playing;
    closePanel();
    showNotice(langf("MISC_LOADED", { d.name }));
    playSfx(Sfx::UiLoaded);
}

bool App::autosaveNow() {
    std::string path = nextAutosavePath();
    std::string name = std::filesystem::path(path).stem().string();   // autosave0, and so on
    return writeSave(path, name, true);
}

void App::performDelete() {
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
    showNotice(ec ? langf("MISC_DELETE_FAILED", { name }) : langf("MISC_DELETED", { name }));
}

void App::clearQuestion() {
    ask = Ask::None;
    pendingOverwrite.reset();
    pendingLoad.reset();
    pendingDelete.reset();
    deleteHolding = false;
}
