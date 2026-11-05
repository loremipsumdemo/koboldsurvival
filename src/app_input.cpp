#include "app.h"

using namespace play;

void App::handleEvent() {
    // F12 takes a screenshot from any screen, whatever menu or text box has the keys
    if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_F12 && !e.key.repeat) {
        screenshotPending = true;
        return;
    }
    switch (e.type) {
    case SDL_EVENT_QUIT:
        running = false;
        break;
    case SDL_EVENT_TEXT_INPUT:
        onTextInput();
        break;
    case SDL_EVENT_TEXT_EDITING:
        // An input method is composing text (for example Chinese or Japanese): it shows in the name box
        if (screen == Screen::PartySetup && partyUi.editingName) partyUi.composition = e.edit.text;
        break;
    case SDL_EVENT_KEY_DOWN:
        onKeyDown();
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        onMouseWheel();
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        onMouseButtonDown();
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        onMouseButtonUp();
        break;
    case SDL_EVENT_MOUSE_MOTION:
        onMouseMotion();
        break;
    }
}

// The typed text: into the # prompt, a party member's name, or the save name
void App::onTextInput() {
    // The # prompt takes digits only, as many as the top of the behavior's range has
    if (percentPrompt.open) {
        const size_t most = std::to_string(percentRange(percentPrompt.kind).hi).size();
        for (const char* c = e.text.text; *c; ++c) {
            if (*c < '0' || *c > '9') continue;
            if (percentPrompt.text == "0") percentPrompt.text.clear();   // no leading zero
            if (percentPrompt.text.size() < most) percentPrompt.text += *c;
        }
        percentPrompt.error.clear();
        return;
    }
    if (screen == Screen::PartySetup) {
        // Typed text goes into the name being edited, up to the limit in characters. Any text an input
        // method was composing is now committed, so it leaves the name box.
        partyUi.composition.clear();
        if (partyUi.editingName) appendUtf8Limited(pickedMember(party, partyUi).name, e.text.text, PARTY_NAME_CHARS);
        return;
    }
    if (panelOpen && panelSaveMode && ask == Ask::None && saveName.size() < NAME_LIMIT) {
        saveName += e.text.text;
    }
}

// A key pressed
void App::onKeyDown() {
    // Escape acts once per press: the repeats sent while it is held down are ignored, so holding it does
    // not back out of several menus at once
    if (e.key.key == SDLK_ESCAPE && e.key.repeat) return;
    // The # prompt takes every key: Enter sets the percent, Escape cancels, Backspace takes off a digit.
    // The game's keys (Space, M, the camera's) wait until it is answered.
    if (percentPrompt.open) {
        if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) {
            answerPercentPrompt(true);
        } else if (e.key.key == SDLK_ESCAPE) {
            answerPercentPrompt(false);
        } else if (e.key.key == SDLK_BACKSPACE && !percentPrompt.text.empty()) {
            percentPrompt.text.pop_back();
            percentPrompt.error.clear();
        }
        return;
    }
    if (screen == Screen::PartySetup) {
        // Escape backs out one step at a time: the dropdown, then the name box, then the map list
        if (e.key.key == SDLK_ESCAPE) {
            if (partyUi.dropKind != DropKind::None) {
                closeDropdown();
            } else if (partyUi.editingName) {
                stopNameEdit();
            } else {
                screen = backScreen;
            }
        } else if (partyUi.editingName) {
            std::string& name = pickedMember(party, partyUi).name;
            if (e.key.key == SDLK_BACKSPACE) {
                popUtf8Char(name);
            } else if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) {
                stopNameEdit();
            }
        }
        return;
    }
    if (panelOpen) {
        if (e.key.key == SDLK_ESCAPE) {
            // Escape backs out of a question first, then closes the popout
            if (ask != Ask::None) {
                ask = Ask::None;
                pendingOverwrite.reset();
                pendingLoad.reset();
                pendingDelete.reset();
                deleteHolding = false;
            } else {
                closePanel();
            }
        } else if (panelSaveMode && ask == Ask::None) {
            if (e.key.key == SDLK_BACKSPACE) {
                popUtf8Char(saveName);
            } else if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) {
                performAction(Action::ConfirmName, 0);
            }
        }
        return;
    }
    // The title and pause menus: up and down (or W and S) light the next item, Enter (or Space on the title)
    // picks it. Items that can't be used are passed over.
    if (screen == Screen::Title || screen == Screen::Pause) {
        const std::vector<Button> items = layoutButtons(screen, winW, winH, saves);
        const SDL_Keycode k = e.key.key;
        const bool up = k == SDLK_UP || k == SDLK_W, down = k == SDLK_DOWN || k == SDLK_S;
        if ((up || down) && !items.empty()) {
            const int n = (int)items.size();
            int i = menuSel;
            for (int tries = 0; tries < n; ++tries) {
                i = ((i < 0 ? (down ? -1 : 0) : i) + (down ? 1 : -1) + n) % n;
                if (items[(size_t)i].enabled) break;
            }
            if (items[(size_t)i].enabled && i != menuSel) menuSel = i;   // the hover sound plays as it lights
            return;
        }
        const bool pick = k == SDLK_RETURN || k == SDLK_KP_ENTER || (k == SDLK_SPACE && screen == Screen::Title);
        if (pick && !e.key.repeat && menuSel >= 0 && menuSel < (int)items.size() && items[(size_t)menuSel].enabled) {
            performAction(items[(size_t)menuSel].action, items[(size_t)menuSel].param);
            return;
        }
    }
    // While a prop is picked to build, Escape puts the pick down, and does nothing else
    if (screen == Screen::Playing && buildPick.any() && e.key.key == SDLK_ESCAPE) {
        buildPick = {};
        playSfx(Sfx::UiBack);
        return;
    }
    if (screen == Screen::Playing && !e.key.repeat && e.key.key == SDLK_M) {
        mapOpen = !mapOpen;   // M opens and closes the world map
        return;
    }
    // Space pauses and resumes the game, but not while a talk or a trade is open
    if (screen == Screen::Playing && e.key.key == SDLK_SPACE && !e.key.repeat && dialogueUnit == NULL_ENTITY) {
        paused = !paused;
        playSfx(paused ? Sfx::UiPause : Sfx::UiResume);
    }
    if (e.key.key == SDLK_ESCAPE) {
        // In a talk, Escape backs out a step at a time, as the buttons do: a "how many" prompt is closed,
        // the trade is Cancelled (back to the talk, or ended if it was a trade between your own units),
        // and the talk itself is ended as with End Dialogue
        if (screen == Screen::Playing && dialogueUnit != NULL_ENTITY) {
            if (quantity.open && tradeOpen && !quantity.toStorage) {
                quantity.open = false;
            } else if (tradeOpen && freeTrade) {
                endTalk();
            } else if (tradeOpen) {
                tradeOpen = false;
                tradeNpc.clear();
                tradePlayer.clear();
                tradeNpcDrag = -1;
            } else {
                endTalk();
            }
            playSfx(Sfx::UiBack);
            return;
        }
        if (screen == Screen::Playing && disbandTarget != NULL_ENTITY) {
            disbandTarget = NULL_ENTITY;   // Escape is Cancel on the Disband popout
            disbandHeldAt = 0;
            playSfx(Sfx::UiBack);
        } else if (screen == Screen::Playing && optimizeAllOpen) {
            optimizeAllOpen = false;   // Escape is Cancel on the Optimize All dialog
            optimizeSliderDrag = false;
            playSfx(Sfx::UiBack);
        } else if (screen == Screen::Playing && mapOpen) {
            mapOpen = false;  // Escape closes the world map first
        } else if (screen == Screen::Playing && itemMenu.open) {
            // Escape closes the open menu or popout first, and only then pauses the game
            itemMenu.open = false;
        } else if (screen == Screen::Playing) {
            leftDown = middleDown = false;
            sel.dragging = false;
            itemDrag = false;
            itemMenu.open = false;
            screen = Screen::Pause;
            refreshSaves();
            playSfx(Sfx::UiPause);
        } else if (screen == Screen::Pause) {
            screen = Screen::Playing;
            playSfx(Sfx::UiResume);
        } else if (screen == Screen::Settings) {
            // Escape closes an open dropdown first, then leaves the menu
            if (settingsUi.openRow >= 0) settingsCloseDropdown(settingsUi);
            else leaveSettings();
        } else if (screen == Screen::MapSelect || screen == Screen::ScenarioSelect) {
            performAction(Action::ReturnToTitle, 0);
        }
    }
}

// The wheel: the lists, the game log, the world map's zoom, the squad panel, the sidebar or the camera
void App::onMouseWheel() {
    // The world map popout takes the wheel over everything under it
    const bool wheelOverMap = mapOpen && screen == Screen::Playing
                              && worldMapFor(winW, winH).window.contains(e.wheel.mouse_x, e.wheel.mouse_y);
    if (panelOpen) {
        // Scroll the save list, keeping the offset within range
        int maxOffset = std::max(0, (int)saves.size() - (int)visibleSaveRows(winH));
        int next = (int)listOffset - (int)e.wheel.y;
        listOffset = (size_t)std::clamp(next, 0, maxOffset);
    } else if (screen == Screen::MapSelect) {
        int maxOffset = std::max(0, (int)maps.size() - (int)visibleSaveRows(winH));
        int next = (int)mapOffset - (int)e.wheel.y;
        mapOffset = (size_t)std::clamp(next, 0, maxOffset);
    } else if (screen == Screen::ScenarioSelect) {
        // The scenario list scrolls a row at a time; the layout keeps it in range
        scenarioScroll = layoutScenario(winW, winH, (int)ScenarioId::Count,
                                        scenarioScroll - (int)e.wheel.y).first;
    } else if (screen == Screen::PartySetup) {
        // Scrolls an open dropdown. The rest of the menu does not scroll.
        if (partyUi.dropKind != DropKind::None) {
            const int count = (int)dropdownEntries(party, partyUi).size();
            partyUi.dropScroll = clampDropScroll(partyUi.dropScroll - (int)e.wheel.y, count);
        } else if (partyPanels(winW, winH).left.contains(e.wheel.mouse_x, e.wheel.mouse_y)) {
            // The unit list scrolls, a unit at a time
            const int maxScroll = partyListMaxScroll((int)party.size(), winH);
            partyUi.listScroll = std::clamp(std::min(partyUi.listScroll, maxScroll) - (int)e.wheel.y, 0, maxScroll);
        }
    } else if (screen == Screen::Settings) {
        // Scrolls an open dropdown. Nothing else on the menu scrolls.
        if (settingsUi.openRow >= 0) {
            const std::vector<RowSpec> rows = settingsRows(settingsUi.tab, settings, displayInfo);
            settingsScrollDropdown(settingsUi, layoutSettings(winW, winH, rows, settingsUi), (int)e.wheel.y);
        }
    } else if (screen == Screen::Playing && !wheelOverMap && gameLogHovered(winW, winH)) {
        // Scrolls the game log, three lines a notch. Wheel up goes back in time.
        const int step = e.wheel.y > 0.0f ? 3 : e.wheel.y < 0.0f ? -3 : 0;
        logOffset = std::clamp(logOffset + step, 0, gameLogPanel(winW, winH).maxOffset());
    } else if (wheelOverMap) {
        // Zooms the world map about the point under the mouse: wheel up zooms in, down zooms out
        const Rect box = worldMapFor(winW, winH).map;
        const float px = std::clamp(e.wheel.mouse_x, box.x, box.x + box.w);
        const float py = std::clamp(e.wheel.mouse_y, box.y, box.y + box.h);
        const float oldK = mapTilesPerPixel(box, mapSpan);
        const float offX = box.x + box.w * 0.5f - px, offY = box.y + box.h * 0.5f - py;
        const float gx = mapCx + offX * oldK;   // the ground under the mouse
        const float gz = mapCz + offY * oldK;
        mapSpan = e.wheel.y > 0.0f ? mapSpan * 0.8f : mapSpan * 1.25f;
        float cx = mapCx, cz = mapCz;
        clampMapView(box, (float)terrain.size(), cx, cz, mapSpan);
        const float k = mapTilesPerPixel(box, mapSpan);
        mapCx = gx - offX * k;
        mapCz = gz - offY * k;
        clampMapView(box, (float)terrain.size(), mapCx, mapCz, mapSpan);
    } else if (screen == Screen::Playing && unitPanelHovered(winW, winH) && factionTab == FACTION_TAB_REPUTATION) {
        // The Reputation tab's list scrolls a faction a notch
        const int step = e.wheel.y > 0.0f ? -1 : e.wheel.y < 0.0f ? 1 : 0;
        repScroll = layoutUnitPanel(winW, winH, (int)ownUnitList().size(), unitPanelRow, squadCollapsed, squadPlace,
                                    factionTab, (int)reputationFactions().size(), repScroll + step).repFirst;
    } else if (screen == Screen::Playing && unitPanelHovered(winW, winH)) {
        // Scrolls the unit list very slowly: a row for every SQUAD_WHEEL_NOTCHES_PER_ROW notches of the
        // wheel. Turning the wheel the other way starts the count again.
        if (unitPanelWheel * e.wheel.y < 0.0f) unitPanelWheel = 0.0f;
        unitPanelWheel += e.wheel.y;
        const int rows = (int)(unitPanelWheel / SQUAD_WHEEL_NOTCHES_PER_ROW);
        if (rows != 0) {
            unitPanelWheel -= rows * SQUAD_WHEEL_NOTCHES_PER_ROW;
            unitPanelRow = layoutUnitPanel(winW, winH, (int)ownUnitList().size(),
                                           unitPanelRow - rows, squadCollapsed, squadPlace, factionTab, (int)reputationFactions().size(), repScroll).scrollRow;
        }
    } else if (screen == Screen::Playing && !onTalkOrTrade(winW, winH, e.wheel.mouse_x, e.wheel.mouse_y)
               && sidebarFor(winW, winH, sidebarScroll).panel.contains(e.wheel.mouse_x, e.wheel.mouse_y)) {
        // Scrolls the sidebar, 40 pixels a notch. Wheel up goes back toward the top.
        const float notch = 40.0f * (winH / 720.0f);
        sidebarScroll = sidebarFor(winW, winH, sidebarScroll - e.wheel.y * notch).scroll;
    } else if (screen == Screen::Playing && !pointerOverPanels(winW, winH)) {
        // Over the panels the wheel does nothing, so the camera doesn't zoom behind them
        cam.distance *= (e.wheel.y > 0.0f) ? 0.9f : 1.1f;
        cam.clampState();
    }
}

// A mouse button pressed: the menu screens here, then play's popups, its panels, and the world
void App::onMouseButtonDown() {
    // The # prompt takes every click: Set and Cancel answer it, a click outside it cancels it
    if (percentPrompt.open) {
        if (e.button.button == SDL_BUTTON_LEFT) {
            const PercentPromptLayout pl = layoutPercentPrompt(winW, winH);
            if (pl.ok.contains(e.button.x, e.button.y)) answerPercentPrompt(true);
            else if (pl.cancel.contains(e.button.x, e.button.y) || !pl.box.contains(e.button.x, e.button.y)) answerPercentPrompt(false);
        }
        return;
    }
    // While a prop is picked to build, a right click puts the pick down and does nothing else
    if (buildPick.any() && screen == Screen::Playing && e.button.button == SDL_BUTTON_RIGHT) {
        buildPick = {};
        playSfx(Sfx::UiBack);
        return;
    }
    if (panelOpen) {
        // The popout takes all clicks while it's open
        if (e.button.button == SDL_BUTTON_LEFT) {
            for (const Button& b : layoutPanel(winW, winH, panelSaveMode, ask, saves, listOffset,
                                               panelFrom == Screen::Title)) {
                if (b.enabled && b.rect.contains(e.button.x, e.button.y)) {
                    if (b.action == Action::DeleteHold) {
                        // Held, not clicked: the timer deletes once it has been held long enough
                        deleteHolding = true;
                        deleteHoldStart = SDL_GetTicks();
                        deleteHoldRect = b.rect;
                    } else {
                        performAction(b.action, b.param);
                    }
                    break;
                }
            }
        }
        return;
    }

    if (screen == Screen::ScenarioSelect) {
        // A row picks its scenario; Continue rolls the party for it; Back returns to the title
        if (e.button.button == SDL_BUTTON_LEFT) {
            const float mx = e.button.x, my = e.button.y;
            const ScenarioLayout sl = layoutScenario(winW, winH, (int)ScenarioId::Count, scenarioScroll);
            scenarioScroll = sl.first;
            for (size_t i = 0; i < sl.rows.size(); ++i) {
                if (sl.rows[i].contains(mx, my)) {
                    scenarioPick = (ScenarioId)(sl.first + (int)i);
                    playSfx(Sfx::UiClick);
                }
            }
            if (sl.next.contains(mx, my)) {
                playSfx(Sfx::UiClick);
                openPartyMenu();
            } else if (sl.back.contains(mx, my)) {
                performAction(Action::ReturnToTitle, 0);
            }
        }
        return;
    }

    if (screen == Screen::PartySetup) {
        // The unit list's scroll bar: the thumb is taken where it was clicked; a click on the track moves
        // the thumb's middle there, and it can be dragged on from that point
        const PartyListBar bar = partyListBar(winW, winH, (int)party.size(), partyUi.listScroll);
        const Rect barGrab{ bar.track.x - 4.0f, bar.track.y, bar.track.w + 8.0f, bar.track.h };
        if (e.button.button == SDL_BUTTON_LEFT && partyUi.dropKind == DropKind::None && bar.track.h > 0.0f
            && barGrab.contains(e.button.x, e.button.y)) {
            partyUi.listGrab = bar.thumb.contains(bar.thumb.x, e.button.y) ? e.button.y - bar.thumb.y : bar.thumb.h * 0.5f;
            partyUi.listScroll = partyListScrollAt(winW, winH, (int)party.size(), e.button.y - partyUi.listGrab);
            partyUi.listDrag = true;
            return;
        }
        // The right button held on the picked unit's model turns it about its vertical axis
        if (e.button.button == SDL_BUTTON_RIGHT && partyUi.dropKind == DropKind::None && !party.empty()
            && partyPreviewRect(winW, winH).contains(e.button.x, e.button.y)) {
            partyUi.previewDrag = true;
            return;
        }
        if (e.button.button == SDL_BUTTON_LEFT) {
            const float mx = e.button.x, my = e.button.y;
            const std::vector<DropEntry> drop = dropdownEntries(party, partyUi);
            const std::vector<Button> buttons = layoutPartySetup(winW, winH, party, partyUi, drop);
            // The last enabled button under the pointer is the one on top, so it gets the click
            const Button* hit = nullptr;
            for (auto it = buttons.rbegin(); it != buttons.rend(); ++it) {
                if (it->enabled && it->rect.contains(mx, my)) {
                    hit = &*it;
                    break;
                }
            }
            const bool onDropdown = hit && (hit->action == Action::PartyDropPick
                                            || hit->action == Action::PartyDropScroll
                                            || hit->action == Action::PartyDropBack);
            if (partyUi.dropKind != DropKind::None && !onDropdown) {
                // A click outside an open dropdown only closes it
                closeDropdown();
            } else if (hit) {
                if (partyUi.editingName && hit->action != Action::PartyNameField) stopNameEdit();
                performAction(hit->action, hit->param);
            } else {
                stopNameEdit();
            }
        }
        return;
    }

    if (screen == Screen::Settings) {
        if (e.button.button == SDL_BUTTON_LEFT) settingsClick(e.button.x, e.button.y, winW, winH);
        return;
    }

    if (screen != Screen::Playing) {
        // Menus: a left click runs the enabled button under the cursor
        if (e.button.button == SDL_BUTTON_LEFT) {
            const std::vector<Button> menuButtons = screen == Screen::MapSelect
                ? layoutMapPanel(winW, winH, maps, mapOffset)
                : layoutButtons(screen, winW, winH, saves);
            for (const Button& b : menuButtons) {
                if (b.enabled && b.rect.contains(e.button.x, e.button.y)) {
                    performAction(b.action, b.param);
                    break;
                }
            }
        }
        return;
    }

if (mouseDownOnPopups()) return;
if (mouseDownOnPanels()) return;

    if (e.button.button == SDL_BUTTON_LEFT) {
        leftDown = true;
        pressX = e.button.x;
        pressY = e.button.y;
        sel.dragging = false;
    } else if (e.button.button == SDL_BUTTON_MIDDLE) {
        // Pressing alone changes nothing. Dragging orbits around the current focus.
        // Pressed over the panels, it orbits nothing.
        if (!overPanels(e.button.x, e.button.y, winW, winH)) {
            middleDown = true;
            lastX = e.button.x;
            lastY = e.button.y;
        }
    } else if (e.button.button == SDL_BUTTON_RIGHT) {
        float mgx = 0.0f, mgz = 0.0f;
        if (screen == Screen::Playing && mapGroundAt(winW, winH, e.button.x, e.button.y, mgx, mgz)) {
            // The minimap: the selected units walk to the point clicked on it
            beginRightHold(mgx, mgz);
        } else if (overPanels(e.button.x, e.button.y, winW, winH)) {
            // Right-click on a unit in the Squad Panel: its menu, as on the unit in the world
            {
                const std::vector<Entity> own = ownUnitList();
                const UnitPanelLayout squadL = layoutUnitPanel(winW, winH, (int)own.size(), unitPanelRow, squadCollapsed, squadPlace, factionTab, (int)reputationFactions().size(), repScroll);
                bool onCell = false;
                for (const UnitCell& c : squadL.cells) {
                    if (!c.rect.contains(e.button.x, e.button.y) || c.index < 0 || c.index >= (int)own.size()) continue;
                    onCell = true;
                    openContextMenuOn(own[(size_t)c.index], e.button.x, e.button.y, true);
                    break;
                }
                if (onCell) return;
            }
            // Right-click on an inventory item: gear goes straight to its slot, and anything
            // else opens the menu with Use and Drop. Right-click on a worn gear item takes it off
            // into the inventory. Elsewhere on the panels, nothing happens. A red unit's sidebar
            // has no unit of yours in it, so neither of those applies there.
            itemMenu.open = false;
            const std::vector<Entity> picked = panelUnits(world);
            const SidebarLayout sl = sidebarFor(winW, winH, sidebarScroll);
            const int slot = sidebarUp() ? sidebarSlotAt(sl, e.button.x, e.button.y) : -1;
            const int gear = sidebarUp() ? gearSlotAt(sl, e.button.x, e.button.y) : -1;
            const Inventory* inv = picked.size() == 1 ? world.get<Inventory>(picked[0]) : nullptr;
            const Equipment* wornHere = picked.size() == 1 ? world.get<Equipment>(picked[0]) : nullptr;
            if (wornHere && gear >= 0 && wornHere->items[gear] != 0) {
                std::string why;
                if (unequipToInventory(world, picked[0], gear, why)) queueSound(Sfx::ItemMove);
                else if (!why.empty()) refuse(why);
            } else if (inv && slot >= 0 && inv->slots[slot].item != 0 && tradeOpen) {
                // During a trade a right-click puts the item in the trade, whatever it is
                requestTrade(false, picked[0], slot);
            } else if (inv && slot >= 0 && inv->slots[slot].item != 0 && storageProp != NULL_ENTITY &&
                       picked[0] == storageUnit) {
                // With a storage panel open, a right-click puts the item in the storage. A stack asks how many.
                requestStore(slot);
            } else if (inv && slot >= 0 && inv->slots[slot].item != 0) {
                const int item = inv->slots[slot].item;
                const ItemDef* def = findItem(item);
                if (def && !def->equipSlot.empty()) {
                    const Equipment* worn = world.get<Equipment>(picked[0]);
                    const int target = worn ? gearSlotFor(*worn, item) : -1;
                    std::string why;
                    if (target < 0 || !equipFromInventory(world, picked[0], slot, target, why)) {
                        refuse(why.empty() ? langf("MISC_CANT_BE_WORN", { itemDisplayName(*def) }) : why);
                    } else {
                        queueSound(Sfx::ItemMove);
                    }
                } else {
                    itemMenu = ItemMenu{ true, picked[0], slot, e.button.x, e.button.y };
                    playSfx(Sfx::UiMenuOpen);
                }
            }
        } else {
            // A unit or a usable prop under the pointer opens the right-click menu: Talk, Trade, Follow,
            // Defend, Attack, Inventory or Use, as fits it. Anywhere else the selected units walk there.
            if (openContextMenu(e.button.x, e.button.y)) {
                rightHold = false;
            } else {
                float gx = 0.0f, gz = 0.0f;
                if (rightClickOrder(world, terrain, cam, e.button.x, e.button.y, winW, winH, gx, gz)) beginRightHold(gx, gz);
            }
        }
    }
}

// In play, a press on a modal popup, the world map, the talk or trade, the right-click menu, the loot or
// storage panel, or a panel's title bar, lock, tabs, scroll bar or edge. True if the press was used.
bool App::mouseDownOnPopups() {
    // The Disband popout is modal: only it answers while it is open. Pressing Disband starts the hold;
    // letting go (or moving off it) before DISBAND_HOLD_MS starts it over.
    if (screen == Screen::Playing && disbandTarget != NULL_ENTITY) {
        if (e.button.button == SDL_BUTTON_LEFT) {
            const DisbandLayout dl = layoutDisband(winW, winH);
            if (dl.disband.contains(e.button.x, e.button.y)) {
                disbandHeldAt = std::max<unsigned long long>(1, SDL_GetTicks());
                playSfx(Sfx::UiClick);
            } else if (dl.cancel.contains(e.button.x, e.button.y)) {
                disbandTarget = NULL_ENTITY;
                disbandHeldAt = 0;
                playSfx(Sfx::UiBack);
            }
        }
        return true;
    }
    // The Optimize All dialog is modal: only it answers while it is open
    if (screen == Screen::Playing && optimizeAllOpen) {
        if (e.button.button == SDL_BUTTON_LEFT) {
            const OptimizeAllLayout ol = layoutOptimizeAll(winW, winH);
            const float x = e.button.x, y = e.button.y;
            const Rect grab{ ol.track.x - 8.0f, ol.track.y - 10.0f, ol.track.w + 16.0f, ol.track.h + 20.0f };
            if (grab.contains(x, y)) {
                settings.optimizeAttackShare = levelAtX(ol.track, x);
                optimizeSliderDrag = true;
            } else if (ol.toggles[0].contains(x, y)) {
                settings.optimizeAccessories = !settings.optimizeAccessories;
                saveSettingsFile();
                playSfx(Sfx::UiToggle);
            } else if (ol.toggles[1].contains(x, y)) {
                settings.optimizeTwoHanded = !settings.optimizeTwoHanded;
                saveSettingsFile();
                playSfx(Sfx::UiToggle);
            } else if (ol.toggles[2].contains(x, y)) {
                settings.optimizeValueTies = !settings.optimizeValueTies;
                saveSettingsFile();
                playSfx(Sfx::UiToggle);
            } else if (ol.cancel.contains(x, y)) {
                optimizeAllOpen = false;
                playSfx(Sfx::UiBack);
            } else if (ol.confirm.contains(x, y)) {
                // Each unit of the squad, on its own: one without the room to change stays as it was
                const std::vector<Entity> squad = ownUnitList();
                const OptimizeAllResult r = optimizeAllGear(world, squad, gearPrefs());
                optimizeAllOpen = false;
                if (r.changed == 0 && r.noRoom == 0) {
                    showNotice(lang("MISC_OPTIMIZE_ALL_SAME"));
                } else if (r.noRoom == 0) {
                    showNotice(langf("MISC_OPTIMIZE_ALL_DONE", { std::to_string(r.changed) }));
                } else {
                    showNotice(langf("MISC_OPTIMIZE_ALL_SOME_ROOM", { std::to_string(r.changed), std::to_string(r.noRoom) }));
                }
                if (r.changed == 0) playSfx(r.noRoom > 0 ? Sfx::UiError : Sfx::UiClick);   // a change plays its own sound
            }
        }
        return true;
    }
    // The world map window is over everything, and a click inside it is its own: the X closes it, and a right
    // click on the map is a move order to that point. Outside the window, clicks go on as if it were shut.
    // The talk box and the trade are drawn over the map, so a click on them is theirs
    if (screen == Screen::Playing && mapOpen && !onTalkOrTrade(winW, winH, e.button.x, e.button.y) &&
        worldMapFor(winW, winH).window.contains(e.button.x, e.button.y)) {
        const WorldMapLayout wm = worldMapFor(winW, winH);
        float mgx = 0.0f, mgz = 0.0f;
        if (e.button.button == SDL_BUTTON_LEFT && wm.close.contains(e.button.x, e.button.y)) {
            mapOpen = false;
            playSfx(Sfx::UiClick);
        } else if (e.button.button == SDL_BUTTON_LEFT && wm.bar.contains(e.button.x, e.button.y)) {
            // The title bar moves the window while it is held
            const float dx = wm.window.x - std::floor((winW - wm.window.w) * 0.5f);
            const float dy = wm.window.y - std::floor((winH - wm.window.h) * 0.5f);
            mapDragX = dx;
            mapDragY = dy;
            mapDragging = true;
            mapGrabX = e.button.x - dx;
            mapGrabY = e.button.y - dy;
        } else if (e.button.button == SDL_BUTTON_RIGHT && mapGroundAt(winW, winH, e.button.x, e.button.y, mgx, mgz)) {
            contextMenu.open = false;
            itemMenu.open = false;
            beginRightHold(mgx, mgz);
        }
        return true;
    }

    // The talk owns the mouse while it is open. During a trade the Unit Panel still works, so a click there
    // passes on to the panel; everything else is the talk's or the trade's.
    if (dialogueUnit != NULL_ENTITY && screen == Screen::Playing) {
        // The quantity prompt is modal over the trade: only its buttons answer
        if (quantity.open && tradeOpen && !quantity.toStorage) {
            if (e.button.button == SDL_BUTTON_LEFT) quantityClick(e.button.x, e.button.y);
            return true;
        }
        const float x = e.button.x, y = e.button.y;
        // The trade's two panels are drawn over the Unit Panel, so a click on them is theirs even where the
        // Unit Panel is under it; only a click on the Unit Panel's uncovered part goes to the Unit Panel
        const TradeLayout tradeL = layoutTrade(winW, winH, renderer.pauseBarHeight(false));
        const bool onTrade = tradeOpen && (tradeL.interaction.contains(x, y) || tradeL.tradePanel.contains(x, y));
        const bool unitPanelClick = tradeOpen && !onTrade && sidebarFor(winW, winH, sidebarScroll).panel.contains(x, y);
        if (!unitPanelClick) {
            if (tradeOpen) {
                const TradeLayout tl = layoutTrade(winW, winH, renderer.pauseBarHeight(false));
                const Inventory* npcInv = world.get<Inventory>(dialogueUnit);
                const bool leftButton = e.button.button == SDL_BUTTON_LEFT;
                if (leftButton && tl.cancelBtn.contains(x, y) && freeTrade) {
                    endTalk();   // a free trade has no talk to go back to
                } else if (leftButton && tl.cancelBtn.contains(x, y)) {
                    tradeOpen = false;
                    tradeNpc.clear();
                    tradePlayer.clear();
                    tradeNpcDrag = -1;
                } else if (leftButton && tl.tradeBtn.contains(x, y)) {
                    if (!tradeItemsPresent(world, dialogueUnit, dialoguePlayer)) {
                        // Something offered is no longer in its inventory: the trade fails and the talk ends
                        tradeLabels.push_back(TradeLabel{ dialogueUnit, lang("MISC_TRADE_FAILED"), 0.0f });
                        tradeLabels.push_back(TradeLabel{ dialoguePlayer, lang("MISC_TRADE_FAILED"), 0.0f });
                        playSfx(Sfx::TradeFailed);
                        endTalk();
                    } else if (checkTrade(world, dialogueUnit, dialoguePlayer).ok) {
                        // Each side shows its own gain or loss: what it receives minus what it gives
                        const int theirGive = tradeValueOf(tradeNpc);
                        const int yourGive = tradeValueOf(tradePlayer);
                        auto signedValue = [](int v) { return (v > 0 ? "+" : "") + std::to_string(v); };
                        if (!freeTrade) {   // a free trade between your own units has no value to show
                            tradeLabels.push_back(TradeLabel{ dialogueUnit, langf("MISC_TRADE_CHANGE", { signedValue(yourGive - theirGive) }), 0.0f });
                            tradeLabels.push_back(TradeLabel{ dialoguePlayer, langf("MISC_TRADE_CHANGE", { signedValue(theirGive - yourGive) }), 0.0f });
                        }
                        completeTrade(world, dialogueUnit, dialoguePlayer);
                        playSfx(Sfx::TradeDone);
                        endTalk();
                        showNotice(lang("MISC_TRADE_COMPLETE"), TRADE_LABEL_LIFE);
                    }
                } else if (takeTradeGhostAt(tradeNpc, tl.theirCells, x, y)
                           || takeTradeGhostAt(tradePlayer, tl.yourCells, x, y)) {
                    // A ghost was taken back out of its cell
                } else {
                    // An NPC item: left button drags it to the left box, right button puts it there
                    for (size_t i = 0; npcInv && i < tl.npcSlots.size(); ++i) {
                        if (!tl.npcSlots[i].contains(x, y) || npcInv->slots[i].item == 0) continue;
                        if (leftButton) {
                            tradeNpcDrag = (int)i;
                        } else if (e.button.button == SDL_BUTTON_RIGHT) {
                            requestTrade(true, dialogueUnit, (int)i);
                        }
                        break;
                    }
                }
            } else if (e.button.button == SDL_BUTTON_LEFT) {
                const DialogueLayout dl = dialogueBox(winW, winH);
                if (dl.end.contains(x, y)) {
                    talkCooldown = TALK_COOLDOWN;
                    dialogueUnit = NULL_ENTITY;
                    dialogueConvo.clear();
                } else {
                    for (size_t i = 0; i < dl.options.size(); ++i) {
                        if (dl.options[i].contains(x, y)) {
                            chooseOption(i);
                            break;
                        }
                    }
                }
            }
            return true;
        }
    }
    // The right-click menu: a left click on an option picks it. A click anywhere else closes the menu, and
    // that click still does what it would have done (a right-click elsewhere opens a new menu or moves).
    if (screen == Screen::Playing && contextMenu.open) {
        const ItemMenuLayout ml = contextMenuLayout();
        if (ml.box.contains(e.button.x, e.button.y)) {
            const int hit = itemMenuButtonAt(ml, e.button.x, e.button.y);
            if (e.button.button == SDL_BUTTON_LEFT && hit >= 0 && hit < (int)contextMenu.actions.size()) {
                pickMenuAction(contextMenu.actions[(size_t)hit]);
            }
            return true;
        }
        contextMenu.open = false;
    }
    // Choosing how many of a stack to store: the prompt is modal, and only its buttons answer
    if (screen == Screen::Playing && quantity.open && quantity.toStorage) {
        if (e.button.button == SDL_BUTTON_LEFT) quantityClick(e.button.x, e.button.y);
        return true;
    }
    // The looting panel takes every click on it: a click on an item takes it (as much as fits), Take All takes
    // everything that fits, Close closes it
    if (screen == Screen::Playing && lootUnit != NULL_ENTITY) {
        const LootLayout ll = layoutLoot(winW, winH);
        const float x = e.button.x, y = e.button.y;
        if (ll.panel.contains(x, y)) {
            if (e.button.button == SDL_BUTTON_LEFT && ll.closeBtn.contains(x, y)) {
                closeLoot();
                playSfx(Sfx::UiBack);
            } else if (e.button.button == SDL_BUTTON_LEFT && ll.takeAllBtn.contains(x, y)) {
                if (lootAll(world, lootBy, lootUnit) == 0) refuse(lang("MISC_LOOT_NOTHING"));
            } else {
                std::string why;
                bool hitCell = false;
                for (int g = 0; g < GEAR_SLOTS && !hitCell; ++g) {
                    if (!ll.gear[(size_t)g].contains(x, y)) continue;
                    hitCell = true;
                    if (!lootTake(world, lootBy, lootUnit, true, g, why) && !why.empty()) refuse(why);
                }
                for (int i = 0; i < INVENTORY_SLOTS && !hitCell; ++i) {
                    if (!ll.slots[(size_t)i].contains(x, y)) continue;
                    hitCell = true;
                    if (!lootTake(world, lootBy, lootUnit, false, i, why) && !why.empty()) refuse(why);
                }
            }
            return true;
        }
    }
    // The storage panel takes every click on it. Left button on an item starts dragging it out; right button
    // moves its whole stack into the unit's inventory, if all of it fits.
    if (screen == Screen::Playing && storageProp != NULL_ENTITY) {
        const StorageLayout stl = storageLayout(world, winW, winH);
        const float x = e.button.x, y = e.button.y;
        if (stl.panel.contains(x, y)) {
            if (e.button.button == SDL_BUTTON_LEFT && stl.lockToggle && stl.lockBtn.contains(x, y)) {
                // The owner opens the lock to everyone, or shuts it again for outsiders to pick
                if (Lock* l = world.get<Lock>(storageProp)) l->locked = !l->locked;
                playSfx(Sfx::UiToggle);
            } else if (e.button.button == SDL_BUTTON_LEFT && stl.closeBtn.contains(x, y)) {
                closeStorage();
                playSfx(Sfx::UiBack);
            } else {
                // The input (or only) grid's cells are panel slots 0 and up, a forge's output cells follow
                std::vector<Rect> cells = stl.slots;
                cells.insert(cells.end(), stl.outSlots.begin(), stl.outSlots.end());
                for (size_t i = 0; i < cells.size(); ++i) {
                    const ItemStack* at = storageSlot(world, (int)i);
                    if (!cells[i].contains(x, y) || !at || at->item == 0) continue;
                    if (e.button.button == SDL_BUTTON_LEFT) {
                        storageDrag = (int)i;
                    } else if (e.button.button == SDL_BUTTON_RIGHT) {
                        std::string why;
                        if (takeFromStorage(world, (int)i, why)) queueSound(Sfx::ItemMove);
                        else if (!why.empty()) refuse(why);
                    }
                    break;
                }
            }
            return true;
        }
    }
    // The locks and the [-] and [+] buttons on the title bars. Folding keeps the panel's size for when it opens.
    if (screen == Screen::Playing && e.button.button == SDL_BUTTON_LEFT) {
        const float tx = e.button.x, ty = e.button.y;
        bool hitLock = false;
        for (PanelId which : ALL_PANELS) {
            if (hitLock) break;
            if (lockRectOf(which, winW, winH).contains(tx, ty)) {
                toggleLock(which);
                hitLock = true;
            }
        }
        if (hitLock) return true;
        const SidebarLayout sbl = sidebarFor(winW, winH, sidebarScroll);
        if (sbl.plus.contains(tx, ty)) {
            setFolded(PanelId::Unit, false, winW, winH);
            return true;
        }
        if (sbl.minus.contains(tx, ty)) {
            setFolded(PanelId::Unit, true, winW, winH);
            return true;
        }
        if (infoPanelClick(winW, winH, tx, ty)) return true;
        if (layoutUnitPanel(winW, winH, (int)ownUnitList().size(), unitPanelRow, squadCollapsed, squadPlace, factionTab, (int)reputationFactions().size(), repScroll).toggle.contains(tx, ty)) {
            setFolded(PanelId::Squad, !squadCollapsed, winW, winH);
            return true;
        }
        if (gameLogPanel(winW, winH).toggle.contains(tx, ty)) {
            setFolded(PanelId::Info, !combatCollapsed, winW, winH);
            return true;
        }
    }
    // The Faction Panel's tabs, and in its Reputation tab a faction's row: that faction's reputations show
    if (screen == Screen::Playing && e.button.button == SDL_BUTTON_LEFT) {
        const UnitPanelLayout fp = layoutUnitPanel(winW, winH, (int)ownUnitList().size(), unitPanelRow, squadCollapsed,
                                                   squadPlace, factionTab, (int)reputationFactions().size(), repScroll);
        const float tx = e.button.x, ty = e.button.y;
        if (fp.tabSquad.contains(tx, ty) || fp.tabReputation.contains(tx, ty)) {
            const int next = fp.tabSquad.contains(tx, ty) ? FACTION_TAB_SQUAD : FACTION_TAB_REPUTATION;
            if (next != factionTab) playSfx(Sfx::UiClick);
            factionTab = next;
            return true;
        }
        bool picked = false;
        for (size_t i = 0; i < fp.repRows.size(); ++i) {
            const size_t idx = (size_t)fp.repFirst + i;
            if (!fp.repRows[i].contains(tx, ty) || idx >= factions().size()) continue;
            if (reputationFactions()[idx].id != repView) playSfx(Sfx::UiClick);
            repView = reputationFactions()[idx].id;
            picked = true;
            break;
        }
        if (picked) return true;
        if (factionTab == FACTION_TAB_REPUTATION && fp.repHeading.contains(tx, ty)) return true;
    }
    // The Squad Panel's scroll bar: an arrow moves the list a row, the thumb can be dragged, and a press on
    // the track moves a row toward it
    if (screen == Screen::Playing && e.button.button == SDL_BUTTON_LEFT) {
        const UnitPanelLayout sq = layoutUnitPanel(winW, winH, (int)ownUnitList().size(), unitPanelRow, squadCollapsed, squadPlace, factionTab, (int)reputationFactions().size(), repScroll);
        const float tx = e.button.x, ty = e.button.y;
        if (sq.scrollUp.contains(tx, ty) || sq.scrollDown.contains(tx, ty)) {
            const int next = std::clamp(sq.scrollRow + (sq.scrollUp.contains(tx, ty) ? -1 : 1), 0, sq.maxScroll());
            if (next != sq.scrollRow) playSfx(Sfx::UiClick);
            unitPanelRow = next;
            return true;
        }
        if (sq.scrollable() && sq.thumb.contains(tx, ty)) {
            squadThumbDrag = true;
            squadThumbGrab = ty - sq.thumb.y;
            return true;
        }
        if (sq.track.contains(tx, ty)) {
            unitPanelRow = std::clamp(sq.scrollRow + (ty < sq.thumb.y ? -1 : 1), 0, sq.maxScroll());
            return true;
        }
    }
    // An edge, a corner or the title bar of an unlocked panel grabs it, to resize or move it
    if (screen == Screen::Playing && e.button.button == SDL_BUTTON_LEFT) {
        const auto g = grabAt(winW, winH, e.button.x, e.button.y);
        if (g.first) {
            const Rect r = panelRect(*g.first, placeOf(*g.first), winW, winH);
            grabWhich = g.first;
            grabKind = g.second;
            grabMouseX = e.button.x;
            grabMouseY = e.button.y;
            grabX = r.x;
            grabY = r.y;
            grabW = r.w;
            grabH = r.h;
            return true;
        }
    }
    return false;
}

// In play, a left click on the Squad Panel or the Unit Panel's sidebar. True if the click was used.
bool App::mouseDownOnPanels() {
    if (e.button.button == SDL_BUTTON_LEFT && screen == Screen::Playing) {
        // The unit panel and sidebar take the click, so it never reaches the ground
        const float mx = e.button.x, my = e.button.y;
        const std::vector<Entity> own = ownUnitList();
        const UnitPanelLayout pl = layoutUnitPanel(winW, winH, (int)own.size(), unitPanelRow, squadCollapsed, squadPlace, factionTab, (int)reputationFactions().size(), repScroll);

        // The right-click menu: a click on one of its buttons does that action. A click
        // anywhere else just closes it, and the click still does what it would have done.
        if (itemMenu.open) {
            const std::vector<std::string> labels = menuLabels();
            if (labels.empty()) {
                itemMenu.open = false;
            } else {
                const ItemMenuLayout ml = renderer.itemMenuLayout(itemMenu.x, itemMenu.y, (int)labels.size(), winW, winH);
                const int hit = itemMenuButtonAt(ml, mx, my);
                const bool onMenu = ml.box.contains(mx, my);
                const Entity unit = itemMenu.unit;
                const int slot = itemMenu.slot;
                itemMenu.open = false;
                if (hit >= 0 && labels[hit] == lang("UILABEL_USE")) {
                    useSlot(unit, slot);
                    playSfx(Sfx::UiClick);
                } else if (hit >= 0 && labels[hit] == lang("UILABEL_DROP")) {
                    // A drop from the inventory plays its own sound, in place of the menu click
                    if (!dropStack(world, unit, slot)) playSfx(Sfx::UiError);
                }
                if (onMenu) return true;
            }
        }

        // The Unit Panel's tabs pick what it shows
        {
            const SidebarLayout st = sidebarFor(winW, winH, sidebarScroll);
            bool tabHit = false;
            for (int t = 0; t < UNIT_TAB_COUNT && st.open; ++t) {
                if (st.tabs[t].contains(mx, my)) {
                    if (!unitTabEnabled(world, sidebarUnitOf(world), (UnitTab)t)) {
                        tabHit = true;   // off for a unit not in your squad: the click does nothing
                        break;
                    }
                    if (unitTab != (UnitTab)t) playSfx(Sfx::UiTab);
                    unitTab = (UnitTab)t;
                    sidebarScroll = 0.0f;
                    tabHit = true;
                    break;
                }
            }
            if (tabHit) return true;
        }

        if (overPanels(mx, my, winW, winH)) {
            // The pace toggle and the View Map button on the bottom panel
            if (pl.paceButton.contains(mx, my)) { runMode = !runMode; playSfx(Sfx::UiToggle); return true; }
            if (pl.optimizeAllButton.contains(mx, my)) {
                contextMenu.open = false;
                itemMenu.open = false;
                optimizeAllOpen = true;
                optimizeSliderDrag = false;
                playSfx(Sfx::UiMenuOpen);
                return true;
            }

            const SidebarLayout sl = sidebarFor(winW, winH, sidebarScroll);
            const bool onSidebar = sl.panel.contains(mx, my);
            if (onSidebar) {
                // The Build tab: a row picks its prop to place, and picking the same one again puts it down
                if (sl.open && sl.tab == UnitTab::Build) {
                    for (size_t i = 0; i < sl.buildRows.size(); ++i) {
                        if (!sl.buildRows[i].contains(mx, my)) continue;
                        const BuildPick pick = buildRowPick((int)i);
                        if (!pick.any()) break;
                        // Grayed out: only Destroy, the Bedroll and the Campfire, unless a selected
                        // unit of yours stands in one of your claims
                        if (!buildAnywhere(pick) && !selectionInClaim(world)) break;
                        if (buildPick == pick) {
                            buildPick = {};
                        } else {
                            buildPick = pick;
                            showNotice(lang(!pick.placing() ? "UILABEL_BUILD_DESTROY_HINT"
                                                                  : "UILABEL_BUILD_PLACE_HINT"), 4.0f);
                        }
                        playSfx(Sfx::UiClick);
                        break;
                    }
                    return true;
                }
                // Sort and the behavior rows change your unit, when the sidebar shows one. They
                // come before the drags, which start on the inventory and gear.
                if (shownIsYours()) {
                    const Entity shown = sidebarUnitOf(world);
                    if (sl.sortButton.contains(mx, my)) {
                        itemMenu.open = false;
                        if (!inventoryLocked(world, shown)) sortInventory(shown);   // a mob's is shut
                        return true;
                    }
                    // The Gear tab: Optimize wears the best gear on hand, Remove All takes it all off.
                    // Either one is checked as a whole first; if the inventory can't take what comes off,
                    // nothing changes.
                    if (sl.optimizeButton.contains(mx, my) || sl.removeAllButton.contains(mx, my)) {
                        itemMenu.open = false;
                        const bool optimize = sl.optimizeButton.contains(mx, my);
                        std::string why;
                        const GearAction done = optimize ? optimizeGear(world, shown, why, gearPrefs()) : removeAllGear(world, shown, why);
                        if (done == GearAction::Done) {
                            queueSound(Sfx::ItemMove);
                        } else if (done == GearAction::NoRoom) {
                            refuse(why);
                        } else {
                            showNotice(lang(optimize ? "MISC_GEAR_ALREADY_BEST" : "MISC_GEAR_NONE_WORN"));
                        }
                        return true;
                    }
                    // The fighting modes: Passive, Block and Sneak switch on and off; Ranged follows the
                    // weapon. Passive and Block end any fight the unit was picking.
                    {
                        int mode = -1;
                        for (int i = 0; i < 3; ++i) {
                            if (sl.modes[(size_t)i].contains(mx, my)) mode = i;
                        }
                        if (sl.modes[3].contains(mx, my)) return true;
                        if (mode >= 0) {
                            if (Behaviors* b = world.get<Behaviors>(shown)) {
                                bool& flag = mode == 0 ? b->passive : mode == 1 ? b->block : b->sneak;
                                flag = !flag;
                                if (flag && mode < 2 && !world.has<AttackOrder>(shown)) {
                                    if (Combat* c = world.get<Combat>(shown)) c->target = NULL_ENTITY;
                                }
                                playSfx(Sfx::UiClick);
                            }
                            return true;
                        }
                    }
                    const BehaviorHit bh = behaviorHitAt(sl, mx, my);
                    if (bh.row >= 0) {
                        behaviorClick(shown, bh);
                        return true;
                    }
                    if (sl.tab == UnitTab::Behavior && behaviorExtraClick(shown, sl, mx, my)) return true;
                }

                // A filled inventory slot starts dragging its stack out of the inventory. A filled
                // gear slot starts dragging the gear item out of the unit's gear. A red unit's
                // sidebar has no unit of yours in it, so nothing there can be dragged.
                const std::vector<Entity> picked = panelUnits(world);
                const Inventory* inv = picked.size() == 1 ? world.get<Inventory>(picked[0]) : nullptr;
                const Equipment* worn = picked.size() == 1 ? world.get<Equipment>(picked[0]) : nullptr;
                const int slot = sidebarSlotAt(sl, mx, my);
                const int gear = gearSlotAt(sl, mx, my);
                if (inv && slot >= 0 && inv->slots[slot].item != 0) {
                    itemDrag = true;
                    dragUnit = picked[0];
                    dragSlot = slot;
                    dragGear = -1;
                } else if (worn && gear >= 0 && worn->items[gear] != 0) {
                    itemDrag = true;
                    dragUnit = picked[0];
                    dragSlot = -1;
                    dragGear = gear;
                }
            }
            for (const UnitCell& c : pl.cells) {
                if (!c.rect.contains(mx, my)) continue;
                const Entity u = own[c.index];
                // Shift toggles the unit in the selection. Alt adds it and never clears or removes anything.
                const SDL_Keymod mods = SDL_GetModState();
                const bool shift = (mods & SDL_KMOD_SHIFT) != 0;
                const bool additive = (mods & (SDL_KMOD_SHIFT | SDL_KMOD_ALT)) != 0;
                inspectUnit = NULL_ENTITY;
                if (shift && world.has<Selected>(u)) {
                    // Shift takes it out of the selection, but never the last one
                    if (selectedUnits(world).size() > 1) world.remove<Selected>(u);
                } else {
                    if (!additive) clearSelection(world);
                    if (!world.has<Selected>(u)) world.add(u, Selected{});
                }
                // Double-click moves the camera to the unit, wherever it is, and keeps it on the unit as it moves
                if (e.button.clicks == 2) {
                    focusCameraOnUnit(world, cam, u);
                    followUnit = u;
                }
                break;
            }
            return true;
        }

        // While a prop is picked to build, a left click on the ground places it. A refusal says why, and
        // the pick stays, so another spot can be tried. The click finds the same spot the preview shows.
        if (buildPick.any() && dialogueUnit == NULL_ENTITY && !mapOpen) {
            if (!buildPick.placing()) {
                // Destroy takes down the prop under the mouse nearest the eye, whatever it is: its
                // items fall where it stood and it becomes Debris (Debris itself is cleared away). Anyone on
                // it gets off first (breakBuilding). Another faction's prop costs standing with it.
                Entity target = NULL_ENTITY;
                float ex = 0.0f, ey = 0.0f, ez = 0.0f, best = 0.0f;
                cam.eye(ex, ey, ez);
                for (Entity e : renderer.propsAtPixel(world, cam, mx, my, winW, winH)) {
                    const Transform* pt = world.get<Transform>(e);
                    const float d2 = (pt->x - ex) * (pt->x - ex) + (pt->y - ey) * (pt->y - ey) + (pt->z - ez) * (pt->z - ez);
                    if (target == NULL_ENTITY || d2 < best) {
                        target = e;
                        best = d2;
                    }
                }
                const Owner* own = target != NULL_ENTITY ? world.get<Owner>(target) : nullptr;
                const int owner = own ? own->faction : FACTION_PLAYER;
                if (target != NULL_ENTITY && storageProp == target) closeStorage();
                if (target != NULL_ENTITY && destroyBuilding(world, target)) {
                    if (owner != FACTION_PLAYER) changePlayerReputation(owner, -REP_DESTROY_PENALTY);
                    playSfx(Sfx::Drop);
                } else {
                    refuse(lang("MISC_DESTROY_NOTHING"));
                }
            } else if (auto hit = pickGround(cam, terrain, mx, my, winW, winH)) {
                float bx = 0.0f, bz = 0.0f;
                snapBuildSpot(buildPick.kind, hit->first, hit->second, bx, bz);
                const BuildCheck why = checkBuildSpot(world, terrain, buildPick.kind, bx, bz, buildYaw);
                if (why == BuildCheck::Ok) {
                    placeBuiltProp(world, terrain, buildPick.kind, bx, bz, buildYaw);
                    playSfx(Sfx::UiClick);
                } else {
                    refuse(lang(buildRefusalKey(why)));
                }
            }
            return true;
        }

        // A click on the visible part of an item on the ground (or, while Alt shows them, on its name)
        // sends the selected units to pick it up
        {
            const bool alt = (SDL_GetModState() & SDL_KMOD_ALT) != 0;
            const Entity item = renderer.groundItemAtPixel(world, terrain, cam, mx, my, winW, winH, alt);
            if (item != NULL_ENTITY) {
                const std::vector<Entity> picked = selectedUnits(world);
                if (picked.empty()) refuse(lang("MISC_PICKUP_SELECT"));
                else {
                    noteLockedIn(picked);
                    issuePickupOrder(world, terrain, picked, item);
                }
                return true;
            }
        }
    }
    return false;
}

// A mouse button let go: ends drags and holds, drops a dragged item, and finishes a box selection
void App::onMouseButtonUp() {
    if (disbandHeldAt != 0 && e.button.button == SDL_BUTTON_LEFT) {
        disbandHeldAt = 0;   // let go before it filled: the hold starts over
        return;
    }
    if (squadThumbDrag && e.button.button == SDL_BUTTON_LEFT) {
        squadThumbDrag = false;
        return;
    }
    if (mapDragging && e.button.button == SDL_BUTTON_LEFT) {
        mapDragging = false;
        return;
    }
    if (partyUi.listDrag && e.button.button == SDL_BUTTON_LEFT) {
        partyUi.listDrag = false;
        return;
    }
    if (partyUi.previewDrag && e.button.button == SDL_BUTTON_RIGHT) {
        partyUi.previewDrag = false;
        return;
    }
    if (optimizeSliderDrag && e.button.button == SDL_BUTTON_LEFT) {
        optimizeSliderDrag = false;
        saveSettingsFile();   // its last place is kept
        playSfx(Sfx::UiSlider);
        return;
    }
    // Letting go of the right button plans the route to where it was held
    if (rightHold && e.button.button == SDL_BUTTON_RIGHT) {
        rightHold = false;
        moveSelectedTo(holdX, holdZ);
        return;
    }
    if (grabWhich && e.button.button == SDL_BUTTON_LEFT) {
        endGrab(winW, winH);
        setGrabCursor(grabAt(winW, winH, e.button.x, e.button.y).second);
        return;
    }
    // A slider let go of: save its final level
    if (e.button.button == SDL_BUTTON_LEFT && settingsUi.dragRow >= 0) {
        settingsUi.dragRow = -1;
        saveSettingsFile();
    }
    // An item dragged out of the storage: let go over the panel it stays; over the Unit Panel it goes into the
    // unit's inventory if it all fits; anywhere else it drops where the unit stands, not at the prop
    if (e.button.button == SDL_BUTTON_LEFT && storageDrag >= 0) {
        const int from = storageDrag;
        storageDrag = -1;
        if (storageProp != NULL_ENTITY) {
            const float x = e.button.x, y = e.button.y;
            if (storageLayout(world, winW, winH).panel.contains(x, y)) {
                // back where it was
            } else if (sidebarFor(winW, winH, sidebarScroll).panel.contains(x, y)) {
                std::string why;
                if (takeFromStorage(world, from, why)) queueSound(Sfx::ItemMove);
                else if (!why.empty()) refuse(why);
            } else if (dropFromStorage(world, from)) {
                queueSound(Sfx::ItemMove);
            }
        }
        return;
    }
    // An inventory stack let go over the storage panel goes into the storage, if all of it fits
    if (e.button.button == SDL_BUTTON_LEFT && itemDrag && storageProp != NULL_ENTITY &&
        storageLayout(world, winW, winH).panel.contains(e.button.x, e.button.y)) {
        if (dragGear < 0 && dragUnit == storageUnit) requestStore(dragSlot);
        itemDrag = false;
        dragUnit = NULL_ENTITY;
        dragSlot = -1;
        dragGear = -1;
        return;
    }
    // The trade: an NPC item let go over its side becomes a ghost there. A unit's item let go over your side
    // does too, if it is the unit being traded with. Either way the drag ends with nothing moved.
    if (e.button.button == SDL_BUTTON_LEFT && tradeOpen && dialogueUnit != NULL_ENTITY) {
        const float x = e.button.x, y = e.button.y;
        const TradeLayout tl = layoutTrade(winW, winH, renderer.pauseBarHeight(false));
        if (tradeNpcDrag >= 0) {
            if (tl.theirBox.contains(x, y)) requestTrade(true, dialogueUnit, tradeNpcDrag);
            tradeNpcDrag = -1;
        }
        // The trade's panels are over the Unit Panel: a drop on them is theirs, wherever the Unit Panel is
        const bool onTrade = tl.interaction.contains(x, y) || tl.tradePanel.contains(x, y);
        if (itemDrag && (onTrade || !sidebarFor(winW, winH, sidebarScroll).panel.contains(x, y))) {
            if (tl.yourBox.contains(x, y) && dragGear < 0) {
                if (dragUnit == dialoguePlayer) requestTrade(false, dragUnit, dragSlot);
                else refuse(lang("MISC_TRADE_ONLY_TALKING"));
            }
            itemDrag = false;
            dragUnit = NULL_ENTITY;
            dragSlot = -1;
            dragGear = -1;
        }
    }
    if (e.button.button == SDL_BUTTON_LEFT && itemDrag) {
        itemDrag = false;
        if (world.has<Unit>(dragUnit)) {
            const SidebarLayout sl = sidebarFor(winW, winH, sidebarScroll);
            const float rx = e.button.x, ry = e.button.y;
            const bool overSidebar = sl.panel.contains(rx, ry);
            const int gear = gearSlotAt(sl, rx, ry);
            const bool onGrid = sidebarSlotAt(sl, rx, ry) >= 0;
            std::string why;
            if (dragGear < 0) {
                // An inventory stack: onto a gear slot it equips, out of the sidebar it drops
                // under the unit. Released on the rest of the sidebar, nothing happens.
                if (gear >= 0) {
                    if (equipFromInventory(world, dragUnit, dragSlot, gear, why)) queueSound(Sfx::ItemMove);
                    else if (!why.empty()) refuse(why);
                } else if (!overSidebar) {
                    if (dropStack(world, dragUnit, dragSlot)) queueSound(Sfx::ItemMove);
                }
            } else {
                // A gear item: onto another gear slot it moves, onto the inventory it comes off,
                // out of the sidebar it drops. Released on the rest of the sidebar, nothing happens.
                if (gear >= 0 && gear != dragGear) {
                    if (!moveEquipped(world, dragUnit, dragGear, gear, why) && !why.empty()) refuse(why);
                } else if (onGrid) {
                    if (unequipToInventory(world, dragUnit, dragGear, why)) queueSound(Sfx::ItemMove);
                    else if (!why.empty()) refuse(why);
                } else if (!overSidebar) {
                    dropEquipped(world, dragUnit, dragGear);
                }
            }
        }
        dragUnit = NULL_ENTITY;
        dragSlot = -1;
        dragGear = -1;
    }
    if (e.button.button == SDL_BUTTON_LEFT && leftDown) {
        leftDown = false;
        // Shift adds to the selection. Alt never clears it, so Alt-click on empty ground does nothing.
        const SDL_Keymod mods = SDL_GetModState();
        const bool additive = (mods & (SDL_KMOD_SHIFT | SDL_KMOD_ALT)) != 0;
        if (sel.dragging) {
            // A box with none of your units in it but items on the ground sends the selected units to
            // pick them all up, as one chain shared between them
            if (!boxSelect(world, cam, sel.x0, sel.y0, sel.x1, sel.y1, winW, winH, additive)) {
                const std::vector<Entity> items =
                    groundItemsInBox(world, cam, sel.x0, sel.y0, sel.x1, sel.y1, winW, winH);
                if (!items.empty()) {
                    const std::vector<Entity> picked = selectedUnits(world);
                    if (picked.empty()) refuse(lang("MISC_PICKUP_SELECT"));
                    else {
                        noteLockedIn(picked);
                        issuePickupChain(world, terrain, picked, items);
                    }
                }
            }
        } else {
            const std::vector<Entity> under = renderer.unitsAtPixel(world, cam, e.button.x, e.button.y, winW, winH);
            clickSelect(world, unitClicked(world, terrain, cam, under, e.button.x, e.button.y, winW, winH), additive);
        }
        sel.dragging = false;
    }
    if (e.button.button == SDL_BUTTON_MIDDLE) middleDown = false;
}

// The mouse moved: drags, sliders, the grab cursor, box selection and the camera's orbit
void App::onMouseMotion() {
    // The party menu's unit list follows its scroll bar thumb while it is held
    if (screen == Screen::PartySetup && partyUi.listDrag) {
        if (e.motion.state & SDL_BUTTON_LMASK) {
            partyUi.listScroll = partyListScrollAt(winW, winH, (int)party.size(), e.motion.y - partyUi.listGrab);
        } else {
            partyUi.listDrag = false;
        }
        return;
    }
    // The held model turns with the mouse's sideways movement only: about its vertical axis, never tipping
    if (screen == Screen::PartySetup && partyUi.previewDrag) {
        if (e.motion.state & SDL_BUTTON_RMASK) {
            partyUi.previewYaw = std::remainder(partyUi.previewYaw + e.motion.xrel * 0.012f, 6.2831853f);
        } else {
            partyUi.previewDrag = false;
        }
        return;
    }
    // The Optimize All slider follows the mouse while its button is held
    if (screen == Screen::Playing && optimizeAllOpen && optimizeSliderDrag) {
        if (e.motion.state & SDL_BUTTON_LMASK) {
            settings.optimizeAttackShare = levelAtX(layoutOptimizeAll(winW, winH).track, e.motion.x);
        } else {
            optimizeSliderDrag = false;
            saveSettingsFile();
        }
        return;
    }
    // The world map window follows the mouse while its title bar is held, kept inside the screen
    if (mapDragging) {
        if ((e.motion.state & SDL_BUTTON_LMASK) && mapOpen && screen == Screen::Playing) {
            mapDragX = e.motion.x - mapGrabX;
            mapDragY = e.motion.y - mapGrabY;
            const WorldMapLayout wm = worldMapFor(winW, winH);   // clamped
            mapDragX = wm.window.x - std::floor((winW - wm.window.w) * 0.5f);
            mapDragY = wm.window.y - std::floor((winH - wm.window.h) * 0.5f);
        } else {
            mapDragging = false;
        }
        return;
    }
    if (grabWhich) {
        applyGrab(winW, winH, e.motion.x, e.motion.y);
        return;
    }
    // The Squad Panel follows its scroll bar thumb while it is held
    if (squadThumbDrag) {
        if ((e.motion.state & SDL_BUTTON_LMASK) && screen == Screen::Playing) {
            const UnitPanelLayout sq = layoutUnitPanel(winW, winH, (int)ownUnitList().size(), unitPanelRow, squadCollapsed, squadPlace, factionTab, (int)reputationFactions().size(), repScroll);
            unitPanelRow = squadRowAtThumb(sq, e.motion.y - squadThumbGrab);
        } else {
            squadThumbDrag = false;
        }
        return;
    }
    if (screen == Screen::Playing) {
        const bool onMap = mapOpen && worldMapFor(winW, winH).window.contains(e.motion.x, e.motion.y);
        setGrabCursor(onMap ? 0 : grabAt(winW, winH, e.motion.x, e.motion.y).second);
    }
    // A slider follows the mouse while its button is held
    if (screen == Screen::Settings && settingsUi.dragRow >= 0) {
        if (e.motion.state & SDL_BUTTON_LMASK) {
            const std::vector<RowSpec> rows = settingsRows(settingsUi.tab, settings, displayInfo);
            const SettingsLayout L = layoutSettings(winW, winH, rows, settingsUi);
            if (settingsUi.dragRow < (int)L.rows.size()) {
                const int level = levelAtX(L.rows[settingsUi.dragRow].track, e.motion.x);
                settingsCommit(setRowLevel(rows[settingsUi.dragRow].id, level, settings), false);
            }
        } else {
            // The button came up outside the window, so no button-up arrived: save here
            settingsUi.dragRow = -1;
            saveSettingsFile();
        }
    }
    if (screen == Screen::Playing) {
        if (screen == Screen::Playing && leftDown) {
            float dx = e.motion.x - pressX, dy = e.motion.y - pressY;
            if (!sel.dragging && dx * dx + dy * dy > 36.0f) sel.dragging = true;
            if (sel.dragging) {
                sel.x0 = pressX; sel.y0 = pressY;
                sel.x1 = e.motion.x; sel.y1 = e.motion.y;
            }
        }
        if (middleDown) {
            cam.yaw += (e.motion.x - lastX) * 0.005f;
            cam.pitch += (e.motion.y - lastY) * 0.005f;
            cam.clampState();
            lastX = e.motion.x;
            lastY = e.motion.y;
        }
    }
}
