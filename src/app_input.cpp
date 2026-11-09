#include "app.h"
#include "building.h"

using namespace play;

void App::handleEvent() {
    if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_F12 && !e.key.repeat) {
        screenshotPending = true;
        return;
    }
    switch (e.type) {
    case SDL_EVENT_QUIT:
        requestLeave(LeaveTo::Desktop);
        break;
    case SDL_EVENT_TEXT_INPUT:
        onTextInput();
        break;
    case SDL_EVENT_TEXT_EDITING:
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

void App::onTextInput() {
    if (screen == Screen::Playing && rename.open) {
        appendUtf8Limited(rename.text, e.text.text, PARTY_NAME_CHARS);
        return;
    }
    if (screen == Screen::Playing && squadRenaming) {
        appendUtf8Limited(squadRenameText, e.text.text, SQUAD_NAME_CHARS);
        return;
    }
    if (percentPrompt.open) {
        const size_t most = std::to_string(percentRange(percentPrompt.kind).hi).size();
        for (const char* c = e.text.text; *c; ++c) {
            if (*c < '0' || *c > '9') continue;
            if (percentPrompt.text == "0") percentPrompt.text.clear();
            if (percentPrompt.text.size() < most) percentPrompt.text += *c;
        }
        percentPrompt.error.clear();
        return;
    }
    if (screen == Screen::WorldGen && worldGenUi.editingName) {
        appendUtf8Limited(worldGenUi.name, e.text.text, WORLD_GEN_NAME_CHARS);
        worldGenUi.nameEdited = true;
        worldGenChanged();
        return;
    }
    if (screen == Screen::Playing && editing && editor.editingName) {
        appendUtf8Limited(editor.name, e.text.text, EDITOR_NAME_CHARS);
        return;
    }
    if (screen == Screen::PartySetup) {
        partyUi.composition.clear();
        if (partyUi.editingName) appendUtf8Limited(pickedMember(party, partyUi).name, e.text.text, PARTY_NAME_CHARS);
        return;
    }
    if (panelOpen && panelSaveMode && ask == Ask::None && saveName.size() < NAME_LIMIT) {
        saveName += e.text.text;
    }
}

void App::onKeyDown() {
    if (e.key.key == SDLK_ESCAPE && e.key.repeat) return;
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
    if (screen == Screen::Playing && session.askMaterial) {
        if (e.key.key == SDLK_ESCAPE) session.askMaterial = false;
        return;
    }
    if (screen == Screen::Playing && rename.open) {
        if (e.key.key == SDLK_BACKSPACE) popUtf8Char(rename.text);
        else if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) finishRename(true);
        else if (e.key.key == SDLK_ESCAPE) finishRename(false);
        return;
    }
    if (screen == Screen::Playing && squadRenaming) {
        if (e.key.key == SDLK_BACKSPACE) popUtf8Char(squadRenameText);
        else if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) stopSquadRename(true);
        else if (e.key.key == SDLK_ESCAPE) stopSquadRename(false);
        return;
    }
    if (screen == Screen::Playing && altar.open) {
        if (e.key.key == SDLK_ESCAPE) {
            altar = AltarUi{};
            playSfx(Sfx::UiBack);
        }
        return;
    }
    if (screen == Screen::Playing && storeFilterUi.open) {
        if (e.key.key == SDLK_ESCAPE) {
            storeFilterUi = StoreFilterUi{};
            playSfx(Sfx::UiBack);
        }
        return;
    }
    if (screen == Screen::Playing && stallUi.open) {
        if (e.key.key == SDLK_ESCAPE) {
            stallUi = StallUi{};
            playSfx(Sfx::UiBack);
        }
        return;
    }
    if (screen == Screen::Playing && groupsUi.open) {
        if (e.key.key == SDLK_ESCAPE) {
            groupsUi = BehaviorGroupsUi{};
            playSfx(Sfx::UiBack);
        }
        return;
    }
    if (screen == Screen::Playing && ghostsUi.open) {
        if (e.key.key == SDLK_ESCAPE) {
            ghostsUi = GhostsUi{};
            playSfx(Sfx::UiBack);
        }
        return;
    }
    if (screen == Screen::WorldGen) {
        if (!worldGenUi.deleteMap.empty()) {
            if (e.key.key == SDLK_ESCAPE) cancelMapDelete();
        } else if (worldGenUi.editingName) {
            if (e.key.key == SDLK_BACKSPACE) {
                popUtf8Char(worldGenUi.name);
                worldGenUi.nameEdited = true;
                worldGenChanged();
            } else if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER || e.key.key == SDLK_ESCAPE) {
                stopWorldGenName();
            }
        } else if (e.key.key == SDLK_ESCAPE) {
            playSfx(Sfx::UiBack);
            screen = Screen::Title;
        }
        return;
    }
    if (screen == Screen::Playing && editing && !panelOpen && editorKeyDown()) return;
    if (screen == Screen::PartySetup) {
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
            if (ask != Ask::None) {
                clearQuestion();
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
            if (items[(size_t)i].enabled && i != menuSel) menuSel = i;
            return;
        }
        const bool pick = k == SDLK_RETURN || k == SDLK_KP_ENTER || (k == SDLK_SPACE && screen == Screen::Title);
        if (pick && !e.key.repeat && menuSel >= 0 && menuSel < (int)items.size() && items[(size_t)menuSel].enabled) {
            performAction(items[(size_t)menuSel].action, items[(size_t)menuSel].param);
            return;
        }
    }
    if (screen == Screen::Playing && !editing && session.buildPick.placing() && propInfo(session.buildPick.kind).stairs &&
        e.key.key == SDLK_R && !e.key.repeat) {
        session.buildDir = (session.buildDir + 1) % 4;
        playSfx(Sfx::UiClick);
        return;
    }
    if (screen == Screen::Playing && !editing && session.dialogueUnit == NO_OBJECT && (e.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT)) == 0 &&
        (e.key.key == SDLK_Q || e.key.key == SDLK_E)) {
        stepFloor(e.key.key == SDLK_E ? 1 : -1);
        return;
    }
    if (screen == Screen::Playing && session.buildPick.any() && e.key.key == SDLK_ESCAPE) {
        session.buildPick = {};
        playSfx(Sfx::UiBack);
        return;
    }
    if (screen == Screen::Playing && !e.key.repeat && (e.key.mod & SDL_KMOD_ALT) != 0 && session.dialogueUnit == NO_OBJECT && formationKeyDown()) return;
    if (squadKeyDown()) return;
    if (screen == Screen::Playing && !e.key.repeat && e.key.key == SDLK_M) {
        mapOpen = !mapOpen;
        return;
    }
    if (screen == Screen::Playing && e.key.key == SDLK_SPACE && !e.key.repeat) {
        session.paused = !session.paused;
        session.autoPaused = false;
        playSfx(session.paused ? Sfx::UiPause : Sfx::UiResume);
    }
    if (e.key.key == SDLK_ESCAPE) {
        if (screen == Screen::Playing && session.dialogueUnit != NO_OBJECT) {
            if (session.quantity.open && session.tradeOpen && !session.quantity.toStorage) {
                session.quantity.open = false;
            } else if (session.tradeOpen && session.freeTrade) {
                endTalk();
            } else if (session.tradeOpen) {
                session.tradeOpen = false;
                clearTradeTable();
            } else {
                endTalk();
            }
            playSfx(Sfx::UiBack);
            return;
        }
        if (screen == Screen::Playing && session.disbandTarget != NO_OBJECT) {
            session.disbandTarget = NO_OBJECT;
            session.disbandHeldAt = 0;
            playSfx(Sfx::UiBack);
        } else if (screen == Screen::Playing && selectMenuOpen) {
            selectMenuOpen = false;
            playSfx(Sfx::UiBack);
        } else if (screen == Screen::Playing && squadMenu != 0) {
            squadMenu = 0;
            playSfx(Sfx::UiBack);
        } else if (screen == Screen::Playing && optimizeAllOpen) {
            optimizeAllOpen = false;
            optimizeSliderDrag = false;
            playSfx(Sfx::UiBack);
        } else if (screen == Screen::Playing && mapOpen) {
            mapOpen = false;
        } else if (screen == Screen::Playing && itemMenu.open) {
            itemMenu.open = false;
        } else if (screen == Screen::Playing) {
            leftDown = middleDown = false;
            formationDrag = false;
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
            if (settingsUi.openRow >= 0) settingsCloseDropdown(settingsUi);
            else leaveSettings();
        } else if (screen == Screen::ScenarioSelect) {
            playSfx(Sfx::UiBack);
            screen = Screen::MapSelect;
        } else if (screen == Screen::MapSelect) {
            if (mapPreview) mapPreview->stop();
            performAction(Action::ReturnToTitle, 0);
        }
    }
}

void App::onMouseWheel() {
    if (wheelOnFormMenu()) return;
    const bool wheelOverMap = mapOpen && screen == Screen::Playing
                              && worldMapFor(winW, winH).window.contains(e.wheel.mouse_x, e.wheel.mouse_y);
    if (screen == Screen::Playing && stallUi.open) {
        scrollStall(e.wheel.mouse_x, e.wheel.mouse_y, e.wheel.y);
        return;
    }
    // A big store's window (a Stall's) scrolls a row at a time
    if (screen == Screen::Playing && session.storageProp != NO_OBJECT && storageScrollMax(world) > 0 &&
        storageLayout(world, winW, winH).panel.contains(e.wheel.mouse_x, e.wheel.mouse_y)) {
        const int step = e.wheel.y > 0.0f ? -1 : e.wheel.y < 0.0f ? 1 : 0;
        session.storageScroll = std::clamp(session.storageScroll + step, 0, storageScrollMax(world));
        return;
    }
    if (screen == Screen::Playing && groupsUi.open) {
        scrollBehaviorGroups(e.wheel.mouse_x, e.wheel.mouse_y, e.wheel.y);
        return;
    }
    if (screen == Screen::Playing && ghostsUi.open) {
        const AltarLayout L = layoutAltar(winW, winH, { (int)ghostBones().size(), (int)ghostOrder().size() }, ghostsUi.scroll);
        for (int c = 0; c < 2; ++c) {
            if (!L.lists[(size_t)c].contains(e.wheel.mouse_x, e.wheel.mouse_y)) continue;
            const int step = e.wheel.y > 0.0f ? -3 : e.wheel.y < 0.0f ? 3 : 0;
            ghostsUi.scroll[(size_t)c] = std::max(0, L.first[(size_t)c] + step);
        }
        return;
    }
    if (screen == Screen::Playing && altar.open) {
        const std::array<std::vector<ObjectId>, 2> cols = altarColumns();
        const AltarLayout L = layoutAltar(winW, winH, { (int)cols[0].size(), (int)cols[1].size() }, altar.scroll);
        for (int c = 0; c < 2; ++c) {
            if (!L.lists[(size_t)c].contains(e.wheel.mouse_x, e.wheel.mouse_y)) continue;
            const int step = e.wheel.y > 0.0f ? -3 : e.wheel.y < 0.0f ? 3 : 0;
            altar.scroll[(size_t)c] = std::clamp(L.first[(size_t)c] + step, 0, altarMaxScroll(L, c, (int)cols[(size_t)c].size()));
        }
        return;
    }
    if (panelOpen) {
        int maxOffset = std::max(0, (int)saves.size() - (int)visibleSaveRows(winH));
        int next = (int)listOffset - (int)e.wheel.y;
        listOffset = (size_t)std::clamp(next, 0, maxOffset);
    } else if (screen == Screen::MapSelect) {
        mapPickWheel(e.wheel.mouse_x, e.wheel.mouse_y, e.wheel.y);
    } else if (screen == Screen::WorldGen) {
        if (worldGenUi.themeTab && layoutWorldGen(winW, winH, (int)worldGenUi.maps.size(), worldGenUi.mapScroll).panel.contains(e.wheel.mouse_x, e.wheel.mouse_y)) {
            themeUi.scroll = std::max(0, themeUi.scroll - (int)e.wheel.y);
        } else {
            worldGenUi.mapScroll = std::max(0, worldGenUi.mapScroll - (int)e.wheel.y);
        }
    } else if (screen == Screen::Playing && editing && editor.useOwnUi && layoutEditor(winW, winH, true).panel.contains(e.wheel.mouse_x, e.wheel.mouse_y)) {
        if (editor.tab == EditorTab::Theme) themeUi.scroll = std::max(0, themeUi.scroll - (int)e.wheel.y);
        else editor.listScroll = std::max(0, editor.listScroll - (int)e.wheel.y);
    } else if (screen == Screen::ScenarioSelect) {
        scenarioScroll = layoutScenario(winW, winH, (int)ScenarioId::Count,
                                        scenarioScroll - (int)e.wheel.y).first;
    } else if (screen == Screen::PartySetup) {
        if (partyUi.dropKind != DropKind::None) {
            const int count = (int)dropdownEntries(party, partyUi).size();
            partyUi.dropScroll = clampDropScroll(partyUi.dropScroll - (int)e.wheel.y, count);
        } else if (partyPanels(winW, winH).left.contains(e.wheel.mouse_x, e.wheel.mouse_y)) {
            const int maxScroll = partyListMaxScroll((int)party.size(), winH);
            partyUi.listScroll = std::clamp(std::min(partyUi.listScroll, maxScroll) - (int)e.wheel.y, 0, maxScroll);
        }
    } else if (screen == Screen::Settings) {
        if (settingsUi.openRow >= 0) {
            const std::vector<RowSpec> rows = settingsRows(settingsUi.tab, settings, displayInfo);
            settingsScrollDropdown(settingsUi, layoutSettings(winW, winH, rows, settingsUi), (int)e.wheel.y);
        }
    } else if (screen == Screen::Playing && !wheelOverMap && gameLogHovered(winW, winH)) {
        const int step = e.wheel.y > 0.0f ? 3 : e.wheel.y < 0.0f ? -3 : 0;
        logOffset = std::clamp(logOffset + step, 0, gameLogPanel(winW, winH).maxOffset());
    } else if (wheelOverMap) {
        const Rect box = worldMapFor(winW, winH).map;
        const float px = std::clamp(e.wheel.mouse_x, box.x, box.x + box.w);
        const float py = std::clamp(e.wheel.mouse_y, box.y, box.y + box.h);
        const float oldK = mapTilesPerPixel(box, mapSpan);
        const float offX = box.x + box.w * 0.5f - px, offY = box.y + box.h * 0.5f - py;
        const float gx = mapCx + offX * oldK;
        const float gz = mapCz + offY * oldK;
        mapSpan = e.wheel.y > 0.0f ? mapSpan * 0.8f : mapSpan * 1.25f;
        float cx = mapCx, cz = mapCz;
        clampMapView(box, (float)terrain.size(), cx, cz, mapSpan);
        const float k = mapTilesPerPixel(box, mapSpan);
        mapCx = gx - offX * k;
        mapCz = gz - offY * k;
        clampMapView(box, (float)terrain.size(), mapCx, mapCz, mapSpan);
    } else if (screen == Screen::Playing && unitPanelHovered(winW, winH) && factionTab == FACTION_TAB_FACTION) {
        const int step = e.wheel.y > 0.0f ? -1 : e.wheel.y < 0.0f ? 1 : 0;
        repScroll = layoutUnitPanel(winW, winH, (int)ownUnitList().size(), unitPanelRow, squadCollapsed, squadPlace,
                                    factionTab, (int)reputationFactions().size(), repScroll + step).repFirst;
    } else if (screen == Screen::Playing && unitPanelHovered(winW, winH)) {
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
        const float notch = 40.0f * (winH / 720.0f);
        sidebarScroll = sidebarFor(winW, winH, sidebarScroll - e.wheel.y * notch).scroll;
    } else if (screen == Screen::Playing && !pointerOverPanels(winW, winH)) {
        cam.distance *= (e.wheel.y > 0.0f) ? 0.9f : 1.1f;
        cam.clampState();
    }
}

void App::onMouseButtonDown() {
    if (percentPrompt.open) {
        if (e.button.button == SDL_BUTTON_LEFT) {
            const PercentPromptLayout pl = layoutPercentPrompt(winW, winH);
            if (pl.ok.contains(e.button.x, e.button.y)) answerPercentPrompt(true);
            else if (pl.cancel.contains(e.button.x, e.button.y) || !pl.box.contains(e.button.x, e.button.y)) answerPercentPrompt(false);
        }
        return;
    }
    if (session.buildPick.any() && screen == Screen::Playing && e.button.button == SDL_BUTTON_RIGHT) {
        session.buildPick = {};
        playSfx(Sfx::UiBack);
        return;
    }
    if (panelOpen) {
        if (e.button.button == SDL_BUTTON_LEFT) {
            for (const Button& b : layoutPanel(winW, winH, panelSaveMode, ask, saves, listOffset,
                                               panelFrom == Screen::Title, panelCanConfirm())) {
                if (b.enabled && b.rect.contains(e.button.x, e.button.y)) {
                    if (b.action == Action::DeleteHold) {
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

    if (screen == Screen::WorldGen) {
        if (e.button.button == SDL_BUTTON_LEFT) worldGenClick(e.button.x, e.button.y);
        return;
    }
    if (mapPickMouseDown()) return;
    if (screen == Screen::Playing && editing && editorMouseDown()) return;

    if (screen == Screen::ScenarioSelect) {
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
                playSfx(Sfx::UiBack);
                screen = Screen::MapSelect;
            }
        }
        return;
    }

    if (screen == Screen::PartySetup) {
        const PartyListBar bar = partyListBar(winW, winH, (int)party.size(), partyUi.listScroll);
        const Rect barGrab{ bar.track.x - 4.0f, bar.track.y, bar.track.w + 8.0f, bar.track.h };
        if (e.button.button == SDL_BUTTON_LEFT && partyUi.dropKind == DropKind::None && bar.track.h > 0.0f
            && barGrab.contains(e.button.x, e.button.y)) {
            partyUi.listGrab = bar.thumb.contains(bar.thumb.x, e.button.y) ? e.button.y - bar.thumb.y : bar.thumb.h * 0.5f;
            partyUi.listScroll = partyListScrollAt(winW, winH, (int)party.size(), e.button.y - partyUi.listGrab);
            partyUi.listDrag = true;
            return;
        }
        if (e.button.button == SDL_BUTTON_RIGHT && partyUi.dropKind == DropKind::None && !party.empty()
            && partyPreviewRect(winW, winH).contains(e.button.x, e.button.y)) {
            partyUi.previewDrag = true;
            return;
        }
        if (e.button.button == SDL_BUTTON_LEFT) {
            const float mx = e.button.x, my = e.button.y;
            const std::vector<DropEntry> drop = dropdownEntries(party, partyUi);
            const std::vector<Button> buttons = layoutPartySetup(winW, winH, party, partyUi, drop);
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
        if (e.button.button == SDL_BUTTON_LEFT) {
            for (const Button& b : layoutButtons(screen, winW, winH, saves)) {
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
        if (!overPanels(e.button.x, e.button.y, winW, winH)) {
            middleDown = true;
            lastX = e.button.x;
            lastY = e.button.y;
        }
    } else if (e.button.button == SDL_BUTTON_RIGHT) {
        float mgx = 0.0f, mgz = 0.0f;
        if (screen == Screen::Playing && mapGroundAt(winW, winH, e.button.x, e.button.y, mgx, mgz)) {
            beginRightHold(mgx, mgz);
        } else if (overPanels(e.button.x, e.button.y, winW, winH)) {
            {
                const std::vector<ObjectId> own = ownUnitList();
                const UnitPanelLayout squadL = squadPanelLayout(winW, winH);
                bool onCell = false;
                for (const UnitCell& c : squadL.cells) {
                    if (!c.rect.contains(e.button.x, e.button.y) || c.index < 0 || c.index >= (int)own.size()) continue;
                    onCell = true;
                    openContextMenuOn(own[(size_t)c.index], e.button.x, e.button.y, true);
                    break;
                }
                if (onCell) return;
            }
            itemMenu.open = false;
            const std::vector<ObjectId> picked = panelUnits(world);
            const SidebarLayout sl = sidebarFor(winW, winH, sidebarScroll);
            const int slot = sidebarUp() ? sidebarSlotAt(sl, e.button.x, e.button.y) : -1;
            const int gear = sidebarUp() ? gearSlotAt(sl, e.button.x, e.button.y) : -1;
            const Inventory* inv = picked.size() == 1 ? world.get<Inventory>(picked[0]) : nullptr;
            const Equipment* wornHere = picked.size() == 1 ? world.get<Equipment>(picked[0]) : nullptr;
            if (wornHere && gear >= 0 && wornHere->items[gear] != 0) {
                std::string why;
                if (unequipToInventory(world, picked[0], gear, why)) queueSound(world, Sfx::ItemMove);
                else if (!why.empty()) refuse(why);
            } else if (inv && slot >= 0 && inv->slots[slot].item != 0 && session.tradeOpen) {
                requestTrade(false, picked[0], slot);
            } else if (inv && slot >= 0 && inv->slots[slot].item != 0 && session.storageProp != NO_OBJECT &&
                       picked[0] == session.storageUnit) {
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
                        queueSound(world, Sfx::ItemMove);
                    }
                } else {
                    itemMenu = ItemMenu{ true, picked[0], slot, e.button.x, e.button.y };
                    playSfx(Sfx::UiMenuOpen);
                }
            }
        } else if (const auto dragFrom = (SDL_GetModState() & SDL_KMOD_ALT) != 0 && selectedUnits(world).size() >= 2
                                              ? pickGround(cam, terrain, e.button.x, e.button.y, winW, winH) : std::nullopt) {
            formationDrag = true;
            dragFromX = dragToX = dragFrom->first;
            dragFromZ = dragToZ = dragFrom->second;
        } else {
            if (openContextMenu(e.button.x, e.button.y)) {
                rightHold = false;
            } else {
                int storey = 0;
                if (!selectedUnits(world).empty()) {
                    if (const auto hit = pickFloor(e.button.x, e.button.y, storey)) {
                        clickStorey = storey;
                        beginRightHold(hit->first, hit->second);
                    }
                }
            }
        }
    }
}

void App::onMouseButtonUp() {
    themeUi.dragRow = -1;
    if (screen == Screen::WorldGen) {
        worldGenUi.dragRow = -1;
        return;
    }
    if (session.disbandHeldAt != 0 && e.button.button == SDL_BUTTON_LEFT) {
        session.disbandHeldAt = 0;
        return;
    }
    if (screen == Screen::Playing && e.button.button == SDL_BUTTON_LEFT && buildRelease()) return;
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
        saveSettingsFile();
        playSfx(Sfx::UiSlider);
        return;
    }
    if (formationDrag && e.button.button == SDL_BUTTON_RIGHT) {
        finishFormationDrag();
        return;
    }
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
    if (e.button.button == SDL_BUTTON_LEFT && settingsUi.dragRow >= 0) {
        settingsUi.dragRow = -1;
        saveSettingsFile();
    }
    if (e.button.button == SDL_BUTTON_LEFT && session.storageDrag >= 0) {
        const int from = session.storageDrag;
        session.storageDrag = -1;
        if (session.storageProp != NO_OBJECT) {
            const float x = e.button.x, y = e.button.y;
            if (storageLayout(world, winW, winH).panel.contains(x, y)) {
            } else if (sidebarFor(winW, winH, sidebarScroll).panel.contains(x, y)) {
                std::string why;
                if (takeFromStorage(world, from, why)) queueSound(world, Sfx::ItemMove);
                else if (!why.empty()) refuse(why);
            } else if (dropFromStorage(world, from)) {
                queueSound(world, Sfx::ItemMove);
            }
        }
        return;
    }
    if (e.button.button == SDL_BUTTON_LEFT && itemDrag && session.storageProp != NO_OBJECT &&
        storageLayout(world, winW, winH).panel.contains(e.button.x, e.button.y)) {
        if (dragGear < 0 && dragUnit == session.storageUnit) requestStore(dragSlot);
        itemDrag = false;
        dragUnit = NO_OBJECT;
        dragSlot = -1;
        dragGear = -1;
        return;
    }
    if (e.button.button == SDL_BUTTON_LEFT && session.tradeOpen && session.dialogueUnit != NO_OBJECT) {
        const float x = e.button.x, y = e.button.y;
        const TradeLayout tl = layoutTrade(winW, winH, renderer.pauseBarHeight(true));
        if (session.tradeNpcDrag >= 0) {
            if (tl.theirBox.contains(x, y)) requestTrade(true, session.dialogueUnit, session.tradeNpcDrag);
            session.tradeNpcDrag = -1;
        }
        const bool onTrade = tl.interaction.contains(x, y) || tl.tradePanel.contains(x, y);
        if (itemDrag && (onTrade || !sidebarFor(winW, winH, sidebarScroll).panel.contains(x, y))) {
            if (tl.yourBox.contains(x, y) && dragGear < 0) {
                if (dragUnit == session.dialoguePlayer) requestTrade(false, dragUnit, dragSlot);
                else refuse(lang("MISC_TRADE_ONLY_TALKING"));
            }
            itemDrag = false;
            dragUnit = NO_OBJECT;
            dragSlot = -1;
            dragGear = -1;
        }
    }
    if (e.button.button == SDL_BUTTON_LEFT && itemDrag) {
        itemDrag = false;
        if (world.has<Entity>(dragUnit)) {
            const SidebarLayout sl = sidebarFor(winW, winH, sidebarScroll);
            const float rx = e.button.x, ry = e.button.y;
            const bool overSidebar = sl.panel.contains(rx, ry);
            const int gear = gearSlotAt(sl, rx, ry);
            const bool onGrid = sidebarSlotAt(sl, rx, ry) >= 0;
            std::string why;
            if (dragGear < 0) {
                if (gear >= 0) {
                    if (equipFromInventory(world, dragUnit, dragSlot, gear, why)) queueSound(world, Sfx::ItemMove);
                    else if (!why.empty()) refuse(why);
                } else if (!overSidebar) {
                    if (dropStack(world, dragUnit, dragSlot)) queueSound(world, Sfx::ItemMove);
                }
            } else {
                if (gear >= 0 && gear != dragGear) {
                    if (!moveEquipped(world, dragUnit, dragGear, gear, why) && !why.empty()) refuse(why);
                } else if (onGrid) {
                    if (unequipToInventory(world, dragUnit, dragGear, why)) queueSound(world, Sfx::ItemMove);
                    else if (!why.empty()) refuse(why);
                } else if (!overSidebar) {
                    dropEquipped(world, dragUnit, dragGear);
                }
            }
        }
        dragUnit = NO_OBJECT;
        dragSlot = -1;
        dragGear = -1;
    }
    if (e.button.button == SDL_BUTTON_LEFT && leftDown) {
        leftDown = false;
        const SDL_Keymod mods = SDL_GetModState();
        const bool additive = (mods & (SDL_KMOD_SHIFT | SDL_KMOD_ALT)) != 0;
        if (sel.dragging) {
            const std::vector<ObjectId> units = unitsInBox(world, cam, sel.x0, sel.y0, sel.x1, sel.y1, winW, winH);
            const std::vector<ObjectId> items = groundItemsInBox(world, cam, sel.x0, sel.y0, sel.x1, sel.y1, winW, winH);
            // Only units you already have selected in the box, with items: the box is for the items, and they stay selected
            const bool onlyChosen = std::all_of(units.begin(), units.end(), [&](ObjectId u) { return world.has<Selected>(u); });
            if (!items.empty() && onlyChosen) {
                const std::vector<ObjectId> picked = selectedUnits(world);
                if (picked.empty()) refuse(lang("MISC_PICKUP_SELECT"));
                else {
                    noteLockedIn(picked);
                    issuePickupChain(world, terrain, picked, items);
                }
            } else if (boxSelect(world, cam, sel.x0, sel.y0, sel.x1, sel.y1, winW, winH, additive)) {
                if (!additive) noteSquadOfSelection();
            }
        } else {
            const std::vector<ObjectId> under = renderer.entitiesAtPixel(world, cam, e.button.x, e.button.y, winW, winH);
            const ObjectId hit = unitClicked(world, under);
            clickSelect(world, hit, additive);
            if (!additive && hit != NO_OBJECT) noteSquadOfSelection();
        }
        sel.dragging = false;
    }
    if (e.button.button == SDL_BUTTON_MIDDLE) middleDown = false;
}

void App::onMouseMotion() {
    if (themeUi.dragRow >= 0 && (screen == Screen::WorldGen || (screen == Screen::Playing && editing))) {
        themeDragAt(e.motion.x);
        return;
    }
    if (screen == Screen::WorldGen) {
        if (worldGenUi.dragRow >= 0) worldGenBarAt(worldGenUi.dragRow, e.motion.x);
        return;
    }
    if (screen == Screen::PartySetup && partyUi.listDrag) {
        if (e.motion.state & SDL_BUTTON_LMASK) {
            partyUi.listScroll = partyListScrollAt(winW, winH, (int)party.size(), e.motion.y - partyUi.listGrab);
        } else {
            partyUi.listDrag = false;
        }
        return;
    }
    if (screen == Screen::PartySetup && partyUi.previewDrag) {
        if (e.motion.state & SDL_BUTTON_RMASK) {
            partyUi.previewYaw = std::remainder(partyUi.previewYaw + e.motion.xrel * 0.012f, TWO_PI);
        } else {
            partyUi.previewDrag = false;
        }
        return;
    }
    if (screen == Screen::Playing && optimizeAllOpen && optimizeSliderDrag) {
        if (e.motion.state & SDL_BUTTON_LMASK) {
            settings.optimizeAttackShare = levelAtX(layoutOptimizeAll(winW, winH).track, e.motion.x);
        } else {
            optimizeSliderDrag = false;
            saveSettingsFile();
        }
        return;
    }
    if (mapDragging) {
        if ((e.motion.state & SDL_BUTTON_LMASK) && mapOpen && screen == Screen::Playing) {
            mapDragX = e.motion.x - mapGrabX;
            mapDragY = e.motion.y - mapGrabY;
            const WorldMapLayout wm = worldMapFor(winW, winH);
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
    if (squadThumbDrag) {
        if ((e.motion.state & SDL_BUTTON_LMASK) && screen == Screen::Playing) {
            const UnitPanelLayout sq = squadPanelLayout(winW, winH);
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
    if (screen == Screen::Settings && settingsUi.dragRow >= 0) {
        if (e.motion.state & SDL_BUTTON_LMASK) {
            const std::vector<RowSpec> rows = settingsRows(settingsUi.tab, settings, displayInfo);
            const SettingsLayout L = layoutSettings(winW, winH, rows, settingsUi);
            if (settingsUi.dragRow < (int)L.rows.size()) {
                const int level = levelAtX(L.rows[settingsUi.dragRow].track, e.motion.x);
                settingsCommit(setRowLevel(rows[settingsUi.dragRow].id, level, settings), false);
            }
        } else {
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

void App::panWithKeys(float dt, bool edgeScroll) {
    const bool* keys = SDL_GetKeyboardState(nullptr);
    float right = 0.0f, forward = 0.0f;
    if (percentPrompt.open || panelOpen) keys = nullptr;
    if (keys && (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT])) right += 1.0f;
    if (keys && (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT])) right -= 1.0f;
    if (keys && (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP])) forward += 1.0f;
    if (keys && (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN])) forward -= 1.0f;

    if (edgeScroll) {
        int winW = 0, winH = 0;
        SDL_GetWindowSizeInPixels(window, &winW, &winH);
        float mx = 0.0f, my = 0.0f;
        SDL_GetMouseState(&mx, &my);
        if (mx < 6.0f) right -= 1.0f;
        if (mx > winW - 6.0f) right += 1.0f;
        if (my < 6.0f) forward += 1.0f;
        if (my > winH - 6.0f) forward -= 1.0f;
    }

    const bool fast = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
    float panSpeed = cam.distance * 0.9f * dt * (fast ? 2.0f : 1.0f);
    if (right != 0.0f || forward != 0.0f) followUnit = NO_OBJECT;
    cam.pan(right * panSpeed, forward * panSpeed);
}
