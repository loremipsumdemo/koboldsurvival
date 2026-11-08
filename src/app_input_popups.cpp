#include "app.h"

using namespace play;

bool App::pressOnDisband() {
    if (screen == Screen::Playing && session.disbandTarget != NO_OBJECT) {
        if (e.button.button == SDL_BUTTON_LEFT) {
            const DisbandLayout dl = layoutDisband(winW, winH);
            if (dl.disband.contains(e.button.x, e.button.y)) {
                session.disbandHeldAt = std::max<unsigned long long>(1, SDL_GetTicks());
                playSfx(Sfx::UiClick);
            } else if (dl.cancel.contains(e.button.x, e.button.y)) {
                session.disbandTarget = NO_OBJECT;
                session.disbandHeldAt = 0;
                playSfx(Sfx::UiBack);
            }
        }
        return true;
    }
    return false;
}

bool App::pressOnOptimizeAll() {
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
                optimizeAllOpen = false;
                optimizeSquadGear();
            }
        }
        return true;
    }
    return false;
}

bool App::pressOnWorldMap() {
    if (screen == Screen::Playing && mapOpen && !onTalkOrTrade(winW, winH, e.button.x, e.button.y) &&
        worldMapFor(winW, winH).window.contains(e.button.x, e.button.y)) {
        const WorldMapLayout wm = worldMapFor(winW, winH);
        float mgx = 0.0f, mgz = 0.0f;
        if (e.button.button == SDL_BUTTON_LEFT && wm.close.contains(e.button.x, e.button.y)) {
            mapOpen = false;
            playSfx(Sfx::UiClick);
        } else if (e.button.button == SDL_BUTTON_LEFT && wm.bar.contains(e.button.x, e.button.y)) {
            const float dx = wm.window.x - std::floor((winW - wm.window.w) * 0.5f);
            const float dy = wm.window.y - std::floor((winH - wm.window.h) * 0.5f);
            mapDragX = dx;
            mapDragY = dy;
            mapDragging = true;
            mapGrabX = e.button.x - dx;
            mapGrabY = e.button.y - dy;
        } else if (e.button.button == SDL_BUTTON_RIGHT && mapGroundAt(winW, winH, e.button.x, e.button.y, mgx, mgz)) {
            session.contextMenu.open = false;
            itemMenu.open = false;
            beginRightHold(mgx, mgz);
        }
        return true;
    }
    return false;
}

void App::pressOnTrade(const TradeLayout& tl, float x, float y) {
    const Inventory* npcInv = world.get<Inventory>(session.dialogueUnit);
    const bool leftButton = e.button.button == SDL_BUTTON_LEFT;
    if (leftButton && tl.cancelBtn.contains(x, y) && session.freeTrade) {
        endTalk();
    } else if (leftButton && tl.cancelBtn.contains(x, y)) {
        session.tradeOpen = false;
        clearTradeTable();
    } else if (leftButton && tl.tradeBtn.contains(x, y)) {
        if (!tradeItemsPresent(world, session.dialogueUnit, session.dialoguePlayer)) {
            tradeLabels.push_back(TradeLabel{ session.dialogueUnit, lang("MISC_TRADE_FAILED"), 0.0f });
            tradeLabels.push_back(TradeLabel{ session.dialoguePlayer, lang("MISC_TRADE_FAILED"), 0.0f });
            playSfx(Sfx::TradeFailed);
            endTalk();
        } else if (checkTrade(world, session.dialogueUnit, session.dialoguePlayer).ok) {
            const int theirGive = tradeValueOf(session.tradeNpc);
            const int yourGive = tradeValueOf(session.tradePlayer);
            auto signedValue = [](int v) { return (v > 0 ? "+" : "") + std::to_string(v); };
            if (!session.freeTrade) {
                tradeLabels.push_back(TradeLabel{ session.dialogueUnit, langf("MISC_TRADE_CHANGE", { signedValue(yourGive - theirGive) }), 0.0f });
                tradeLabels.push_back(TradeLabel{ session.dialoguePlayer, langf("MISC_TRADE_CHANGE", { signedValue(theirGive - yourGive) }), 0.0f });
            }
            completeTrade(world, session.dialogueUnit, session.dialoguePlayer);
            playSfx(Sfx::TradeDone);
            endTalk();
            showNotice(lang("MISC_TRADE_COMPLETE"), TRADE_LABEL_LIFE);
        }
    } else if (takeTradeGhostAt(session.tradeNpc, tl.theirCells, x, y)
               || takeTradeGhostAt(session.tradePlayer, tl.yourCells, x, y)) {
    } else {
        for (size_t i = 0; npcInv && i < tl.npcSlots.size(); ++i) {
            if (!tl.npcSlots[i].contains(x, y) || npcInv->slots[i].item == 0) continue;
            if (leftButton) {
                session.tradeNpcDrag = (int)i;
            } else if (e.button.button == SDL_BUTTON_RIGHT) {
                requestTrade(true, session.dialogueUnit, (int)i);
            }
            break;
        }
    }
}

bool App::pressOnTalk() {
    if (session.dialogueUnit != NO_OBJECT && screen == Screen::Playing) {
        if (session.quantity.open && session.tradeOpen && !session.quantity.toStorage) {
            if (e.button.button == SDL_BUTTON_LEFT) quantityClick(e.button.x, e.button.y);
            return true;
        }
        const float x = e.button.x, y = e.button.y;
        const TradeLayout tradeL = layoutTrade(winW, winH, renderer.pauseBarHeight(true));
        const bool onTrade = session.tradeOpen && (tradeL.interaction.contains(x, y) || tradeL.tradePanel.contains(x, y));
        const bool unitPanelClick = session.tradeOpen && !onTrade && sidebarFor(winW, winH, sidebarScroll).panel.contains(x, y);
        if (!unitPanelClick) {
            if (session.tradeOpen) {
                pressOnTrade(tradeL, x, y);
            } else if (e.button.button == SDL_BUTTON_LEFT) {
                const DialogueLayout dl = dialogueBox(winW, winH);
                if (dl.end.contains(x, y)) {
                    endTalk();
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
    if (screen == Screen::Playing && session.contextMenu.open) {
        const ItemMenuLayout ml = contextMenuLayout();
        if (ml.box.contains(e.button.x, e.button.y)) {
            const int hit = itemMenuButtonAt(ml, e.button.x, e.button.y);
            if (e.button.button == SDL_BUTTON_LEFT && hit >= 0 && hit < (int)session.contextMenu.actions.size()) {
                pickMenuAction(session.contextMenu.actions[(size_t)hit]);
            }
            return true;
        }
        session.contextMenu.open = false;
    }
    return false;
}

bool App::pressOnStoreQuantity() {
    if (screen == Screen::Playing && session.quantity.open && session.quantity.toStorage) {
        if (e.button.button == SDL_BUTTON_LEFT) quantityClick(e.button.x, e.button.y);
        return true;
    }
    return false;
}

bool App::pressOnLoot() {
    if (screen == Screen::Playing && session.lootUnit != NO_OBJECT) {
        const LootLayout ll = layoutLoot(winW, winH);
        const float x = e.button.x, y = e.button.y;
        if (ll.panel.contains(x, y)) {
            if (e.button.button == SDL_BUTTON_LEFT && ll.closeBtn.contains(x, y)) {
                closeLoot();
                playSfx(Sfx::UiBack);
            } else if (e.button.button == SDL_BUTTON_LEFT && ll.takeAllBtn.contains(x, y)) {
                if (lootAll(world, session.lootBy, session.lootUnit) == 0) refuse(lang("MISC_LOOT_NOTHING"));
            } else {
                std::string why;
                bool hitCell = false;
                for (int g = 0; g < GEAR_SLOTS && !hitCell; ++g) {
                    if (!ll.gear[(size_t)g].contains(x, y)) continue;
                    hitCell = true;
                    if (!lootTake(world, session.lootBy, session.lootUnit, true, g, why) && !why.empty()) refuse(why);
                }
                for (int i = 0; i < INVENTORY_SLOTS && !hitCell; ++i) {
                    if (!ll.slots[(size_t)i].contains(x, y)) continue;
                    hitCell = true;
                    if (!lootTake(world, session.lootBy, session.lootUnit, false, i, why) && !why.empty()) refuse(why);
                }
            }
            return true;
        }
    }
    return false;
}

bool App::pressOnStorage() {
    if (screen == Screen::Playing && session.storageProp != NO_OBJECT) {
        const StorageLayout stl = storageLayout(world, winW, winH);
        const float x = e.button.x, y = e.button.y;
        if (stl.panel.contains(x, y)) {
            if (e.button.button == SDL_BUTTON_LEFT && stl.lockToggle && stl.lockBtn.contains(x, y)) {
                if (Lock* l = world.get<Lock>(session.storageProp)) l->locked = !l->locked;
                playSfx(Sfx::UiToggle);
            } else if (e.button.button == SDL_BUTTON_LEFT && stl.closeBtn.contains(x, y)) {
                closeStorage();
                playSfx(Sfx::UiBack);
            } else {
                std::vector<Rect> cells = stl.slots;
                cells.insert(cells.end(), stl.outSlots.begin(), stl.outSlots.end());
                for (size_t i = 0; i < cells.size(); ++i) {
                    const ItemStack* at = storageSlot(world, (int)i);
                    if (!cells[i].contains(x, y) || !at || at->item == 0) continue;
                    if (e.button.button == SDL_BUTTON_LEFT) {
                        session.storageDrag = (int)i;
                    } else if (e.button.button == SDL_BUTTON_RIGHT) {
                        std::string why;
                        if (takeFromStorage(world, (int)i, why)) queueSound(world, Sfx::ItemMove);
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
    if (screen == Screen::Playing && e.button.button == SDL_BUTTON_LEFT) {
        const UnitPanelLayout fp = squadPanelLayout(winW, winH);
        const float tx = e.button.x, ty = e.button.y;
        if (fp.tabSquad.contains(tx, ty) || fp.tabFaction.contains(tx, ty)) {
            const int next = fp.tabSquad.contains(tx, ty) ? FACTION_TAB_SQUAD : FACTION_TAB_FACTION;
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
        if (factionTab == FACTION_TAB_FACTION && fp.repHeading.contains(tx, ty)) return true;
    }
    return false;
}

bool App::pressOnSquadScroll() {
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

bool App::mouseDownOnPopups() {
    if (pressOnRename()) return true;
    if (pressOnMaterialAsk()) return true;
    if (pressOnAltar()) return true;
    if (pressOnGhosts()) return true;
    if (pressOnBehaviorGroups()) return true;
    if (pressOnDisband()) return true;
    if (pressOnOptimizeAll()) return true;
    if (pressOnWorldMap()) return true;
    if (pressOnTalk()) return true;
    if (pressOnContextMenu()) return true;
    if (pressOnSelectMenu()) return true;
    if (pressOnStoreQuantity()) return true;
    if (pressOnLoot()) return true;
    if (pressOnStorage()) return true;
    if (pressOnPanelBars()) return true;
    if (pressOnFactionTabs()) return true;
    if (pressOnFactionGrid()) return true;
    if (pressOnSquadScroll()) return true;
    if (pressToGrabPanel()) return true;
    return false;
}
