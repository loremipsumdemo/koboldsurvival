#include "app.h"
#include "prop_info.h"

using namespace play;

// The save is deleted once the button has been held for DELETE_HOLD_MS
void App::updateDeleteHold() {
    if (deleteHolding) {
        float hx = 0.0f, hy = 0.0f;
        const bool leftHeld = (SDL_GetMouseState(&hx, &hy) & SDL_BUTTON_LMASK) != 0;
        if (!leftHeld || !deleteHoldRect.contains(hx, hy)) {
            deleteHolding = false;          // let go early, or moved off the button: start over
        } else if (SDL_GetTicks() - deleteHoldStart >= DELETE_HOLD_MS) {
            performDelete();
        }
    }
}

// The stats popout and the item menu belong to one lone selected unit. They close when that changes.
void App::updateUnitPanelState() {
    const Entity shown = sidebarUnitOf(world);
    const bool lone = shown != NULL_ENTITY;
    // The Unit Panel stays as the player left it, open or folded, whatever is selected. Its contents start at the
    // top for each new unit.
    if (lone && shown != lastLone) sidebarScroll = 0.0f;
    lastLone = shown;
    // A folded panel opens toward the middle of the window from wherever its bar is, also after the window
    // changes size (not while it is being dragged). An open panel's bar changes side only when it is moved or
    // resized (endGrab), so it never jumps just after opening.
    for (PanelId which : ALL_PANELS) {
        if (which != grabWhich && panelFolded(which)) placeOf(which) = settlePanel(which, placeOf(which), winW, winH, true);
    }
    if (itemMenu.open) {
        const Inventory* inv = world.get<Inventory>(itemMenu.unit);
        const bool same = lone && shown == itemMenu.unit;
        const bool hasItem = inv && itemMenu.slot >= 0 && itemMenu.slot < INVENTORY_SLOTS
                          && inv->slots[itemMenu.slot].item != 0;
        if (!same || !hasItem) itemMenu.open = false;
    }
    // The sidebar starts at the top for a unit it was not scrolled for. The editor closes when its rule is gone.
    if (shown != sidebarScrollUnit) {
        sidebarScrollUnit = shown;
        sidebarScroll = 0.0f;
    }
}

void App::updateRightHold() {
    if (rightHold && screen == Screen::Playing) {
        // The hold's point follows the mouse, and the units walk straight at it: no route is planned until release
        float hx = holdX, hz = holdZ, mhx = 0.0f, mhy = 0.0f;
        SDL_GetMouseState(&mhx, &mhy);
        if (holdTarget(winW, winH, mhx, mhy, hx, hz)) {
            holdX = hx;
            holdZ = hz;
        }
        steerUnits(world, selectedUnits(world), Vec2{ holdX, holdZ });
        actionX = holdX;
        actionZ = holdZ;
        actionAlpha = 1.0f;
        markMove(holdX, holdZ);
    } else {
        rightHold = false;
        actionAlpha = std::max(0.0f, actionAlpha - dt);   // fades out over about a second
    }
}

// A talk order ends the first time one of its units is in talk range of its NPC
void App::updateTalkOrders() {
    Entity talkNpc = NULL_ENTITY, talker = NULL_ENTITY;
    bool tradeTalk = false, shopTalk = false;
    for (Entity u : world.view<TalkOrder>()) {
        const Entity npcOf = world.get<TalkOrder>(u)->npc;
        if (!world.has<Unit>(npcOf)) {
            clearTalkOrders(world);
            break;
        }
        if (flatGap(world, u, npcOf) <= TALK_RANGE) {
            talkNpc = npcOf;
            talker = u;
            tradeTalk = world.get<TalkOrder>(u)->trade;
            shopTalk = world.get<TalkOrder>(u)->shop;
            break;
        }
    }
    if (talker != NULL_ENTITY && tradeTalk) startFreeTrade(talkNpc, talker);
    else if (talker != NULL_ENTITY) tryStartTalk(talkNpc, talker, {}, shopTalk);
}

void App::reportPropArrivals() {
    // Units that reached the prop they were sent to. Inventory opens the panel. Use says if the bed was taken, the
    // prop had all its workers, or a campfire had nothing to cook or no room.
    for (const PropArrival& a : propOrderSystem(world)) {
        if (a.automatic) continue;   // an automated behavior's step: nothing to tell you, and no panel to open
        const Prop* p = world.get<Prop>(a.prop);
        if (!p) continue;
        const PropInfo& info = propInfo(p->kind);
        if (a.locked || a.craft == WorkStart::Locked) {
            refuse(lang("MISC_PROP_LOCKED"));   // locked: it can't be opened or used until it is unlocked
            continue;
        }
        if (a.action == PropAction::Unlock) {
            if (a.craft == WorkStart::SkillTooLow) {
                const Lock* lock = world.get<Lock>(a.prop);
                refuse(langf("MISC_LOCKPICK_NEEDS", { std::to_string(lock ? lock->level : 1) }));
            } else if (a.craft == WorkStart::Full) {
                refuse(lang("MISC_PROP_TAKEN"));
            }
            continue;
        }
        if (a.action == PropAction::Lock) {
            if (a.started) playSfx(Sfx::UiToggle);
            continue;
        }
        if (a.action == PropAction::Inventory) {
            if (!(storageProp == a.prop && storageUnit == a.unit)) {
                closeStorage();
                storageProp = a.prop;
                storageUnit = a.unit;
                itemMenu.open = false;
            }
            continue;
        }
        if (a.corpse) {
            refuse(lang("MISC_CORPSE_NO_PLACE"));
            continue;
        }
        if (a.placed) continue;
        if (a.unreachable) {
            refuse(lang("MISC_PROP_UNREACHABLE"));
        } else if (a.taken && info.use == PropUse::Occupy) {
            refuse(lang("MISC_PROP_TAKEN"));
        } else if (a.taken) {
            refuse(langf("MISC_PROP_WORKERS_FULL", { std::to_string(info.workers) }));
        } else if (info.use == PropUse::Crafting && a.craft == WorkStart::NoInput) {
            refuse(langf("MISC_CRAFT_NEEDS", { info.input == INPUT_COOKABLE ? std::string(lang("MISC_FOOD_TO_COOK"))
                                                                                 : itemNameOf(info.input) }));
        }
    }
}

void App::showWorkGains(double gameSecs) {
    // Resource and crafting props: the units working them make items by the in-game time that passed
    // Each item a worker receives shows over it, as "+Iron Ore", like a trade's label
    for (const WorkGain& g : workSystem(world, gameSecs, rng)) {
        switch (g.kind) {
        case WorkGain::Kind::Item:
            tradeLabels.push_back(TradeLabel{ g.unit, itemGainLabel(1, itemNameOf(g.item)), 0.0f });
            // Each prop has its own sound for an item finished
            switch (g.propKind) {
            case PropKind::Ore: queueSoundFrom(world, Sfx::DoneOre, g.unit); break;
            case PropKind::Tree: queueSoundFrom(world, Sfx::DoneTree, g.unit); break;
            case PropKind::Mushroom: queueSoundFrom(world, Sfx::DoneMushroom, g.unit); break;
            case PropKind::Stove: queueSoundFrom(world, Sfx::DoneCooking, g.unit); break;
            case PropKind::WaterPump: queueSoundFrom(world, Sfx::DoneWater, g.unit); break;
            default: queueSoundFrom(world, Sfx::ItemMove, g.unit); break;
            }
            break;
        case WorkGain::Kind::Unlocked:
            tradeLabels.push_back(TradeLabel{ g.unit, lang("MISC_UNLOCKED"), 0.0f });
            queueSoundFrom(world, Sfx::Unlocked, g.unit);
            break;
        case WorkGain::Kind::LockpickFailed:
            tradeLabels.push_back(TradeLabel{ g.unit, lang("MISC_LOCKPICK_FAILED"), 0.0f });
            queueSoundFrom(world, Sfx::LockpickFailed, g.unit);
            break;
        case WorkGain::Kind::Trained:
            queueSoundFrom(world, Sfx::DoneTraining, g.unit);
            tradeLabels.push_back(TradeLabel{ g.unit, langf("MISC_XP_GAINED", { std::to_string((int)std::lround(g.xp)), statName((StatId)g.item) }), 0.0f });
            break;
        }
    }
}

// While a prop is worked, its working sound now and then: the pick on the ore, the axe on the tree, the lockpick in
// the lock. Only props near the camera are heard, and each kind of sound plays at most once at a time.
void App::playWorkSounds() {
    std::map<Entity, float> nextTicks;
    // The props someone is picking the lock of, sorted, found once for every worked prop below
    std::vector<Entity> pickedProps;
    for (Entity u : world.view<Working>()) {
        const Working* wk = world.get<Working>(u);
        if (wk->unlocking) pickedProps.push_back(wk->prop);
    }
    std::sort(pickedProps.begin(), pickedProps.end());
    for (Entity e : world.view<PropWork, Prop, Transform>()) {
        const PropWork* pw = world.get<PropWork>(e);
        if (pw->workers <= 0) continue;
        const Transform* pt = world.get<Transform>(e);
        if (std::hypot(pt->x - cam.focusX, pt->z - cam.focusZ) > WORK_SOUND_RANGE) continue;
        const PropKind kind = world.get<Prop>(e)->kind;
        const bool picking = std::binary_search(pickedProps.begin(), pickedProps.end(), e);
        Sfx sound = Sfx::Count;
        if (picking) sound = Sfx::WorkLockpick;
        else if (kind == PropKind::Ore) sound = Sfx::WorkMining;
        else if (kind == PropKind::Tree) sound = Sfx::WorkWoodcutting;
        else if (kind == PropKind::Mushroom) sound = Sfx::WorkHerbalism;
        else if (kind == PropKind::Campfire || kind == PropKind::Stove) sound = Sfx::WorkCooking;
        else if (kind == PropKind::WaterPump) sound = Sfx::WorkPump;
        else if (kind == PropKind::TrainingDummy) sound = Sfx::WorkTraining;
        else if (kind == PropKind::Forge) sound = Sfx::ForgeBurning;
        if (sound == Sfx::Count) continue;
        const float every = sound == Sfx::ForgeBurning ? FORGE_SOUND_SECONDS : WORK_SOUND_SECONDS;
        float t = workSoundTimers.count(e) ? workSoundTimers[e] : every * 0.5f;
        t += dt;
        if (t >= every) {
            t = 0.0f;
            queueSoundFrom(world, sound, e);
        }
        nextTicks[e] = t;
    }
    workSoundTimers.swap(nextTicks);
}

// Autosave every N minutes of play
void App::autosaveTick() {
    autoTimer += dt;
    if (settings.autosaveMinutes > 0 && autoTimer >= settings.autosaveMinutes * 60.0f) {
        autoTimer = 0.0f;
        if (autosaveNow()) {
            showNotice(lang("MISC_AUTOSAVED"));
            playSfx(Sfx::UiSaved);
        } else {
            refuse(lang("MISC_AUTOSAVE_FAILED"));
        }
    }
}

// The camera, each frame while a game is on screen, paused or not. Only a talk holds it still.
void App::updateCamera() {
    if (screen != Screen::Playing || dialogueUnit != NULL_ENTITY) return;
    panWithKeys(dt, settings.edgeScroll && !leftDown && !middleDown);
    if (followUnit != NULL_ENTITY && !followUnitCamera(world, cam, followUnit)) followUnit = NULL_ENTITY;
    cam.updateClearance(terrain);
    // Open worlds: read in the ground near the camera and the units, a few chunks each frame
    if (terrain.streamed()) terrain.stream(streamFocuses(world, cam), STREAM_RADIUS, STREAM_LOADS_PER_FRAME);
}

// As many steps of the game as the time that passed calls for, each of the same length, so the game plays the same at
// any frame rate. If the machine can't keep up, the game slows down rather than taking ever more steps.
void App::stepSimulation() {
    if (screen != Screen::Playing || dialogueUnit != NULL_ENTITY || paused) return;
    const float frameDt = dt;
    simLeft += frameDt;
    dt = SIM_STEP_SECONDS;
    int steps = 0;
    while (simLeft >= SIM_STEP_SECONDS && steps < SIM_MOST_STEPS_PER_FRAME) {
        updateTalkOrders();
        if (dialogueUnit != NULL_ENTITY) break;   // the game stands still while a talk is open
        simBefore.clear();
        for (Entity u : world.view<Unit, Transform>()) simBefore.push_back({ u, *world.get<Transform>(u) });
        stepGame();
        simLeft -= SIM_STEP_SECONDS;
        ++steps;
    }
    if (steps == SIM_MOST_STEPS_PER_FRAME) simLeft = std::min(simLeft, SIM_STEP_SECONDS);
    dt = frameDt;
}

// For the frame's drawing, each unit stands between where the last two steps put it, by how far the clock has gone into
// the next step. Without this, motion would stutter whenever the screen does not run at the step rate. Everything from
// here to the end of the frame only reads the places, and putUnitsBack ends it, so the game itself never sees them.
void App::placeUnitsBetweenSteps() {
    const float along = std::clamp(simLeft / SIM_STEP_SECONDS, 0.0f, 1.0f);
    simTrue.clear();
    for (const auto& [u, before] : simBefore) {
        Transform* t = world.get<Transform>(u);
        if (!t || (t->x == before.x && t->y == before.y && t->z == before.z)) continue;
        simTrue.push_back({ u, *t });
        t->x = before.x + (t->x - before.x) * along;
        t->y = before.y + (t->y - before.y) * along;
        t->z = before.z + (t->z - before.z) * along;
    }
}

void App::putUnitsBack() {
    for (const auto& [u, place] : simTrue) {
        if (Transform* t = world.get<Transform>(u)) *t = place;
    }
    simTrue.clear();
}

// One step of the game itself, while it is playing and not stopped
void App::stepGame() {
    // Fighting first, so a unit that dies this frame is gone before the others move. One sound covers the
    // swings of the frame: a hit if any landed, otherwise a miss if any were missed or dodged.
    factionSystem(world, dt);   // who is hostile to your units, from the reputations and provocations
    defendSystem(world);   // defenders pick the units attacking what they defend, before the fight
    combatSystem(world, terrain, rng, dt, gameLog, floatingNumbers);
    patrolSpawnSystem(world, terrain, dt);   // a mob gone too far from its spawn gives up and walks home
    // Behaviors after the fight, so a unit hit this frame can drink in the same frame
    behaviorSystem(world, gameLog, dt);
    // Anyone left at 0 health falls Unconscious
    downedSystem(world);
    // A log scrolled up keeps its place as lines arrive. One at its newest line follows them.
    const size_t logCount = gameLog.lines.size();
    if (logCount > logSeen && logOffset > 0) logOffset += (int)(logCount - logSeen);
    logSeen = logCount;
    squadSystem(world, dt, runMode);
    steeringSystem(world, terrain, dt, runMode);
    pickupSystem(world, terrain);   // a pickup plays its own sound
    reportPropArrivals();
    // Units with automated behaviors (a camp's woodcutter) take their next step: to a tree, or to the vault with
    // a load
    wildMobSystem(world, terrain, dt);   // wild mobs keep to their temperament: the Avoidant ones run off
    automationSystem(world, terrain, dt, rng);
    // Picking up, putting down and carrying Unconscious units; looters that got there open the panel
    const CarryEvents carried = carrySystem(world);
    for (const auto& [looter, looted] : carried.loot) {
        closeStorage();
        lootUnit = looted;
        lootBy = looter;
        playSfx(Sfx::UiMenuOpen);
    }
    cleanupActiveSquads(world);
    // Open worlds: read in the ground near the camera and the units, a few chunks each frame
    if (terrain.streamed()) terrain.stream(streamFocuses(world, cam), STREAM_RADIUS, STREAM_LOADS_PER_FRAME);
    // The in-game clock runs while it is not stopped. Hunger ticks once for each in-game hour it passes,
    // and health comes back by the in-game seconds that passed.
    int hoursPassed = 0;
    double gameSecs = 0.0;
    if (sky.clockRunning) {
        gameSecs = dt * GAME_SECONDS_PER_REAL_SECOND;
        hoursPassed = advanceClock(gs.clockSeconds, dt);
    }
    needsSystem(world, hoursPassed, gameSecs);
    if (hoursPassed > 0) forgetSpotted(world);   // each in-game hour, spotting a unit gives Perception XP again
    downedSystem(world);   // starving to 0 knocks a unit out too
    // Unconscious units gain Conscious, paid for in Rested and then Dying: 100 Conscious wakes, 100 Dying is a corpse
    const WakeChanges wakes = unconsciousSystem(world, gameSecs);
    playDeadSystem(world);
    for (Entity u : wakes.woke) {
        tradeLabels.push_back(TradeLabel{ u, langf("MISC_EFFECT_LABEL", { lang("UILABEL_HEALTH"), "+" + std::to_string(RECOVERY_HEALTH) }), 0.0f });
        queueSoundFrom(world, Sfx::Recovers, u);
    }
    for (Entity u : wakes.died) queueSoundFrom(world, Sfx::Dies, u);
    corpseDecaySystem(world, gameSecs);   // a corpse that has lain long enough turns to bones
    showWorkGains(gameSecs);
    // Passive crafting props (the forge) burn their fuel and fill their output by themselves
    passiveCraftSystem(world, gameSecs);
    // Flags and foundations wear when their vault is dry; what is worn through breaks into debris
    claimDecaySystem(world, gameSecs);
    if (sky.clockRunning) playWorkSounds();
    autosaveTick();
}

// Panels and selections that no longer have what they were opened for
void App::closeStalePanels() {
    // The storage panel closes once its unit is out of reach of the prop, is downed, or either is gone, or when the
    // selection becomes one unit other than the panel's. A how-many prompt for it closes with it.
    if (storageProp != NULL_ENTITY) {
        const Needs* sn = world.get<Needs>(storageUnit);
        const std::vector<Entity> picked = selectedUnits(world);
        if (!world.has<Storage>(storageProp) || !world.has<Unit>(storageUnit) ||
            (sn && sn->hp <= 0) || flatGap(world, storageUnit, storageProp) > STORAGE_CLOSE_RANGE ||
            (picked.size() == 1 && picked[0] != storageUnit)) {
            closeStorage();
        }
    }
    if (quantity.open && quantity.toStorage && storageProp == NULL_ENTITY) quantity.open = false;
    // The looting panel closes once its looter is out of reach, is knocked out, or either is gone, or the unit it
    // shows wakes up
    if (lootUnit != NULL_ENTITY) {
        if (!world.has<Unconscious>(lootUnit) || !world.has<Unit>(lootBy) || world.has<Unconscious>(lootBy) ||
            flatGap(world, lootBy, lootUnit) > LOOT_RANGE) {
            closeLoot();
        }
    }
    // A unit of your squad is always selected while there is one: when none is (a new game, the selected unit
    // died), the first of the squad is (firstSquadUnit), the one the camera starts on. With none left, nobody is.
    if (screen == Screen::Playing && selectedUnits(world).empty()) {
        const Entity first = firstSquadUnit(world);
        if (first != NULL_ENTITY) world.add(first, Selected{});
    }
    if (inspectUnit != NULL_ENTITY && !world.has<Unit>(inspectUnit)) inspectUnit = NULL_ENTITY;
    // A unit that is not in your squad shows only its Profile
    if (!unitTabEnabled(world, sidebarUnitOf(world), unitTab)) unitTab = UnitTab::Profile;
    // The right-click menu closes if its target is gone or a talk or trade opens
    if (contextMenu.open && ((!world.has<Unit>(contextMenu.target) && !world.has<Prop>(contextMenu.target)) ||
                             dialogueUnit != NULL_ENTITY)) {
        contextMenu.open = false;
    }
}

// Sounds the systems asked for this frame. The plain item sound gives way to any more particular one.
void App::playQueuedSounds() {
    // A world sound is as loud as its nearest copy this frame, and is panned to its side of the screen. A
    // frame of fighting plays one swing sound: a hit if any landed, else a miss.
    const std::vector<QueuedSound> queued = takeQueuedSounds();
    std::array<float, (size_t)Sfx::Count> volume{}, pan{};
    volume.fill(-1.0f);
    float ex = 0.0f, ey = 0.0f, ez = 0.0f;
    cam.eye(ex, ey, ez);
    const Mat4 vp = cam.viewProj((float)winW / (float)std::max(1, winH));
    for (const QueuedSound& q : queued) {
        if (!settings.craftGatherSounds && isCraftGatherSfx(q.sfx)) continue;
        float v = 1.0f, p = 0.0f;
        if (q.placed) {
            v = soundGain(std::sqrt((q.x - ex) * (q.x - ex) + (q.y - ey) * (q.y - ey) + (q.z - ez) * (q.z - ez)));
            const float cx = vp.m[0] * q.x + vp.m[4] * q.y + vp.m[8] * q.z + vp.m[12];
            const float cw = vp.m[3] * q.x + vp.m[7] * q.y + vp.m[11] * q.z + vp.m[15];
            p = cw > 1e-4f ? soundPan(cx / cw) : 0.0f;   // behind the eye: centred
        }
        const size_t i = (size_t)q.sfx;
        if (i < volume.size() && v > volume[i]) {
            volume[i] = v;
            pan[i] = p;
        }
    }
    auto heard = [&](Sfx s) { return volume[(size_t)s] >= 0.0f; };
    const bool particular = heard(Sfx::Equip) || heard(Sfx::Unequip) || heard(Sfx::Drop) || heard(Sfx::Pickup) ||
                            heard(Sfx::Store) || heard(Sfx::Take) || heard(Sfx::GearChanged) || heard(Sfx::Eat) ||
                            heard(Sfx::Drink) || heard(Sfx::Poison);
    if (heard(Sfx::Hit)) volume[(size_t)Sfx::Miss] = -1.0f;
    if (particular) volume[(size_t)Sfx::ItemMove] = -1.0f;
    for (size_t i = 0; i < volume.size(); ++i) {
        if (volume[i] >= 0.0f) playSfx((Sfx)i, volume[i], pan[i]);
    }
    // Units selected: a sound when the selection changes to some units
    size_t selKey = 0, selCount = 0;
    for (Entity u : world.view<Selected>()) {
        selKey = selKey * 31u + (size_t)u + 1u;
        ++selCount;
    }
    if (screen == Screen::Playing && selKey != lastSelectionKey && selCount > 0) playSfx(Sfx::UiSelect);
    lastSelectionKey = selKey;
    // (A unit falling Unconscious plays its own sound, from knockOut)
}

void App::update() {
    updateDeleteHold();

    // ---- Timing ----
    const Uint64 now = SDL_GetTicksNS();
    dt = (float)((double)(now - lastTicks) / 1e9);
    lastTicks = now;
    if (dt > 0.25f) dt = 0.25f;
    if (noticeSec > 0.0f) noticeSec -= dt;
    if (screen == Screen::Playing) updateUnitPanelState();
    pivotAlpha = std::clamp(pivotAlpha + (middleDown ? 1.0f : -1.0f) * dt * 4.0f, 0.0f, 1.0f);
    updateRightHold();
    moveMarkTime = std::max(0.0f, moveMarkTime - dt);

    // Game messages written outside a fight (XP, pickups, drops) join the log every frame
    flushQueuedLog(gameLog);
    cam.mapSize = (float)terrain.size();
    cam.viewRange = (float)settings.viewRange;

    // ---- Game update: only while playing ----
    // The game stands still while a talk is open, and only End Dialogue starts it again
    talkCooldown = std::max(0.0f, talkCooldown - dt);
    stepSimulation();
    placeUnitsBetweenSteps();
    updateCamera();   // after the units are placed, so a camera following one moves as smoothly as it does

    closeStalePanels();
    playQueuedSounds();
}

void App::updateHover() {
    // ---- The window title shows the game's name and the date ----
    std::string title = gameTitle() + "   " + formatDate(gs.clockSeconds);
    if (title != lastTitle) {
        SDL_SetWindowTitle(window, title.c_str());
        lastTitle = title;
    }

    // ---- Hover list: what is under the mouse, in the top-left corner. Units first (white), then items on the
    // ground (orange-yellow), then props (gray), up to the limit in all, then "..." ----
    hud.clear();
    mouseX = mouseY = 0.0f;
    SDL_GetMouseState(&mouseX, &mouseY);
    renderer.setMouse(mouseX, mouseY);
    // The props under the mouse, found once for the hover list and the claim circle below
    std::vector<Entity> propsUnder;
    bool propsFound = false;
    auto propsAt = [&](float x, float y) -> const std::vector<Entity>& {
        if (!propsFound || x != mouseX || y != mouseY) {
            propsUnder = renderer.propsAtPixel(world, cam, x, y, winW, winH);
            propsFound = x == mouseX && y == mouseY;
        }
        return propsUnder;
    };
    if (screen == Screen::Playing) {
        std::vector<HudEntry> under;
        for (Entity u : renderer.unitsAtPixel(world, cam, mouseX, mouseY, winW, winH))
            under.push_back({ world.get<Name>(u)->text, HUD_UNIT_COLOR });

        std::vector<Entity> items = renderer.groundItemsAtPixel(world, terrain, cam, mouseX, mouseY, winW, winH);
        // While Alt shows the name boxes, the name line under the mouse counts too
        if ((SDL_GetModState() & SDL_KMOD_ALT) != 0) {
            const Entity labelled = renderer.groundItemAtPixel(world, terrain, cam, mouseX, mouseY, winW, winH);
            if (labelled != NULL_ENTITY && std::find(items.begin(), items.end(), labelled) == items.end())
                items.insert(items.begin(), labelled);
        }
        for (Entity e : items) {
            under.push_back({ itemNameOf(world.get<GroundItem>(e)->item), HUD_ITEM_COLOR });
        }

        for (Entity e : propsAt(mouseX, mouseY)) {
            // A prop locked against the player says so: "Chest (Locked)". The player's own, or one in a player
            // claim, does not.
            const std::string name = lang(propInfo(world.get<Prop>(e)->kind).nameKey);
            under.push_back({ lockShutFor(world, e, NULL_ENTITY) ? langf("MISC_LOCKED_NAME", { name }) : name, HUD_PROP_COLOR });
            // A flag also shows its faction, health and vault
            for (const std::string& line : flagInfoLines(world, e)) under.push_back({ line, HUD_PROP_COLOR });
        }

        for (size_t i = 0; i < under.size() && i < HOVER_NAME_LIMIT; ++i) hud.push_back(under[i]);
        if (under.size() > HOVER_NAME_LIMIT) hud.push_back({ "...", HUD_UNIT_COLOR });
    }

    // ---- Build placement: while a prop is picked, its footprint follows the mouse over the ground. Holding R,
    // the mouse's left and right movement turns it. The preview shows exactly what a click would do. ----
    {
        // A kind that needs a claim is put down once no selected unit of yours stands in one
        const bool inClaim = selectionInClaim(world);
        if (buildPick.any() && !buildAnywhere(buildPick) && !inClaim) buildPick = {};
        renderer.setBuildInClaim(inClaim);
        const bool* keys = SDL_GetKeyboardState(nullptr);
        float bmx = 0.0f, bmy = 0.0f;
        SDL_GetMouseState(&bmx, &bmy);
        const bool placing = buildPick.placing() && screen == Screen::Playing && !panelOpen && !mapOpen
                             && dialogueUnit == NULL_ENTITY && !optimizeAllOpen && disbandTarget == NULL_ENTITY;
        if (placing && keys[SDL_SCANCODE_R] && buildPrevX >= 0.0f) buildYaw += (bmx - buildPrevX) * BUILD_TURN_PER_PIXEL;
        buildPrevX = bmx;
        BuildPreview bp;
        if (placing && !overPanels(bmx, bmy, winW, winH)) {
            if (auto hit = pickGround(cam, terrain, bmx, bmy, winW, winH)) {
                bp.on = true;
                bp.kind = buildPick.kind;
                bp.yaw = buildYaw;
                snapBuildSpot(buildPick.kind, hit->first, hit->second, bp.x, bp.z);
                bp.valid = checkBuildSpot(world, terrain, buildPick.kind, bp.x, bp.z, buildYaw) == BuildCheck::Ok;
            }
        }
        if (bp.on && buildPick.places(PropKind::Flag)) {
            bp.ringR = CLAIM_RADIUS;
            bp.ringTone = bp.valid ? 1 : 2;
        }
        Entity hoverFlag = NULL_ENTITY;
        // Not placing anything: an existing flag under the mouse shows its claim circle
        if (!placing && screen == Screen::Playing && !panelOpen && !mapOpen && !overPanels(bmx, bmy, winW, winH)) {
            for (Entity e : propsAt(bmx, bmy)) {
                const Prop* fp = world.get<Prop>(e);
                const Transform* ft = world.get<Transform>(e);
                if (!fp || !ft || fp->kind != PropKind::Flag) continue;
                bp.ringR = CLAIM_RADIUS;
                bp.ringTone = 3;
                bp.x = ft->x;
                bp.z = ft->z;
                hoverFlag = e;
                break;
            }
        }
        // When one claim circle shows, every flag's circle shows with it
        if (bp.ringR > 0.0f) {
            for (Entity e : world.view<Prop, Transform>()) {
                if (e == hoverFlag || world.get<Prop>(e)->kind != PropKind::Flag) continue;
                const Transform* ft = world.get<Transform>(e);
                bp.otherRings.push_back({ ft->x, ft->z });
            }
        }
        renderer.setBuildPreview(bp);
        renderer.setBuildSelected(buildPick);
    }
}
