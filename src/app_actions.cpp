#include "app.h"

using namespace play;

void App::enterLevel() {
    static_cast<AppSession&>(*this) = AppSession{};
    takeEvents(world);
    world.resource<FormationChoice>() = formationChoice();
    closePanel();
    gs.claimDecayDue = 0.0;
    cam = Camera{};
    cam.mapSize = (float)terrain.size();
    focusCameraOnFirstUnit(world, cam);
    gameActive = true;
    editing = false;
    screen = Screen::Playing;
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
    renderer.pickLoadingArt();
    const LoadProgress progress = [&](float p, const std::string& what) { loadingFrame(window, renderer, p, std::string(), what); };
    if (!loadLevelGround(choice, terrain, renderer, err, progress)) {
        refuse(langf("MISC_LOAD_FAILED", { choice.name, err }));
        return;
    }
    audio::stopTracks();
    if (!pendingLevel.empty()) {
        LevelData level;
        if (!readLevelFile(levelFilePath(pendingLevel), level, err)) {
            refuse(langf("MISC_LOAD_FAILED", { pendingLevel, err }));
            return;
        }
        buildLevelFrom(world, terrain, rng, (unsigned)std::time(nullptr), level);
    } else {
        buildLevel(world, terrain, rng, (unsigned)std::time(nullptr), false, progress);
    }
    settleFactionHomes(world, terrain);
    // The party from the menu replaces the units that the seed made: its people, then its mobs after them
    std::vector<MobKind> mobKinds;
    for (const PartyMember& m : party) {
        if (m.mob != MobKind::None) mobKinds.push_back(m.mob);
    }
    const int people = partyPeople(party);
    ensurePlayerUnits(world, terrain, rng, people);
    ensurePlayerMobs(world, terrain, rng, mobKinds, people);
    applyParty(world, party);
    if (const ObjectId first = firstSquadUnit(world); first != NO_OBJECT) {
        const Transform* ft = world.get<Transform>(first);
        if (ft && unreachableLand(terrain, ft->x, ft->z, AIRSHIP_LAND_TILES)) placeStartingAirship(world, terrain, ft->x, ft->z);
    }
    currentMap = choice;
    gs = GameState{};
    enterLevel();
    primeStreaming(world, terrain, cam);
    clearSelection(world);
    const ObjectId firstUnit = firstSquadUnit(world);
    if (firstUnit != NO_OBJECT) world.add(firstUnit, Selected{});
    followUnit = firstUnit;
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
    const bool back = action == Action::Back || action == Action::ReturnToTitle || action == Action::CancelName ||
                      action == Action::OverwriteNo || action == Action::LoadNo || action == Action::LeaveNo || action == Action::DeleteNo ||
                      action == Action::DefaultWorldNo;
    playSfx(back ? Sfx::UiBack : action == Action::Resume ? Sfx::UiResume : Sfx::UiClick);
    if (partyAction(action, param) || savePanelAction(action, param)) return;
    switch (action) {
    case Action::Continue:
        if (!saves.empty()) {
            const auto newest = std::max_element(saves.begin(), saves.end(),
                                                 [](const SaveSummary& a, const SaveSummary& b) { return a.timestamp < b.timestamp; });
            loadSave(newest->path);
        }
        return;
    case Action::NewGame: {
        // With no world made yet, offer to make one from the default settings instead of sending a new player to World Generation
        if (listMaps().empty()) {
            screen = Screen::DefaultWorldAsk;
            return;
        }
        openMapPick();
        return;
    }
    case Action::DefaultWorldYes:
        makeDefaultWorld();
        return;
    case Action::DefaultWorldNo:
        screen = Screen::Title;
        return;
    case Action::WorldGeneration:
        openWorldGen();
        return;
    case Action::Language:
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
        requestLeave(LeaveTo::Title);
        return;
    case Action::Quit:
        running = false;
        return;
    default:
        return;
    }
}

