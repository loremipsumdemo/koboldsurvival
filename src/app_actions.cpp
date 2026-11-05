#include "app.h"

using namespace play;

void App::clearCombatText() {
    gameLog = GameLog{};
    floatingNumbers.clear();
    tradeLabels.clear();
    discardQueuedLabels();
    logOffset = 0;
    logSeen = 0;
}

void App::refreshSaves() {
    saves = listSaves();
    sortSaves(saves, sortKey, sortAsc);
}

void App::refreshMaps() {
    maps = listMaps();
    sortMaps(maps, mapSortByName, mapSortAsc);
}

void App::showNotice(const std::string& text, float seconds) {
    notice = text;
    noticeSec = seconds;
}

void App::refuse(const std::string& text) {
    showNotice(text);
    playSfx(Sfx::UiError);
}

void App::closePanel() {
    panelOpen = false;
    ask = Ask::None;
    pendingOverwrite.reset();
    pendingLoad.reset();
    pendingDelete.reset();
    deleteHolding = false;
    SDL_StopTextInput(window);
}

void App::openPanel(bool saveMode) {
    panelOpen = true;
    panelSaveMode = saveMode;
    panelFrom = screen;
    ask = Ask::None;
    pendingOverwrite.reset();
    pendingLoad.reset();
    pendingDelete.reset();
    deleteHolding = false;
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

void App::startMapLevel(const MapChoice& choice) {
    std::string err;
    levelSeed = (unsigned)std::time(nullptr);
    levelPlayerCamp = scenarioHasCamp(scenarioPick);   // only Full Random Camp starts with your camp
    // The title music ends as a new game starts, before its level loads
    audio::stopTracks();
    if (!prepareLevel(choice, world, terrain, renderer, rng, levelSeed, true, err,
                      [&](float p, const std::string& what) { loadingFrame(window, renderer, p, std::string(), what); })) {
        refuse(langf("MISC_LOAD_FAILED", { choice.name, err }));
        return;
    }
    // The party from the menu replaces the three units that the seed made: its people, then its mobs after them
    {
        std::vector<MobKind> mobKinds;
        for (const PartyMember& m : party) {
            if (m.mob != MobKind::None) mobKinds.push_back(m.mob);
        }
        const int people = partyPeople(party);
        ensurePlayerUnits(world, terrain, rng, people);
        ensurePlayerMobs(world, terrain, rng, mobKinds, people);
    }
    applyParty(world, party);
    // In Pack of Two the wolf starts out following its Vagabond
    if (scenarioPick == ScenarioId::PackOfTwo) {
        const Entity lead = firstSquadUnit(world);
        std::vector<Entity> beasts;
        for (Entity u : world.view<Unit, Commandable, Mob>()) beasts.push_back(u);
        if (lead != NULL_ENTITY && !world.has<Mob>(lead)) issueFollowOrder(world, beasts, lead);
    }
    clearCombatText();
    closePanel();
    currentMap = choice;
    // A new game starts with the default camera, not the zoom and angle the last game was left at. The focus is
    // clamped to the map, so the map's size has to be known before it is set.
    cam = Camera{};
    cam.mapSize = (float)terrain.size();
    focusCameraOnFirstUnit(world, cam);
    primeStreaming(world, terrain, cam);
    gs = GameState{};
    sel = Selection{};
    // The game starts with the unit the camera is on selected, and the camera following it as a double-click on its
    // panel card would, until the first manual pan
    clearSelection(world);
    const Entity firstUnit = firstSquadUnit(world);
    if (firstUnit != NULL_ENTITY) world.add(firstUnit, Selected{});
    leftDown = middleDown = false;
    itemDrag = false;
    followUnit = firstUnit;
    autoTimer = 0.0f;
    gameActive = true;
    lastSaveTicks.reset();
    screen = Screen::Playing;
}

void App::openParty(const MapChoice& choice) {
    pendingMap = choice;
    scenarioPick = ScenarioId::Vagabond;
    scenarioScroll = 0;
    screen = Screen::ScenarioSelect;
}

void App::openPartyMenu() {
    party = scenarioParty(scenarioPick, partyRng);
    partyUi = PartyUi{};
    partyUi.resizable = scenarioResizable(scenarioPick);
    partyUi.editable = scenarioEditable(scenarioPick);
    partyUi.mobKindEditable = scenarioMobKindEditable(scenarioPick);
    backScreen = Screen::ScenarioSelect;
    screen = Screen::PartySetup;
}

void App::stopNameEdit() {
    if (partyUi.editingName) SDL_StopTextInput(window);
    partyUi.editingName = false;
    partyUi.composition.clear();
}

void App::closeDropdown() {
    partyUi.dropKind = DropKind::None;
    partyUi.dropScroll = 0;
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

void App::panWithKeys(float dt, bool edgeScroll) {
    const bool* keys = SDL_GetKeyboardState(nullptr);
    float right = 0.0f, forward = 0.0f;   // edge scrolling adds to these below
    if (percentPrompt.open) keys = nullptr;   // the keys go into the Behavior tab's # prompt
    if (keys && (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT])) right += 1.0f;
    if (keys && (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT])) right -= 1.0f;
    if (keys && (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP])) forward += 1.0f;
    if (keys && (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN])) forward -= 1.0f;

    if (edgeScroll) {
        int winW = 0, winH = 0;
        SDL_GetWindowSizeInPixels(window, &winW, &winH);
        float mx = 0.0f, my = 0.0f;
        SDL_GetMouseState(&mx, &my);
        if (mx < 6.0f) right -= 1.0f;
        if (mx > winW - 6.0f) right += 1.0f;
        if (my < 6.0f) forward += 1.0f;
        if (my > winH - 6.0f) forward -= 1.0f;
    }

    // Holding shift pans twice as fast
    const bool fast = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
    float panSpeed = cam.distance * 0.9f * dt * (fast ? 2.0f : 1.0f);
    // Any manual move takes the camera off the unit it was following
    if (right != 0.0f || forward != 0.0f) followUnit = NULL_ENTITY;
    cam.pan(right * panSpeed, forward * panSpeed);
}

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

void App::saveSettingsFile() { saveSettings(settings, SETTINGS_PATH); }

GearPrefs App::gearPrefs() {
    GearPrefs p;
    p.attackShare = settings.optimizeAttackShare;
    p.accessories = settings.optimizeAccessories;
    p.twoHanded = settings.optimizeTwoHanded;
    p.valueTies = settings.optimizeValueTies;
    return p;
}

void App::applyFontScale() {
    if (settings.fontScale == appliedFontScale && settings.fontPath == appliedFontPath
        && settings.language == appliedLanguage) return;
    UiFonts next = openUiFonts(settings.fontScale, settings.fontPath, settings.language);
    if (!next.valid()) {
        closeUiFonts(next);
        return;
    }
    renderer.setFonts(next.hud, next.ui, next.list, next.statsTitle, next.statsRow);
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

void App::performAction(Action action, int param) {
    // Going back or saying no has its own sound, and so does resuming the game
    const bool back = action == Action::Back || action == Action::ReturnToTitle || action == Action::CancelName ||
                      action == Action::OverwriteNo || action == Action::LoadNo || action == Action::DeleteNo;
    playSfx(back ? Sfx::UiBack : action == Action::Resume ? Sfx::UiResume : Sfx::UiClick);
    switch (action) {
    case Action::Continue:
        if (!saves.empty()) loadSave(saves.front().path);
        return;
    case Action::NewGame: {
        // New Game always starts on the main map, so there is no map to pick
        const std::optional<MapChoice> mainMap = findMap(listMaps(), "packed", MAIN_MAP_STEM);
        if (!mainMap) {
            showNotice("The main map is missing from data.pak");
            return;
        }
        openParty(*mainMap);
        return;
    }
    case Action::PickMap:
        if (param >= 0 && param < (int)maps.size()) openParty(maps[param]);
        return;
    case Action::MapSortName:
    case Action::MapSortTime: {
        // Clicking the active column flips the order. A new column starts ascending.
        const bool byName = action == Action::MapSortName;
        if (mapSortByName == byName) {
            mapSortAsc = !mapSortAsc;
        } else {
            mapSortByName = byName;
            mapSortAsc = true;
        }
        sortMaps(maps, mapSortByName, mapSortAsc);
        mapOffset = 0;
        return;
    }
    case Action::MapScrollUp: {
        const size_t page = visibleSaveRows(viewH);
        mapOffset = (mapOffset > page) ? mapOffset - page : 0;
        return;
    }
    case Action::MapScrollDown: {
        const size_t page = visibleSaveRows(viewH);
        const size_t maxOffset = maps.size() > page ? maps.size() - page : 0;
        mapOffset = std::min(mapOffset + page, maxOffset);
        return;
    }
    case Action::PartyPick:
        partyUi.selected = std::clamp(param, 0, (int)party.size() - 1);
        stopNameEdit();
        closeDropdown();
        return;
    case Action::PartyTabPick:
        partyUi.tab = (PartyTab)std::clamp(param, 0, PARTY_TAB_COUNT - 1);
        stopNameEdit();
        closeDropdown();
        return;
    case Action::PartyNameField:
        closeDropdown();
        if (!partyUi.editingName) SDL_StartTextInput(window);
        partyUi.editingName = true;
        return;
    case Action::PartyBodyBox:
        stopNameEdit();
        partyUi.dropKind = DropKind::Body;
        partyUi.dropScroll = 0;
        return;
    case Action::PartySpriteBox:
        // The list opens scrolled so the unit's current sprite is near the middle
        stopNameEdit();
        partyUi.dropKind = DropKind::Sprite;
        partyUi.dropScroll = clampDropScroll(pickedMember(party, partyUi).sprite - 1 - DROP_VISIBLE_ROWS / 2,
                                             UNIT_SPRITE_COUNT);
        return;
    case Action::PartyMobBox:
        // A mob member's kind: a list of the regular kinds, in Custom and Beastmaster only
        if (!partyUi.mobKindEditable) return;
        stopNameEdit();
        partyUi.dropKind = DropKind::MobKind;
        partyUi.dropSlot = 0;
        partyUi.dropScroll = 0;
        return;
    case Action::PartyAddMob:
        // The list of regular kinds; picking one adds a mob of that kind
        if (!partyUi.resizable || (int)party.size() >= PARTY_MAX_UNITS) return;
        stopNameEdit();
        partyUi.dropKind = DropKind::MobKind;
        partyUi.dropSlot = -1;
        partyUi.dropScroll = 0;
        return;
    case Action::PartyRandomUnit: {
        // A new member for the picked unit, with a name that no other unit in the party has
        const int picked = std::clamp(partyUi.selected, 0, (int)party.size() - 1);
        if (party[picked].mob != MobKind::None) {
            // A mob: in Custom a new one of the same kind, rolled afresh; elsewhere it stays as the scenario made it
            if (partyUi.editable) party[picked] = mobMember(party[picked].mob, partyRng);
            stopNameEdit();
            closeDropdown();
            return;
        }
        std::vector<std::string> others;
        for (int i = 0; i < (int)party.size(); ++i) {
            if (i != picked) others.push_back(party[i].name);
        }
        PartyMember fresh = randomMember(partyRng, others);
        if (!partyUi.editable) {
            // Outside Custom only who the unit is changes: its stats, gear, items and hunger stay as the scenario made them
            const PartyMember& old = party[picked];
            fresh.stats = old.stats;
            fresh.gear = old.gear;
            fresh.items = old.items;
            fresh.hunger = old.hunger;
        }
        party[picked] = fresh;
        stopNameEdit();
        closeDropdown();
        return;
    }
    case Action::PartyGearSlot:
        if (!partyUi.editable) return;
        stopNameEdit();
        partyUi.dropKind = DropKind::Gear;
        partyUi.dropSlot = std::clamp(param, 0, GEAR_SLOTS - 1);
        partyUi.dropScroll = 0;
        return;
    case Action::PartyStep: {
        // Minus is the even parameter and plus the odd one. Holding Shift moves a level by ten.
        if (!partyUi.editable) return;
        const StatId stat = (StatId)(param / 2);
        const int step = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0 ? 10 : 1;
        Stats& st = pickedMember(party, partyUi).stats;
        setStatLevel(st, stat, levelOf(st, stat) + ((param % 2) != 0 ? step : -step));
        return;
    }
    case Action::PartyInvSlot:
        stopNameEdit();
        partyUi.stackSlot = std::clamp(param, 0, INVENTORY_SLOTS - 1);
        if (!partyUi.editable) return;   // the slot is picked to read about, with no list to change it
        partyUi.dropKind = DropKind::Item;
        partyUi.dropSlot = partyUi.stackSlot;
        partyUi.dropScroll = 0;
        return;
    case Action::PartyCount: {
        if (!partyUi.editable) return;
        ItemStack& st = pickedMember(party, partyUi).items[partyUi.stackSlot];
        if (st.item > 0) st.count = std::clamp(st.count + param, 1, stackLimit(st.item));
        return;
    }
    case Action::PartyDropPick: {
        const std::vector<DropEntry> entries = dropdownEntries(party, partyUi);
        if (param < 0 || param >= (int)entries.size()) return;
        PartyMember& m = pickedMember(party, partyUi);
        const int value = entries[param].value;
        switch (partyUi.dropKind) {
        case DropKind::Sprite:
            m.sprite = value;
            break;
        case DropKind::Body:
            // A new body type brings its own model with it; the model can still be changed after
            m.body = (BodyType)value;
            m.sprite = modelForBody(m.body);
            break;
        case DropKind::Gear:
            if (partyUi.editable) m.gear[partyUi.dropSlot] = value;
            break;
        case DropKind::Item: {
            if (!partyUi.editable) break;
            ItemStack& st = m.items[partyUi.dropSlot];
            if (value == 0) {
                st = ItemStack{};
            } else {
                // Choosing the same item again keeps its count. Anything else starts at one.
                const int count = st.item == value ? st.count : 1;
                st = ItemStack{ value, std::clamp(count, 1, stackLimit(value)) };
            }
            break;
        }
        case DropKind::MobKind:
            if (partyUi.dropSlot < 0) {
                // Add mob: a new member at the end of the party, picked and scrolled to
                if (partyUi.resizable && (int)party.size() < PARTY_MAX_UNITS) {
                    party.push_back(mobMember((MobKind)value, partyRng));
                    partyUi.selected = (int)party.size() - 1;
                    partyUi.listScroll = (int)party.size();
                }
            } else if (partyUi.mobKindEditable && m.mob != MobKind::None && m.mob != (MobKind)value) {
                // A mob of the new kind, with that kind's stats. A name typed for it is kept; the old kind's own
                // name gives way to the new kind's.
                const bool renamed = cleanName(m.name) != lang(mobInfo(m.mob).nameKey);
                const std::string name = m.name;
                m = mobMember((MobKind)value, partyRng);
                if (renamed) m.name = name;
            }
            break;
        case DropKind::None:
            break;
        }
        closeDropdown();
        return;
    }
    case Action::PartyDropScroll: {
        const int count = (int)dropdownEntries(party, partyUi).size();
        partyUi.dropScroll = clampDropScroll(partyUi.dropScroll + param, count);
        return;
    }
    case Action::PartyDropBack:
    case Action::PartyHeading:
    case Action::PartyNote:
    case Action::PartyStatLabel:
    case Action::PartyMessage:
        return;
    case Action::PartyAddUnit:
        // A random unit at the end of the party. The list is scrolled to the end, and the layout clamps it.
        if (partyUi.resizable && (int)party.size() < PARTY_MAX_UNITS) {
            addRandomMember(party, partyRng);
            partyUi.selected = (int)party.size() - 1;
            partyUi.listScroll = (int)party.size();
            stopNameEdit();
            closeDropdown();
        }
        return;
    case Action::PartyRemoveUnit:
        // The picked unit leaves the party. The party can be emptied, but a game needs at least one unit to start.
        if (partyUi.resizable && !party.empty()) {
            const int picked = std::clamp(partyUi.selected, 0, (int)party.size() - 1);
            party.erase(party.begin() + picked);
            partyUi.selected = std::clamp(picked, 0, (int)party.size() - 1);
            stopNameEdit();
            closeDropdown();
        }
        return;
    case Action::PartyRandomAll:
        party = scenarioParty(scenarioPick, partyRng, partyPeople(party));
        stopNameEdit();
        closeDropdown();
        return;
    case Action::PartyConfirm:
        if (party.empty() || !partyNamesValid(party)) return;
        stopNameEdit();
        closeDropdown();
        startMapLevel(pendingMap);
        return;
    case Action::LoadGame:
    case Action::Load:
        openPanel(false);
        return;
    case Action::Save:
        openPanel(true);
        return;
    case Action::LoadSlot:
        if (param < 0 || param >= (int)saves.size()) return;
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
        return;
    case Action::LoadYes: {
        if (!pendingLoad) {
            ask = Ask::None;
            return;
        }
        SaveSummary target = *pendingLoad;
        loadSave(target.path);
        return;
    }
    case Action::LoadNo:
        ask = Ask::None;
        pendingLoad.reset();
        return;
    case Action::DeleteSave:
        if (param < 0 || param >= (int)saves.size()) return;
        pendingDelete = saves[param];
        ask = Ask::Delete;
        return;
    case Action::DeleteHold:
        // Handled by the hold timer, not by clicks
        return;
    case Action::DeleteNo:
        ask = Ask::None;
        pendingDelete.reset();
        deleteHolding = false;
        return;
    case Action::ScrollUp: {
        // Jump up one page of saves
        const size_t page = visibleSaveRows(viewH);
        listOffset = (listOffset > page) ? listOffset - page : 0;
        return;
    }
    case Action::ScrollDown: {
        const size_t page = visibleSaveRows(viewH);
        size_t maxOffset = saves.size() > page ? saves.size() - page : 0;
        listOffset = std::min(listOffset + page, maxOffset);
        return;
    }
    case Action::SortName:
    case Action::SortTime: {
        // Clicking the active column flips the direction. A new column starts ascending.
        SortKey k = (action == Action::SortName) ? SortKey::Name : SortKey::Time;
        if (sortKey == k) {
            sortAsc = !sortAsc;
        } else {
            sortKey = k;
            sortAsc = true;
        }
        sortSaves(saves, sortKey, sortAsc);
        listOffset = 0;
        return;
    }
    case Action::ConfirmName: {
        // Save button. A name that matches an existing save asks before overwriting it.
        std::string name = trimmedName(saveName);
        if (name.empty()) return;
        if (std::optional<SaveSummary> existing = findSaveByName(saves, name)) {
            pendingOverwrite = existing;
            ask = Ask::Overwrite;
            return;
        }
        bool ok = writeSave(manualSavePath(nowMs()), name, false);
        closePanel();
        showNotice(ok ? lang("MISC_GAME_SAVED") : lang("MISC_SAVE_FAILED"));
        playSfx(ok ? Sfx::UiSaved : Sfx::UiError);
        return;
    }
    case Action::OverwriteYes: {
        if (!pendingOverwrite) {
            ask = Ask::None;
            return;
        }
        SaveSummary target = *pendingOverwrite;
        // Keep the save's file and its AUTO or MANUAL type; only the contents change
        bool ok = writeSave(target.path, target.name, target.autosave);
        closePanel();
        showNotice(ok ? lang("MISC_GAME_SAVED") : lang("MISC_SAVE_FAILED"));
        playSfx(ok ? Sfx::UiSaved : Sfx::UiError);
        return;
    }
    case Action::OverwriteNo:
        ask = Ask::None;
        pendingOverwrite.reset();
        return;
    case Action::CancelName:
        // Close button
        closePanel();
        return;
    case Action::Settings:
        backScreen = screen;
        screen = Screen::Settings;
        settingsUi.openRow = -1;
        settingsUi.dragRow = -1;
        rescanLanguages();
        return;
    case Action::Language:
        // The start menu's Language button: the same Settings screen, opened on its Language tab
        backScreen = screen;
        screen = Screen::Settings;
        settingsUi.tab = SettingsTab::Language;
        settingsUi.openRow = -1;
        settingsUi.dragRow = -1;
        rescanLanguages();
        return;
    case Action::Back:
        stopNameEdit();
        closeDropdown();
        screen = backScreen;
        return;
    case Action::Resume:
        screen = Screen::Playing;
        return;
    case Action::ReturnToTitle:
        screen = Screen::Title;
        gameActive = false;
        refreshSaves();
        return;
    case Action::Quit:
        running = false;
        return;
    }
}
