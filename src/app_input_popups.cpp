// Mouse presses in play on the popups, the world map, the talk and trade, menus and the panels' bars (app_input.cpp
// has the rest of the input)
#include "app.h"

using namespace play;

bool App::pressOnDisband() {
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
    return false;
}

bool App::pressOnOptimizeAll() {
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
    return false;
}

bool App::pressOnWorldMap() {
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
    return false;
}

// A press on the trade's two panels
void App::pressOnTrade(const TradeLayout& tl, float x, float y) {
    const Inventory* npcInv = world.get<Inventory>(dialogueUnit);
    const bool leftButton = e.button.button == SDL_BUTTON_LEFT;
    if (leftButton && tl.cancelBtn.contains(x, y) && freeTrade) {
        endTalk();   // a free trade has no talk to go back to
    } else if (leftButton && tl.cancelBtn.contains(x, y)) {
        tradeOpen = false;
        clearTradeTable();
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
}

bool App::pressOnTalk() {
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
                pressOnTrade(tradeL, x, y);
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
    return false;
}

bool App::pressOnContextMenu() {
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
    return false;
}

bool App::pressOnStoreQuantity() {
    // Choosing how many of a stack to store: the prompt is modal, and only its buttons answer
    if (screen == Screen::Playing && quantity.open && quantity.toStorage) {
        if (e.button.button == SDL_BUTTON_LEFT) quantityClick(e.button.x, e.button.y);
        return true;
    }
    return false;
}

bool App::pressOnLoot() {
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
    return false;
}

bool App::pressOnStorage() {
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
    return false;
}

bool App::pressOnPanelBars() {
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
        if (squadPanelLayout(winW, winH).toggle.contains(tx, ty)) {
            setFolded(PanelId::Squad, !squadCollapsed, winW, winH);
            return true;
        }
        if (gameLogPanel(winW, winH).toggle.contains(tx, ty)) {
            setFolded(PanelId::Info, !combatCollapsed, winW, winH);
            return true;
        }
    }
    return false;
}

bool App::pressOnFactionTabs() {
    // The Faction Panel's tabs, and in its Reputation tab a faction's row: that faction's reputations show
    if (screen == Screen::Playing && e.button.button == SDL_BUTTON_LEFT) {
        const UnitPanelLayout fp = squadPanelLayout(winW, winH);
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
    return false;
}

bool App::pressOnSquadScroll() {
    // The Squad Panel's scroll bar: an arrow moves the list a row, the thumb can be dragged, and a press on
    // the track moves a row toward it
    if (screen == Screen::Playing && e.button.button == SDL_BUTTON_LEFT) {
        const UnitPanelLayout sq = squadPanelLayout(winW, winH);
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
    return false;
}

bool App::pressToGrabPanel() {
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

// In play, a press on a modal popup, the world map, the talk or trade, the right-click menu, the loot or
// storage panel, or a panel's title bar, lock, tabs, scroll bar or edge. True if the press was used.
bool App::mouseDownOnPopups() {
    if (pressOnDisband()) return true;
    if (pressOnOptimizeAll()) return true;
    if (pressOnWorldMap()) return true;
    if (pressOnTalk()) return true;
    if (pressOnContextMenu()) return true;
    if (pressOnStoreQuantity()) return true;
    if (pressOnLoot()) return true;
    if (pressOnStorage()) return true;
    if (pressOnPanelBars()) return true;
    if (pressOnFactionTabs()) return true;
    if (pressOnSquadScroll()) return true;
    if (pressToGrabPanel()) return true;
    return false;
}
