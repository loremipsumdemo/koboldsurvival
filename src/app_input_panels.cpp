// Left clicks in play on the Squad Panel and the Unit Panel's sidebar (app_input.cpp has the rest of the input)
#include "app.h"

using namespace play;

// In play, a left click on the Squad Panel or the Unit Panel's sidebar. True if the click was used.
bool App::mouseDownOnPanels() {
    if (e.button.button == SDL_BUTTON_LEFT && screen == Screen::Playing) {
        // The unit panel and sidebar take the click, so it never reaches the ground
        const float mx = e.button.x, my = e.button.y;
        const std::vector<Entity> own = ownUnitList();
        const UnitPanelLayout pl = squadPanelLayout(winW, winH);

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
            // The tab's contents scroll under the bar and the tabs: only the part in the body can be clicked
            const bool inBody = sl.body.contains(mx, my);
            if (onSidebar) {
                // The Build tab: a row picks its prop to place, and picking the same one again puts it down
                if (sl.open && sl.tab == UnitTab::Build) {
                    for (size_t i = 0; inBody && i < sl.buildRows.size(); ++i) {
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
                if (shownIsYours() && inBody) {
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
