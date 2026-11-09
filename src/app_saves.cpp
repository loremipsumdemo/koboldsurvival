#include "app.h"

using namespace play;

namespace {
// When a file was last written, in milliseconds since 1970 like a save's timestamp
long long fileTimeMs(const std::string& path) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::file_time_type at = fs::last_write_time(fs::u8path(path), ec);
    if (ec) return 0;
    const auto sys = at - fs::file_time_type::clock::now() + std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(sys.time_since_epoch()).count();
}
}

void App::refreshSaves() {
    if (panelLevels) {
        saves.clear();
        for (const std::string& name : listLevelFiles(currentMap.stem)) {
            SaveSummary s;
            s.path = levelFilePath(name);
            s.name = name;
            s.timestamp = fileTimeMs(s.path);
            s.mapSource = currentMap.source;
            s.mapStem = currentMap.stem;
            s.mapName = currentMap.name;
            saves.push_back(std::move(s));
        }
    } else {
        saves = listSaves();
        // The map names are only shown in the panel, and finding them reads the MAP folder
        if (panelOpen) {
            const std::vector<MapChoice> maps = listMaps();
            for (SaveSummary& s : saves) {
                const std::optional<MapChoice> m = findMap(maps, s.mapSource, s.mapStem);
                s.mapName = m ? m->name : s.mapStem;
            }
        }
    }
    sortSaves(saves, sortKey, sortAsc);
    if (pickedIndex() < 0) {
        pickedPath.clear();
        pickedLevel.reset();
    }
}

void App::closePanel() {
    panelOpen = false;
    clearQuestion();
    SDL_StopTextInput(window);
    pickedPath.clear();
    pickedLevel.reset();
    if (panelLevels) {
        // The list goes back to the saved games, which the title screen's Continue looks at
        panelLevels = false;
        refreshSaves();
    }
}

void App::openPanel(bool saveMode, bool levels) {
    if (levels) stopEditorName();
    panelOpen = true;
    panelSaveMode = saveMode;
    panelLevels = levels;
    panelFrom = screen;
    clearQuestion();
    saveName = levels ? editor.name : std::string();
    pickedPath.clear();
    pickedLevel.reset();
    listOffset = 0;
    sortKey = SortKey::Time;
    sortAsc = false;
    refreshSaves();
    // Saving keeps a picture of the world as it is now
    if (saveMode) renderer.requestSceneGrab();
    renderer.forgetPreview();
    if (saveMode) SDL_StartTextInput(window);
    else SDL_StopTextInput(window);
}

int App::pickedIndex() const {
    if (pickedPath.empty()) return -1;
    for (size_t i = 0; i < saves.size(); ++i) {
        if (saves[i].path == pickedPath) return (int)i;
    }
    return -1;
}

bool App::panelCanConfirm() const {
    return panelSaveMode ? !trimmedName(saveName).empty() : pickedIndex() >= 0;
}

void App::pickSave(int index) {
    if (index < 0 || index >= (int)saves.size()) return;
    if (saves[(size_t)index].path == pickedPath) return;
    pickedPath = saves[(size_t)index].path;
    pickedLevel.reset();
    if (panelLevels) {
        LevelData d;
        std::string err;
        if (readLevelFile(pickedPath, d, err)) pickedLevel = std::move(d);
    }
}

void App::loadPicked() {
    const int i = pickedIndex();
    if (i < 0) return;
    const SaveSummary s = saves[(size_t)i];
    if (panelLevels ? editorDirty() : wouldLoseProgress()) {
        pendingLoad = s;
        ask = Ask::LoadUnsaved;
        return;
    }
    if (panelLevels) {
        editor.name = s.name;
        if (editorLoad()) closePanel();
        return;
    }
    loadSave(s.path);
}

// The picture of the world taken when the save panel opened (or just before an autosave), kept beside the file
void App::writePreview(const std::string& savePath) {
    const std::string png = savePreviewPath(savePath);
    std::error_code ec;
    int w = 0, h = 0;
    const std::vector<uint8_t>* px = renderer.sceneGrab(w, h);
    renderer.forgetPreview();
    if (!px) {
        // An older picture at this path would be of another save
        std::filesystem::remove(std::filesystem::u8path(png), ec);
        return;
    }
    SDL_Surface* s = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_ABGR8888, (void*)px->data(), w * 4);
    if (!s) return;
    if (!IMG_SavePNG(s, png.c_str())) SDL_Log("Could not save the picture %s: %s", png.c_str(), SDL_GetError());
    SDL_DestroySurface(s);
}

bool App::wouldLoseProgress() {
    if (!gameActive) return false;
    if (!lastSaveTicks) return true;
    return SDL_GetTicks() - *lastSaveTicks > UNSAVED_WARN_MS;
}

// No leaves the save panel open, so the game can be saved before trying again
void App::requestLeave(LeaveTo to) {
    if (!wouldLoseProgress() || (panelOpen && ask == Ask::LeaveUnsaved)) {
        leave(to);
        return;
    }
    if (screen != Screen::Pause) screen = Screen::Pause;
    openPanel(true);
    leaveTo = to;
    ask = Ask::LeaveUnsaved;
}

void App::leave(LeaveTo to) {
    if (to == LeaveTo::Desktop) {
        running = false;
        return;
    }
    closePanel();
    screen = Screen::Title;
    gameActive = false;
    refreshSaves();
}

bool App::writeSave(const std::string& path, const std::string& name, bool autosave) {
    SaveData d = captureWorld(world, gs, cam, rng, nowMs(), name, currentMap);
    d.autosave = autosave;
    const bool ok = writeSaveFile(path, d);
    if (ok) {
        writePreview(path);
        lastSaveTicks = SDL_GetTicks();
        saves.erase(std::remove_if(saves.begin(), saves.end(), [&](const SaveSummary& s) { return s.path == path; }), saves.end());
        SaveSummary s{ path, name, d.timestamp, autosave };
        s.mapSource = d.mapSource;
        s.mapStem = d.mapStem;
        s.mapName = currentMap.name;
        s.clock = d.gs.clockSeconds;
        saves.push_back(std::move(s));
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
    if (screen == Screen::WorldGen && !worldGenUi.deleteMap.empty()) {
        deleteWorldMap();
        return;
    }
    if (!pendingDelete) {
        ask = Ask::None;
        return;
    }
    const std::string name = pendingDelete->name;
    std::error_code ec;
    std::filesystem::remove(std::filesystem::u8path(pendingDelete->path), ec);
    std::error_code pictureEc;
    std::filesystem::remove(std::filesystem::u8path(savePreviewPath(pendingDelete->path)), pictureEc);
    renderer.forgetPreview();
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
    case Action::LoadSlot: {
        // Picks the row; clicking the picked row again loads it, like the Load button
        if (param < 0 || param >= (int)saves.size()) return true;
        const bool again = saves[(size_t)param].path == pickedPath;
        pickSave(param);
        if (panelSaveMode) saveName = saves[(size_t)param].name;
        else if (again) loadPicked();
        return true;
    }
    case Action::LoadPicked:
        loadPicked();
        return true;
    case Action::LoadYes: {
        if (!pendingLoad) {
            ask = Ask::None;
            return true;
        }
        SaveSummary target = *pendingLoad;
        if (panelLevels) {
            clearQuestion();
            editor.name = target.name;
            if (editorLoad()) closePanel();
            return true;
        }
        loadSave(target.path);
        return true;
    }
    case Action::LeaveYes:
        leave(leaveTo);
        return true;
    case Action::LeaveNo:
        ask = Ask::None;
        return true;
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
        if (panelLevels) name = safeFileName(name);
        if (name.empty()) return true;
        if (std::optional<SaveSummary> existing = findSaveByName(saves, name)) {
            pendingOverwrite = existing;
            ask = Ask::Overwrite;
            return true;
        }
        if (panelLevels) {
            editor.name = name;
            if (editorSave()) closePanel();
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
        if (panelLevels) {
            clearQuestion();
            editor.name = target.name;
            if (editorSave()) closePanel();
            return true;
        }
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
