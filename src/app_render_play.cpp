#include "app.h"
#include "prop_info.h"
#include "profile.h"

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
    if (sel.dragging) {
        renderer.drawSelectArrows(world, cam, unitsInBox(world, cam, sel.x0, sel.y0, sel.x1, sel.y1, winW, winH), winW, winH);
    }
    if (settings.showUnitNames || settings.showNpcNames) {
        renderer.drawUnitNames(world, cam, winW, winH, settings.showUnitNames, settings.showNpcNames);
    }
    renderer.drawEmotes(world, cam, winW, winH, settings.showUnitNames, settings.showNpcNames);
    renderer.drawPropEmotes(world, cam, winW, winH);
    if (!mouseOverPanels() && !session.contextMenu.open && !itemMenu.open && session.dialogueUnit == NO_OBJECT) {
        for (ObjectId u : unitsUnderMouse) {
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
    for (Puff& p : puffs) p.age += dt;
    puffs.erase(std::remove_if(puffs.begin(), puffs.end(), [](const Puff& p) { return p.age >= PUFF_LIFE; }), puffs.end());
    renderer.drawPuffs(puffs, cam, winW, winH);
    for (TradeLabel& l : tradeLabels) l.age += dt;
    tradeLabels.erase(std::remove_if(tradeLabels.begin(), tradeLabels.end(),
                                     [](const TradeLabel& l) { return l.age >= TRADE_LABEL_LIFE; }),
                      tradeLabels.end());
    renderer.drawTradeLabels(tradeLabels, world, cam, winW, winH);
    renderer.drawPropProgress(world, cam, winW, winH);
    if ((SDL_GetModState() & SDL_KMOD_ALT) != 0) {
        renderer.drawGroundLabels(world, terrain, cam, winW, winH, mouseX, mouseY);
    } else if (!mouseOverPanels() && !session.contextMenu.open && !itemMenu.open) {
        const ObjectId hovered = renderer.nearestGroundItem(itemsUnderMouse, world, cam, mouseX, mouseY, winW, winH);
        if (hovered != NO_OBJECT) renderer.drawGroundItemName(world, cam, winW, winH, hovered);
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
    const std::vector<ObjectId> own = ownUnitList();
    const UnitPanelLayout L = squadPanelLayout(winW, winH);
    unitPanelRow = L.scrollRow;
    renderer.drawUnitPanel(world, L, own, runMode, repView, squadRenaming ? &squadRenameText : nullptr, session.floorLevel,
                           session.floorManual, squadMenu == SQUAD_MENU_FORM);
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
    const ObjectId shown = sidebarUnitOf(world);
    {
        const int draggedId = itemDrag ? draggedStack().item : 0;
        const SidebarLayout sl = sidebarFor(winW, winH, sidebarScroll);
        sidebarScroll = sl.scroll;
        renderer.drawInventorySidebar(sl, world, shown, mouseX, mouseY,
                                      itemDrag && dragGear < 0 ? dragSlot : -1,
                                      itemDrag && dragGear >= 0 ? dragGear : -1,
                                      draggedId, shownIsYours());
        if (shown != NO_OBJECT && !itemDrag) renderer.drawBehaviorTip(sl, world, shown, mouseX, mouseY);
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
    if (session.storageProp != NO_OBJECT) {
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
            } else if (sinfo.item == 0) hint = langf("UILABEL_UPKEEP_HINT", { sinfo.input == INPUT_BONES ? std::string(lang("MISC_ANY_BONES")) : itemNameOf(sinfo.input) });
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
        // A store with a capacity, or more slots than fit: how full it is, and that it scrolls
        if (st && (st->capacity > 0 || storageScrollMax(world) > 0)) {
            const int cap = st->capacity > 0 ? st->capacity : 0;
            hint = langf("UILABEL_STORE_HOLDS", { std::to_string(storedCount(*st)), cap > 0 ? std::to_string(cap) : std::string("-") }) + " " + hint;
            if (storageScrollMax(world) > 0) hint += " " + lang("UILABEL_STORE_SCROLL");
        }
        session.storageScroll = std::clamp(session.storageScroll, 0, storageScrollMax(world));
        const Lock* ownLock = ownedLockOf(world, session.storageProp) ? world.get<Lock>(session.storageProp) : nullptr;
        renderer.drawStorage(storageLayout(world, winW, winH), propNameOf(world, session.storageProp), hint, st,
                             world.get<Output>(session.storageProp), status, session.storageDrag, mouseX, mouseY,
                             ownLock ? lang(ownLock->locked ? "UILABEL_UNLOCK" : "UILABEL_LOCK") : std::string(),
                             session.storageScroll);
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
    if (session.disbandTarget != NO_OBJECT && !world.has<Commandable>(session.disbandTarget)) {
        session.disbandTarget = NO_OBJECT;
        session.disbandHeldAt = 0;
    }
    if (session.disbandTarget == NO_OBJECT || session.disbandHeldAt == 0) return;
    if (!layoutDisband(winW, winH).disband.contains(mouseX, mouseY)) {
        session.disbandHeldAt = 0;
        return;
    }
    if (disbandHeldShare() < 1.0f) return;
    const ObjectId u = session.disbandTarget;
    session.disbandTarget = NO_OBJECT;
    session.disbandHeldAt = 0;
    if (session.storageUnit == u) closeStorage();
    if (session.lootBy == u) closeLoot();
    if (session.dialoguePlayer == u) {
        endTalk();
        session.dialoguePlayer = NO_OBJECT;
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
    for (ObjectId e : world.view<GroundItem, Transform>()) {
        const Transform* t = world.get<Transform>(e);
        mapDots.push_back(MapDot{ Vec2{ t->x, t->z }, MapDotKind::Item });
    }
    for (ObjectId e : world.view<Prop, Transform>()) {
        const Transform* t = world.get<Transform>(e);
        const PropKind kind = world.get<Prop>(e)->kind;
        const PropInfo& info = propInfo(kind);
        MapDotKind mark = MapDotKind::Prop;
        if (kind == PropKind::Altar) mark = MapDotKind::Altar;
        else if (info.airship || info.wheelbarrow) mark = MapDotKind::Vehicle;
        else if (world.has<Storage>(e)) mark = MapDotKind::Storage;
        mapDots.push_back(MapDot{ Vec2{ t->x, t->z }, mark });
    }
    for (ObjectId e : world.view<Entity, Transform>()) {
        const Transform* t = world.get<Transform>(e);
        MapDotKind kind = MapDotKind::Player;
        if (world.has<Citizen>(e) && factionOf(world, e) == FACTION_PLAYER) kind = MapDotKind::Player;
        else if (world.has<Hostile>(e)) kind = MapDotKind::Hostile;
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
    if (session.dialogueUnit != NO_OBJECT) {
        if (!world.has<Entity>(session.dialogueUnit)) {
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
    PROFILED("draw: world", renderer.drawFrame(world, terrain, cam, sel, hud, winW, winH));
    renderer.drawPivotRing(cam, terrain, pivotAlpha, winW, winH);
    if (formationDrag) {
        const std::vector<ObjectId> units = selectedUnits(world);
        Vec2 point{};
        const FormationSpec spec = formationFromDrag(world, units, Vec2{ dragFromX, dragFromZ }, Vec2{ dragToX, dragToZ }, formationChoice(), point);
        renderer.drawFormationPreview(cam, terrain, previewFormation(world, terrain, units, point, spec), winW, winH);
    }
    renderer.drawActionCrystal(cam, terrain, actionX, actionZ, actionAlpha, winW, winH);
    renderer.beginUi(winW, winH);
    PROFILED("draw: overlays", drawWorldOverlays());
    if (!(editing && editor.useOwnUi)) PROFILED("draw: panels", drawGamePanels());
    if (hoverProp != NO_OBJECT && SDL_GetTicks() - hoverPropSince >= PROP_TOOLTIP_DELAY_MS) {
        renderer.drawPropTooltip(propInfoLines(world, hoverProp), mouseX, mouseY);
    }
    if (session.lootUnit != NO_OBJECT) {
        const std::string who = world.has<Corpse>(session.lootUnit) ? langf("UILABEL_CORPSE_OF", { nameOf(world, session.lootUnit) })
                                                           : nameOf(world, session.lootUnit);
        renderer.drawLoot(layoutLoot(winW, winH), who, world.get<Equipment>(session.lootUnit), world.get<Inventory>(session.lootUnit),
                          mouseX, mouseY);
    }
    drawStoragePanel();
    if (session.contextMenu.open) renderer.drawItemMenu(contextMenuLayout(), contextLabels(), mouseX, mouseY);
    if (selectMenuOpen) {
        std::vector<std::string> labels;
        for (const char* key : { "UILABEL_SELECT_ALL", "UILABEL_SELECT_CLOSEST", "UILABEL_SELECT_IDLE", "UILABEL_SELECT_BUSY" }) {
            labels.push_back(lang(key));
        }
        renderer.drawItemMenu(selectMenuLayout(), labels, mouseX, mouseY);
    }
    if (squadMenu == 1 || squadMenu == 2) renderer.drawItemMenu(squadMenuLayout(), squadMenuLabels(), mouseX, mouseY);
    if (session.disbandTarget != NO_OBJECT) {
        renderer.drawDisband(world, layoutDisband(winW, winH), session.disbandTarget, disbandHeldShare(), mouseX, mouseY);
    }
    if (optimizeAllOpen) {
        renderer.drawOptimizeAll(layoutOptimizeAll(winW, winH), (int)ownUnitList().size(), settings.optimizeAttackShare,
                                 { settings.optimizeAccessories, settings.optimizeTwoHanded, settings.optimizeValueTies },
                                 mouseX, mouseY);
    }
    if (mapOpen) drawWorldMapPopup();
    drawPropAdjust();
    drawAltar();
    drawManageSquads();
    drawFactionRules();
    drawCraftWindow();
    drawDeveloper();
    drawBlueprintPanel();
    drawPlanPopup();
    drawGhosts();
    drawBehaviorGroups();
    drawStoreFilter();
    drawStall();
    drawRename();
    drawFoundationSize();
    drawMaterialAsk();
    drawTalkOrTrade();
    if (session.paused && !editing && !bpEdit.active) renderer.drawPauseBar(true);
    drawFloorBanner();
    drawFormMenu();
    if (editing) {
        drawEditorPanel();
        const LevelInfo& level = world.resource<LevelInfo>();
        actionX = level.spawnX;
        actionZ = level.spawnZ;
        actionAlpha = level.spawnSet ? 1.0f : 0.0f;
    }
    renderer.endUi();
}
