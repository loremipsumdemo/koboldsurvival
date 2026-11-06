#include "app.h"

using namespace play;

void App::enterLevel() {
    static_cast<AppSession&>(*this) = AppSession{};
    discardQueuedLabels();
    discardQueuedLog();
    takeQueuedSounds();
    closePanel();
    cam = Camera{};
    cam.mapSize = (float)terrain.size();
    focusCameraOnFirstUnit(world, cam);
    gameActive = true;
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
    const LoadProgress progress = [&](float p, const std::string& what) { loadingFrame(window, renderer, p, std::string(), what); };
    if (!loadLevelGround(choice, terrain, renderer, err, progress)) {
        refuse(langf("MISC_LOAD_FAILED", { choice.name, err }));
        return;
    }
    audio::stopTracks();
    buildLevel(world, terrain, rng, (unsigned)std::time(nullptr), scenarioHasCamp(scenarioPick), progress);
    // The party from the menu replaces the units that the seed made: its people, then its mobs after them
    std::vector<MobKind> mobKinds;
    for (const PartyMember& m : party) {
        if (m.mob != MobKind::None) mobKinds.push_back(m.mob);
    }
    const int people = partyPeople(party);
    ensurePlayerUnits(world, terrain, rng, people);
    ensurePlayerMobs(world, terrain, rng, mobKinds, people);
    applyParty(world, party);
    if (scenarioPick == ScenarioId::PackOfTwo) {
        const Entity lead = firstSquadUnit(world);
        std::vector<Entity> beasts;
        for (Entity u : world.view<Unit, Commandable, Mob>()) beasts.push_back(u);
        if (lead != NULL_ENTITY && !world.has<Mob>(lead)) issueFollowOrder(world, beasts, lead);
    }
    currentMap = choice;
    gs = GameState{};
    enterLevel();
    primeStreaming(world, terrain, cam);
    clearSelection(world);
    const Entity firstUnit = firstSquadUnit(world);
    if (firstUnit != NULL_ENTITY) world.add(firstUnit, Selected{});
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
                      action == Action::OverwriteNo || action == Action::LoadNo || action == Action::LeaveNo || action == Action::DeleteNo;
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
        const std::optional<MapChoice> mainMap = findMap(listMaps(), "packed", MAIN_MAP_STEM);
        if (!mainMap) {
            showNotice(lang("MISC_MAIN_MAP_MISSING"));
            return;
        }
        openParty(*mainMap);
        return;
    }
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

