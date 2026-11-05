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

void App::showNotice(const std::string& text, float seconds) {
    notice = text;
    noticeSec = seconds;
}

void App::refuse(const std::string& text) {
    showNotice(text);
    playSfx(Sfx::UiError);
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

GearPrefs App::gearPrefs() {
    GearPrefs p;
    p.attackShare = settings.optimizeAttackShare;
    p.accessories = settings.optimizeAccessories;
    p.twoHanded = settings.optimizeTwoHanded;
    p.valueTies = settings.optimizeValueTies;
    return p;
}

void App::performAction(Action action, int param) {
    // Going back or saying no has its own sound, and so does resuming the game
    const bool back = action == Action::Back || action == Action::ReturnToTitle || action == Action::CancelName ||
                      action == Action::OverwriteNo || action == Action::LoadNo || action == Action::DeleteNo;
    playSfx(back ? Sfx::UiBack : action == Action::Resume ? Sfx::UiResume : Sfx::UiClick);
    if (partyAction(action, param) || savePanelAction(action, param)) return;
    switch (action) {
    case Action::Continue:
        if (!saves.empty()) loadSave(saves.front().path);
        return;
    case Action::NewGame: {
        // New Game always starts on the main map, so there is no map to pick
        const std::optional<MapChoice> mainMap = findMap(listMaps(), "packed", MAIN_MAP_STEM);
        if (!mainMap) {
            showNotice(lang("MISC_MAIN_MAP_MISSING"));
            return;
        }
        openParty(*mainMap);
        return;
    }
    case Action::Language:
        // The start menu's Language button: the same Settings screen, opened on its Language tab
        settingsUi.tab = SettingsTab::Language;
        [[fallthrough]];
    case Action::Settings:
        backScreen = screen;
        screen = Screen::Settings;
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
    default:
        return;
    }
}

