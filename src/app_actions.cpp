#include "patrons.h"
#include "app.h"
#include "scenario.h"

using namespace play;

void App::enterLevel() {
    static_cast<AppSession&>(*this) = AppSession{};
    takeEvents(world);
    world.resource<FormationChoice>() = formationChoice();
    closePanel();
    gs.claimDecayDue = 0.0;
    cam = Camera{};
    cameraLeash = CameraLeash{};
    noUnits = NoUnitsUi{};
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

void App::logBlocked(const std::string& text, ObjectId at) {
    gameLog.add(makeLogLine(world, text, LogKind::Blocked, LogWho(false, at)));
    if (at == NO_OBJECT || factionOf(world, at) == FACTION_PLAYER) playSfx(Sfx::UiError);   // other factions' units are only noted
}

// The camera goes to where a message happened, and the floor looked at is that message's floor
void App::goToLogLine(const LogLine& line) {
    if (!line.located) return;
    followUnit = NO_OBJECT;   // or the camera would go straight back to the unit it follows
    cam.focusX = line.x;
    cam.focusY = line.y;
    cam.focusZ = line.z;
    cam.clampState();
    session.floorLevel = line.storey;
}

void App::fadeOutThen(std::function<void()> then) {
    if (afterFadeOut) return;
    afterFadeOut = std::move(then);
    fadeOutFrom = SDL_GetTicksNS();
}

void App::startMapLevel(const MapChoice& choice) {
    std::string err;
    renderer.pickLoadingArt();
    LoadingScreen loading(window, renderer);
    const LoadProgress progress = [&](float p, const std::string& what) { loading.show(p, what); };
    if (!loadLevelGround(choice, terrain, renderer, err, progress)) {
        refuse(langf("MISC_LOAD_FAILED", { choice.name, err }));
        return;
    }
    audio::stopTracks();
    const Scenario scenario = pickedScenario();   // a copy: the list is read again when its screen is opened
    // Which color you are: the one picked in character creation, or a random one. The other 15 factions are the rest.
    setPlayerSlot(resolveFactionPick());
    setPlayerFactionName(factionNameText);   // empty keeps the color's own name
    if (!pendingLevel.empty()) {
        LevelData level;
        if (!readLevelFile(levelFilePath(pendingLevel), level, err)) {
            refuse(langf("MISC_LOAD_FAILED", { pendingLevel, err }));
            return;
        }
        buildLevelFrom(world, terrain, rng, (unsigned)std::time(nullptr), level);
        // The NPC Factions the level gave a spawn to are made now that it is known which colors they are: each with its camp, as on a regular map
        placeLevelFactions(world, terrain, rng, scenario);
    } else {
        buildLevel(world, terrain, rng, (unsigned)std::time(nullptr), scenario, progress);
    }
    settleFactionHomes(world, terrain);
    newPatrons(world);   // each faction its own, yours the one of the color you are
    // The party from the menu replaces the units that the seed made: its people, then its mobs after them
    std::vector<MobKind> mobKinds;
    for (const PartyMember& m : party) {
        if (m.mob != MobKind::None) mobKinds.push_back(m.mob);
    }
    const int people = partyPeople(party);
    ensurePlayerUnits(world, terrain, rng, people);
    ensurePlayerMobs(world, terrain, rng, mobKinds, people);
    applyParty(world, party);
    const FactionStart yours = scenarioStart(scenario, FACTION_PLAYER);
    const std::vector<Claim> claims = claimsOf(world);
    const bool haveAltar = std::any_of(claims.begin(), claims.end(), [](const Claim& c) { return c.owner == FACTION_PLAYER; });
    if (haveAltar && pendingLevel.empty()) {
        // With a camp (the Outpost's): the first of your people, as many as the scenario says, are yours to command, and the rest start as
        // citizens who tend the camp
        std::vector<ObjectId> squad = world.view<Entity, Commandable>();
        std::sort(squad.begin(), squad.end());
        std::vector<ObjectId> military, tenders;
        int fortified = 0;
        for (ObjectId u : squad) {
            if (world.has<Mob>(u)) continue;
            if ((int)military.size() < yours.military) {
                military.push_back(u);
                continue;
            }
            if (fortified < yours.fortified) {
                makeFortified(world, u);
                ++fortified;
            } else {
                makeCitizen(world, u);
            }
            tenders.push_back(u);
        }
        // They start round your Altar, on the nearest free tiles to it, as the units of the other factions do round theirs
        placeUnitsRoundAltar(world, terrain, FACTION_PLAYER, tenders);
        // Those left military keep just the first three default behaviors: Eat, Heal and Share
        for (ObjectId u : military) {
            if (Behaviors* b = world.get<Behaviors>(u)) {
                b->rules.erase(std::remove_if(b->rules.begin(), b->rules.end(), [](const BehaviorRule& r) {
                                   return r.kind != BehaviorKind::Eat && r.kind != BehaviorKind::Heal && r.kind != BehaviorKind::Share;
                               }),
                               b->rules.end());
                b->pending = true;
            }
        }
    }
    if (const ObjectId first = firstSquadUnit(world); first != NO_OBJECT) {
        const Transform* ft = world.get<Transform>(first);
        if (ft && unreachableLand(terrain, ft->x, ft->z, AIRSHIP_LAND_TILES)) placeStartingAirship(world, terrain, ft->x, ft->z);
    }
    currentMap = choice;
    gs = GameState{};
    enterLevel();
    // A faction that found no land, and one that was eliminated, are told of
    for (int f : world.resource<FactionLandNotes>().nomadic) gameLog.add(langf("MISC_FACTION_NOMADIC", { factionName(f) }));
    for (int f : world.resource<FactionLandNotes>().eliminated) gameLog.add(langf("MISC_FACTION_ELIMINATED", { factionName(f) }));
    primeStreaming(world, terrain, cam);
    clearSelection(world);
    const ObjectId firstUnit = firstSquadUnit(world);
    if (firstUnit != NO_OBJECT) world.add(firstUnit, Selected{});
    followUnit = firstUnit;
    loading.fadeOut();
}

void App::openParty(const MapChoice& choice) {
    pendingMap = choice;
    refreshScenarios();
    scenarioPick = 0;   // the Outpost
    scenarioScroll = 0;
    screen = Screen::ScenarioSelect;
}

void App::openPartyMenu() {
    party = scenarioParty(pickedScenario(), partyRng);
    partyUi = PartyUi{};
    // How many units the party has, and of which animals, is the scenario's to say; what each one is called, its stats and its gear are yours
    partyUi.resizable = false;
    partyUi.editable = true;
    partyUi.mobKindEditable = false;
    backScreen = Screen::ScenarioSelect;
    screen = Screen::PartySetup;
}

void App::refreshScenarios() {
    scenarioList.clear();
    scenarioList.push_back(outpostScenario());
    for (const std::string& name : listScenarioFiles()) {
        Scenario s;
        std::string err;
        if (readScenarioFile(name, s, err)) scenarioList.push_back(std::move(s));
    }
}

const Scenario& App::pickedScenario() const {
    return scenarioPick >= 0 && scenarioPick < (int)scenarioList.size() ? scenarioList[(size_t)scenarioPick] : outpostScenario();
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
            fadeOutThen([this, path = newest->path] { loadSave(path); });
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
    case Action::AddCustomScenario:
        openScenarioEditor();
        return;
    case Action::BuildingBlueprints:
        fadeOutThen([this] { openBlueprintEditor(); });
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

