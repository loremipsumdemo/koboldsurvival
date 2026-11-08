#include "app.h"
#include "building.h"
#include "camera_limit.h"
#include "prop_info.h"
#include "profile.h"

using namespace play;

void App::updateDeleteHold() {
    if (deleteHolding) {
        float hx = 0.0f, hy = 0.0f;
        const bool leftHeld = (SDL_GetMouseState(&hx, &hy) & SDL_BUTTON_LMASK) != 0;
        if (!leftHeld || !deleteHoldRect.contains(hx, hy)) {
            deleteHolding = false;
        } else if (SDL_GetTicks() - deleteHoldStart >= DELETE_HOLD_MS) {
            performDelete();
        }
    }
}

void App::updateUnitPanelState() {
    const ObjectId shown = sidebarUnitOf(world);
    const bool lone = shown != NO_OBJECT;
    if (lone && shown != lastLone) sidebarScroll = 0.0f;
    lastLone = shown;
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
    if (shown != sidebarScrollUnit) {
        sidebarScrollUnit = shown;
        sidebarScroll = 0.0f;
    }
}

void App::updateRightHold() {
    if (formationDrag && screen == Screen::Playing) {
        float mx = 0.0f, my = 0.0f;
        SDL_GetMouseState(&mx, &my);
        if (const auto at = pickGround(cam, terrain, mx, my, winW, winH)) {
            dragToX = at->first;
            dragToZ = at->second;
        }
    }
    if (rightHold && screen == Screen::Playing) {
        float hx = holdX, hz = holdZ, mhx = 0.0f, mhy = 0.0f;
        SDL_GetMouseState(&mhx, &mhy);
        if (holdTarget(winW, winH, mhx, mhy, hx, hz)) {
            holdX = hx;
            holdZ = hz;
        }
        std::vector<ObjectId> steered;
        for (ObjectId u : selectedUnits(world)) {
            if (clickStorey == 0 && !world.has<Upstairs>(u) && !world.has<Route>(u) && !world.has<Climbing>(u)) steered.push_back(u);
        }
        steerUnits(world, steered, Vec2{ holdX, holdZ });
        actionX = holdX;
        actionZ = holdZ;
        actionAlpha = 1.0f;
        markMove(holdX, holdZ);
    } else {
        rightHold = false;
        actionAlpha = std::max(0.0f, actionAlpha - dt);
    }
}

void App::updateTalkOrders() {
    if (session.dialogueUnit != NO_OBJECT) return;
    ObjectId talkNpc = NO_OBJECT, talker = NO_OBJECT;
    bool tradeTalk = false, shopTalk = false;
    for (ObjectId u : world.view<TalkOrder>()) {
        const ObjectId npcOf = world.get<TalkOrder>(u)->npc;
        if (!world.has<Entity>(npcOf)) {
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
    if (talker != NO_OBJECT && tradeTalk) startFreeTrade(talkNpc, talker);
    else if (talker != NO_OBJECT) tryStartTalk(talkNpc, talker, {}, shopTalk);
}

void App::reportPropArrivals(const std::vector<PropArrival>& arrivals) {
    for (const PropArrival& a : arrivals) {
        if (a.automatic) continue;
        const Prop* p = world.get<Prop>(a.prop);
        if (!p) continue;
        const PropInfo& info = propInfo(p->kind);
        if (a.locked || a.craft == WorkStart::Locked) {
            refuse(lang("MISC_PROP_LOCKED"));
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
        if (a.action == PropAction::Lock || a.action == PropAction::Evacuate) {
            if (a.started) playSfx(Sfx::UiToggle);
            continue;
        }
        if (a.occupyBanned) {
            refuse(langf("MISC_CANT_OCCUPY_YET", { nameOf(world, a.unit) }));
            continue;
        }
        if (a.action == PropAction::Inventory) {
            if (!(session.storageProp == a.prop && session.storageUnit == a.unit)) {
                closeStorage();
                session.storageProp = a.prop;
                session.storageUnit = a.unit;
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

void App::showWorkGains(const std::vector<WorkGain>& gains) {
    for (const WorkGain& g : gains) {
        switch (g.kind) {
        case WorkGain::Kind::Item:
            tradeLabels.push_back(TradeLabel{ g.unit, itemGainLabel(1, itemNameOf(g.item)), 0.0f });
            queueSoundFrom(world, propInfo((PropKind)g.propKind).doneSound, g.unit);
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

void App::playWorkSounds() {
    std::map<ObjectId, float> nextTicks;
    std::vector<ObjectId> pickedProps;
    for (ObjectId u : world.view<Working>()) {
        const Working* wk = world.get<Working>(u);
        if (wk->unlocking) pickedProps.push_back(wk->prop);
    }
    std::sort(pickedProps.begin(), pickedProps.end());
    for (ObjectId e : world.view<PropWork, Prop, Transform>()) {
        const PropWork* pw = world.get<PropWork>(e);
        if (pw->workers <= 0) continue;
        const Transform* pt = world.get<Transform>(e);
        if (std::hypot(pt->x - cam.focusX, pt->z - cam.focusZ) > WORK_SOUND_RANGE) continue;
        const PropKind kind = world.get<Prop>(e)->kind;
        const bool picking = std::binary_search(pickedProps.begin(), pickedProps.end(), e);
        const Sfx sound = picking ? Sfx::WorkLockpick : propInfo(kind).workSound;
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

void App::updateCamera() {
    if (screen != Screen::Playing) return;
    if (session.dialogueUnit == NO_OBJECT && !(editing && editor.editingName)) panWithKeys(dt, settings.edgeScroll && !leftDown && !middleDown);
    if (followUnit != NO_OBJECT && !followUnitCamera(world, cam, followUnit)) followUnit = NO_OBJECT;
    if (!editing) keepCameraNearUnits(world, cam);
    updateFloorView();
    cam.updateClearance(terrain);
    if (terrain.streamed()) PROFILED("terrain.stream", terrain.stream(streamFocuses(world, cam), STREAM_RADIUS, STREAM_LOADS_PER_FRAME));
}

void App::stepSimulation() {
    if (screen != Screen::Playing || session.paused || editing) return;
    const float frameDt = dt;
    simLeft += frameDt;
    dt = SIM_STEP_SECONDS;
    int steps = 0;
    while (simLeft >= SIM_STEP_SECONDS && steps < SIM_MOST_STEPS_PER_FRAME) {
        updateTalkOrders();
        if (session.paused) break;
        simBefore.clear();
        for (ObjectId u : world.view<Entity, Transform>()) simBefore.push_back({ u, *world.get<Transform>(u) });
        PROFILED("step: total", stepGame());
        endBrokenConversation();
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

// Puts on screen what the world says happened. A simulation step adds to it, and so do the player's clicks between steps.
void App::showEvents() {
    SimEvents ev = takeEvents(world);
    reportPropArrivals(ev.arrivals);
    for (const auto& [looter, looted] : ev.lootOpened) {
        closeStorage();
        session.lootUnit = looted;
        session.lootBy = looter;
        playSfx(Sfx::UiMenuOpen);
    }
    for (ObjectId u : ev.wakes.woke) {
        tradeLabels.push_back(TradeLabel{ u, langf("MISC_EFFECT_LABEL", { lang("UILABEL_HEALTH"), "+" + std::to_string(RECOVERY_HEALTH) }), 0.0f });
        queueSoundFrom(world, Sfx::Recovers, u);
    }
    for (ObjectId u : ev.wakes.died) queueSoundFrom(world, Sfx::Dies, u);
    showWorkGains(ev.gains);

    for (const LogLine& ln : ev.log) gameLog.add(ln.text, ln.tint, ln.kind, ln.npc);
    const size_t logCount = gameLog.lines.size();
    if (logCount > logSeen && logOffset > 0) logOffset += (int)(logCount - logSeen);
    logSeen = logCount;
    floatingNumbers.insert(floatingNumbers.end(), ev.floats.begin(), ev.floats.end());
    puffs.insert(puffs.end(), ev.puffs.begin(), ev.puffs.end());
    tradeLabels.insert(tradeLabels.end(), ev.labels.begin(), ev.labels.end());
    soundsToPlay.insert(soundsToPlay.end(), ev.sounds.begin(), ev.sounds.end());

    // What was queued just above
    const SimEvents more = takeEvents(world);
    soundsToPlay.insert(soundsToPlay.end(), more.sounds.begin(), more.sounds.end());
}

void App::stepGame() {
    simStep(world, terrain, rng, gs, dt, runMode);
    for (FloatingNumber& f : floatingNumbers) f.age += dt;
    floatingNumbers.erase(std::remove_if(floatingNumbers.begin(), floatingNumbers.end(),
                                         [](const FloatingNumber& f) { return f.age >= FLOAT_LIFE; }),
                          floatingNumbers.end());
    showEvents();
    PROFILED("playWorkSounds", playWorkSounds());
    PROFILED("autosaveTick", autosaveTick());
}

void App::closeStalePanels() {
    if (session.storageProp != NO_OBJECT) {
        const Needs* sn = world.get<Needs>(session.storageUnit);
        const std::vector<ObjectId> picked = selectedUnits(world);
        if (!world.has<Storage>(session.storageProp) || !world.has<Entity>(session.storageUnit) ||
            (sn && sn->hp <= 0) || flatGap(world, session.storageUnit, session.storageProp) > STORAGE_CLOSE_RANGE ||
            (picked.size() == 1 && picked[0] != session.storageUnit)) {
            closeStorage();
        }
    }
    if (session.quantity.open && session.quantity.toStorage && session.storageProp == NO_OBJECT) session.quantity.open = false;
    if (session.lootUnit != NO_OBJECT) {
        if (!outCold(world, session.lootUnit) || !world.has<Entity>(session.lootBy) || outCold(world, session.lootBy) ||
            flatGap(world, session.lootBy, session.lootUnit) > LOOT_RANGE) {
            closeLoot();
        }
    }
    if (screen == Screen::Playing && selectedUnits(world).empty()) {
        const ObjectId first = firstSquadUnit(world);
        if (first != NO_OBJECT) world.add(first, Selected{});
    }
    if (session.inspectUnit != NO_OBJECT && !world.has<Entity>(session.inspectUnit)) session.inspectUnit = NO_OBJECT;
    if (!unitTabEnabled(world, sidebarUnitOf(world), unitTab)) unitTab = UnitTab::Profile;
    if (session.contextMenu.open && ((!world.has<Entity>(session.contextMenu.target) && !world.has<Prop>(session.contextMenu.target)) ||
                             session.dialogueUnit != NO_OBJECT)) {
        session.contextMenu.open = false;
    }
}

void App::playQueuedSounds() {
    showEvents();
    std::vector<QueuedSound> queued;
    queued.swap(soundsToPlay);
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
            p = cw > 1e-4f ? soundPan(cx / cw) : 0.0f;
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
    size_t selKey = 0, selCount = 0;
    for (ObjectId u : world.view<Selected>()) {
        selKey = selKey * 31u + (size_t)u + 1u;
        ++selCount;
    }
    if (screen == Screen::Playing && selKey != lastSelectionKey && selCount > 0) playSfx(Sfx::UiSelect);
    lastSelectionKey = selKey;
}

void App::update() {
    updateDeleteHold();

    const Uint64 now = SDL_GetTicksNS();
    dt = (float)((double)(now - lastTicks) / 1e9);
    lastTicks = now;
    if (dt > 0.25f) dt = 0.25f;
    if (noticeSec > 0.0f) noticeSec -= dt;
    // Even while paused, new and loaded units go straight into squads
    if (screen == Screen::Playing && gameActive) settleSquads(world);
    if (screen == Screen::Playing) updateUnitPanelState();
    pivotAlpha = std::clamp(pivotAlpha + (middleDown ? 1.0f : -1.0f) * dt * 4.0f, 0.0f, 1.0f);
    updateRightHold();
    moveMarkTime = std::max(0.0f, moveMarkTime - dt);

    showEvents();
    cam.mapSize = (float)terrain.size();
    cam.viewRange = (float)settings.viewRange;

    session.talkCooldown = std::max(0.0f, session.talkCooldown - dt);
    if (screen == Screen::MapSelect) updateMapPick();
    stepSimulation();
    if (screen == Screen::Playing) updateDisbandHold();
    placeUnitsBetweenSteps();
    updateCamera();

    PROFILED("closeStalePanels", closeStalePanels());
    PROFILED("playQueuedSounds", playQueuedSounds());
}

void App::updateHover() {
    std::string title = gameTitle() + "   " + formatDate(gs.clockSeconds);
    if (title != lastTitle) {
        SDL_SetWindowTitle(window, title.c_str());
        lastTitle = title;
    }

    hud.clear();
    mouseX = mouseY = 0.0f;
    SDL_GetMouseState(&mouseX, &mouseY);
    renderer.setMouse(mouseX, mouseY);
    mouseOnPanels.reset();
    unitsUnderMouse.clear();
    itemsUnderMouse.clear();
    std::vector<ObjectId> propsUnder;
    bool propsFound = false;
    auto propsAt = [&](float x, float y) -> const std::vector<ObjectId>& {
        if (!propsFound || x != mouseX || y != mouseY) {
            propsUnder = renderer.propsAtPixel(world, cam, x, y, winW, winH);
            propsFound = x == mouseX && y == mouseY;
        }
        return propsUnder;
    };
    if (screen == Screen::Playing && world.resource<Graveyard>().haunted) {
        hud.push_back({ langf("UILABEL_HUD_HAUNTED", { std::to_string(unappeasedGhosts(world)) }), RGBA{ 1.0f, 0.45f, 0.4f, 1.0f } });
    }
    if (screen == Screen::Playing) {
        std::vector<HudEntry> under;
        unitsUnderMouse = renderer.entitiesAtPixel(world, cam, mouseX, mouseY, winW, winH);
        for (ObjectId u : unitsUnderMouse)
            under.push_back({ world.get<Name>(u)->text, HUD_UNIT_COLOR });

        itemsUnderMouse = renderer.groundItemsAtPixel(world, terrain, cam, mouseX, mouseY, winW, winH);
        std::vector<ObjectId> items = itemsUnderMouse;
        if ((SDL_GetModState() & SDL_KMOD_ALT) != 0) {
            const ObjectId labelled = renderer.groundItemAtPixel(world, terrain, cam, mouseX, mouseY, winW, winH);
            if (labelled != NO_OBJECT && std::find(items.begin(), items.end(), labelled) == items.end())
                items.insert(items.begin(), labelled);
        }
        for (ObjectId e : items) {
            under.push_back({ itemNameOf(world.get<GroundItem>(e)->item), HUD_ITEM_COLOR });
        }

        for (ObjectId e : propsAt(mouseX, mouseY)) {
            const std::string name = propDisplayName(world, e);
            under.push_back({ lockShutFor(world, e, NO_OBJECT) ? langf("MISC_LOCKED_NAME", { name }) : name, HUD_PROP_COLOR });
            for (const std::string& line : altarInfoLines(world, e)) under.push_back({ line, HUD_PROP_COLOR });
        }

        for (size_t i = 0; i < under.size() && i < HOVER_NAME_LIMIT; ++i) hud.push_back(under[i]);
        if (under.size() > HOVER_NAME_LIMIT) hud.push_back({ "...", HUD_UNIT_COLOR });
    }

    ObjectId nearestProp = NO_OBJECT;
    if (screen == Screen::Playing && !panelOpen && !mapOpen && !session.contextMenu.open && !itemMenu.open
        && session.dialogueUnit == NO_OBJECT && !mouseOverPanels()) {
        const std::vector<ObjectId>& props = propsAt(mouseX, mouseY);
        if (!props.empty()) nearestProp = props.front();
    }
    if (nearestProp != hoverProp) {
        hoverProp = nearestProp;
        hoverPropSince = SDL_GetTicks();
    }

    {
        const bool inClaim = selectionInClaim(world);
        if (session.buildPick.any() && !buildAnywhere(session.buildPick) && !inClaim && !editing) session.buildPick = {};
        renderer.setBuildInClaim(inClaim);
        float bmx = 0.0f, bmy = 0.0f;
        SDL_GetMouseState(&bmx, &bmy);
        const bool placing = session.buildPick.placing() && screen == Screen::Playing && !panelOpen && !mapOpen
                             && session.dialogueUnit == NO_OBJECT && !optimizeAllOpen && session.disbandTarget == NO_OBJECT;
        BuildPreview bp;
        if (placing && !mouseOverPanels()) {
            if (auto hit = buildPickAt(bmx, bmy); hit && !previewBuilding(bp, hit->first, hit->second)) {
                bp.on = true;
                bp.kind = session.buildPick.kind;
                snapBuildSpot(session.buildPick.kind, hit->first, hit->second, bp.x, bp.z);
                const int storey = propInfo(session.buildPick.kind).claimsLand ? 0 : session.floorLevel;
                bp.lift = storeyBaseHeight(storey);
                bp.valid = checkBuildSpot(world, terrain, session.buildPick.kind, bp.x, bp.z, storey) == BuildCheck::Ok;
            }
        }
        if (placing) updateBuildPaint();
        else session.foundationDrag = session.floorDrag = session.paintPiece = false;
        {
        }
        if (bp.on && propInfo(session.buildPick.kind).claimsLand) {
            bp.ringR = CLAIM_RADIUS;
            bp.ringTone = bp.valid ? 1 : 2;
        }
        ObjectId hoverAltar = NO_OBJECT;
        if (!placing && screen == Screen::Playing && !panelOpen && !mapOpen && !mouseOverPanels()) {
            for (ObjectId e : propsAt(bmx, bmy)) {
                const Prop* fp = world.get<Prop>(e);
                const Transform* ft = world.get<Transform>(e);
                if (!fp || !ft || !propInfo(fp->kind).claimsLand) continue;
                bp.ringR = CLAIM_RADIUS;
                bp.ringTone = 3;
                bp.x = ft->x;
                bp.z = ft->z;
                hoverAltar = e;
                break;
            }
        }
        if (bp.ringR > 0.0f) {
            for (ObjectId e : world.view<Prop, Transform>()) {
                if (e == hoverAltar || !propInfo(world.get<Prop>(e)->kind).claimsLand) continue;
                const Transform* ft = world.get<Transform>(e);
                bp.otherRings.push_back({ ft->x, ft->z });
            }
        }
        renderer.setBuildPreview(bp);
        renderer.setBuildSelected(session.buildPick);
    }
}
