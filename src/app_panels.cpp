#include "app.h"

using namespace play;

std::vector<Entity> App::ownUnitList() {
    std::vector<Entity> units = world.view<Unit, Transform, Commandable>();
    std::sort(units.begin(), units.end());
    return units;
}

bool App::sidebarUp() { return sidebarUnitOf(world) != NULL_ENTITY; }

std::vector<BehaviorOption> App::behaviorOptionsFor(Entity u) {
    std::vector<BehaviorOption> out;
    const Behaviors* b = world.get<Behaviors>(u);
    if (!b) return out;
    auto offer = [&](const BehaviorRule& r, std::string label) {
        BehaviorOption o;
        o.rule = r;
        o.label = std::move(label);
        o.have = holdsBehavior(b->rules, r);
        out.push_back(std::move(o));
    };
    for (BehaviorKind k : addableKinds(world.has<Mob>(u))) {
        BehaviorRule r;
        r.kind = k;
        offer(r, behaviorName(r));
    }
    const std::vector<Claim>& claims = claimsOf(world);
    std::array<bool, PROP_KIND_COUNT> harvestable{};
    std::vector<std::pair<float, Entity>> stores;
    const Transform* ut = world.get<Transform>(u);
    for (Entity p : world.view<Prop, Transform>()) {
        const Transform* pt = world.get<Transform>(p);
        if (!claimAt(claims, pt->x, pt->z, FACTION_PLAYER)) continue;
        const PropKind kind = world.get<Prop>(p)->kind;
        if (idx(kind) >= 0 && idx(kind) < PROP_KIND_COUNT && yieldsItems(kind)) harvestable[(size_t)idx(kind)] = true;
        if (world.has<Storage>(p)) {
            const float d = ut ? std::hypot(pt->x - ut->x, pt->z - ut->z) : 0.0f;
            stores.push_back({ d, p });
        }
    }
    // Ones the unit holds count too, so a list can always show them, wherever their props are now
    for (const BehaviorRule& r : b->rules) {
        if (r.kind == BehaviorKind::AutoHarvest && r.propKind >= 0 && r.propKind < PROP_KIND_COUNT) harvestable[(size_t)r.propKind] = true;
    }
    for (int k = 0; k < PROP_KIND_COUNT; ++k) {
        if (!harvestable[(size_t)k]) continue;
        BehaviorRule r;
        r.kind = BehaviorKind::AutoHarvest;
        r.propKind = k;
        offer(r, behaviorName(r));
    }
    std::sort(stores.begin(), stores.end());
    constexpr size_t STORES_SHOWN = 8;   // the nearest ones, so the list stays short in a big camp
    for (size_t i = 0; i < stores.size() && i < STORES_SHOWN; ++i) {
        BehaviorRule r;
        r.kind = BehaviorKind::AutoHaul;
        r.prop = stores[i].second;
        r.propKind = idx(world.get<Prop>(r.prop)->kind);
        offer(r, behaviorName(r) + "  (" + std::to_string((int)std::lround(stores[i].first)) + "m)");
    }
    return out;
}

SidebarLayout App::sidebarFor(int W, int H, float scroll) {
    const Entity shown = sidebarUnitOf(world);
    const Behaviors* b = shown != NULL_ENTITY ? world.get<Behaviors>(shown) : nullptr;
    // The Behavior tab's list is for one unit: another one shown shuts it
    if (shown != behaviorUiUnit) {
        behaviorUiUnit = shown;
        behaviorAddOpen = false;
    }
    const bool editable = shown != NULL_ENTITY && world.has<Commandable>(shown);
    BehaviorUi bui;
    std::vector<BehaviorOption> options;
    if (b && editable && unitTab == UnitTab::Behavior) {
        if (behaviorAddOpen) {
            options = behaviorOptionsFor(shown);
            bui.options = (int)options.size();
        }
    }
    SidebarLayout L = layoutInventorySidebar(W, H, b ? (int)b->rules.size() : 0, scroll, unitOpen, unitTab, unitPlace,
                                             renderer.profileCardHeight(H), bui);
    if (!editable || unitTab != UnitTab::Behavior) L.behaviorAdd = Rect{ 0.0f, 0.0f, 0.0f, 0.0f };   // nothing to add to
    L.behaviorOptionInfo = std::move(options);
    // A mob has no gear slots, nor gear buttons, so nothing there can be clicked or dragged. Its inventory is
    // Not available until it is a corpse: no slots, and no sort button.
    if (shown != NULL_ENTITY && world.has<Mob>(shown)) {
        L.gear.clear();
        L.optimizeButton = L.removeAllButton = Rect{ 0.0f, 0.0f, 0.0f, 0.0f };
        if (!world.has<Corpse>(shown)) {
            L.slots.clear();
            L.sortButton = Rect{ 0.0f, 0.0f, 0.0f, 0.0f };
        }
    }
    return L;
}

bool App::shownIsYours() {
    const Entity shown = sidebarUnitOf(world);
    return shown != NULL_ENTITY && world.has<Commandable>(shown);
}

void App::sortInventory(Entity unit) {
    Inventory* inv = world.get<Inventory>(unit);
    if (!inv) return;
    sortInventoryByType(*inv);
    playSfx(Sfx::UiClick);
}

void App::behaviorClick(Entity unit, const BehaviorHit& hit) {
    Behaviors* b = world.get<Behaviors>(unit);
    if (!b || hit.row < 0 || hit.row >= (int)b->rules.size()) return;
    const size_t row = (size_t)hit.row;
    BehaviorRule& r = b->rules[row];
    switch (hit.part) {
    case BehaviorPart::Toggle:
        r.on = !r.on;
        r.cooldown = 0.0f;
        if (r.on) b->wait = 0.0f;   // an automated behavior switched on looks for its step at once
        break;
    case BehaviorPart::Number:
        // Eat, Heal and Rest: the prompt asks for the percent, once
        if (!hasPercent(r.kind)) return;
        percentPrompt = PercentPrompt{};
        percentPrompt.open = true;
        percentPrompt.unit = unit;
        percentPrompt.row = (int)row;
        percentPrompt.kind = r.kind;
        percentPrompt.text = std::to_string(r.percent);   // what it is now, to change or type over
        SDL_StartTextInput(window);
        playSfx(Sfx::UiClick);
        return;
    case BehaviorPart::Up:
    case BehaviorPart::Down:
        if (!moveBehavior(b->rules, row, hit.part == BehaviorPart::Up ? -1 : 1)) return;
        break;
    case BehaviorPart::Remove:
        if (!removeBehavior(b->rules, row)) return;
        break;
    case BehaviorPart::None:
        return;
    }
    b->pending = true;   // Eat and Heal are checked again, so the change takes effect at once
    playSfx(Sfx::UiClick);
}

void App::answerPercentPrompt(bool keep) {
    if (!percentPrompt.open) return;
    if (keep) {
        Behaviors* b = world.get<Behaviors>(percentPrompt.unit);
        const int row = percentPrompt.row;
        // The list may have changed under it (the unit fell, was traded away): then there is nothing to set
        if (!b || row < 0 || row >= (int)b->rules.size() || b->rules[(size_t)row].kind != percentPrompt.kind) {
            keep = false;
        } else {
            const PercentRange range = percentRange(percentPrompt.kind);
            const int value = percentPrompt.text.empty() ? -1 : std::atoi(percentPrompt.text.c_str());
            if (!percentInRange(percentPrompt.kind, value)) {
                percentPrompt.error = langf("UILABEL_BEHAVIOR_PROMPT_BAD", { std::to_string(range.lo), std::to_string(range.hi) });
                playSfx(Sfx::UiBack);
                return;
            }
            BehaviorRule& r = b->rules[(size_t)row];
            if (setPercent(r, value)) {
                r.cooldown = 0.0f;
                b->pending = true;   // checked again at once
            }
        }
    }
    percentPrompt = PercentPrompt{};
    SDL_StopTextInput(window);
    playSfx(keep ? Sfx::UiClick : Sfx::UiBack);
}

bool App::behaviorExtraClick(Entity unit, const SidebarLayout& sl, float x, float y) {
    const BehaviorExtraHit hit = behaviorExtraHitAt(sl, x, y);
    if (hit.add) {
        behaviorAddOpen = !behaviorAddOpen;
        playSfx(Sfx::UiClick);
        return true;
    }
    if (hit.option >= 0) {
        const std::vector<BehaviorOption> options = behaviorOptionsFor(unit);
        Behaviors* b = world.get<Behaviors>(unit);
        if (!b || hit.option >= (int)options.size()) return true;
        const BehaviorOption& o = options[(size_t)hit.option];
        if (!addBehavior(b->rules, o.rule)) {
            refuse(langf("MISC_BEHAVIOR_LIST_FULL", { std::to_string(BEHAVIOR_LIMIT) }));
            return true;
        }
        // Patrol Spawn walks about where it was given, for a unit that was not placed by a spawn
        if (o.rule.kind == BehaviorKind::PatrolSpawn && !world.has<SpawnHome>(unit)) {
            if (const Transform* t = world.get<Transform>(unit)) world.add(unit, SpawnHome{ t->x, t->z, false });
        }
        b->pending = true;
        b->wait = 0.0f;   // an automated behavior looks for its step at once
        showNotice(langf(o.have ? "MISC_BEHAVIOR_SWITCHED_ON" : "MISC_BEHAVIOR_ADDED", { behaviorName(o.rule) }));
        playSfx(Sfx::UiClick);
        return true;
    }
    return false;
}

bool App::unitPanelHovered(int W, int H) {
    float mx = 0.0f, my = 0.0f;
    SDL_GetMouseState(&mx, &my);
    return squadPanelLayout(W, H).panel.contains(mx, my);
}

std::vector<std::string> App::menuLabels() {
    std::vector<std::string> labels;
    const Inventory* inv = world.get<Inventory>(itemMenu.unit);
    if (!itemMenu.open || !inv || itemMenu.slot < 0 || itemMenu.slot >= INVENTORY_SLOTS) return labels;
    const ItemDef* def = findItem(inv->slots[itemMenu.slot].item);
    if (!def) return labels;
    if (def->typeId == "consumable" && !def->onUse.empty()) labels.push_back(lang("UILABEL_USE"));
    labels.push_back(lang("UILABEL_DROP"));
    return labels;
}

void App::useSlot(Entity unit, int slot) {
    const Inventory* inv = world.get<Inventory>(unit);
    if (!inv || slot < 0 || slot >= INVENTORY_SLOTS) return;
    const std::string itemName = itemNameOf(inv->slots[slot].item);
    switch (useStackInSlot(world, unit, slot)) {
    case UseResult::Used:
        showNotice(langf("MISC_USED", { itemName }));
        break;
    case UseResult::NotUsable:
        refuse(itemName + " can't be used");
        break;
    case UseResult::Empty:
        break;
    }
}

GameLogLayout App::gameLogPanel(int W, int H) {
    return renderer.gameLogLayout(W, H, infoTab, logFilterOpen, logOffset,
                                    (int)infoLines(gameLog, infoTab, logFilter).size(), combatCollapsed, logPlace);
}

DialogueLayout App::dialogueBox(int W, int H) {
    const Dialogue* dg = world.get<Dialogue>(dialogueUnit);
    const int lines = (int)std::min<size_t>(2, dialogueConvo.size());
    return layoutDialogue(W, H, lines, dg ? (int)dg->options.size() : 0);
}

bool App::onTalkOrTrade(int W, int H, float x, float y) {
    if (dialogueUnit == NULL_ENTITY) return false;
    if (tradeOpen) {
        const TradeLayout tl = layoutTrade(W, H, renderer.pauseBarHeight(false));
        return tl.interaction.contains(x, y) || tl.tradePanel.contains(x, y);
    }
    return dialogueBox(W, H).box.contains(x, y);
}

bool App::infoPanelClick(int W, int H, float tx, float ty) {
    const GameLogLayout cl = gameLogPanel(W, H);
    if (cl.collapsed) return false;
    if (cl.tabMessage.contains(tx, ty)) {
        if (infoTab != 0) { infoTab = 0; logOffset = 0; }
        logFilterOpen = false;
        return true;
    }
    if (cl.tabGame.contains(tx, ty)) {
        if (infoTab != 1) { infoTab = 1; logOffset = 0; }
        logFilterOpen = false;
        return true;
    }
    if (cl.tabTime.contains(tx, ty)) {
        if (infoTab != 2) { infoTab = 2; logOffset = 0; }
        logFilterOpen = false;
        return true;
    }
    if (infoTab == 1 && cl.filterBtn.contains(tx, ty)) {
        logFilterOpen = !logFilterOpen;
        return true;
    }
    if (cl.filterOpen) {
        for (const GameLogLayout::FilterRow& fr : cl.filterRows) {
            if (fr.heading || !fr.r.contains(tx, ty)) continue;
            logFilter.on[(int)fr.kind] = !logFilter.on[(int)fr.kind];
            return true;
        }
        if (cl.filterMenu.contains(tx, ty)) return true;
        logFilterOpen = false;   // a click elsewhere closes the menu, and still does its own job
    }
    return false;
}

bool App::gameLogHovered(int W, int H) {
    float mx = 0.0f, my = 0.0f;
    SDL_GetMouseState(&mx, &my);
    return gameLogPanel(W, H).panel.contains(mx, my);
}

WorldMapLayout App::worldMapFor(int W, int H) {
    return layoutWorldMap(W, H, mapDragX, mapDragY);
}

bool App::mapGroundAt(int W, int H, float x, float y, float& gx, float& gz) {
    if (!mapOpen) return false;
    return mapGround(worldMapFor(W, H).map, mapCx, mapCz, mapSpan, (float)terrain.size(), x, y, gx, gz);
}

void App::markMove(float x, float z) {
    moveMarkX = x;
    moveMarkZ = z;
    moveMarkTime = MOVE_MARK_SECONDS;
}

void App::beginRightHold(float x, float z) {
    const std::vector<Entity> units = selectedUnits(world);
    if (units.empty()) return;
    noteLockedIn(units);   // a unit locked in a cage stays put
    rightHold = true;
    holdX = x;
    holdZ = z;
    actionX = x;
    actionZ = z;
    actionAlpha = 1.0f;
    markMove(x, z);
    steerUnits(world, units, Vec2{ x, z });
    playSfx(Sfx::UiOrder);
}

void App::moveSelectedTo(float x, float z) {
    const std::vector<Entity> units = selectedUnits(world);
    if (units.empty()) return;
    issueMoveOrder(world, terrain, units, Vec2{ x, z });
    actionX = x;
    actionZ = z;
    actionAlpha = 1.0f;
    markMove(x, z);
}

bool App::overPanels(float x, float y, int W, int H) {
    const std::vector<std::string> labels = menuLabels();
    return overGamePanels(squadPanelLayout(W, H),
                          sidebarFor(W, H, sidebarScroll), true, x, y)
        || gameLogPanel(W, H).panel.contains(x, y)
        || (lootUnit != NULL_ENTITY && layoutLoot(W, H).panel.contains(x, y))
        || (mapOpen && worldMapFor(W, H).window.contains(x, y))
        || (!labels.empty() && renderer.itemMenuLayout(itemMenu.x, itemMenu.y, (int)labels.size(), W, H).box.contains(x, y));
}

bool App::holdTarget(int W, int H, float x, float y, float& gx, float& gz) {
    if (mapGroundAt(W, H, x, y, gx, gz)) return true;
    if (overPanels(x, y, W, H)) return false;
    auto hit = pickGround(cam, terrain, x, y, W, H);
    if (!hit) return false;
    gx = hit->first;
    gz = hit->second;
    return true;
}

bool App::pointerOverPanels(int W, int H) {
    float mx = 0.0f, my = 0.0f;
    SDL_GetMouseState(&mx, &my);
    return overPanels(mx, my, W, H);
}

UnitPanelLayout App::squadPanelLayout(int W, int H) {
    return layoutUnitPanel(W, H, (int)ownUnitList().size(), unitPanelRow, squadCollapsed, squadPlace, factionTab,
                           (int)reputationFactions().size(), repScroll);
}
