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

// The panel closes, and the notice and sound say how the save went
void App::finishSave(bool ok) {
    closePanel();
    showNotice(ok ? lang("MISC_GAME_SAVED") : lang("MISC_SAVE_FAILED"));
    playSfx(ok ? Sfx::UiSaved : Sfx::UiError);
}

// True if the action was one of the save and load panel's
bool App::savePanelAction(Action action, int param) {
    switch (action) {
    case Action::LoadGame:
    case Action::Load:
        openPanel(false);
        return true;
    case Action::Save:
        openPanel(true);
        return true;
    case Action::LoadSlot:
        if (param < 0 || param >= (int)saves.size()) return true;
        if (panelSaveMode) {
            // Save mode: a click copies that save's name into the name field
            saveName = saves[param].name;
        } else if (wouldLoseProgress()) {
            // Load mode, and the current game would be lost: ask first
            pendingLoad = saves[param];
            ask = Ask::LoadUnsaved;
        } else {
            loadSave(saves[param].path);
        }
        return true;
    case Action::LoadYes: {
        if (!pendingLoad) {
            ask = Ask::None;
            return true;
        }
        SaveSummary target = *pendingLoad;
        loadSave(target.path);
        return true;
    }
    case Action::LoadNo:
        ask = Ask::None;
        pendingLoad.reset();
        return true;
    case Action::DeleteSave:
        if (param < 0 || param >= (int)saves.size()) return true;
        pendingDelete = saves[param];
        ask = Ask::Delete;
        return true;
    case Action::DeleteHold:
        // Handled by the hold timer, not by clicks
        return true;
    case Action::DeleteNo:
        ask = Ask::None;
        pendingDelete.reset();
        deleteHolding = false;
        return true;
    case Action::ScrollUp:
        // Jump up one page of saves
        pageUp(listOffset, visibleSaveRows(viewH));
        return true;
    case Action::ScrollDown:
        pageDown(listOffset, saves.size(), visibleSaveRows(viewH));
        return true;
    case Action::SortName:
    case Action::SortTime: {
        pickSortColumn(sortKey, action == Action::SortName ? SortKey::Name : SortKey::Time, sortAsc);
        sortSaves(saves, sortKey, sortAsc);
        listOffset = 0;
        return true;
    }
    case Action::ConfirmName: {
        // Save button. A name that matches an existing save asks before overwriting it.
        std::string name = trimmedName(saveName);
        if (name.empty()) return true;
        if (std::optional<SaveSummary> existing = findSaveByName(saves, name)) {
            pendingOverwrite = existing;
            ask = Ask::Overwrite;
            return true;
        }
        finishSave(writeSave(manualSavePath(nowMs()), name, false));
        return true;
    }
    case Action::OverwriteYes: {
        if (!pendingOverwrite) {
            ask = Ask::None;
            return true;
        }
        SaveSummary target = *pendingOverwrite;
        // Keep the save's file and its AUTO or MANUAL type; only the contents change
        finishSave(writeSave(target.path, target.name, target.autosave));
        return true;
    }
    case Action::OverwriteNo:
        ask = Ask::None;
        pendingOverwrite.reset();
        return true;
    case Action::CancelName:
        // Close button
        closePanel();
        return true;
    default:
        return false;
    }
}
