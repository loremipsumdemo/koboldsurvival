#include "app.h"
#include "build_plan.h"
#include "building.h"
#include "claims.h"
#include "npc_builders.h"
#include "spatial.h"

using namespace play;

// The Build tab's Building Blueprint popout: the saved plans (the ones the game can build) in a scrolling list on the left, and on
// the right the selected one drawn as a model, what it is and costs, and what to make it of. Place Blueprint takes it up: it follows
// the pointer as green or red tiles, R turns it, and a click lays its foundation as blueprints in your claim. Your citizens build it
// like any building, and the rest of it is laid stage by stage as each is finished, by the same system that builds NPC houses.

static_assert((int)BuildMaterial::Count <= 4, "the popout has room for four materials");

PlanPopupLayout layoutPlanPopup(int W, int H, int count, int scroll) {
    const float s = uiScaleFor((float)H);
    const float pad = 12.0f * s, gap = 8.0f * s, line = 24.0f * s, rowH = 28.0f * s;
    PlanPopupLayout L;
    const float w = std::min((float)W - 40.0f * s, 900.0f * s), h = std::min((float)H - 40.0f * s, 560.0f * s);
    L.panel = Rect{ std::floor(((float)W - w) * 0.5f), std::floor(((float)H - h) * 0.5f), w, h };
    float y = L.panel.y + pad;
    L.title = Rect{ L.panel.x + pad, y, w - 2.0f * pad - 36.0f * s, 30.0f * s };
    L.close = Rect{ L.panel.x + w - pad - 30.0f * s, y, 30.0f * s, 30.0f * s };
    y += 30.0f * s + gap;
    const float listW = std::max(180.0f * s, w * 0.34f);
    L.list = Rect{ L.panel.x + pad, y, listW, L.panel.y + h - pad - y };
    L.shown = std::max(1, (int)(L.list.h / rowH));
    L.total = count;
    L.first = std::clamp(scroll, 0, std::max(0, count - L.shown));
    const bool bar = count > L.shown;
    const float barW = 12.0f * s;
    if (bar) {
        L.track = Rect{ L.list.x + L.list.w - barW, L.list.y, barW, L.list.h };
        const float thumbH = std::max(24.0f * s, L.track.h * (float)L.shown / (float)count);
        const float travel = L.track.h - thumbH;
        L.thumb = Rect{ L.track.x, L.track.y + travel * (float)L.first / (float)(count - L.shown), barW, thumbH };
    }
    for (int i = 0; i < L.shown && L.first + i < count; ++i) {
        L.rows.push_back(Rect{ L.list.x, L.list.y + (float)i * rowH, L.list.w - (bar ? barW + 2.0f * s : 0.0f), rowH });
    }
    const float rx = L.list.x + listW + pad, rw = L.panel.x + w - pad - rx;
    const float previewH = L.list.h * 0.5f;
    L.preview = Rect{ rx, y, rw, previewH };
    float ry = y + previewH + gap;
    L.info = Rect{ rx, ry, rw, 4.0f * line };
    ry += 4.0f * line + gap;
    const int n = (int)BuildMaterial::Count;
    const float mw = (rw - (float)(n - 1) * gap) / (float)n;
    for (int i = 0; i < n; ++i) L.mats[i] = Rect{ rx + (float)i * (mw + gap), ry, mw, 30.0f * s };
    L.place = Rect{ rx, L.list.y + L.list.h - 34.0f * s, rw, 34.0f * s };
    return L;
}

void App::togglePlanPopup() {
    if (planPopup.open) {
        closePlanPopup();
        return;
    }
    reloadPlanLibrary();   // plans saved since
    planPopup.open = true;
    planPopup.selected = 0;
    planPopup.scroll = 0;
    planPopup.material = std::clamp(session.buildMaterial, 0, (int)BuildMaterial::Count - 1);
    renderer.forgetPlanPreview();
    playSfx(Sfx::UiMenuOpen);
}

void App::closePlanPopup() {
    if (!planPopup.open) return;
    planPopup.open = false;
    renderer.forgetPlanPreview();
    playSfx(Sfx::UiBack);
}

bool App::planPopupKey() {
    if (screen != Screen::Playing || !planPopup.open) return false;
    const SDL_Keycode k = e.key.key;
    const int count = (int)planLibrary().size();
    if (k == SDLK_ESCAPE) {
        closePlanPopup();
        return true;
    }
    if ((k == SDLK_UP || k == SDLK_DOWN) && count > 0) {
        planPopup.selected = std::clamp(planPopup.selected + (k == SDLK_DOWN ? 1 : -1), 0, count - 1);
        const PlanPopupLayout L = layoutPlanPopup(winW, winH, count, planPopup.scroll);
        // Kept in view
        if (planPopup.selected < L.first) planPopup.scroll = planPopup.selected;
        else if (planPopup.selected >= L.first + L.shown) planPopup.scroll = planPopup.selected - L.shown + 1;
        playSfx(Sfx::UiClick);
        return true;
    }
    return false;
}

void App::scrollPlanPopup(float wheel) {
    const int count = (int)planLibrary().size();
    const PlanPopupLayout L = layoutPlanPopup(winW, winH, count, planPopup.scroll);
    planPopup.scroll = std::clamp(L.first + (wheel > 0.0f ? -2 : wheel < 0.0f ? 2 : 0), 0, std::max(0, count - L.shown));
}

bool App::pressOnPlanPopup() {
    if (screen != Screen::Playing || !planPopup.open) return false;
    const float x = e.button.x, y = e.button.y;
    const std::vector<BuildPlan>& lib = planLibrary();
    const int count = (int)lib.size();
    const PlanPopupLayout L = layoutPlanPopup(winW, winH, count, planPopup.scroll);
    // A press outside the popout puts it away
    if (!L.panel.contains(x, y)) {
        closePlanPopup();
        return true;
    }
    if (e.button.button != SDL_BUTTON_LEFT) return true;
    if (L.close.contains(x, y)) {
        closePlanPopup();
        return true;
    }
    for (size_t i = 0; i < L.rows.size(); ++i) {
        if (!L.rows[i].contains(x, y)) continue;
        planPopup.selected = L.first + (int)i;
        playSfx(Sfx::UiClick);
        return true;
    }
    if (L.total > L.shown && L.track.contains(x, y)) {
        // The scroll bar's track: the list goes to where it was pressed
        const float along = std::clamp((y - L.track.y) / std::max(1.0f, L.track.h), 0.0f, 1.0f);
        planPopup.scroll = (int)std::lround(along * (float)(L.total - L.shown));
        return true;
    }
    if (count == 0) return true;
    for (int i = 0; i < (int)BuildMaterial::Count; ++i) {
        if (!L.mats[i].contains(x, y)) continue;
        planPopup.material = (int)MATERIAL_TIERS[i];
        playSfx(Sfx::UiClick);
        return true;
    }
    if (L.place.contains(x, y)) startPlanPick();
    return true;
}

void App::drawPlanPopup() {
    if (screen != Screen::Playing || !planPopup.open) return;
    const std::vector<BuildPlan>& lib = planLibrary();
    const int count = (int)lib.size();
    planPopup.selected = std::clamp(planPopup.selected, 0, std::max(0, count - 1));
    const PlanPopupLayout L = layoutPlanPopup(winW, winH, count, planPopup.scroll);
    planPopup.scroll = L.first;
    const float s = uiScaleFor((float)winH);
    const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f }, gray{ 0.80f, 0.72f, 0.64f, 1.0f }, gold{ 1.0f, 0.78f, 0.30f, 1.0f };
    const RGBA well{ 0.14f, 0.09f, 0.08f, 1.0f };
    const float textH = renderer.listTextHeight("Ag");
    renderer.drawScrim();
    renderer.drawThemedPanel(L.panel);
    renderer.drawListText(renderer.fitListText(lang("UILABEL_PLAN_TITLE"), L.title.w), L.title.x, L.title.y + (L.title.h - textH) * 0.5f, gold);
    renderer.drawCloseButton(L.close);

    renderer.drawRect(L.list, well);
    if (count == 0) {
        const std::string none = lang("UILABEL_PLAN_NONE");
        float ty = L.list.y + 8.0f * s;
        for (const std::string& t : renderer.wrapListText(none, L.list.w - 16.0f * s)) {
            renderer.drawListText(t, L.list.x + 8.0f * s, ty, gray);
            ty += textH + 2.0f * s;
        }
        renderer.drawRect(L.preview, well);
        return;
    }
    for (size_t i = 0; i < L.rows.size(); ++i) {
        const int at = L.first + (int)i;
        const Rect& row = L.rows[i];
        const bool picked = at == planPopup.selected, hot = row.contains(mouseX, mouseY);
        if (picked) renderer.drawRect(row, theme::alpha(theme::GOLD_DEEP, 0.85f));
        else if (hot) renderer.drawRect(row, RGBA{ 1.0f, 1.0f, 1.0f, 0.08f });
        renderer.drawListText(renderer.fitListText(lib[(size_t)at].name, row.w - 12.0f * s), row.x + 6.0f * s, row.y + (row.h - textH) * 0.5f,
                              picked ? white : gray);
    }
    if (L.total > L.shown) {
        renderer.drawRect(L.track, RGBA{ 0.0f, 0.0f, 0.0f, 0.35f });
        renderer.drawRect(L.thumb, theme::alpha(theme::GOLD_DEEP, 0.9f));
    }

    const BuildPlan& plan = lib[(size_t)planPopup.selected];
    renderer.drawRect(L.preview, well);
    renderer.drawPlanPreview(plan, L.preview, (float)SDL_GetTicks() * 0.0004f);
    const PlanCost cost = planCost(plan);
    const float line = 24.0f * s;
    auto info = [&](int row, const std::string& text, const RGBA& tint) {
        renderer.drawListText(renderer.fitListText(text, L.info.w), L.info.x, L.info.y + (float)row * line, tint);
    };
    info(0, plan.name, gold);
    info(1, langf("UILABEL_PLAN_SIZE", { std::to_string(plan.w), std::to_string(plan.d), std::to_string(plan.floors) }), white);
    info(2, langf("UILABEL_PLAN_COST", { std::to_string(cost.material), std::to_string(cost.logs) }), gray);
    info(3, lang(plan.npcAllowed ? "UILABEL_PLAN_NPCS_ON" : "UILABEL_PLAN_NPCS_OFF"), gray);
    for (int i = 0; i < (int)BuildMaterial::Count; ++i) {
        const bool on = planPopup.material == (int)MATERIAL_TIERS[i];
        renderer.drawButton(L.mats[i], (on ? "> " : "") + lang(materialNameKey(MATERIAL_TIERS[i])), true, L.mats[i].contains(mouseX, mouseY));
    }
    renderer.drawButton(L.place, lang("UILABEL_PLAN_PLACE"), true, L.place.contains(mouseX, mouseY));
}

// ---- Laying one

void App::startPlanPick() {
    const std::vector<BuildPlan>& lib = planLibrary();
    if (planPopup.selected < 0 || planPopup.selected >= (int)lib.size()) return;
    planPick = PlanPick{};
    planPick.active = true;
    planPick.plan = lib[(size_t)planPopup.selected];
    planPick.material = planPopup.material;
    session.buildPick = {};
    session.buildMaterial = planPopup.material;
    planPopup.open = false;
    renderer.forgetPlanPreview();
    showNotice(lang("MISC_PLAN_PLACE_HINT"), 5.0f);
    playSfx(Sfx::UiClick);
}

void App::cancelPlanPick() {
    planPick = PlanPick{};
}

namespace {
// The plan turned as it is asked to be, worked out once for each turn
void ensureTurned(App::PlanPick& pick) {
    if (pick.turnedFor == pick.turns) return;
    pick.turned = planTurned(pick.plan, pick.turns);
    pick.footprint = planFootprint(pick.turned);
    pick.rects = planFoundationRects(pick.turned);
    pick.turnedFor = pick.turns;
}
}

// The corner of the plan's box with its middle at the pointer
bool App::planPickOrigin(float mx, float my, int& x, int& z) {
    const auto hit = pickGround(cam, terrain, mx, my, winW, winH);
    if (!hit) return false;
    ensureTurned(planPick);
    x = (int)std::floor(hit->first - (float)planPick.turned.w * 0.5f + 0.5f);
    z = (int)std::floor(hit->second - (float)planPick.turned.d * 0.5f + 0.5f);
    return true;
}

// Whether the plan's foundation may be laid with its corner here: all of its box inside your claim (as for NPC houses), and the
// ground clear for the foundation. Kept for the same spot until the buildings change or a quarter second passes.
bool App::planSpotFree(const BuildPlan& turned, int x, int z, const std::vector<Claim>& claims, bool cached) {
    PlanSpotCache& c = planSpot;
    const std::uint64_t props = world.revision<Prop>(), foundations = world.revision<Foundation>();
    const Uint64 now = SDL_GetTicks();
    if (cached && c.filled && c.x == x && c.z == z && c.turns == planPick.turns && c.props == props && c.foundations == foundations &&
        now - c.tick < 250) {
        return c.ok;
    }
    bool inside = bpEdit.active;
    if (!inside) {
        const float cx = (float)x + (float)turned.w * 0.5f, cz = (float)z + (float)turned.d * 0.5f;
        if (const Claim* held = claimAt(claims, cx, cz, FACTION_PLAYER)) {
            inside = true;
            const float px[2] = { (float)x, (float)(x + turned.w) }, pz[2] = { (float)z, (float)(z + turned.d) };
            for (int i = 0; i < 2; ++i) {
                for (int j = 0; j < 2; ++j) inside = inside && distSq(px[i], pz[j], held->x, held->z) <= (held->radius - 1.0f) * (held->radius - 1.0f);
            }
        }
    }
    c.ok = inside && planFoundationFits(world, terrain, planPick.rects, x, z, FACTION_PLAYER);
    c.filled = true;
    c.x = x;
    c.z = z;
    c.turns = planPick.turns;
    c.props = props;
    c.foundations = foundations;
    c.tick = now;
    return c.ok;
}

// The plan's foundation under the pointer, tile by tile: green where it may be laid, red where not
void App::previewPlanPick(BuildPreview& bp, const std::vector<Claim>& claims) {
    float mx = 0.0f, my = 0.0f;
    SDL_GetMouseState(&mx, &my);
    int x = 0, z = 0;
    if (!planPickOrigin(mx, my, x, z)) return;
    const BuildPlan& turned = planPick.turned;
    const bool ok = planSpotFree(turned, x, z, claims, true);
    for (int j = 0; j < turned.d; ++j) {
        for (int i = 0; i < turned.w; ++i) {
            if (!planPick.footprint[(size_t)i + (size_t)j * (size_t)turned.w]) continue;
            bp.marks.push_back(BuildPreview::Mark{ x + i, z + j, terrain.heightAt((float)(x + i) + 0.5f, (float)(z + j) + 0.5f) + 0.08f, ok ? 1 : 2, false });
        }
    }
}

bool App::placePlanPick(float mx, float my) {
    int x = 0, z = 0;
    if (!planPickOrigin(mx, my, x, z)) return true;
    const std::vector<Claim> claims = claimsOf(world);
    if (!planSpotFree(planPick.turned, x, z, claims, false)) {
        refuse(lang("MISC_PLAN_BLOCKED"));
        return true;
    }
    const ObjectId anchor = placePlanFoundation(world, terrain, planPick.rects, x, z, FACTION_PLAYER, (BuildMaterial)planPick.material);
    if (anchor == NO_OBJECT) {
        refuse(lang("MISC_PLAN_BLOCKED"));
        return true;
    }
    // Where it is and how far it has come: the next stage of it is laid when this one is built (npcBuilderSystem)
    PlanSite site;
    site.plan = planPick.plan.name;
    site.x = x;
    site.z = z;
    site.turns = planPick.turns;
    site.material = planPick.material;
    world.add(anchor, site);
    showNotice(langf("MISC_PLAN_LAID", { planPick.plan.name }));
    playSfx(Sfx::UiOrder);
    cancelPlanPick();
    return true;
}
