#include "app.h"

using namespace play;

void App::render() {
    // ---- Render ----
    renderer.setDayMinutes(timeOfDayMinutes(gs.clockSeconds));
    renderer.setSky(sky);
    auto drawButtons = [&](const std::vector<Button>& buttons) {
        for (const Button& b : buttons) {
            renderer.drawButton(b.rect, b.label, b.enabled, b.rect.contains(mouseX, mouseY));
        }
    };
    // The title and pause menus' items. A new screen lights its first usable item, and its items slide in one after
    // another. Moving the mouse onto an item lights it.
    auto drawMenuItems = [&](Screen which, const std::vector<Button>& buttons) {
        const unsigned long long now = SDL_GetTicks();
        if (menuScreen != which) {
            menuScreen = which;
            menuShownAt = now;
            menuSel = -1;
            for (size_t i = 0; i < buttons.size(); ++i) {
                if (buttons[i].enabled) { menuSel = (int)i; break; }
            }
        }
        const bool moved = mouseX != menuMouseX || mouseY != menuMouseY;
        menuMouseX = mouseX;
        menuMouseY = mouseY;
        for (size_t i = 0; i < buttons.size(); ++i) {
            if (moved && buttons[i].enabled && buttons[i].rect.contains(mouseX, mouseY)) menuSel = (int)i;
        }
        const float since = (float)(now - menuShownAt) / 1000.0f;
        for (size_t i = 0; i < buttons.size(); ++i) {
            const Button& b = buttons[i];
            const float appear = std::clamp((since - (0.055f * (float)i + 0.1f)) / 0.34f, 0.0f, 1.0f);
            const std::string sub = b.action == Action::Language ? languageDisplayName() : std::string();
            renderer.drawMenuItem(b.rect, b.label, sub, (int)i == menuSel && b.enabled, b.enabled, appear);
        }
    };
    // Notices sit at the top middle of the window
    auto drawNotice = [&]() {
        if (noticeSec > 0.0f) renderer.drawNotice(notice, winW, winH);
    };
    auto dim = [&](float alpha) {
        renderer.drawRect(Rect{ 0.0f, 0.0f, (float)winW, (float)winH }, RGBA{ 0.0f, 0.0f, 0.0f, alpha });
    };

    if (screen != Screen::Title && screen != Screen::Pause) menuScreen = screen;   // the next menu opens fresh
    renderer.setMenuText(screen != Screen::Playing);
    renderer.hoverFrameStart();
    switch (screen) {
    case Screen::Title:
        renderer.beginUi(winW, winH);
        renderer.drawTitleBackground();
        {
            // The game's name and the notice board, top left; the menu down the right; along the bottom, the build
            // stamp and the keys
            const TitleLayout tl = layoutTitle(winW, winH);
            renderer.drawTitleHeader(tl, lang("GAME_TITLE_FIRST"), lang("GAME_TITLE_SECOND"),
                                     messageOfTheDay());
            drawMenuItems(Screen::Title, layoutButtons(Screen::Title, winW, winH, saves));
            renderer.drawTitleFooter(tl.footer, BUILD_STAMP, lang("MISC_TITLE_KEYS"));
        }
        renderer.endUi();
        break;

    case Screen::Settings: {
        renderer.beginUi(winW, winH);
        renderer.drawTitleBackground();
        dim(0.45f);
        const std::vector<RowSpec> rows = settingsRows(settingsUi.tab, settings, displayInfo);
        drawSettingsMenu(renderer, layoutSettings(winW, winH, rows, settingsUi), rows, settingsUi, mouseX, mouseY);
        renderer.endUi();
        break;
    }

    case Screen::Playing: {
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
        renderer.drawFrame(world, terrain, cam, sel, hud, winW, winH);
        renderer.drawPivotRing(cam, terrain, pivotAlpha, winW, winH);
        renderer.drawActionCrystal(cam, terrain, actionX, actionZ, actionAlpha, winW, winH);
        // In-game panels, drawn over the world: item names while Alt is held, the unit list along the
        // bottom, the inventory of a lone selected unit, and the stack being dragged under the mouse
        renderer.beginUi(winW, winH);
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
        {
            const std::vector<Entity> own = ownUnitList();
            const UnitPanelLayout L = layoutUnitPanel(winW, winH, (int)own.size(), unitPanelRow, squadCollapsed, squadPlace, factionTab, (int)reputationFactions().size(), repScroll);
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
                int draggedId = 0;
                if (itemDrag && dragGear >= 0) {
                    const Equipment* worn = world.get<Equipment>(dragUnit);
                    if (worn) draggedId = worn->items[dragGear];
                } else if (itemDrag) {
                    const Inventory* inv = world.get<Inventory>(dragUnit);
                    if (inv && dragSlot >= 0 && dragSlot < INVENTORY_SLOTS) draggedId = inv->slots[dragSlot].item;
                }
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
                if (dragGear >= 0) {
                    const Equipment* worn = world.get<Equipment>(dragUnit);
                    if (worn && worn->items[dragGear] != 0) renderer.drawDraggedStack(mouseX, mouseY, worn->items[dragGear], 1);
                } else {
                    const Inventory* inv = world.get<Inventory>(dragUnit);
                    if (inv && dragSlot >= 0 && dragSlot < INVENTORY_SLOTS && inv->slots[dragSlot].item != 0) {
                        renderer.drawDraggedStack(mouseX, mouseY, inv->slots[dragSlot].item, inv->slots[dragSlot].count);
                    }
                }
            }
        }
        // The looting panel over an Unconscious unit or a corpse
        if (lootUnit != NULL_ENTITY) {
            const std::string who = world.has<Corpse>(lootUnit) ? langf("UILABEL_CORPSE_OF", { nameOf(world, lootUnit) })
                                                               : nameOf(world, lootUnit);
            renderer.drawLoot(layoutLoot(winW, winH), who, world.get<Equipment>(lootUnit), world.get<Inventory>(lootUnit),
                              mouseX, mouseY);
        }
        // The storage panel, and the item being dragged out of it
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
        if (contextMenu.open) renderer.drawItemMenu(contextMenuLayout(), contextLabels(), mouseX, mouseY);
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
        if (optimizeAllOpen) {
            renderer.drawOptimizeAll(layoutOptimizeAll(winW, winH), (int)ownUnitList().size(), settings.optimizeAttackShare,
                                     { settings.optimizeAccessories, settings.optimizeTwoHanded, settings.optimizeValueTies },
                                     mouseX, mouseY);
        }
        // The world map popout is over everything, the panels included
        if (mapOpen) {
            WorldMapMarks marks;
            marks.camX = cam.focusX;
            marks.camZ = cam.focusZ;
            marks.camYaw = cam.yaw;
            marks.moveX = moveMarkX;
            marks.moveZ = moveMarkZ;
            marks.moveAlpha = std::clamp(moveMarkTime, 0.0f, 1.0f);   // the last second fades it out
            renderer.drawWorldMap(worldMapFor(winW, winH), mapDots, mapCx, mapCz, mapSpan, marks);
        }
        // The talk box, over everything. It ends if the unit it is with is gone.
        if (dialogueUnit != NULL_ENTITY) {
            if (!world.has<Unit>(dialogueUnit)) {
                dialogueUnit = NULL_ENTITY;
                dialogueConvo.clear();
                tradeOpen = false;
            } else {
                float mhx = 0.0f, mhy = 0.0f;
                SDL_GetMouseState(&mhx, &mhy);
                if (dialogueUnit == NULL_ENTITY) quantity.open = false;
                if (tradeOpen) {
                    const Inventory* npcInv = world.get<Inventory>(dialogueUnit);
                    renderer.drawTrade(layoutTrade(winW, winH, renderer.pauseBarHeight(false)), nameOf(world, dialogueUnit), nameOf(world, dialoguePlayer),
                                       npcInv, tradeNpc, tradePlayer, tradeNpcDrag,
                                       checkTrade(world, dialogueUnit, dialoguePlayer), !freeTrade, mhx, mhy);
                    if (npcInv && tradeNpcDrag >= 0 && npcInv->slots[tradeNpcDrag].item != 0) {
                        renderer.drawDraggedStack(mhx, mhy, npcInv->slots[tradeNpcDrag].item, npcInv->slots[tradeNpcDrag].count);
                    }
                    if (quantity.open) {
                        renderer.drawQuantityPrompt(layoutQuantity(winW, winH), quantity.name, quantity.chosen,
                                                    quantity.available, mhx, mhy);
                    }
                } else {
                    const Dialogue* dg = world.get<Dialogue>(dialogueUnit);
                    renderer.drawDialogue(dialogueBox(winW, winH), dialogueConvo,
                                          dg ? dg->options : std::vector<DialogueOption>{}, mhx, mhy);
                }
            }
        }
        // The PAUSED bar, over everything in the game and its popouts, while the game stands still
        if (paused || dialogueUnit != NULL_ENTITY) renderer.drawPauseBar(paused && dialogueUnit == NULL_ENTITY);
        renderer.endUi();
        }
        break;

    case Screen::MapSelect: {
        // The same popout as the load list: a name and date for each map, sorted, scrolled in a window
        const float s = winH / 720.0f;
        PanelGeometry g = panelGeometry(winW, winH);
        const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f };
        const RGBA gray{ 0.68f, 0.60f, 0.54f, 1.0f };
        const RGBA hover{ 0.961f, 0.788f, 0.459f, 1.0f };
        const RGBA hint{ 0.957f, 0.902f, 0.796f, 1.0f };
        const RGBA builtIn{ 1.0f, 0.62f, 0.62f, 1.0f };
        const float cx = g.box.x + g.box.w * 0.5f;

        renderer.beginUi(winW, winH);
        renderer.drawTitleBackground();
        dim(0.45f);
        renderer.drawThemedPanel(g.box);
        renderer.drawUiTextCentered(uiUpper(lang("UILABEL_NEW_GAME")), cx, g.box.y + g.pad, white);
        renderer.drawUiTextCentered(maps.empty() ? lang("UILABEL_NO_MAPS")
                                                 : lang("UILABEL_CLICK_MAP"),
                                    cx, g.nameBox.y + g.nameBox.h * 0.22f, gray);

        // Column headers. The active sort column shows its direction.
        const float innerX = g.box.x + g.pad, innerW = g.box.w - 2.0f * g.pad;
        const float textInset = 12.0f * s;
        const float listH = renderer.listTextHeight("Ag");
        const float hy = g.headerY + (g.headerH - listH) * 0.5f;
        auto headerLabel = [&](const std::string& label, bool active) {
            if (!active) return label;
            return label + (mapSortAsc ? "  ^" : "  v");
        };
        renderer.drawListText(headerLabel(lang("UILABEL_NAME"), mapSortByName), innerX + textInset, hy,
                              mapSortByName ? white : gray);
        renderer.drawListText(lang("UILABEL_TYPE"), innerX + COL_TYPE * innerW, hy, gray);
        renderer.drawListText(headerLabel(lang("UILABEL_CREATED"), !mapSortByName), innerX + COL_TIME * innerW, hy,
                              !mapSortByName ? white : gray);

        // "..." rows: how many maps are hidden above and below the visible rows
        const size_t total = maps.size();
        const size_t shownFirst = std::min(mapOffset, total);
        const size_t shownLast = std::min(shownFirst + visibleSaveRows(winH), total);
        if (shownFirst > 0) {
            const bool hot = g.aboveBand.contains(mouseX, mouseY);
            renderer.drawListText("...   " + std::to_string(shownFirst) + " more above",
                                  innerX + textInset, g.aboveBand.y + (g.aboveBand.h - listH) * 0.5f,
                                  hot ? hover : hint);
        }
        if (shownLast < total) {
            const bool hot = g.belowBand.contains(mouseX, mouseY);
            renderer.drawListText("...   " + std::to_string(total - shownLast) + " more below",
                                  innerX + textInset, g.belowBand.y + (g.belowBand.h - listH) * 0.5f,
                                  hot ? hover : hint);
        }

        for (const Button& b : layoutMapPanel(winW, winH, maps, mapOffset)) {
            const bool isHover = b.rect.contains(mouseX, mouseY);
            if (b.action == Action::PickMap) {
                // A map row: background, then its name, type and date in the columns
                const MapChoice& m = maps[b.param];
                renderer.drawButton(b.rect, "", true, isHover);
                const float ry = b.rect.y + (b.rect.h - listH) * 0.5f;
                renderer.drawListText(m.name, b.rect.x + textInset, ry, isHover ? hover : white);
                renderer.drawListText(mapTypeLabel(m), b.rect.x + COL_TYPE * b.rect.w, ry,
                                      (m.source == "data" || m.source == "packed") ? builtIn : white);
                const std::string when = (m.source == "data" || m.source == "packed") ? std::string(lang("UILABEL_BUILT_IN_WHEN"))
                                       : m.created > 0 ? timeString(m.created) : std::string(lang("UILABEL_UNKNOWN"));
                renderer.drawListText(when, b.rect.x + COL_TIME * b.rect.w, ry, isHover ? hover : white);
            } else if (b.action == Action::MapScrollUp || b.action == Action::MapScrollDown
                       || b.action == Action::MapSortName || b.action == Action::MapSortTime) {
                // Drawn above as text: the "..." rows and the headers only need their click areas
                continue;
            } else {
                drawButtons({ b });
            }
        }
        renderer.endUi();
        break;
    }

    case Screen::ScenarioSelect: {
        // The picked scenario's name and description at the top, the list of scenarios at the bottom
        const float s = winH / 720.0f;
        const ScenarioLayout sl = layoutScenario(winW, winH, (int)ScenarioId::Count, scenarioScroll);
        scenarioScroll = sl.first;
        const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f };
        const RGBA gray{ 0.80f, 0.72f, 0.64f, 1.0f };
        const RGBA accent{ 1.0f, 0.78f, 0.30f, 1.0f };
        renderer.beginUi(winW, winH);
        renderer.drawTitleBackground();
        dim(0.45f);
        renderer.drawThemedPanel(sl.box);
        renderer.drawListText(lang("UILABEL_SCENARIO"), sl.heading.x, sl.heading.y, gray);
        const std::string name = lang(scenarioNameKey(scenarioPick));
        renderer.drawUiTextInBox(name, Rect{ sl.name.x, sl.name.y, sl.name.w, sl.name.h }, accent);
        float ty = sl.desc.y;
        const float lh = renderer.listTextHeight("Ag");
        for (const std::string& ln : renderer.wrapListText(lang(scenarioDescKey(scenarioPick)), sl.desc.w)) {
            if (ty + lh > sl.desc.y + sl.desc.h + 0.5f) break;
            renderer.drawListText(ln, sl.desc.x, ty, white);
            ty += lh;
        }
        renderer.drawRect(Rect{ sl.list.x, sl.list.y - 8.0f * s, sl.list.w, 1.0f * s }, RGBA{ 0.588f, 0.196f, 0.259f, 1.0f });
        for (size_t i = 0; i < sl.rows.size(); ++i) {
            const ScenarioId id = (ScenarioId)(sl.first + (int)i);
            const Rect& r = sl.rows[i];
            renderer.drawButton(r, lang(scenarioNameKey(id)), true, r.contains(mouseX, mouseY));
            if (id == scenarioPick) {
                // The picked one: a bar down its left edge and a frame
                renderer.drawRect(Rect{ r.x, r.y, 5.0f * s, r.h }, accent);
                renderer.drawRect(Rect{ r.x, r.y, r.w, 2.0f * s }, accent);
                renderer.drawRect(Rect{ r.x, r.y + r.h - 2.0f * s, r.w, 2.0f * s }, accent);
                renderer.drawRect(Rect{ r.x + r.w - 2.0f * s, r.y, 2.0f * s, r.h }, accent);
            }
        }
        if (sl.track.h > 0.0f) {
            renderer.drawRect(sl.track, RGBA{ 0.20f, 0.09f, 0.11f, 0.9f });
            renderer.drawRect(sl.thumb, RGBA{ 0.910f, 0.376f, 0.431f, 0.9f });
        }
        renderer.drawButton(sl.back, lang("UILABEL_BACK"), true, sl.back.contains(mouseX, mouseY));
        renderer.drawButton(sl.next, lang("UILABEL_CONTINUE"), true, sl.next.contains(mouseX, mouseY));
        renderer.endUi();
        break;
    }

    case Screen::PartySetup: {
        const std::vector<DropEntry> drop = dropdownEntries(party, partyUi);
        renderer.beginUi(winW, winH);
        renderer.drawTitleBackground();
        dim(0.45f);
        renderer.drawPartySetup(layoutPartySetup(winW, winH, party, partyUi, drop), party, partyUi,
                                partyPanels(winW, winH), mouseX, mouseY);
        renderer.endUi();
        break;
    }

    case Screen::Pause:
        // The world and its PAUSED bar as in the game, then the menu over them in the menu fonts
        renderer.setMenuText(false);
        renderer.drawFrame(world, terrain, cam, sel, {}, winW, winH);
        renderer.beginUi(winW, winH);
        dim(0.55f);
        renderer.drawPauseBar(false);
        renderer.setMenuText(true);
        drawMenuItems(Screen::Pause, layoutButtons(Screen::Pause, winW, winH, saves));
        renderer.endUi();
        break;
    }

    // ---- Save/load popout, drawn over whichever screen is showing ----
    if (panelOpen) {
        const float s = winH / 720.0f;
        PanelGeometry g = panelGeometry(winW, winH);
        const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f };
        const RGBA gray{ 0.68f, 0.60f, 0.54f, 1.0f };
        const RGBA hover{ 0.961f, 0.788f, 0.459f, 1.0f };
        const RGBA hint{ 0.957f, 0.902f, 0.796f, 1.0f };
        const float cx = g.box.x + g.box.w * 0.5f;

        renderer.beginUi(winW, winH);
        dim(0.25f);
        renderer.drawThemedPanel(g.box);
        renderer.drawUiTextCentered(uiUpper(panelSaveMode ? lang("UILABEL_SAVE_GAME") : lang("UILABEL_LOAD_GAME")), cx, g.box.y + g.pad, white);

        if (ask != Ask::None) {
            DialogGeometry d = questionGeometry(winW, winH);
            renderer.drawThemedPanel(d.box);
            if (ask == Ask::Overwrite) {
                std::string who = pendingOverwrite ? pendingOverwrite->name : std::string();
                renderer.drawUiTextCentered(langf("MISC_OVERWRITE_Q", { who }),
                                            d.box.x + d.box.w * 0.5f, d.box.y + 36.0f * s, white);
            } else if (ask == Ask::LoadUnsaved) {
                std::string who = pendingLoad ? pendingLoad->name : std::string();
                renderer.drawUiTextCentered(langf("MISC_LOAD_Q", { who }),
                                            d.box.x + d.box.w * 0.5f, d.box.y + 30.0f * s, white);
                renderer.drawUiTextCentered(lang("UILABEL_UNSAVED_WARNING"),
                                            d.box.x + d.box.w * 0.5f, d.box.y + 36.0f * s + 48.0f * s, white);
            } else {
                // Delete: the name is shown so it's clear which save goes
                const std::string who = pendingDelete ? pendingDelete->name : std::string();
                renderer.drawUiTextCentered(langf("MISC_DELETE_Q", { who }),
                                            d.box.x + d.box.w * 0.5f, d.box.y + 30.0f * s, white);
                renderer.drawUiTextCentered(lang("UILABEL_HOLD_DELETE"),
                                            d.box.x + d.box.w * 0.5f, d.box.y + 36.0f * s + 48.0f * s, white);
            }
            for (const Button& b : layoutPanel(winW, winH, panelSaveMode, ask, saves, listOffset,
                                               panelFrom == Screen::Title)) {
                if (b.action == Action::DeleteHold) {
                    const float fill = deleteHolding
                        ? std::min(1.0f, (float)(SDL_GetTicks() - deleteHoldStart) / (float)DELETE_HOLD_MS)
                        : 0.0f;
                    renderer.drawHoldButton(b.rect, b.label, fill);
                } else {
                    drawButtons({ b });
                }
            }
        } else {
            if (panelSaveMode) {
                // Name field with a blinking cursor, shown even while the name is empty
                // so it's clear the field has focus as soon as the popout opens
                renderer.drawRect(g.nameBox, RGBA{ 0.14f, 0.06f, 0.075f, 1.0f });
                float textY = g.nameBox.y + g.nameBox.h * 0.22f;
                bool cursorOn = (SDL_GetTicks() / 500) % 2 == 0;
                renderer.drawUiText(saveName + (cursorOn ? "|" : ""), g.nameBox.x + 12.0f, textY, white);
            } else {
                renderer.drawUiTextCentered(lang("UILABEL_CLICK_SAVE"),
                                            cx, g.nameBox.y + g.nameBox.h * 0.22f, gray);
            }

            // Column headers. The active sort column shows its direction.
            const float innerX = g.box.x + g.pad, innerW = g.box.w - 2.0f * g.pad;
            const float textInset = 12.0f * s;
            const float listH = renderer.listTextHeight("Ag");
            const float hy = g.headerY + (g.headerH - listH) * 0.5f;
            auto headerLabel = [&](const std::string& label, bool active) {
                if (!active) return label;
                return label + (sortAsc ? "  ^" : "  v");
            };
            renderer.drawListText(headerLabel(lang("UILABEL_NAME"), sortKey == SortKey::Name),
                                  innerX + textInset, hy, sortKey == SortKey::Name ? white : gray);
            renderer.drawListText(lang("UILABEL_TYPE"), innerX + COL_TYPE * innerW, hy, gray);
            renderer.drawListText(headerLabel(lang("UILABEL_TIME"), sortKey == SortKey::Time),
                                  innerX + COL_TIME * innerW, hy, sortKey == SortKey::Time ? white : gray);

            if (saves.empty()) {
                renderer.drawUiTextCentered(lang("UILABEL_NO_SAVES"), cx, g.listTop + g.nameBox.h * 0.22f, gray);
            }

            // "..." rows: how many saves are hidden above and below the visible rows
            const size_t total = saves.size();
            const size_t shownFirst = std::min(listOffset, total);
            const size_t shownLast = std::min(shownFirst + visibleSaveRows(winH), total);
            if (shownFirst > 0) {
                const bool hot = g.aboveBand.contains(mouseX, mouseY);
                renderer.drawListText("...   " + std::to_string(shownFirst) + " more above",
                                      innerX + textInset, g.aboveBand.y + (g.aboveBand.h - listH) * 0.5f,
                                      hot ? hover : hint);
            }
            if (shownLast < total) {
                const bool hot = g.belowBand.contains(mouseX, mouseY);
                renderer.drawListText("...   " + std::to_string(total - shownLast) + " more below",
                                      innerX + textInset, g.belowBand.y + (g.belowBand.h - listH) * 0.5f,
                                      hot ? hover : hint);
            }

            for (const Button& b : layoutPanel(winW, winH, panelSaveMode, Ask::None, saves, listOffset,
                                               panelFrom == Screen::Title)) {
                const bool isHover = b.rect.contains(mouseX, mouseY);
                if (b.action == Action::LoadSlot) {
                    // A save row: background, then left-aligned name and the type and time columns
                    const SaveSummary& sv = saves[b.param];
                    renderer.drawButton(b.rect, "", true, isHover);
                    const float ry = b.rect.y + (b.rect.h - listH) * 0.5f;
                    renderer.drawListText(sv.name, b.rect.x + textInset, ry, isHover ? hover : white);
                    renderer.drawListText(sv.autosave ? lang("UILABEL_AUTO") : lang("UILABEL_MANUAL"),
                                          b.rect.x + COL_TYPE * b.rect.w, ry,
                                          sv.autosave ? RGBA{ 1.0f, 0.62f, 0.62f, 1.0f } : white);
                    renderer.drawListText(timeString(sv.timestamp),
                                          b.rect.x + COL_TIME * b.rect.w, ry, isHover ? hover : white);
                } else if (b.action == Action::ScrollUp || b.action == Action::ScrollDown
                           || b.action == Action::SortName || b.action == Action::SortTime) {
                    // Drawn above as text: the "..." rows and the headers only need their click areas
                    continue;
                } else if (b.action == Action::DeleteSave) {
                    renderer.drawListButton(b.rect, lang("UILABEL_DELETE"), isHover);
                } else {
                    renderer.drawButton(b.rect, b.label, b.enabled, isHover);
                }
            }
        }
        renderer.endUi();
    }

    renderer.beginUi(winW, winH);
    // The Behavior tab's # prompt, over the whole game
    if (percentPrompt.open) {
        if (screen != Screen::Playing || !world.has<Behaviors>(percentPrompt.unit)) {
            answerPercentPrompt(false);   // the game it was asked in is gone
        } else {
            const char* title = percentPrompt.kind == BehaviorKind::Eat    ? "UILABEL_BEHAVIOR_PROMPT_EAT"
                                : percentPrompt.kind == BehaviorKind::Heal ? "UILABEL_BEHAVIOR_PROMPT_HEAL"
                                                                           : "UILABEL_BEHAVIOR_PROMPT_REST";
            const PercentRange range = percentRange(percentPrompt.kind);
            renderer.drawPercentPrompt(layoutPercentPrompt(winW, winH), lang(title), percentPrompt.text,
                                       langf("UILABEL_BEHAVIOR_PROMPT_RANGE", { std::to_string(range.lo), std::to_string(range.hi) }),
                                       percentPrompt.error, mouseX, mouseY);
        }
    }
    drawNotice();
    // The frame rate in the bottom-left corner, white on a black box, over everything else
    ++fpsFrames;
    if (const Uint64 at = SDL_GetTicks(); at - fpsSince >= 500) {
        fpsText = std::to_string((int)std::lround(fpsFrames * 1000.0 / (double)(at - fpsSince))) + " FPS";
        fpsSince = at;
        fpsFrames = 0;
    }
    if (settings.showFps) {
        const float pad = 4.0f, tw = renderer.uiTextWidth(fpsText), th = renderer.uiTextHeight(fpsText);
        const float bx = 6.0f, by = (float)winH - th - 2.0f * pad - 6.0f;
        renderer.drawRect(Rect{ bx, by, tw + 2.0f * pad, th + 2.0f * pad }, RGBA{ 0.0f, 0.0f, 0.0f, 0.85f });
        renderer.drawUiText(fpsText, bx + pad, by + pad, RGBA{ 1.0f, 1.0f, 1.0f, 1.0f });
    }
    renderer.endUi();

    // The frame is complete but not yet shown, so it is the one saved
    if (screenshotPending) {
        screenshotPending = false;
        const std::string saved = saveScreenshot(window);
        showNotice(saved.empty() ? std::string(lang("MISC_SCREENSHOT_FAILED")) : langf("MISC_SCREENSHOT_SAVED", { saved }));
    }
}
