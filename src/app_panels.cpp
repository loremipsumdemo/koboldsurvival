#include "building.h"
#include "app.h"

using namespace play;

bool App::sidebarUp() { return sidebarUnitOf(world) != NO_OBJECT; }

std::vector<BehaviorOption> App::behaviorOptionsFor(ObjectId u) {
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
    std::vector<char> harvestable((size_t)propKindCount(), 0);
    std::vector<std::pair<float, ObjectId>> stores;
    const Transform* ut = world.get<Transform>(u);
    for (ObjectId p : world.view<Prop, Transform>()) {
        const Transform* pt = world.get<Transform>(p);
        if (!claimAt(claims, pt->x, pt->z, FACTION_PLAYER)) continue;
        const PropKind kind = world.get<Prop>(p)->kind;
        if (idx(kind) >= 0 && idx(kind) < propKindCount() && yieldsItems(kind)) harvestable[(size_t)idx(kind)] = true;
        if (world.has<Storage>(p)) {
            const float d = ut ? std::hypot(pt->x - ut->x, pt->z - ut->z) : 0.0f;
            stores.push_back({ d, p });
        }
    }
    for (const BehaviorRule& r : b->rules) {
        if (r.kind == BehaviorKind::AutoHarvest && r.propKind >= 0 && r.propKind < propKindCount()) harvestable[(size_t)r.propKind] = true;
    }
    for (int k = 0; k < propKindCount(); ++k) {
        if (!harvestable[(size_t)k]) continue;
        BehaviorRule r;
        r.kind = BehaviorKind::AutoHarvest;
        r.propKind = k;
        offer(r, behaviorName(r));
    }
    std::sort(stores.begin(), stores.end());
    constexpr size_t STORES_SHOWN = 8;
    for (size_t i = 0; i < stores.size() && i < STORES_SHOWN; ++i) {
        BehaviorRule r;
        r.kind = BehaviorKind::AutoHaul;
        r.prop = stores[i].second;
        r.propKind = idx(world.get<Prop>(r.prop)->kind);
        const std::string away = "  (" + std::to_string((int)std::lround(stores[i].first)) + "m)";
        // An Altar burns bones: Auto-Haul Bones keeps it going
        if (!propInfo(world.get<Prop>(r.prop)->kind).claimsLand) offer(r, behaviorName(r) + away);
        if (propInfo(world.get<Prop>(r.prop)->kind).claimsLand) {
            r.kind = BehaviorKind::AutoHaulBones;
            offer(r, behaviorName(r) + away);
        }
    }
    return out;
}

SidebarLayout App::sidebarFor(int W, int H, float scroll) {
    const ObjectId shown = sidebarUnitOf(world);
    const Behaviors* b = shown != NO_OBJECT ? world.get<Behaviors>(shown) : nullptr;
    if (shown != behaviorUiUnit) {
        behaviorUiUnit = shown;
        behaviorAddOpen = false;
    }
    const bool editable = shown != NO_OBJECT && world.has<Commandable>(shown);
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
    if (!editable || unitTab != UnitTab::Behavior) L.behaviorAdd = Rect{ 0.0f, 0.0f, 0.0f, 0.0f };
    L.behaviorOptionInfo = std::move(options);
    if (shown != NO_OBJECT && world.has<Mob>(shown)) {
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
    const ObjectId shown = sidebarUnitOf(world);
    return shown != NO_OBJECT && world.has<Commandable>(shown);
}

void App::sortInventory(ObjectId unit) {
    Inventory* inv = world.get<Inventory>(unit);
    if (!inv) return;
    sortInventoryByType(*inv);
    playSfx(Sfx::UiClick);
}

void App::behaviorClick(ObjectId unit, const BehaviorHit& hit) {
    Behaviors* b = world.get<Behaviors>(unit);
    if (!b || hit.row < 0 || hit.row >= (int)b->rules.size()) return;
    const size_t row = (size_t)hit.row;
    BehaviorRule& r = b->rules[row];
    switch (hit.part) {
    case BehaviorPart::Toggle:
        r.on = !r.on;
        r.cooldown = 0.0f;
        if (r.on) b->wait = 0.0f;
        break;
    case BehaviorPart::Number:
        if (!hasPercent(r.kind)) return;
        percentPrompt = PercentPrompt{};
        percentPrompt.open = true;
        percentPrompt.unit = unit;
        percentPrompt.row = (int)row;
        percentPrompt.kind = r.kind;
        percentPrompt.text = std::to_string(r.percent);
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
    b->pending = true;
    playSfx(Sfx::UiClick);
}

void App::answerPercentPrompt(bool keep) {
    if (!percentPrompt.open) return;
    if (keep) {
        Behaviors* b = world.get<Behaviors>(percentPrompt.unit);
        const int row = percentPrompt.row;
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
                b->pending = true;
            }
        }
    }
    percentPrompt = PercentPrompt{};
    SDL_StopTextInput(window);
    playSfx(keep ? Sfx::UiClick : Sfx::UiBack);
}

bool App::behaviorExtraClick(ObjectId unit, const SidebarLayout& sl, float x, float y) {
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
        if (o.rule.kind == BehaviorKind::AutoHarvest) addHaulForHarvest(world, unit, o.rule.propKind);
        if (o.rule.kind == BehaviorKind::PatrolSpawn && !world.has<SpawnHome>(unit)) {
            if (const Transform* t = world.get<Transform>(unit)) world.add(unit, SpawnHome{ t->x, t->z, false });
        }
        b->pending = true;
        b->wait = 0.0f;
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

void App::useSlot(ObjectId unit, int slot) {
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
                                    (int)infoLineCount(gameLog, infoTab, logFilter), combatCollapsed, logPlace);
}

DialogueLayout App::dialogueBox(int W, int H) {
    const Dialogue* dg = world.get<Dialogue>(session.dialogueUnit);
    const int lines = (int)std::min<size_t>(2, session.dialogueConvo.size());
    return layoutDialogue(W, H, lines, dg ? (int)dg->options.size() : 0);
}

bool App::onTalkOrTrade(int W, int H, float x, float y) {
    if (session.dialogueUnit == NO_OBJECT) return false;
    if (session.tradeOpen) {
        const TradeLayout tl = layoutTrade(W, H, renderer.pauseBarHeight(true));
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
            settings.logFilter = logFilterText(logFilter);
            saveSettingsFile();
            return true;
        }
        if (cl.filterMenu.contains(tx, ty)) return true;
        logFilterOpen = false;
    }
    // The * at the start of a line of the Game tab: the camera goes to where the message happened
    if (infoTab == 1) {
        const std::vector<const LogLine*> rows = infoLines(gameLog, infoTab, logFilter);
        for (int k = 0; k < cl.shown && cl.first + k < (int)rows.size(); ++k) {
            const int row = cl.linesVisible - cl.shown + k;
            if (!Rect{ cl.textX, cl.rowsTop + row * cl.lineH, cl.starW, cl.lineH }.contains(tx, ty)) continue;
            const LogLine& line = *rows[(size_t)(cl.first + k)];
            if (line.located) {
                goToLogLine(line);
                playSfx(Sfx::UiClick);
            }
            return true;
        }
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

FormationChoice App::formationChoice() const {
    return FormationChoice{ (FormationShape)settings.formationShape, settings.formationLoose };
}

// Alt and a number: picks the shape, or tight or loose, for the selected units and every order after
bool App::formationKeyDown() {
    const SDL_Keycode k = e.key.key;
    if (k < SDLK_1 || k > SDLK_5) return false;
    applyFormationKey((int)(k - SDLK_1));
    return true;
}

void App::applyFormationKey(int index) {
    if (index >= 0 && index < (int)FormationShape::Count) settings.formationShape = index;
    else if (index == (int)FormationShape::Count) settings.formationLoose = !settings.formationLoose;
    else return;
    saveSettingsFile();
    const FormationChoice choice = formationChoice();
    world.resource<FormationChoice>() = choice;
    reformUnits(world, terrain, selectedUnits(world), choice);
    showNotice(langf("MISC_FORMATION", { lang(formationLabelKey(choice.shape)), lang(choice.loose ? "UILABEL_FORMATION_LOOSE" : "UILABEL_FORMATION_TIGHT") }));
    playSfx(Sfx::UiOrder);
}

// What the Form list shows, a row each: the four shapes, tight or loose, and placing by a drag
std::vector<Renderer::FormationHelpEntry> App::formationEntries() {
    static constexpr int PICTURE_DOTS[(int)FormationShape::Count] = { 8, 10, 12, 10 };
    const FormationChoice choice = formationChoice();
    std::vector<Renderer::FormationHelpEntry> entries;
    for (int i = 0; i < (int)FormationShape::Count; ++i) {
        const FormationShape shape = (FormationShape)i;
        entries.push_back({ "Alt+" + std::to_string(i + 1), lang(formationLabelKey(shape)), formationOffsets(shape, PICTURE_DOTS[i], 1.0f), shape == choice.shape });
    }
    // The picture is what the key changes it to
    entries.push_back({ "Alt+" + std::to_string((int)FormationShape::Count + 1),
                        lang(choice.loose ? "UILABEL_FORMATION_MAKE_TIGHT" : "UILABEL_FORMATION_MAKE_LOOSE"),
                        formationOffsets(FormationShape::Line, 4, choice.loose ? 1.0f : 2.0f), false });
    entries.push_back({ "Alt+" + std::string(lang("UILABEL_FORMATION_DRAG_KEY")), lang("UILABEL_FORMATION_DRAG"), {}, false });
    return entries;
}

Renderer::FormationMenuLayout App::formMenuLayout() {
    const Rect b = squadPanelLayout(winW, winH).formButton;
    Renderer::FormationMenuLayout L = renderer.formationMenuLayout(b, (int)FormationShape::Count + 2, formMenuScroll, winH);
    formMenuScroll = L.first;
    return L;
}

// A click while the Form list is open: a row picks it; either way the list closes, except on its own empty space
bool App::pressOnFormMenu() {
    if (screen != Screen::Playing || squadMenu != SQUAD_MENU_FORM) return false;
    const float mx = e.button.x, my = e.button.y;
    const Renderer::FormationMenuLayout L = formMenuLayout();
    if (e.button.button == SDL_BUTTON_LEFT) {
        for (size_t r = 0; r < L.rows.size(); ++r) {
            const int index = L.first + (int)r;
            if (!L.rows[r].contains(mx, my) || index > (int)FormationShape::Count) continue;
            squadMenu = 0;
            applyFormationKey(index);
            return true;
        }
    }
    if (L.box.contains(mx, my)) return true;
    squadMenu = 0;
    // A click on the button itself only closes the list
    return squadPanelLayout(winW, winH).formButton.contains(mx, my);
}

bool App::wheelOnFormMenu() {
    if (screen != Screen::Playing || squadMenu != SQUAD_MENU_FORM) return false;
    if (!formMenuLayout().box.contains(e.wheel.mouse_x, e.wheel.mouse_y)) return false;
    const int step = e.wheel.y > 0.0f ? -1 : e.wheel.y < 0.0f ? 1 : 0;
    formMenuScroll = std::max(0, formMenuScroll + step);
    formMenuLayout();
    return true;
}

void App::drawFormMenu() {
    if (squadMenu != SQUAD_MENU_FORM || editing || screen != Screen::Playing) return;
    if (squadCollapsed || factionTab != FACTION_TAB_SQUAD) {
        squadMenu = 0;
        return;
    }
    renderer.drawFormationMenu(formMenuLayout(), formationEntries(), mouseX, mouseY);
}

void App::finishFormationDrag() {
    formationDrag = false;
    const std::vector<ObjectId> units = flyAirshipsTo(selectedUnits(world), dragToX, dragToZ);
    if (units.empty()) return;
    Vec2 point{};
    const FormationSpec spec = formationFromDrag(world, units, Vec2{ dragFromX, dragFromZ }, Vec2{ dragToX, dragToZ }, formationChoice(), point);
    issueFormationOrder(world, terrain, units, point, spec);
    actionX = point.x;
    actionZ = point.z;
    actionStorey = clickStorey;
    actionAlpha = 1.0f;
    markMove(point.x, point.z);
    playSfx(Sfx::UiOrder);
}

void App::beginRightHold(float x, float z) {
    const std::vector<ObjectId> picked = selectedUnits(world);
    if (picked.empty()) return;
    const std::vector<ObjectId> units = flyAirshipsTo(picked, x, z);
    if (units.empty()) {
        actionX = x;
        actionZ = z;
        actionStorey = clickStorey;
        actionAlpha = 1.0f;
        markMove(x, z);
        playSfx(Sfx::UiOrder);
        // Units on or going to an upper floor: the hold still moves them with the pointer
        bool walking = false;
        for (ObjectId u : picked) walking = walking || airshipOf(world, u) == NO_OBJECT;
        if (walking) {
            holdPlanned = levelKey((int)std::floor(x), (int)std::floor(z), clickStorey);
            rightHold = true;
            holdX = x;
            holdZ = z;
        }
        return;
    }
    noteLockedIn(units);
    holdPlanned = levelKey((int)std::floor(x), (int)std::floor(z), clickStorey);
    rightHold = true;
    holdX = x;
    holdZ = z;
    actionX = x;
    actionZ = z;
    actionStorey = clickStorey;
    actionAlpha = 1.0f;
    markMove(x, z);
    steerUnits(world, units, Vec2{ x, z });
    playSfx(Sfx::UiOrder);
}

// Selected units aboard an Airship fly it to the point; the rest are left to walk
std::vector<ObjectId> App::flyAirshipsTo(const std::vector<ObjectId>& units, float x, float z) {
    std::vector<ObjectId> walkers;
    for (ObjectId u : units) {
        const ObjectId ship = airshipOf(world, u);
        if (ship != NO_OBJECT) flyAirship(world, ship, Vec2{ x, z });
        else if (world.has<Upstairs>(u) || clickStorey > 0) moveToFloor(world, terrain, u, Vec2{ x, z }, clickStorey);   // by way of the ladders
        else walkers.push_back(u);
    }
    return walkers;
}

void App::moveSelectedTo(float x, float z) {
    const std::vector<ObjectId> picked = selectedUnits(world);
    if (picked.empty()) return;
    const std::vector<ObjectId> units = flyAirshipsTo(picked, x, z);
    if (!units.empty()) issueMoveOrder(world, terrain, units, Vec2{ x, z });
    actionX = x;
    actionZ = z;
    actionStorey = clickStorey;
    actionAlpha = 1.0f;
    markMove(x, z);
}

bool App::overPanels(float x, float y, int W, int H) {
    if (planPopup.open) return true;   // the Building Blueprint popout takes the whole screen's clicks
    if (perim.active && layoutPerimeter(W, H, perim.x, perim.y).panel.contains(x, y)) return true;
    if (planPick.active && sidebarFor(W, H, sidebarScroll).panel.contains(x, y)) return true;
    if (bpEdit.active) {
        const BlueprintLayout bl = layoutBlueprint(W, H, bpEdit);
        if (bl.panel.contains(x, y) || (bpEdit.listOpen && !bpEdit.folded && bl.list.contains(x, y)) ||
            ((bpEdit.overwriteAsk || !bpEdit.deleteName.empty()) && !bpEdit.folded && bl.confirm.contains(x, y))) {
            return true;
        }
    }
    if (editing && editor.useOwnUi) {
        const EditorLayout editorLayout = layoutEditor(W, H, true);
        return editorLayout.panel.contains(x, y) || editorLayout.uiToggle.contains(x, y) || (mapOpen && worldMapFor(W, H).window.contains(x, y));
    }
    const std::vector<std::string> labels = menuLabels();
    return overGamePanels(squadPanelLayout(W, H),
                          sidebarFor(W, H, sidebarScroll), true, x, y)
        || gameLogPanel(W, H).panel.contains(x, y)
        || (logFilterOpen && infoTab == 1 && gameLogPanel(W, H).filterMenu.contains(x, y))
        || (session.lootUnit != NO_OBJECT && layoutLoot(W, H).panel.contains(x, y))
        || (mapOpen && worldMapFor(W, H).window.contains(x, y))
        || (!labels.empty() && renderer.itemMenuLayout(itemMenu.x, itemMenu.y, (int)labels.size(), W, H, labels).box.contains(x, y));
}

bool App::mouseOverPanels() {
    if (!mouseOnPanels) mouseOnPanels = overPanels(mouseX, mouseY, winW, winH);
    return *mouseOnPanels;
}

bool App::holdTarget(int W, int H, float x, float y, float& gx, float& gz) {
    if (mapGroundAt(W, H, x, y, gx, gz)) {
        clickStorey = 0;
        return true;
    }
    if (overPanels(x, y, W, H)) return false;
    int storey = 0;
    auto hit = pickFloor(x, y, storey);
    if (hit) clickStorey = storey;
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
                           reputationFactionCount(), factionScroll[std::clamp(factionSub, 0, 2)], factionSub);
}
