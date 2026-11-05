// Drawing the game itself: the world's overlays, the panels and popups over it.
#include "app.h"

using namespace play;

void App::updateWorldMapView() {
    // The world map's view starts out showing the whole level, and starts over each time it opens
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
        // From one chunk across to the whole level
        clampMapView(box, size, mapCx, mapCz, mapSpan);
    }
}

void App::drawWorldOverlays() {
    if (settings.showPropNames) renderer.drawPropNames(world, cam, winW, winH, settings.showPropInfo);
    if (settings.showUnitNames || settings.showNpcNames) {
        renderer.drawUnitNames(world, cam, winW, winH, settings.showUnitNames, settings.showNpcNames);
    }
    // What each unit is doing: mining, woodcutting, cooking, sleeping, encumbered
    renderer.drawEmotes(world, cam, winW, winH, settings.showUnitNames, settings.showNpcNames);
    renderer.drawPropEmotes(world, cam, winW, winH, settings.showPropNames);
    // What a right-click would do to the unit under the pointer: put down a unit your selected unit carries, or
    // talk to an NPC
    if (!overPanels(mouseX, mouseY, winW, winH) && !contextMenu.open && !itemMenu.open && dialogueUnit == NULL_ENTITY) {
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
    // Damage numbers rise over the units they hit
    renderer.drawFloatingNumbers(floatingNumbers, cam, winW, winH);
    // Labels the systems queued this frame: items gained, and what used items did
    for (const TradeLabel& l : takeQueuedLabels()) tradeLabels.push_back(l);
    for (TradeLabel& l : tradeLabels) l.age += dt;
    tradeLabels.erase(std::remove_if(tradeLabels.begin(), tradeLabels.end(),
                                     [](const TradeLabel& l) { return l.age >= TRADE_LABEL_LIFE; }),
                      tradeLabels.end());
    renderer.drawTradeLabels(tradeLabels, world, cam, winW, winH);
    renderer.drawPropProgress(world, cam, winW, winH, settings.showPropNames);
    if ((SDL_GetModState() & SDL_KMOD_ALT) != 0) {
        renderer.drawGroundLabels(world, terrain, cam, winW, winH, mouseX, mouseY);
    } else if (!overPanels(mouseX, mouseY, winW, winH) && !contextMenu.open && !itemMenu.open) {
        // The item under the pointer (its visible part) shows its name over it; a click picks it up
        const Entity hovered = renderer.groundItemAtPixel(world, terrain, cam, mouseX, mouseY, winW, winH, false);
        if (hovered != NULL_ENTITY) renderer.drawGroundItemName(world, cam, winW, winH, hovered);
    }
}

// What is being dragged from a unit: one gear item, or a stack from its inventory. Empty if that is gone.
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
    // The Information Panel, top left. Its offset is written back, so it stays in range.
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
    if (!cl.filterOpen) drawPanelLock(PanelId::Info);   // the open filter menu covers it
    // The sidebar shows the lone selected unit, yours or a red one. A red one's gear and inventory
    // are only shown: the mouse handling never starts a drag or a menu on them.
    const Entity shown = sidebarUnitOf(world);
    {
        // The item being dragged, so the gear slots can show where it fits
        const int draggedId = itemDrag ? draggedStack().item : 0;
        const SidebarLayout sl = sidebarFor(winW, winH, sidebarScroll);
        sidebarScroll = sl.scroll;   // kept within range as the list grows and shrinks
        renderer.drawInventorySidebar(sl, world, shown, mouseX, mouseY,
                                      itemDrag && dragGear < 0 ? dragSlot : -1,
                                      itemDrag && dragGear >= 0 ? dragGear : -1,
                                      draggedId, shownIsYours());
        // What the behavior under the mouse does, when nothing else is open over it
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
        // The item under the mouse: a gear item, or a stack from the inventory
        const ItemStack dragged = draggedStack();
        if (dragged.item != 0) renderer.drawDraggedStack(mouseX, mouseY, dragged.item, dragged.count);
    }
}

void App::drawStoragePanel() {
    if (storageProp != NULL_ENTITY) {
        const Storage* st = world.get<Storage>(storageProp);
        const Prop* sp = world.get<Prop>(storageProp);
        const PropInfo& sinfo = propInfo(sp ? sp->kind : PropKind::Wagon);
        std::string hint, status;
        if (sinfo.use == PropUse::Crafting) {
            hint = langf("UILABEL_CRAFT_HINT", { propNameOf(world, storageProp) });
        } else if (sinfo.use == PropUse::PassiveCrafting) {
            if (sinfo.input == INPUT_COOKABLE) {   // the campfire: any raw meat, cooked
                hint = langf("UILABEL_PASSIVE_HINT", { lang("MISC_FOOD_TO_COOK"),
                                                       std::to_string((int)std::lround(sinfo.fuelHours * 60.0)),
                                                       lang("MISC_COOKED_FOOD") });
            } else if (sinfo.item == 0) hint = langf("UILABEL_UPKEEP_HINT", { itemNameOf(sinfo.input) });
            else hint = langf("UILABEL_PASSIVE_HINT", { itemNameOf(sinfo.input),
                                                   std::to_string((int)std::lround(sinfo.fuelHours * 60.0)),
                                                   itemNameOf(sinfo.item) });
            const Fuel* fuel = world.get<Fuel>(storageProp);
            switch (passiveStateOf(world, storageProp)) {
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
        const Lock* ownLock = ownedLockOf(world, storageProp) ? world.get<Lock>(storageProp) : nullptr;
        renderer.drawStorage(storageLayout(world, winW, winH), propNameOf(world, storageProp), hint, st,
                             world.get<Output>(storageProp), status, storageDrag, mouseX, mouseY,
                             ownLock ? lang(ownLock->locked ? "UILABEL_UNLOCK" : "UILABEL_LOCK") : std::string());
        if (const ItemStack* dragged = storageSlot(world, storageDrag); dragged && dragged->item != 0) {
            renderer.drawDraggedStack(mouseX, mouseY, dragged->item, dragged->count);
        }
        if (quantity.open && quantity.toStorage) {
            renderer.drawQuantityPrompt(layoutQuantity(winW, winH), quantity.name, quantity.chosen,
                                        quantity.available, mouseX, mouseY);
        }
    }
}

void App::updateDisbandPopup() {
    // The Disband popout: the hold fills Disband's bar while the button is held and the pointer stays on it;
    // full, the unit is disbanded. A unit no longer yours (it died, or was disbanded) shuts it.
    if (disbandTarget != NULL_ENTITY && !world.has<Commandable>(disbandTarget)) {
        disbandTarget = NULL_ENTITY;
        disbandHeldAt = 0;
    }
    if (disbandTarget != NULL_ENTITY) {
        const DisbandLayout dl = layoutDisband(winW, winH);
        float held = 0.0f;
        if (disbandHeldAt != 0) {
            if (!dl.disband.contains(mouseX, mouseY)) disbandHeldAt = 0;   // moved off it: starts over
            else held = (float)(SDL_GetTicks() - disbandHeldAt) / (float)DISBAND_HOLD_MS;
        }
        if (held >= 1.0f) {
            const Entity u = disbandTarget;
            disbandTarget = NULL_ENTITY;
            disbandHeldAt = 0;
            // Panels that were the unit's shut with it
            if (storageUnit == u) closeStorage();
            if (lootBy == u) closeLoot();
            if (dialoguePlayer == u) {
                endTalk();
                dialoguePlayer = NULL_ENTITY;
            }
            if (contextMenu.open && contextMenu.target == u) contextMenu.open = false;
            const bool mob = world.has<Mob>(u);
            const std::string who = nameOf(world, u);
            if (disbandUnit(world, u)) {
                showNotice(langf(mob ? "MISC_DISBANDED_MOB" : "MISC_DISBANDED", { who }));
                playSfx(Sfx::UiOrder);
            }
        } else {
            renderer.drawDisband(world, dl, disbandTarget, held, mouseX, mouseY);
        }
    }
}

void App::drawWorldMapPopup() {
    // The world map's marks: every prop of any kind as a purple square, every player unit as a green dot and
    // the other units as red or gray dots
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
    marks.moveAlpha = std::clamp(moveMarkTime, 0.0f, 1.0f);   // the last second fades it out
    renderer.drawWorldMap(worldMapFor(winW, winH), mapDots, mapCx, mapCz, mapSpan, marks);
}

void App::drawTalkOrTrade() {
    // The talk box, over everything. It ends if the unit it is with is gone.
    if (dialogueUnit != NULL_ENTITY) {
        if (!world.has<Unit>(dialogueUnit)) {
            dialogueUnit = NULL_ENTITY;
            dialogueConvo.clear();
            tradeOpen = false;
            quantity.open = false;
        } else {
            if (tradeOpen) {
                const Inventory* npcInv = world.get<Inventory>(dialogueUnit);
                renderer.drawTrade(layoutTrade(winW, winH, renderer.pauseBarHeight(false)), nameOf(world, dialogueUnit), nameOf(world, dialoguePlayer),
                                   npcInv, tradeNpc, tradePlayer, tradeNpcDrag,
                                   checkTrade(world, dialogueUnit, dialoguePlayer), !freeTrade, mouseX, mouseY);
                if (npcInv && tradeNpcDrag >= 0 && npcInv->slots[tradeNpcDrag].item != 0) {
                    renderer.drawDraggedStack(mouseX, mouseY, npcInv->slots[tradeNpcDrag].item, npcInv->slots[tradeNpcDrag].count);
                }
                if (quantity.open) {
                    renderer.drawQuantityPrompt(layoutQuantity(winW, winH), quantity.name, quantity.chosen,
                                                quantity.available, mouseX, mouseY);
                }
            } else {
                const Dialogue* dg = world.get<Dialogue>(dialogueUnit);
                renderer.drawDialogue(dialogueBox(winW, winH), dialogueConvo,
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
    // In-game panels, drawn over the world: item names while Alt is held, the unit list along the
    // bottom, the inventory of a lone selected unit, and the stack being dragged under the mouse
    renderer.beginUi(winW, winH);
    drawWorldOverlays();
    drawGamePanels();
    // The looting panel over an Unconscious unit or a corpse
    if (lootUnit != NULL_ENTITY) {
        const std::string who = world.has<Corpse>(lootUnit) ? langf("UILABEL_CORPSE_OF", { nameOf(world, lootUnit) })
                                                           : nameOf(world, lootUnit);
        renderer.drawLoot(layoutLoot(winW, winH), who, world.get<Equipment>(lootUnit), world.get<Inventory>(lootUnit),
                          mouseX, mouseY);
    }
    drawStoragePanel();
    if (contextMenu.open) renderer.drawItemMenu(contextMenuLayout(), contextLabels(), mouseX, mouseY);
    updateDisbandPopup();
    if (optimizeAllOpen) {
        renderer.drawOptimizeAll(layoutOptimizeAll(winW, winH), (int)ownUnitList().size(), settings.optimizeAttackShare,
                                 { settings.optimizeAccessories, settings.optimizeTwoHanded, settings.optimizeValueTies },
                                 mouseX, mouseY);
    }
    // The world map popout is over everything, the panels included
    if (mapOpen) drawWorldMapPopup();
    drawTalkOrTrade();
    // The PAUSED bar, over everything in the game and its popouts, while the game stands still
    if (paused || dialogueUnit != NULL_ENTITY) renderer.drawPauseBar(paused && dialogueUnit == NULL_ENTITY);
    renderer.endUi();
}
