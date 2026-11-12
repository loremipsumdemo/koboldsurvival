#include "app.h"
#include "squads.h"
#include "systems.h"
#include "building.h"

using namespace play;

namespace {
constexpr unsigned long long SQUAD_DOUBLE_TAP_MS = 400;
// Select All, Select Closest, Select Idle, Select Non-Idle
constexpr int SELECT_MENU_COUNT = 4;
}

std::vector<ObjectId> App::ownUnitList() {
    return squadUnits(world, world.resource<SquadBook>().current);
}

// Optimize: the gear of the squad the Squad tab shows, with the settings the Custom window last saved
void App::optimizeSquadGear() {
    const OptimizeAllResult r = optimizeAllGear(world, ownUnitList(), gearPrefs());
    if (r.changed == 0 && r.noRoom == 0) {
        showNotice(lang("MISC_OPTIMIZE_ALL_SAME"));
    } else if (r.noRoom == 0) {
        showNotice(langf("MISC_OPTIMIZE_ALL_DONE", { std::to_string(r.changed) }));
    } else {
        showNotice(langf("MISC_OPTIMIZE_ALL_SOME_ROOM", { std::to_string(r.changed), std::to_string(r.noRoom) }));
    }
    playSfx(r.changed == 0 && r.noRoom > 0 ? Sfx::UiError : Sfx::UiClick);
}

void App::pickSquad(int squad, bool additive) {
    if (squad < 0 || squad >= SQUAD_COUNT) return;
    const unsigned long long now = SDL_GetTicks();
    const bool again = squad == lastSquadTap && now - lastSquadTapAt < SQUAD_DOUBLE_TAP_MS;
    lastSquadTap = squad;
    lastSquadTapAt = now;
    world.resource<SquadBook>().current = squad;
    unitPanelRow = 0;
    const std::vector<ObjectId> units = squadUnits(world, squad);
    session.inspectUnit = NO_OBJECT;
    if (!additive) clearSelection(world);
    for (ObjectId u : units) {
        if (!world.has<Selected>(u)) world.add(u, Selected{});
    }
    // A second press soon after the first brings the camera to the squad
    if (again && !units.empty()) focusCameraOnUnit(world, cam, units.front());
    playSfx(Sfx::UiClick);
}

void App::pickClosestUnit() {
    ObjectId best = NO_OBJECT;
    float bestD2 = 0.0f;
    for (ObjectId u : militaryUnits(world)) {
        const Transform* tr = world.get<Transform>(u);
        if (!tr) continue;
        const float d2 = distSq(tr->x, tr->z, cam.focusX, cam.focusZ);
        if (best == NO_OBJECT || d2 < bestD2) {
            best = u;
            bestD2 = d2;
        }
    }
    session.inspectUnit = NO_OBJECT;
    clearSelection(world);
    if (best == NO_OBJECT) return;
    world.add(best, Selected{});
    if (const int squad = squadOf(world, best); squad >= 0) world.resource<SquadBook>().current = squad;
    playSfx(Sfx::UiClick);
}

void App::optimizeSelectedGear() {
    const OptimizeAllResult r = optimizeAllGear(world, selectedUnits(world), gearPrefs());
    if (r.changed == 0 && r.noRoom == 0) {
        showNotice(lang("MISC_OPTIMIZE_ALL_SAME"));
    } else if (r.noRoom == 0) {
        showNotice(langf("MISC_OPTIMIZE_ALL_DONE", { std::to_string(r.changed) }));
    } else {
        showNotice(langf("MISC_OPTIMIZE_ALL_SOME_ROOM", { std::to_string(r.changed), std::to_string(r.noRoom) }));
    }
    playSfx(r.changed == 0 && r.noRoom > 0 ? Sfx::UiError : Sfx::UiClick);
}

std::vector<std::string> App::squadMenuLabels() {
    std::vector<std::string> out;
    if (squadMenu == 1) {
        for (const char* key : { "UILABEL_OPTIMIZE_SELECTED", "UILABEL_OPTIMIZE_EVERYONE", "UILABEL_OPTIMIZE_CUSTOM" }) out.push_back(lang(key));
    } else if (squadMenu == 2) {
        const char* mark = "\xE2\x80\xA2 ";
        out.push_back(std::string(session.floorManual ? "" : mark) + lang("UILABEL_FLOOR_AUTO"));
        out.push_back(std::string(session.floorManual ? mark : "") + lang("UILABEL_FLOOR_MANUAL"));
    }
    return out;
}

ItemMenuLayout App::squadMenuLayout() {
    const UnitPanelLayout pl = squadPanelLayout(winW, winH);
    const Rect b = squadMenu == 1 ? pl.optimizeAllButton : pl.floorButton;
    const int n = (int)squadMenuLabels().size();
    ItemMenuLayout L = renderer.itemMenuLayout(b.x, b.y + b.h, n, winW, winH);
    if (L.box.y < b.y + b.h - 0.5f) L = renderer.itemMenuLayout(b.x, b.y - L.box.h, n, winW, winH);
    return L;
}

void App::runSquadMenu(int choice) {
    if (squadMenu == 1) {
        if (choice == 0) {
            optimizeSelectedGear();
        } else if (choice == 1) {
            optimizeSquadGear();
        } else {
            // Custom: the window that says how
            optimizeAllOpen = true;
            optimizeSliderDrag = false;
            playSfx(Sfx::UiMenuOpen);
        }
    } else if (squadMenu == 2) {
        session.floorManual = choice == 1;
        showNotice(lang(session.floorManual ? "MISC_FLOOR_MANUAL" : "MISC_FLOOR_AUTO"), 3.0f);
        playSfx(Sfx::UiClick);
    }
}

ItemMenuLayout App::selectMenuLayout() {
    const Rect b = squadPanelLayout(winW, winH).selectButton;
    ItemMenuLayout L = renderer.itemMenuLayout(b.x, b.y + b.h, SELECT_MENU_COUNT, winW, winH);
    // No room below the button, as when the panel sits at the bottom: open upward instead
    if (L.box.y < b.y + b.h - 0.5f) L = renderer.itemMenuLayout(b.x, b.y - L.box.h, SELECT_MENU_COUNT, winW, winH);
    return L;
}

void App::runSelectMenu(int choice) {
    session.inspectUnit = NO_OBJECT;
    if (choice == 1) {
        pickClosestUnit();
        return;
    }
    clearSelection(world);
    for (ObjectId u : militaryUnits(world)) {
        const bool idle = unitIdle(world, u);
        const bool take = choice == 0 || (choice == 2 && idle) || (choice == 3 && !idle && !outCold(world, u));
        if (take) world.add(u, Selected{});
    }
    noteSquadOfSelection();
    playSfx(Sfx::UiClick);
}

bool App::pressOnSelectMenu() {
    if (pressOnFormMenu()) return true;
    if (screen == Screen::Playing && squadMenu != 0) {
        const ItemMenuLayout L = squadMenuLayout();
        const int hit = e.button.button == SDL_BUTTON_LEFT ? itemMenuButtonAt(L, e.button.x, e.button.y) : -1;
        const UnitPanelLayout pl = squadPanelLayout(winW, winH);
        const Rect button = squadMenu == 1 ? pl.optimizeAllButton : pl.floorButton;
        if (hit >= 0) runSquadMenu(hit);
        squadMenu = 0;
        // A click on the button itself only closes the menu
        if (button.contains(e.button.x, e.button.y)) return true;
        return L.box.contains(e.button.x, e.button.y);
    }
    if (screen != Screen::Playing || !selectMenuOpen) return false;
    selectMenuOpen = false;
    const ItemMenuLayout L = selectMenuLayout();
    const int hit = e.button.button == SDL_BUTTON_LEFT ? itemMenuButtonAt(L, e.button.x, e.button.y) : -1;
    if (hit >= 0) runSelectMenu(hit);
    // A click on the button itself only closes the menu
    if (squadPanelLayout(winW, winH).selectButton.contains(e.button.x, e.button.y)) return true;
    return L.box.contains(e.button.x, e.button.y);
}

void App::startSquadRename() {
    squadRenaming = true;
    squadRenameText = world.resource<SquadBook>().names[(size_t)world.resource<SquadBook>().current];
    SDL_StartTextInput(window);
    playSfx(Sfx::UiClick);
}

void App::stopSquadRename(bool keep) {
    if (!squadRenaming) return;
    squadRenaming = false;
    SDL_StopTextInput(window);
    if (!keep) return;
    SquadBook& book = world.resource<SquadBook>();
    std::string name = cleanName(squadRenameText);
    while (utf8Length(name) > SQUAD_NAME_CHARS) popUtf8Char(name);
    book.names[(size_t)book.current] = name;
}

// Numpad 1 to 9 picks that squad (Shift adds it to the selection), Ctrl + numpad puts the selected units in that
// squad, and numpad 0 picks your unit nearest the camera's pivot
bool App::squadKeyDown() {
    if (screen != Screen::Playing || editing || e.key.repeat || session.dialogueUnit != NO_OBJECT) return false;
    // By scancode, so the keys work with Num Lock on or off
    const SDL_Scancode k = e.key.scancode;
    if (k == SDL_SCANCODE_KP_0) {
        pickClosestUnit();
        return true;
    }
    int squad = -1;
    if (k >= SDL_SCANCODE_KP_1 && k <= SDL_SCANCODE_KP_9) squad = (int)(k - SDL_SCANCODE_KP_1);
    if (squad < 0) return false;
    if ((e.key.mod & SDL_KMOD_CTRL) != 0) {
        int moved = 0, full = 0;
        for (ObjectId u : selectedUnits(world)) {
            if (squadOf(world, u) == squad) continue;
            if (moveToSquad(world, u, squad)) ++moved;
            else ++full;
        }
        world.resource<SquadBook>().current = squad;
        if (full > 0) {
            refuse(langf("MISC_SQUAD_FULL_SOME", { std::to_string(squad + 1), std::to_string(moved), std::to_string(full) }));
        } else if (moved > 0) {
            showNotice(langf("MISC_SQUAD_ASSIGNED", { std::to_string(moved), std::to_string(squad + 1) }));
            playSfx(Sfx::UiClick);
        }
        return true;
    }
    pickSquad(squad, (e.key.mod & SDL_KMOD_SHIFT) != 0);
    return true;
}

// After a plain selection: when every selected unit is in one squad, the Squad tab shows that squad
void App::noteSquadOfSelection() {
    int squad = -1;
    for (ObjectId u : selectedUnits(world)) {
        const int s = squadOf(world, u);
        if (s < 0 || (squad >= 0 && s != squad)) return;
        squad = s;
    }
    if (squad >= 0 && squad != world.resource<SquadBook>().current) {
        world.resource<SquadBook>().current = squad;
        unitPanelRow = 0;
    }
}

void App::startRename(ObjectId u) {
    if (!world.has<Entity>(u) || (!world.has<Commandable>(u) && !world.has<Citizen>(u))) return;
    stopSquadRename(true);
    rename = RenamePrompt{ true, u, nameOf(world, u) };
    SDL_StartTextInput(window);
    playSfx(Sfx::UiMenuOpen);
}

void App::finishRename(bool keep) {
    if (!rename.open) return;
    const std::string name = cleanName(rename.text);
    if (keep && name.empty()) {
        refuse(lang("MISC_RENAME_EMPTY"));
        return;
    }
    if (keep && world.has<Entity>(rename.unit)) {
        if (Name* n = world.get<Name>(rename.unit)) n->text = name;
        else world.add(rename.unit, Name{ name });
    }
    rename = RenamePrompt{};
    SDL_StopTextInput(window);
    playSfx(keep ? Sfx::UiClick : Sfx::UiBack);
}

bool App::pressOnRename() {
    if (screen != Screen::Playing || !rename.open) return false;
    if (e.button.button != SDL_BUTTON_LEFT) return true;
    const RenameLayout L = layoutRename(winW, winH);
    if (L.ok.contains(e.button.x, e.button.y)) finishRename(true);
    else if (L.cancel.contains(e.button.x, e.button.y) || !L.box.contains(e.button.x, e.button.y)) finishRename(false);
    return true;
}

void App::drawRename() {
    if (!rename.open) return;
    const RenameLayout L = layoutRename(winW, winH);
    const float th = renderer.listTextHeight("Ag");
    renderer.drawScrim();
    renderer.drawThemedPanel(L.box);
    renderer.drawListText(langf("UILABEL_RENAME_UNIT", { nameOf(world, rename.unit) }), L.title.x, L.title.y + (L.title.h - th) * 0.5f,
                          RGBA{ 1.0f, 1.0f, 1.0f, 1.0f });
    renderer.drawRect(L.field, RGBA{ 0.10f, 0.04f, 0.05f, 1.0f });
    const std::string shown = rename.text + ((SDL_GetTicks() / 500) % 2 ? "|" : "");
    renderer.drawListText(shown, L.field.x + 8.0f, L.field.y + (L.field.h - th) * 0.5f, RGBA{ 1.0f, 1.0f, 1.0f, 1.0f });
    renderer.drawButton(L.ok, lang("UILABEL_CONFIRM"), !cleanName(rename.text).empty(), L.ok.contains(mouseX, mouseY));
    renderer.drawButton(L.cancel, lang("UILABEL_CANCEL"), true, L.cancel.contains(mouseX, mouseY));
}

bool App::pressOnFactionGrid() {
    if (screen != Screen::Playing) return false;
    const UnitPanelLayout fp = squadPanelLayout(winW, winH);
    const float tx = e.button.x, ty = e.button.y;
    // A click anywhere else ends the renaming, keeping the name
    const bool onName = fp.renameButton.contains(tx, ty) || fp.squadCells[(size_t)world.resource<SquadBook>().current].contains(tx, ty);
    if (squadRenaming && (!onName || factionTab != FACTION_TAB_FACTION || squadCollapsed)) stopSquadRename(true);
    if (factionTab != FACTION_TAB_FACTION || squadCollapsed || e.button.button != SDL_BUTTON_LEFT) return false;
    // Only what shows in the tab's view can be clicked
    if (!fp.view.contains(tx, ty)) return false;
    if (fp.manageSquadsButton.contains(tx, ty)) {
        openManageSquads();
        return true;
    }
    if (fp.factionRulesButton.contains(tx, ty)) {
        openFactionRules();
        return true;
    }
    if (fp.renameButton.contains(tx, ty)) {
        if (squadRenaming) stopSquadRename(true);
        else startSquadRename();
        return true;
    }
    for (int k = 0; k < SQUAD_COUNT; ++k) {
        if (!fp.squadCells[(size_t)k].contains(tx, ty)) continue;
        // A double click renames the squad the first click picked
        if (e.button.clicks == 2 && k == world.resource<SquadBook>().current) {
            if (!squadRenaming) startSquadRename();
            return true;
        }
        stopSquadRename(true);
        lastSquadTap = -1;
        pickSquad(k, (SDL_GetModState() & SDL_KMOD_SHIFT) != 0);
        return true;
    }
    return false;
}
