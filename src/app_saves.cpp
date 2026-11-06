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
    SaveData d = captureWorld(world, gs, cam, rng, nowMs(), name, currentMap);
    d.autosave = autosave;
    const bool ok = writeSaveFile(path, d);
    if (ok) {
        lastSaveTicks = SDL_GetTicks();
        saves.erase(std::remove_if(saves.begin(), saves.end(), [&](const SaveSummary& s) { return s.path == path; }), saves.end());
        saves.push_back(SaveSummary{ path, name, d.timestamp, autosave });
        sortSaves(saves, sortKey, sortAsc);
    }
    return ok;
}

void App::loadSave(std::string path) {
    SaveData d;
    if (!readSaveFile(path, d)) {
        refuse(lang("MISC_READ_FAILED"));
        refreshSaves();
        return;
    }
    const std::optional<MapChoice> found = findMap(listMaps(), d.mapSource, d.mapStem);
    if (!found) {
        refuse(langf("MISC_MAP_MISSING", { d.name }));
        return;
    }
    std::string err;
    if (!loadLevelGround(*found, terrain, renderer, err)) {
        refuse(langf("MISC_MAP_LOAD_FAILED", { found->name, err }));
        return;
    }
    audio::stopTracks();
    resetPlayState();
    restoreWorld(world, terrain, rng, d);
    currentMap = *found;
    gs = d.gs;
    enterLevel();
    applySaveCamera(d.cam, cam);
    primeStreaming(world, terrain, cam);
    lastSaveTicks = SDL_GetTicks();
    showNotice(langf("MISC_LOADED", { d.name }));
    playSfx(Sfx::UiLoaded);
}

bool App::autosaveNow() {
    std::string path = nextAutosavePath();
    std::string name = std::filesystem::u8path(path).stem().u8string();
    return writeSave(path, name, true);
}

void App::performDelete() {
    if (!pendingDelete) {
        ask = Ask::None;
        return;
    }
    const std::string name = pendingDelete->name;
    std::error_code ec;
    std::filesystem::remove(std::filesystem::u8path(pendingDelete->path), ec);
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

void App::finishSave(bool ok) {
    closePanel();
    showNotice(ok ? lang("MISC_GAME_SAVED") : lang("MISC_SAVE_FAILED"));
    playSfx(ok ? Sfx::UiSaved : Sfx::UiError);
}

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
            saveName = saves[param].name;
        } else if (wouldLoseProgress()) {
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
        return true;
    case Action::DeleteNo:
        ask = Ask::None;
        pendingDelete.reset();
        deleteHolding = false;
        return true;
    case Action::ScrollUp:
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
        finishSave(writeSave(target.path, target.name, target.autosave));
        return true;
    }
    case Action::OverwriteNo:
        ask = Ask::None;
        pendingOverwrite.reset();
        return true;
    case Action::CancelName:
        closePanel();
        return true;
    default:
        return false;
    }
}
