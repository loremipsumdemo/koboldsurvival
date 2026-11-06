#include "app.h"

using namespace play;

void App::updateWorldMapView() {
    if (!mapOpen) {
        mapSpan = 0.0f;
    } else {
        const float size = (float)terrain.size();
        const Rect box = worldMapFor(winW, winH).map;
        if (mapSpan <= 0.0f) {
            mapSpan = mapMaxSpan(box, size);
            mapCx = size * 0.5f;
            mapCz = size * 0.5f;
        }
        clampMapView(box, size, mapCx, mapCz, mapSpan);
    }
}

void App::drawWorldOverlays() {
    if (settings.showPropNames) renderer.drawPropNames(world, cam, winW, winH, settings.showPropInfo);
    if (settings.showUnitNames || settings.showNpcNames) {
        renderer.drawUnitNames(world, cam, winW, winH, settings.showUnitNames, settings.showNpcNames);
    }
    renderer.drawEmotes(world, cam, winW, winH, settings.showUnitNames, settings.showNpcNames);
    renderer.drawPropEmotes(world, cam, winW, winH, settings.showPropNames);
    if (!overPanels(mouseX, mouseY, winW, winH) && !session.contextMenu.open && !itemMenu.open && session.dialogueUnit == NULL_ENTITY) {
        for (Entity u : renderer.unitsAtPixel(world, cam, mouseX, mouseY, winW, winH)) {
            const Carried* cd = world.get<Carried>(u);
            const bool names = world.has<Commandable>(u) ? settings.showUnitNames : settings.showNpcNames;
            if (cd && world.has<Selected>(cd->by)) {
                renderer.drawHoverCue(world, cam, winW, winH, u, EmoteId::CuePutDown, names);
                break;
            }
            if (talkable(u)) {
                renderer.drawHoverCue(world, cam, winW, winH, u, EmoteId::CueTalk, names);
                break;
            }
        }
    }
    renderer.drawFloatingNumbers(floatingNumbers, cam, winW, winH);
    for (const TradeLabel& l : takeQueuedLabels()) tradeLabels.push_back(l);
    for (TradeLabel& l : tradeLabels) l.age += dt;
    tradeLabels.erase(std::remove_if(tradeLabels.begin(), tradeLabels.end(),
                                     [](const TradeLabel& l) { return l.age >= TRADE_LABEL_LIFE; }),
                      tradeLabels.end());
    renderer.drawTradeLabels(tradeLabels, world, cam, winW, winH);
    renderer.drawPropProgress(world, cam, winW, winH, settings.showPropNames);
    if ((SDL_GetModState() & SDL_KMOD_ALT) != 0) {
        renderer.drawGroundLabels(world, terrain, cam, winW, winH, mouseX, mouseY);
    } else if (!overPanels(mouseX, mouseY, winW, winH) && !session.contextMenu.open && !itemMenu.open) {
        const Entity hovered = renderer.groundItemAtPixel(world, terrain, cam, mouseX, mouseY, winW, winH, false);
        if (hovered != NULL_ENTITY) renderer.drawGroundItemName(world, cam, winW, winH, hovered);
    }
}

ItemStack App::draggedStack() {
    if (dragGear >= 0) {
        const Equipment* worn = world.get<Equipment>(dragUnit);
        return worn ? ItemStack{ worn->items[dragGear], worn->items[dragGear] != 0 ? 1 : 0 } : ItemStack{};
    }
    const Inventory* inv = world.get<Inventory>(dragUnit);
    return inv && dragSlot >= 0 && dragSlot < INVENTORY_SLOTS ? inv->slots[dragSlot] : ItemStack{};
}

void App::drawGamePanels() {
    const std::vector<Entity> own = ownUnitList();
    const UnitPanelLayout L = squadPanelLayout(winW, winH);
    unitPanelRow = L.scrollRow;
    renderer.drawUnitPanel(world, L, own, runMode, repView);
    drawPanelLock(PanelId::Squad);
    const GameLogLayout cl = gameLogPanel(winW, winH);
    logOffset = cl.offset;
    InfoTime timeInfo;
    if (infoTab == 2) {
        timeInfo.clock = formatTime(gs.clockSeconds);
        timeInfo.date = formatDate(gs.clockSeconds);
        timeInfo.ymd = formatYmd(gs.clockSeconds);
        timeInfo.days = langf("UILABEL_DAYS_PASSED", { std::to_string(dayIndexOf(gs.clockSeconds)) });
    }
    renderer.drawGameLog(cl, infoLines(gameLog, infoTab, logFilter), logFilter, mouseX, mouseY, timeInfo);
    if (!cl.filterOpen) drawPanelLock(PanelId::Info);
    const Entity shown = sidebarUnitOf(world);
    {
        const int draggedId = itemDrag ? draggedStack().item : 0;
        const SidebarLayout sl = sidebarFor(winW, winH, sidebarScroll);
        sidebarScroll = sl.scroll;
        renderer.drawInventorySidebar(sl, world, shown, mouseX, mouseY,
                                      itemDrag && dragGear < 0 ? dragSlot : -1,
                                      itemDrag && dragGear >= 0 ? dragGear : -1,
                                      draggedId, shownIsYours());
        if (shown != NULL_ENTITY && !itemDrag) renderer.drawBehaviorTip(sl, world, shown, mouseX, mouseY);
    }
    drawPanelLock(PanelId::Unit);
    if (itemMenu.open) {
        const std::vector<std::string> labels = menuLabels();
        if (!labels.empty()) {
            renderer.drawItemMenu(renderer.itemMenuLayout(itemMenu.x, itemMenu.y, (int)labels.size(), winW, winH),
                                  labels, mouseX, mouseY);
        }
    }
    if (itemDrag) {
        const ItemStack dragged = draggedStack();
        if (dragged.item != 0) renderer.drawDraggedStack(mouseX, mouseY, dragged.item, dragged.count);
    }
}

void App::drawStoragePanel() {
    if (session.storageProp != NULL_ENTITY) {
        const Storage* st = world.get<Storage>(session.storageProp);
        const Prop* sp = world.get<Prop>(session.storageProp);
        const PropInfo& sinfo = propInfo(sp ? sp->kind : PropKind::Wagon);
        std::string hint, status;
        if (sinfo.use == PropUse::Crafting) {
            hint = langf("UILABEL_CRAFT_HINT", { propNameOf(world, session.storageProp) });
        } else if (sinfo.use == PropUse::PassiveCrafting) {
            if (sinfo.input == INPUT_COOKABLE) {
                hint = langf("UILABEL_PASSIVE_HINT", { lang("MISC_FOOD_TO_COOK"),
                                                       std::to_string((int)std::lround(sinfo.fuelHours * 60.0)),
                                                       lang("MISC_COOKED_FOOD") });
            } else if (sinfo.item == 0) hint = langf("UILABEL_UPKEEP_HINT", { itemNameOf(sinfo.input) });
            else hint = langf("UILABEL_PASSIVE_HINT", { itemNameOf(sinfo.input),
                                                   std::to_string((int)std::lround(sinfo.fuelHours * 60.0)),
                                                   itemNameOf(sinfo.item) });
            const Fuel* fuel = world.get<Fuel>(session.storageProp);
            switch (passiveStateOf(world, session.storageProp)) {
            case PassiveState::OutputFull: status = lang("MISC_OUTPUT_FULL"); break;
            case PassiveState::NoFuel: status = lang(sinfo.item == 0 ? "MISC_NO_UPKEEP" : "MISC_NO_FUEL"); break;
            case PassiveState::Burning: {
                const std::string mins = std::to_string((int)std::ceil((fuel ? fuel->hours : 0.0) * 60.0 - 1e-6));
                status = langf(sinfo.item == 0 ? "MISC_UPKEEP_LEFT" : "MISC_FUEL_LEFT", { mins });
                break;
            }
            }
        } else if (sinfo.accepts != ItemFilter::Any) {
            hint = langf("UILABEL_STORAGE_TAKES", { lang(itemFilterKey(sinfo.accepts)) }) + " " + lang("UILABEL_STORAGE_HINT");
        } else {
            hint = lang("UILABEL_STORAGE_HINT");
        }
        const Lock* ownLock = ownedLockOf(world, session.storageProp) ? world.get<Lock>(session.storageProp) : nullptr;
        renderer.drawStorage(storageLayout(world, winW, winH), propNameOf(world, session.storageProp), hint, st,
                             world.get<Output>(session.storageProp), status, session.storageDrag, mouseX, mouseY,
                             ownLock ? lang(ownLock->locked ? "UILABEL_UNLOCK" : "UILABEL_LOCK") : std::string());
        if (const ItemStack* dragged = storageSlot(world, session.storageDrag); dragged && dragged->item != 0) {
            renderer.drawDraggedStack(mouseX, mouseY, dragged->item, dragged->count);
        }
        if (session.quantity.open && session.quantity.toStorage) {
            renderer.drawQuantityPrompt(layoutQuantity(winW, winH), session.quantity.name, session.quantity.chosen,
                                        session.quantity.available, mouseX, mouseY);
        }
    }
}

float App::disbandHeldShare() {
    if (session.disbandHeldAt == 0) return 0.0f;
    return (float)(SDL_GetTicks() - session.disbandHeldAt) / (float)DISBAND_HOLD_MS;
}

void App::updateDisbandHold() {
    if (session.disbandTarget != NULL_ENTITY && !world.has<Commandable>(session.disbandTarget)) {
        session.disbandTarget = NULL_ENTITY;
        session.disbandHeldAt = 0;
    }
    if (session.disbandTarget == NULL_ENTITY || session.disbandHeldAt == 0) return;
    if (!layoutDisband(winW, winH).disband.contains(mouseX, mouseY)) {
        session.disbandHeldAt = 0;
        return;
    }
    if (disbandHeldShare() < 1.0f) return;
    const Entity u = session.disbandTarget;
    session.disbandTarget = NULL_ENTITY;
    session.disbandHeldAt = 0;
    if (session.storageUnit == u) closeStorage();
    if (session.lootBy == u) closeLoot();
    if (session.dialoguePlayer == u) {
        endTalk();
        session.dialoguePlayer = NULL_ENTITY;
    }
    if (session.contextMenu.open && session.contextMenu.target == u) session.contextMenu.open = false;
    const bool mob = world.has<Mob>(u);
    const std::string who = nameOf(world, u);
    if (disbandUnit(world, u)) {
        showNotice(langf(mob ? "MISC_DISBANDED_MOB" : "MISC_DISBANDED", { who }));
        playSfx(Sfx::UiOrder);
    }
}

void App::drawWorldMapPopup() {
    std::vector<MapDot> mapDots;
    for (Entity e : world.view<Prop, Transform>()) {
        const Transform* t = world.get<Transform>(e);
        mapDots.push_back(MapDot{ Vec2{ t->x, t->z }, MapDotKind::Prop });
    }
    for (Entity e : world.view<Unit, Transform>()) {
        const Transform* t = world.get<Transform>(e);
        MapDotKind kind = MapDotKind::Player;
        if (world.has<Hostile>(e)) kind = MapDotKind::Hostile;
        else if (world.has<Neutral>(e)) kind = MapDotKind::Neutral;
        else if (!world.has<Commandable>(e)) continue;
        mapDots.push_back(MapDot{ Vec2{ t->x, t->z }, kind });
    }
    WorldMapMarks marks;
    marks.camX = cam.focusX;
    marks.camZ = cam.focusZ;
    marks.camYaw = cam.yaw;
    marks.moveX = moveMarkX;
    marks.moveZ = moveMarkZ;
    marks.moveAlpha = std::clamp(moveMarkTime, 0.0f, 1.0f);
    renderer.drawWorldMap(worldMapFor(winW, winH), mapDots, mapCx, mapCz, mapSpan, marks);
}

void App::drawTalkOrTrade() {
    if (session.dialogueUnit != NULL_ENTITY) {
        if (!world.has<Unit>(session.dialogueUnit)) {
            endTalk();
        } else {
            if (session.tradeOpen) {
                const Inventory* npcInv = world.get<Inventory>(session.dialogueUnit);
                renderer.drawTrade(layoutTrade(winW, winH, renderer.pauseBarHeight(true)), nameOf(world, session.dialogueUnit), nameOf(world, session.dialoguePlayer),
                                   npcInv, session.tradeNpc, session.tradePlayer, session.tradeNpcDrag,
                                   checkTrade(world, session.dialogueUnit, session.dialoguePlayer), !session.freeTrade, mouseX, mouseY);
                if (npcInv && session.tradeNpcDrag >= 0 && npcInv->slots[session.tradeNpcDrag].item != 0) {
                    renderer.drawDraggedStack(mouseX, mouseY, npcInv->slots[session.tradeNpcDrag].item, npcInv->slots[session.tradeNpcDrag].count);
                }
                if (session.quantity.open) {
                    renderer.drawQuantityPrompt(layoutQuantity(winW, winH), session.quantity.name, session.quantity.chosen,
                                                session.quantity.available, mouseX, mouseY);
                }
            } else {
                const Dialogue* dg = world.get<Dialogue>(session.dialogueUnit);
                renderer.drawDialogue(dialogueBox(winW, winH), session.dialogueConvo,
                                      dg ? dg->options : std::vector<DialogueOption>{}, mouseX, mouseY);
            }
        }
    }
}

void App::drawPlayingScreen() {
    updateWorldMapView();
    renderer.drawFrame(world, terrain, cam, sel, hud, winW, winH);
    renderer.drawPivotRing(cam, terrain, pivotAlpha, winW, winH);
    renderer.drawActionCrystal(cam, terrain, actionX, actionZ, actionAlpha, winW, winH);
    renderer.beginUi(winW, winH);
    drawWorldOverlays();
    drawGamePanels();
    if (session.lootUnit != NULL_ENTITY) {
        const std::string who = world.has<Corpse>(session.lootUnit) ? langf("UILABEL_CORPSE_OF", { nameOf(world, session.lootUnit) })
                                                           : nameOf(world, session.lootUnit);
        renderer.drawLoot(layoutLoot(winW, winH), who, world.get<Equipment>(session.lootUnit), world.get<Inventory>(session.lootUnit),
                          mouseX, mouseY);
    }
    drawStoragePanel();
    if (session.contextMenu.open) renderer.drawItemMenu(contextMenuLayout(), contextLabels(), mouseX, mouseY);
    if (session.disbandTarget != NULL_ENTITY) {
        renderer.drawDisband(world, layoutDisband(winW, winH), session.disbandTarget, disbandHeldShare(), mouseX, mouseY);
    }
    if (optimizeAllOpen) {
        renderer.drawOptimizeAll(layoutOptimizeAll(winW, winH), (int)ownUnitList().size(), settings.optimizeAttackShare,
                                 { settings.optimizeAccessories, settings.optimizeTwoHanded, settings.optimizeValueTies },
                                 mouseX, mouseY);
    }
    if (mapOpen) drawWorldMapPopup();
    drawTalkOrTrade();
    if (session.paused) renderer.drawPauseBar(true);
    renderer.endUi();
}
