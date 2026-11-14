#include "app.h"
#include "claims.h"

using namespace play;

// "You have no units selected.": a window that stays up, and can be dragged, for as long as no unit of yours is in a squad, with the ways
// back to something of yours. It goes as soon as there is one (that unit is selected for you, see selectReturningMilitary).

bool App::noUnitsShown() {
    return screen == Screen::Playing && gameActive && !editing && !bpEdit.active && militaryUnits(world).empty();
}

bool App::pressOnNoUnits() {
    if (!noUnitsShown()) return false;
    const float x = e.button.x, y = e.button.y;
    const NoUnitsLayout L = layoutNoUnits(winW, winH, noUnits.x, noUnits.y);
    if (!L.panel.contains(x, y)) return false;
    if (e.button.button != SDL_BUTTON_LEFT) return true;
    if (L.title.contains(x, y)) {
        noUnits.moving = true;
        noUnits.x = L.panel.x;
        noUnits.y = L.panel.y;
        noUnits.grabX = x - L.panel.x;
        noUnits.grabY = y - L.panel.y;
        return true;
    }
    const bool altarButton = L.altar.contains(x, y), unitButton = L.unit.contains(x, y), claimButton = L.claim.contains(x, y);
    if (!altarButton && !unitButton && !claimButton) return true;

    // The nearest of each to where the camera is, which is where they would send it from
    float bestAltar = 0.0f, bestClaim = 0.0f, bestUnit = 0.0f;
    const Claim *altar = nullptr, *claim = nullptr;
    const std::vector<Claim> claims = claimsOf(world);
    for (const Claim& c : claims) {
        if (c.owner != FACTION_PLAYER) continue;
        const float d = std::hypot(c.x - cam.focusX, c.z - cam.focusZ);
        if (!claim || d < bestClaim) {
            claim = &c;
            bestClaim = d;
        }
        if (!c.camp && (!altar || d < bestAltar)) {
            altar = &c;
            bestAltar = d;
        }
    }
    ObjectId unit = NO_OBJECT;
    for (ObjectId u : citizenUnits(world)) {
        const Transform* t = world.get<Transform>(u);
        if (!t) continue;
        const float d = std::hypot(t->x - cam.focusX, t->z - cam.focusZ);
        if (unit == NO_OBJECT || d < bestUnit) {
            unit = u;
            bestUnit = d;
        }
    }
    const Claim* target = altarButton ? altar : claimButton ? claim : nullptr;
    if (unitButton ? unit == NO_OBJECT : target == nullptr) {
        playSfx(Sfx::UiError);   // nothing of that kind to go to
        return true;
    }
    followUnit = NO_OBJECT;
    if (unitButton) {
        focusCameraOnUnit(world, cam, unit);
    } else {
        cam.focusX = target->x;
        cam.focusZ = target->z;
        cam.clampState();
    }
    playSfx(Sfx::UiClick);
    return true;
}

// Called every frame: the window follows the pointer while its title bar is held
void App::updateNoUnits() {
    if (!noUnits.moving) return;
    float mx = 0.0f, my = 0.0f;
    if (noUnitsShown() && (SDL_GetMouseState(&mx, &my) & SDL_BUTTON_LMASK)) {
        noUnits.x = mx - noUnits.grabX;
        noUnits.y = my - noUnits.grabY;
    } else {
        noUnits.moving = false;
    }
}

void App::drawNoUnitsPanel() {
    if (!noUnitsShown()) return;
    const NoUnitsLayout L = layoutNoUnits(winW, winH, noUnits.x, noUnits.y);
    const float th = renderer.listTextHeight("Ag");
    renderer.drawThemedPanel(L.panel);
    renderer.drawListText(renderer.fitListText(lang("UILABEL_NO_UNITS_TITLE"), L.title.w), L.title.x, L.title.y + (L.title.h - th) * 0.5f,
                          RGBA{ 1.0f, 1.0f, 1.0f, 1.0f });
    renderer.drawListText(renderer.fitListText(lang("UILABEL_NO_UNITS_TEXT"), L.message.w), L.message.x,
                          L.message.y + (L.message.h - th) * 0.5f, theme::TEXT_DIM);

    bool haveAltar = false, haveClaim = false;
    for (const Claim& c : claimsOf(world)) {
        if (c.owner != FACTION_PLAYER) continue;
        haveClaim = true;
        if (!c.camp) haveAltar = true;
    }
    const bool haveUnit = !citizenUnits(world).empty();
    const float mx = (float)mouseX, my = (float)mouseY;
    renderer.drawButton(L.altar, renderer.fitListText(lang("UILABEL_NO_UNITS_ALTAR"), L.altar.w - 12.0f), haveAltar, haveAltar && L.altar.contains(mx, my));
    renderer.drawButton(L.unit, renderer.fitListText(lang("UILABEL_NO_UNITS_UNIT"), L.unit.w - 12.0f), haveUnit, haveUnit && L.unit.contains(mx, my));
    renderer.drawButton(L.claim, renderer.fitListText(lang("UILABEL_NO_UNITS_CLAIM"), L.claim.w - 12.0f), haveClaim, haveClaim && L.claim.contains(mx, my));
}
