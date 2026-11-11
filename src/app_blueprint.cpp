#include "app.h"
#include "build_plan.h"
#include "build.h"
#include "claims.h"
#include "world_gen.h"

using namespace play;

static_assert(BLUEPRINT_MOST_FLOORS == STOREY_MAX + 1 && PLAN_FLOORS_MAX == BLUEPRINT_MOST_FLOORS, "the editor keeps every floor the game has");

// The Building Blueprints editor. It is the game with nothing in it but a flat test area and an Altar of yours, whose claim covers
// all of it so that the Build tab's tools work anywhere. Nothing is paid for or waited for: whatever is placed is built at once, a
// frame later. The plan is what stands in the box at the area's corner, read off the world when it is saved.

BlueprintLayout layoutBlueprint(int W, int H, const BlueprintUi& ui) {
    const float s = uiScaleFor((float)H);
    const float m = 10.0f * s, pad = 10.0f * s, gap = 6.0f * s, btnH = 32.0f * s, line = 28.0f * s;
    BlueprintLayout L;
    const float w = 340.0f * s;
    const float textH = 2.0f * 20.0f * s + gap;
    const float full = pad + line + gap + btnH + gap + 3.0f * (btnH + gap) + 2.0f * (btnH + gap) + textH + pad;
    const float h = ui.folded ? pad + line + pad : full;
    // Where it was put, kept on the screen; the top left corner until it is moved
    const float px = std::clamp(ui.x < 0.0f ? m : ui.x, 0.0f, std::max(0.0f, (float)W - w));
    const float py = std::clamp(ui.y < 0.0f ? m : ui.y, 0.0f, std::max(0.0f, (float)H - h));
    L.panel = Rect{ std::floor(px), std::floor(py), w, h };
    const float x = L.panel.x + pad, inner = w - 2.0f * pad;
    float y = L.panel.y + pad;
    L.title = Rect{ x, y, inner - line - gap, line };
    L.fold = Rect{ x + inner - line, y, line, line };
    if (ui.folded) return L;
    y += line + gap;
    L.nameBox = Rect{ x, y, inner, btnH };
    y += btnH + gap;
    const float small = 30.0f * s, box = 46.0f * s, labelW = 90.0f * s;
    auto stepper = [&](Rect& label, Rect& down, Rect& boxR, Rect& up) {
        label = Rect{ x, y, labelW, btnH };
        up = Rect{ x + inner - small, y, small, btnH };
        boxR = Rect{ up.x - gap - box, y, box, btnH };
        down = Rect{ boxR.x - gap - small, y, small, btnH };
        y += btnH + gap;
    };
    stepper(L.widthLabel, L.widthDown, L.widthBox, L.widthUp);
    stepper(L.depthLabel, L.depthDown, L.depthBox, L.depthUp);
    stepper(L.floorsLabel, L.floorsDown, L.floorsBox, L.floorsUp);
    const float half = (inner - gap) * 0.5f;
    L.save = Rect{ x, y, half, btnH };
    L.load = Rect{ x + half + gap, y, half, btnH };
    y += btnH + gap;
    L.clear = Rect{ x, y, half, btnH };
    L.exit = Rect{ x + half + gap, y, half, btnH };
    y += btnH + gap;
    L.status = Rect{ x, y, inner, textH };
    // The plans to open, in a list under the panel
    const float rowH = 28.0f * s;
    const int count = (int)ui.plans.size();
    const int shown = std::min(8, std::max(1, count));
    L.list = Rect{ L.panel.x, L.panel.y + L.panel.h + gap, w, 2.0f * pad + shown * rowH };
    const int top = std::clamp(ui.listScroll, 0, std::max(0, count - shown));
    for (int i = 0; i < shown && top + i < count; ++i) L.rows.push_back(Rect{ L.list.x + pad, L.list.y + pad + i * rowH, inner, rowH });
    return L;
}

// What stands in the box, whole: every floor there is, however many the floors setting lets be looked at
BuildPlan App::blueprintPlan() {
    BuildPlan plan = capturePlan(world, bpEdit.name, bpEdit.w, bpEdit.d, BLUEPRINT_MOST_FLOORS, BLUEPRINT_CORNER, BLUEPRINT_CORNER);
    int top = 0;
    for (const PlanPiece& p : plan.pieces) top = std::max(top, propInfo(p.kind).upperFloor ? p.level + 1 : p.level);
    plan.floors = std::clamp(top + 1, 1, BLUEPRINT_MOST_FLOORS);
    return plan;
}

std::string App::blueprintText() {
    return planToText(blueprintPlan());
}

void App::openBlueprintEditor() {
    audio::stopTracks();
    resetPlayState();
    rng.gen.seed((unsigned)std::time(nullptr));
    world = World{};
    terrain.open(flatChunkSource(BLUEPRINT_AREA));
    renderer.uploadTerrain(terrain);
    currentMap = MapChoice{};
    gs = GameState{};
    gs.clockSeconds = 12.0 * SECONDS_PER_HOUR;   // midday
    enterLevel();
    bpEdit = BlueprintUi{};
    bpEdit.active = true;
    // Named for a number, as a new world is: blueprint followed by digits
    bpEdit.name = lang("UILABEL_BLUEPRINT_DEFAULT_NAME") + std::to_string((uint32_t)SDL_GetTicksNS() | 1u);
    // An Altar of yours beside the box, its claim grown over the whole area: the Build tab builds wherever it is
    const ObjectId altar = placeBuiltProp(world, terrain, PropKind::Altar, 32.5f, 96.5f, 0.0f, FACTION_PLAYER);
    world.add(altar, ClaimGrowth{ 60 });
    cam.focusX = (float)(BLUEPRINT_CORNER + bpEdit.w) * 0.5f + (float)BLUEPRINT_CORNER * 0.5f;
    cam.focusZ = cam.focusX;
    cam.focusY = 0.0f;
    unitTab = UnitTab::Build;
    bpEdit.savedText = blueprintText();
}

void App::updateBlueprintEditor() {
    if (bpEdit.dragging) {
        float mx = 0.0f, my = 0.0f;
        const bool held = (SDL_GetMouseState(&mx, &my) & SDL_BUTTON_LMASK) != 0;
        if (held) {
            bpEdit.x = mx - bpEdit.grabX;
            bpEdit.y = my - bpEdit.grabY;
        } else {
            bpEdit.dragging = false;
        }
    }
    bpEdit.dirtyIn -= dt;
    if (bpEdit.dirtyIn <= 0.0f) {
        bpEdit.dirty = blueprintText() != bpEdit.savedText;
        bpEdit.dirtyIn = 0.5f;
    }
    // What was put down is built, and the ground and the walls are brought up to date
    finishAllBlueprints(world, terrain);
    refreshBuildingTiles(world, terrain);
}

void App::blueprintClear() {
    for (ObjectId e : world.view<Prop>()) {
        if (propInfo(world.get<Prop>(e)->kind).claimsLand) continue;
        world.destroy(e);
    }
    world.resource<BuildingsChanged>().dirty = true;
    hoverProp = NO_OBJECT;
    bpEdit.exitWarned = false;
    bpEdit.saveWarned = false;
    playSfx(Sfx::UiBack);
}

bool App::blueprintSave() {
    std::string name = safeFileName(bpEdit.name);
    std::replace(name.begin(), name.end(), ' ', '_');
    if (name.empty()) {
        refuse(lang("MISC_BLUEPRINT_NEEDS_NAME"));
        return false;
    }
    bpEdit.name = name;
    // Pieces outside the box are left out of the plan: said once, and saved anyway when Save is pressed again
    const int outside = planPiecesOutside(world, bpEdit.w, bpEdit.d, BLUEPRINT_CORNER, BLUEPRINT_CORNER);
    if (outside > 0 && !bpEdit.saveWarned) {
        bpEdit.saveWarned = true;
        refuse(langf("MISC_BLUEPRINT_OUTSIDE", { std::to_string(outside) }));
        return false;
    }
    bpEdit.saveWarned = false;
    const BuildPlan plan = blueprintPlan();
    const std::string problem = planProblem(plan);
    if (!problem.empty()) {
        refuse(lang(problem.c_str()));
        return false;
    }
    std::string err;
    if (!writePlanFile(planFilePath(name), plan, err)) {
        refuse(err);
        return false;
    }
    reloadPlanLibrary();
    bpEdit.savedText = planToText(plan);
    bpEdit.exitWarned = false;
    showNotice(langf("MISC_BLUEPRINT_SAVED", { name }));
    playSfx(Sfx::UiSaved);
    return true;
}

bool App::blueprintLoad(const std::string& name) {
    BuildPlan plan;
    std::string err;
    if (!readPlanFile(planFilePath(name), plan, err)) {
        refuse(err);
        return false;
    }
    blueprintClear();
    bpEdit.w = plan.w;
    bpEdit.d = plan.d;
    bpEdit.floors = BLUEPRINT_MOST_FLOORS;
    bpEdit.name = name;
    placePlanInstant(world, terrain, plan, BLUEPRINT_CORNER, BLUEPRINT_CORNER, FACTION_PLAYER);
    finishAllBlueprints(world, terrain);
    bpEdit.savedText = blueprintText();
    bpEdit.exitWarned = false;
    bpEdit.listOpen = false;
    showNotice(langf("MISC_BLUEPRINT_LOADED", { name }));
    playSfx(Sfx::UiLoaded);
    return true;
}

void App::blueprintExit() {
    if (blueprintText() != bpEdit.savedText && !bpEdit.exitWarned) {
        bpEdit.exitWarned = true;
        playSfx(Sfx::UiError);
        return;
    }
    if (bpEdit.editingName) SDL_StopTextInput(window);
    bpEdit = BlueprintUi{};
    session.buildPick = {};
    world = World{};
    leave(LeaveTo::Title);
    playSfx(Sfx::UiBack);
}

bool App::blueprintKeyDown() {
    const SDL_Keycode k = e.key.key;
    if (bpEdit.editingName) {
        if (k == SDLK_BACKSPACE) popUtf8Char(bpEdit.name);
        else if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_ESCAPE) {
            SDL_StopTextInput(window);
            bpEdit.editingName = false;
        }
        return true;
    }
    if (k == SDLK_ESCAPE) {
        if (bpEdit.listOpen) bpEdit.listOpen = false;
        else if (session.buildPick.any()) session.buildPick = {};
        else blueprintExit();
        return true;
    }
    if (k == SDLK_S && (SDL_GetModState() & SDL_KMOD_CTRL) != 0) {
        blueprintSave();
        return true;
    }
    return false;
}

bool App::pressOnBlueprint() {
    if (screen != Screen::Playing || !bpEdit.active) return false;
    const float x = e.button.x, y = e.button.y;
    const BlueprintLayout L = layoutBlueprint(winW, winH, bpEdit);
    if (!bpEdit.folded && bpEdit.listOpen && L.list.contains(x, y)) {
        if (e.button.button != SDL_BUTTON_LEFT) return true;
        const int top = std::clamp(bpEdit.listScroll, 0, std::max(0, (int)bpEdit.plans.size() - 8));
        for (size_t i = 0; i < L.rows.size(); ++i) {
            if (L.rows[i].contains(x, y)) blueprintLoad(bpEdit.plans[(size_t)top + i]);
        }
        return true;
    }
    if (!L.panel.contains(x, y)) return false;
    if (e.button.button != SDL_BUTTON_LEFT) return true;
    if (L.fold.contains(x, y)) {
        bpEdit.folded = !bpEdit.folded;
        bpEdit.listOpen = false;
        if (bpEdit.editingName) SDL_StopTextInput(window);
        bpEdit.editingName = false;
        playSfx(Sfx::UiClick);
        return true;
    }
    if (L.title.contains(x, y)) {
        bpEdit.dragging = true;
        bpEdit.x = L.panel.x;
        bpEdit.y = L.panel.y;
        bpEdit.grabX = x - L.panel.x;
        bpEdit.grabY = y - L.panel.y;
        return true;
    }
    if (bpEdit.folded) return true;
    if (L.nameBox.contains(x, y)) {
        if (!bpEdit.editingName) SDL_StartTextInput(window);
        bpEdit.editingName = true;
        playSfx(Sfx::UiClick);
        return true;
    }
    if (bpEdit.editingName) {
        SDL_StopTextInput(window);
        bpEdit.editingName = false;
    }
    auto step = [&](int& value, int lo, int hi, const Rect& down, const Rect& up) {
        const int by = down.contains(x, y) ? -1 : up.contains(x, y) ? 1 : 0;
        if (by == 0) return false;
        value = std::clamp(value + by, lo, hi);
        bpEdit.exitWarned = false;
        bpEdit.saveWarned = false;
        playSfx(Sfx::UiClick);
        return true;
    };
    if (step(bpEdit.w, PLAN_SIZE_MIN, PLAN_SIZE_MAX, L.widthDown, L.widthUp) || step(bpEdit.d, PLAN_SIZE_MIN, PLAN_SIZE_MAX, L.depthDown, L.depthUp) ||
        step(bpEdit.floors, 1, PLAN_FLOORS_MAX, L.floorsDown, L.floorsUp)) {
        return true;
    }
    if (L.save.contains(x, y)) {
        blueprintSave();
    } else if (L.load.contains(x, y)) {
        bpEdit.listOpen = !bpEdit.listOpen;
        if (bpEdit.listOpen) {
            bpEdit.plans = listPlanFiles();
            bpEdit.listScroll = 0;
            if (bpEdit.plans.empty()) {
                bpEdit.listOpen = false;
                refuse(lang("MISC_BLUEPRINT_NO_PLANS"));
            }
        }
        playSfx(Sfx::UiClick);
    } else if (L.clear.contains(x, y)) {
        blueprintClear();
    } else if (L.exit.contains(x, y)) {
        blueprintExit();
    }
    return true;
}

void App::drawBlueprintPanel() {
    if (screen != Screen::Playing || !bpEdit.active) return;
    const BlueprintLayout L = layoutBlueprint(winW, winH, bpEdit);
    const float s = uiScaleFor(winH);
    const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f }, gray{ 0.80f, 0.72f, 0.64f, 1.0f }, gold{ 1.0f, 0.78f, 0.30f, 1.0f };
    const float textH = renderer.listTextHeight("Ag");
    renderer.drawThemedPanel(L.panel);
    renderer.drawListText(renderer.fitListText(lang("UILABEL_BLUEPRINT_TITLE"), L.title.w), L.title.x, L.title.y + (L.title.h - textH) * 0.5f, gold);
    renderer.drawButton(L.fold, bpEdit.folded ? "+" : "-", true, L.fold.contains(mouseX, mouseY));
    if (bpEdit.folded) return;
    renderer.drawRect(L.nameBox, bpEdit.editingName ? RGBA{ 0.30f, 0.20f, 0.12f, 1.0f } : RGBA{ 0.14f, 0.09f, 0.08f, 1.0f });
    const std::string shown = bpEdit.name.empty() && !bpEdit.editingName ? lang("UILABEL_BLUEPRINT_NAME") : bpEdit.name + (bpEdit.editingName ? "|" : "");
    renderer.drawListText(shown, L.nameBox.x + 8.0f * s, L.nameBox.y + (L.nameBox.h - textH) * 0.5f, bpEdit.name.empty() && !bpEdit.editingName ? gray : white);
    auto stepper = [&](const char* key, int value, int lo, int hi, const Rect& label, const Rect& down, const Rect& box, const Rect& up) {
        renderer.drawListText(renderer.fitListText(lang(key), label.w), label.x, label.y + (label.h - textH) * 0.5f, gray);
        renderer.drawButton(down, "-", value > lo, down.contains(mouseX, mouseY));
        renderer.drawButton(box, std::to_string(value), true, false);
        renderer.drawButton(up, "+", value < hi, up.contains(mouseX, mouseY));
    };
    stepper("UILABEL_BLUEPRINT_WIDTH", bpEdit.w, PLAN_SIZE_MIN, PLAN_SIZE_MAX, L.widthLabel, L.widthDown, L.widthBox, L.widthUp);
    stepper("UILABEL_BLUEPRINT_DEPTH", bpEdit.d, PLAN_SIZE_MIN, PLAN_SIZE_MAX, L.depthLabel, L.depthDown, L.depthBox, L.depthUp);
    stepper("UILABEL_BLUEPRINT_FLOORS", bpEdit.floors, 1, PLAN_FLOORS_MAX, L.floorsLabel, L.floorsDown, L.floorsBox, L.floorsUp);
    const bool dirty = bpEdit.dirty;
    renderer.drawButton(L.save, lang("UILABEL_SAVE") + (dirty ? " *" : ""), true, L.save.contains(mouseX, mouseY));
    renderer.drawButton(L.load, (bpEdit.listOpen ? "> " : "") + lang("UILABEL_LOAD"), true, L.load.contains(mouseX, mouseY));
    renderer.drawButton(L.clear, lang("UILABEL_BLUEPRINT_CLEAR"), true, L.clear.contains(mouseX, mouseY));
    renderer.drawButton(L.exit, lang(bpEdit.exitWarned ? "UILABEL_BLUEPRINT_EXIT_SURE" : "UILABEL_BLUEPRINT_EXIT"), true, L.exit.contains(mouseX, mouseY));
    float y = L.status.y;
    for (const std::string& text : renderer.wrapListText(lang("UILABEL_BLUEPRINT_HELP"), L.status.w)) {
        renderer.drawListText(text, L.status.x, y, gray);
        y += textH + 2.0f * s;
    }
    if (bpEdit.listOpen) {
        renderer.drawThemedPanel(L.list);
        const int top = std::clamp(bpEdit.listScroll, 0, std::max(0, (int)bpEdit.plans.size() - 8));
        for (size_t i = 0; i < L.rows.size(); ++i) {
            renderer.drawButton(L.rows[i], renderer.fitListText(bpEdit.plans[(size_t)top + i], L.rows[i].w - 12.0f * s), true, L.rows[i].contains(mouseX, mouseY));
        }
    }
}
