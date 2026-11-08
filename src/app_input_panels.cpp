#include "building.h"
#include "app.h"

using namespace play;

bool App::mouseDownOnPanels() {
    if (e.button.button == SDL_BUTTON_LEFT && screen == Screen::Playing) {
        const float mx = e.button.x, my = e.button.y;
        const std::vector<ObjectId> own = ownUnitList();
        const UnitPanelLayout pl = squadPanelLayout(winW, winH);

        if (itemMenu.open) {
            const std::vector<std::string> labels = menuLabels();
            if (labels.empty()) {
                itemMenu.open = false;
            } else {
                const ItemMenuLayout ml = renderer.itemMenuLayout(itemMenu.x, itemMenu.y, (int)labels.size(), winW, winH);
                const int hit = itemMenuButtonAt(ml, mx, my);
                const bool onMenu = ml.box.contains(mx, my);
                const ObjectId unit = itemMenu.unit;
                const int slot = itemMenu.slot;
                itemMenu.open = false;
                if (hit >= 0 && labels[hit] == lang("UILABEL_USE")) {
                    useSlot(unit, slot);
                    playSfx(Sfx::UiClick);
                } else if (hit >= 0 && labels[hit] == lang("UILABEL_DROP")) {
                    if (!dropStack(world, unit, slot)) playSfx(Sfx::UiError);
                }
                if (onMenu) return true;
            }
        }

        {
            const SidebarLayout st = sidebarFor(winW, winH, sidebarScroll);
            bool tabHit = false;
            for (int t = 0; t < UNIT_TAB_COUNT && st.open; ++t) {
                if (st.tabs[t].contains(mx, my)) {
                    if (!unitTabEnabled(world, sidebarUnitOf(world), (UnitTab)t)) {
                        tabHit = true;
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
            if (pl.paceButton.contains(mx, my)) { runMode = !runMode; playSfx(Sfx::UiToggle); return true; }
            if (pl.selectButton.contains(mx, my)) {
                session.contextMenu.open = false;
                itemMenu.open = false;
                selectMenuOpen = true;
                playSfx(Sfx::UiMenuOpen);
                return true;
            }
            if (pl.optimizeAllButton.contains(mx, my)) {
                session.contextMenu.open = false;
                itemMenu.open = false;
                if ((SDL_GetModState() & SDL_KMOD_SHIFT) != 0) {
                    optimizeAllOpen = true;
                    optimizeSliderDrag = false;
                    playSfx(Sfx::UiMenuOpen);
                } else {
                    optimizeSquadGear();
                }
                return true;
            }

            const SidebarLayout sl = sidebarFor(winW, winH, sidebarScroll);
            const bool onSidebar = sl.panel.contains(mx, my);
            const bool inBody = sl.body.contains(mx, my);
            if (onSidebar) {
                if (sl.open && sl.tab == UnitTab::Build) {
                    for (size_t i = 0; inBody && i < sl.buildRows.size(); ++i) {
                        if (!sl.buildRows[i].contains(mx, my)) continue;
                        const BuildPick pick = buildRowPick((int)i);
                        if (!pick.any()) break;
                        if (!buildAnywhere(pick) && !selectionInClaim(world)) break;
                        if (session.buildPick == pick) {
                            session.buildPick = {};
                        } else if (pick.placing() && takesMaterial(pick.kind)) {
                            // A foundation, wall or door: first, what it is made of
                            session.buildPick = {};
                            session.askMaterial = true;
                            session.askKind = pick.kind;
                        } else {
                            session.buildPick = pick;
                            showNotice(lang(!pick.placing() ? "UILABEL_BUILD_DESTROY_HINT"
                                                                  : "UILABEL_BUILD_PLACE_HINT"), 4.0f);
                        }
                        playSfx(Sfx::UiClick);
                        break;
                    }
                    return true;
                }
                if (shownIsYours() && inBody) {
                    const ObjectId shown = sidebarUnitOf(world);
                    if (sl.sortButton.contains(mx, my)) {
                        itemMenu.open = false;
                        if (!inventoryLocked(world, shown)) sortInventory(shown);
                        return true;
                    }
                    if (sl.optimizeButton.contains(mx, my) || sl.removeAllButton.contains(mx, my)) {
                        itemMenu.open = false;
                        const bool optimize = sl.optimizeButton.contains(mx, my);
                        std::string why;
                        const GearAction done = optimize ? optimizeGear(world, shown, why, gearPrefs()) : removeAllGear(world, shown, why);
                        if (done == GearAction::Done) {
                            queueSound(world, Sfx::ItemMove);
                        } else if (done == GearAction::NoRoom) {
                            refuse(why);
                        } else {
                            showNotice(lang(optimize ? "MISC_GEAR_ALREADY_BEST" : "MISC_GEAR_NONE_WORN"));
                        }
                        return true;
                    }
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
                                    if (Combat* c = world.get<Combat>(shown)) c->target = NO_OBJECT;
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

                const std::vector<ObjectId> picked = panelUnits(world);
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
                const ObjectId u = own[c.index];
                const SDL_Keymod mods = SDL_GetModState();
                const bool shift = (mods & SDL_KMOD_SHIFT) != 0;
                const bool additive = (mods & (SDL_KMOD_SHIFT | SDL_KMOD_ALT)) != 0;
                session.inspectUnit = NO_OBJECT;
                if (shift && world.has<Selected>(u)) {
                    if (selectedUnits(world).size() > 1) world.remove<Selected>(u);
                } else {
                    if (!additive) clearSelection(world);
                    if (!world.has<Selected>(u)) world.add(u, Selected{});
                }
                if (e.button.clicks == 2) {
                    focusCameraOnUnit(world, cam, u);
                    followUnit = u;
                }
                break;
            }
            return true;
        }

        if (session.buildPick.any() && session.dialogueUnit == NO_OBJECT && !mapOpen) {
            if (!session.buildPick.placing()) {
                ObjectId target = NO_OBJECT;
                float ex = 0.0f, ey = 0.0f, ez = 0.0f, best = 0.0f;
                cam.eye(ex, ey, ez);
                for (ObjectId e : renderer.propsAtPixel(world, cam, mx, my, winW, winH)) {
                    const Transform* pt = world.get<Transform>(e);
                    const float d2 = (pt->x - ex) * (pt->x - ex) + (pt->y - ey) * (pt->y - ey) + (pt->z - ez) * (pt->z - ez);
                    if (target == NO_OBJECT || d2 < best) {
                        target = e;
                        best = d2;
                    }
                }
                const Owner* targetOwner = target != NO_OBJECT && !destroyedByHand(world.get<Prop>(target)->kind) ? world.get<Owner>(target) : nullptr;
                const int owner = targetOwner ? targetOwner->faction : FACTION_PLAYER;
                if (target != NO_OBJECT && session.storageProp == target) closeStorage();
                if (target != NO_OBJECT && destroyBuilding(world, target)) {
                    if (owner != FACTION_PLAYER) changePlayerReputation(world, owner, -REP_DESTROY_PENALTY);
                    playSfx(Sfx::Drop);
                } else {
                    refuse(lang("MISC_DESTROY_NOTHING"));
                }
            } else if (auto hit = buildPickAt(mx, my)) {
                if (buildPressAt(hit->first, hit->second)) return true;
                float bx = 0.0f, bz = 0.0f;
                snapBuildSpot(session.buildPick.kind, hit->first, hit->second, bx, bz);
                const BuildCheck why = checkBuildSpot(world, terrain, session.buildPick.kind, bx, bz);
                if (why == BuildCheck::Ok) {
                    placeBuiltProp(world, terrain, session.buildPick.kind, bx, bz, 0.0f);
                    playSfx(Sfx::UiClick);
                } else {
                    refuse(lang(buildRefusalKey(why)));
                }
            }
            return true;
        }

        {
            const bool alt = (SDL_GetModState() & SDL_KMOD_ALT) != 0;
            const ObjectId item = renderer.groundItemAtPixel(world, terrain, cam, mx, my, winW, winH, alt);
            if (item != NO_OBJECT) {
                const std::vector<ObjectId> picked = selectedUnits(world);
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
