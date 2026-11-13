#include "app.h"
#include "claims.h"

using namespace play;

namespace {
int tileOf(float v) { return (int)std::floor(v); }
}

// Set Perimeter: a window of tools over the world, and tiles painted on the ground by dragging a rectangle, as Deconstruct is
// dragged out. The drawing is on a copy, kept only by Confirm.

void App::openPerimeter(ObjectId altarProp) {
    const Claim* held = nullptr;
    const std::vector<Claim> claims = claimsOf(world);
    for (const Claim& c : claims) {
        if (c.altar == altarProp) held = &c;
    }
    if (!held || held->owner != FACTION_PLAYER) return;
    perim = PerimeterUi{};
    perim.active = true;
    perim.altar = altarProp;
    perim.cx = held->x;
    perim.cz = held->z;
    perim.radius = held->radius;
    if (const Perimeter* p = world.get<Perimeter>(altarProp)) perim.draft = p->tiles;
    else perim.draft = defaultPerimeter(world, altarProp);
    session.buildPick = {};
    session.contextMenu.open = false;
    itemMenu.open = false;
    showNotice(lang("UILABEL_PERIMETER_HINT"), 6.0f);
    playSfx(Sfx::UiMenuOpen);
}

void App::closePerimeter() {
    perim = PerimeterUi{};
}

// Tiles of the draft inside the claim
void App::paintPerimeter(int x0, int z0, int x1, int z1, bool erase) {
    for (int z = std::min(z0, z1); z <= std::max(z0, z1); ++z) {
        for (int x = std::min(x0, x1); x <= std::max(x0, x1); ++x) {
            if (erase) {
                perim.draft.erase({ x, z });
                continue;
            }
            const float dx = (float)x + 0.5f - perim.cx, dz = (float)z + 0.5f - perim.cz;
            if (dx * dx + dz * dz > perim.radius * perim.radius || inPerimeterCore(world, perim.altar, x, z)) continue;
            if (!terrain.inBounds(x, z)) continue;
            perim.draft.insert({ x, z });
        }
    }
}

bool App::pressOnPerimeter() {
    if (screen != Screen::Playing || !perim.active) return false;
    if (!world.has<Prop>(perim.altar)) {
        closePerimeter();   // the Altar is gone
        return false;
    }
    const float x = e.button.x, y = e.button.y;
    const PerimeterLayout L = layoutPerimeter(winW, winH, perim.x, perim.y);
    if (L.panel.contains(x, y)) {
        if (e.button.button != SDL_BUTTON_LEFT) return true;
        if (L.title.contains(x, y)) {
            perim.moving = true;
            perim.x = L.panel.x;
            perim.y = L.panel.y;
            perim.grabX = x - L.panel.x;
            perim.grabY = y - L.panel.y;
            return true;
        } else if (L.draw.contains(x, y)) {
            perim.erase = false;
        } else if (L.erase.contains(x, y)) {
            perim.erase = true;
        } else if (L.reset.contains(x, y)) {
            perim.draft = defaultPerimeter(world, perim.altar);   // back to every tile of the claim
        } else if (L.confirm.contains(x, y)) {
            if (perim.draft == defaultPerimeter(world, perim.altar)) {
                world.remove<Perimeter>(perim.altar);   // the default: every tile, as the claim grows
            } else {
                Perimeter p;
                p.tiles = perim.draft;
                world.add(perim.altar, std::move(p));
            }
            showNotice(lang("MISC_PERIMETER_SET"));
            closePerimeter();
        } else if (L.cancel.contains(x, y)) {
            closePerimeter();
            playSfx(Sfx::UiBack);
            return true;
        } else {
            return true;
        }
        playSfx(Sfx::UiClick);
        return true;
    }
    // Only the left button paints; the middle one still moves the camera, and the right one is kept from giving orders
    if (e.button.button != SDL_BUTTON_LEFT) return e.button.button == SDL_BUTTON_RIGHT;
    if (const auto hit = buildPickAt(x, y)) {
        perim.drag = true;
        perim.dragX = tileOf(hit->first);
        perim.dragZ = tileOf(hit->second);
    }
    return true;
}

// Called every frame: the window follows the pointer while its title bar is held
void App::updatePerimeter() {
    if (!perim.active || !perim.moving) return;
    float mx = 0.0f, my = 0.0f;
    if (SDL_GetMouseState(&mx, &my) & SDL_BUTTON_LMASK) {
        perim.x = mx - perim.grabX;
        perim.y = my - perim.grabY;
    } else {
        perim.moving = false;
    }
}

bool App::releasePerimeter() {
    if (perim.moving) {
        perim.moving = false;
        return true;
    }
    if (!perim.active || !perim.drag) return false;
    perim.drag = false;
    float mx = 0.0f, my = 0.0f;
    SDL_GetMouseState(&mx, &my);
    if (const auto hit = buildPickAt(mx, my)) paintPerimeter(perim.dragX, perim.dragZ, tileOf(hit->first), tileOf(hit->second), perim.erase);
    return true;
}

// The tiles as marks on the ground, and the rectangle being dragged
void App::perimeterPreview(BuildPreview& bp, float mx, float my) {
    if (!perim.active) return;
    // What is drawn, and the core that is always there at half the strength
    for (const auto& [x, z] : perim.draft) bp.marks.push_back({ x, z, terrain.heightAt(x + 0.5f, z + 0.5f) + 0.12f, 3 });
    for (const auto& [x, z] : perimeterOf(world, perim.altar)) {
        if (inPerimeterCore(world, perim.altar, x, z)) bp.marks.push_back({ x, z, terrain.heightAt(x + 0.5f, z + 0.5f) + 0.12f, 5 });
    }
    if (perim.drag) {
        if (const auto hit = buildPickAt(mx, my)) {
            bp.rect = true;
            bp.rx0 = std::min(perim.dragX, tileOf(hit->first));
            bp.rx1 = std::max(perim.dragX, tileOf(hit->first));
            bp.rz0 = std::min(perim.dragZ, tileOf(hit->second));
            bp.rz1 = std::max(perim.dragZ, tileOf(hit->second));
            bp.valid = !perim.erase;
        }
    }
    bp.ringR = perim.radius;
    bp.ringTone = 3;
    bp.x = perim.cx;
    bp.z = perim.cz;
}

void App::drawPerimeterPanel() {
    if (!perim.active) return;
    const PerimeterLayout L = layoutPerimeter(winW, winH, perim.x, perim.y);
    const float th = renderer.listTextHeight("Ag");
    renderer.drawThemedPanel(L.panel);
    renderer.drawListText(lang("UILABEL_PERIMETER_TITLE"), L.title.x, L.title.y + (L.title.h - th) * 0.5f, RGBA{ 1.0f, 1.0f, 1.0f, 1.0f });
    const float mx = (float)mouseX, my = (float)mouseY;
    renderer.drawButton(L.draw, lang("UILABEL_PERIMETER_DRAW"), true, !perim.erase || L.draw.contains(mx, my));
    renderer.drawButton(L.erase, lang("UILABEL_PERIMETER_ERASE"), true, perim.erase || L.erase.contains(mx, my));
    renderer.drawButton(L.reset, lang("UILABEL_PERIMETER_RESET"), true, L.reset.contains(mx, my));
    renderer.drawButton(L.confirm, lang("UILABEL_PERIMETER_CONFIRM"), true, L.confirm.contains(mx, my));
    renderer.drawButton(L.cancel, lang("UILABEL_PERIMETER_CANCEL"), true, L.cancel.contains(mx, my));
}
