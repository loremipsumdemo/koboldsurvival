#include "app.h"
#include "building.h"
#include <unordered_set>
#include "claims.h"

using namespace play;

// The Build tab's foundations and the pieces built on them. A foundation, wall or door first asks what it is made of. A
// foundation is dragged out as a rectangle. Walls, doors, ladders and upper floors go on single tiles, and dragging
// paints them along.

namespace {
int tileOf(float v) { return (int)std::floor(v); }

// A floor as players count them: the ground floor is Floor 1, the first upper floor Floor 2, and so on
std::string floorName(int level) {
    return langf("MISC_FLOOR_NOW", { std::to_string(std::max(0, level) + 1) });
}

struct MaterialAskLayout {
    Rect box, title;
    Rect choice[(int)BuildMaterial::Count];
    Rect cancel;
};

MaterialAskLayout layoutMaterialAsk(int W, int H) {
    const float s = uiScaleFor((float)H);
    MaterialAskLayout L;
    const float bw = 200.0f * s, bh = 40.0f * s, gap = 10.0f * s, pad = 18.0f * s, head = 30.0f * s;
    const int n = (int)BuildMaterial::Count;
    const float w = bw + 2.0f * pad, h = pad + head + n * (bh + gap) + bh + pad;
    L.box = Rect{ std::floor((W - w) * 0.5f), std::floor((H - h) * 0.5f), w, h };
    L.title = Rect{ L.box.x + pad, L.box.y + pad, bw, head };
    for (int i = 0; i < n; ++i) L.choice[i] = Rect{ L.box.x + pad, L.title.y + head + i * (bh + gap), bw, bh };
    L.cancel = Rect{ L.box.x + pad, L.choice[n - 1].y + bh + gap, bw, bh };
    return L;
}
}

bool App::pressOnMaterialAsk() {
    if (!session.askMaterial) return false;
    if (e.button.button != SDL_BUTTON_LEFT) return true;
    const MaterialAskLayout L = layoutMaterialAsk(winW, winH);
    for (int i = 0; i < (int)BuildMaterial::Count; ++i) {
        if (!L.choice[i].contains(e.button.x, e.button.y)) continue;
        session.buildMaterial = i;
        session.buildPick = BuildPick::place(session.askKind);
        session.askMaterial = false;
        showNotice(lang("UILABEL_BUILD_PLACE_HINT"), 4.0f);
        playSfx(Sfx::UiClick);
        return true;
    }
    if (L.cancel.contains(e.button.x, e.button.y) || !L.box.contains(e.button.x, e.button.y)) session.askMaterial = false;
    return true;
}

void App::drawMaterialAsk() {
    if (!session.askMaterial) return;
    const MaterialAskLayout L = layoutMaterialAsk(winW, winH);
    const float th = renderer.listTextHeight("Ag");
    renderer.drawScrim();
    renderer.drawThemedPanel(L.box);
    renderer.drawListText(langf("UILABEL_MATERIAL_TITLE", { lang(propInfo(session.askKind).nameKey) }), L.title.x,
                          L.title.y + (L.title.h - th) * 0.5f, RGBA{ 1.0f, 0.78f, 0.30f, 1.0f });
    for (int i = 0; i < (int)BuildMaterial::Count; ++i) {
        const BuildMaterial m = (BuildMaterial)i;
        renderer.drawButton(L.choice[i], lang(materialNameKey(m)), true, L.choice[i].contains(mouseX, mouseY));
    }
    renderer.drawButton(L.cancel, lang("UILABEL_CANCEL"), true, L.cancel.contains(mouseX, mouseY));
}

// The floor looked at. Auto: the floor of the unit the camera follows, and with the camera moved by hand, no higher than
// the top floor under its pivot (the ground floor outside a building). Inside a building, the cutaway leaves out what
// stands on the floors above it.
void App::updateFloorView() {
    const BuildingMap m = buildingMap(world);
    const int px = tileOf(cam.focusX), pz = tileOf(cam.focusZ);
    const bool inside = m.foundation.count(tileKey(px, pz)) > 0;
    int top = 0;
    for (int level = 1; inside && level <= STOREY_MAX; ++level) {
        const auto it = m.floor.find(levelKey(px, pz, level));
        if (it != m.floor.end() && !world.has<Blueprint>(it->second)) top = level;
    }
    pivotTopFloor = top;
    if (!session.floorManual && followUnit != NO_OBJECT && world.has<Transform>(followUnit)) session.floorLevel = storeyOf(world, followUnit);
    // No higher than the top floor under the pivot, in Manual too: off a building (or with no floor over this tile),
    // back to the ground floor
    else session.floorLevel = std::min(session.floorLevel, top);
    session.floorLevel = std::clamp(session.floorLevel, 0, STOREY_MAX);
    // The pivot rises to the top of the floor looked at: an upper floor's, a finished foundation's deck, or the ground
    float rise = 0.0f;
    if (inside) {
        const ObjectId f = m.foundation.find(tileKey(px, pz))->second;
        if (session.floorLevel > 0) rise = storeyBaseHeight(session.floorLevel);
        else if (!world.has<Blueprint>(f)) rise = DECK_HEIGHT;
    }
    cam.floorLift += (rise - cam.floorLift) * std::min(1.0f, dt * 10.0f);
    if (std::abs(rise - cam.floorLift) < 0.001f) cam.floorLift = rise;
    Renderer::Cutaway cut;
    if (inside) {
        cut.on = true;
        cut.level = session.floorLevel;
        const std::vector<long long> tiles = buildingTiles(m, px, pz);
        cut.tiles.insert(tiles.begin(), tiles.end());
    }
    renderer.setCutaway(std::move(cut));
}

// Q and E: down and up a floor. In Auto, the first press turns to Manual.
void App::stepFloor(int by) {
    if (!session.floorManual) {
        session.floorManual = true;
        followUnit = NO_OBJECT;
    }
    const int before = session.floorLevel;
    session.floorLevel = std::clamp(session.floorLevel + by, 0, std::min(pivotTopFloor, STOREY_MAX));
    playSfx(session.floorLevel != before ? Sfx::UiClick : Sfx::UiError);
    showNotice(floorName(session.floorLevel), 2.0f);
}

// Z: back to Auto, the floor following the camera again
void App::floorAuto() {
    if (!session.floorManual) {
        playSfx(Sfx::UiError);
        return;
    }
    session.floorManual = false;
    playSfx(Sfx::UiClick);
    showNotice(lang("MISC_FLOOR_AUTO"), 2.0f);
}

// In Manual: a banner at the top middle saying which floor is shown, and that Z goes back to Auto
void App::drawFloorBanner() {
    if (!session.floorManual || editing) return;
    const float s = uiScaleFor((float)winH);
    const std::string text = langf("MISC_FLOOR_MANUAL_BANNER", { floorName(session.floorLevel) });
    const float pad = 10.0f * s, th = renderer.listTextHeight("Ag"), tw = renderer.listTextWidth(text);
    // Under the notice when one shows, and under the pause bar
    float top = 14.0f * s;
    if (noticeSec > 0.0f && !notice.empty()) top += renderer.listTextHeight("Ag") + 12.0f * s + 8.0f * s;
    if (session.paused) top = std::max(top, renderer.pauseBarHeight(true) + 8.0f * s);
    const Rect box{ std::floor((winW - tw) * 0.5f - pad), std::floor(top), tw + 2.0f * pad, th + pad };
    renderer.drawRect(box, RGBA{ 0.0f, 0.0f, 0.0f, 0.65f });
    renderer.drawRect(Rect{ box.x, box.y + box.h - 2.0f * s, box.w, 2.0f * s }, RGBA{ 0.75f, 0.85f, 1.0f, 0.9f });
    renderer.drawListText(text, box.x + pad, box.y + pad * 0.5f, RGBA{ 0.75f, 0.85f, 1.0f, 1.0f });
}

// The point under the pointer this high above the ground
std::optional<std::pair<float, float>> App::pickAbove(float mx, float my, std::optional<std::pair<float, float>> ground, float above) {
    auto hit = ground;
    for (int i = 0; hit && i < 3; ++i) {
        const auto up = pickPlane(cam, mx, my, winW, winH, terrain.heightAt(hit->first, hit->second) + above);
        if (!up) break;
        hit = up;
    }
    return hit;
}

// Where a click lands: on the highest floor seen under the pointer (an upper floor, or the top step of stairs, which is
// a tile of the floor above them), passing over what the cutaway leaves out, so units go up or down the stairs to get
// there. A lower step of stairs is where it leads: the bottom one their foot, the middle one their top. Else the ground.
std::optional<std::pair<float, float>> App::pickFloor(float mx, float my, int& storey) {
    storey = 0;
    const auto ground = pickGround(cam, terrain, mx, my, winW, winH);
    if (!ground) return ground;
    const BuildingMap m = buildingMap(world);
    if (m.floor.empty() && m.stairTop.empty()) return ground;
    for (int level = STOREY_MAX; level >= 0; --level) {
        // Stairs standing on this floor: their middle step, then their bottom one
        const float base = storeyBaseHeight(level), rise = storeyBaseHeight(level + 1) - base;
        for (int step = 1; step >= 0; --step) {
            const auto hit = pickAbove(mx, my, ground, base + rise * (float)(step + 1) / 3.0f);
            if (!hit) continue;
            const int x = tileOf(hit->first), z = tileOf(hit->second);
            ObjectId stairs = NO_OBJECT;
            if (stairsStepAt(world, m, x, z, level, stairs) != step || world.has<Blueprint>(stairs) || renderer.tileCutAway(x, z, level)) continue;
            Vec2 spot = climbFoot(world, stairs);
            storey = level;
            if (step == 1 && climbTop(world, stairs, spot)) storey = level + 1;
            if (step == 0 || storey == level + 1) return std::make_pair(spot.x, spot.z);
        }
        if (level == 0) break;
        const auto hit = pickAbove(mx, my, ground, base);
        if (!hit) continue;
        const int x = tileOf(hit->first), z = tileOf(hit->second);
        if (!floorSurfaceOn(world, m, x, z, level)) continue;
        // A stairs' top step shows with the stairs, on the floor below
        const bool topStep = !m.floor.count(levelKey(x, z, level));
        if (renderer.tileCutAway(x, z, topStep ? level - 1 : level)) continue;
        storey = level;
        return hit;
    }
    storey = 0;
    return ground;
}

// Where the pointer points for building. An upper floor: the tiles at the height of the floor above the one looked at,
// so the tiles dragged over are the ones seen there. A foundation or something that claims land: the ground. Anything
// else: on the highest finished floor seen under the pointer (passing over what the cutaway leaves out), or the ground,
// and buildLevel says which.
std::optional<std::pair<float, float>> App::buildPickAt(float mx, float my) {
    auto hit = pickGround(cam, terrain, mx, my, winW, winH);
    session.buildLevel = session.floorLevel;
    if (!hit || !session.buildPick.placing()) return hit;
    const PropInfo& info = propInfo(session.buildPick.kind);
    if (info.upperFloor) {
        const auto up = pickAbove(mx, my, hit, (session.floorLevel + 1) * FLOOR_HEIGHT);
        return up ? up : hit;
    }
    session.buildLevel = 0;
    if (info.foundation || info.claimsLand) return hit;
    const BuildingMap m = buildingMap(world);
    for (int level = STOREY_MAX; level >= 1 && !m.floor.empty(); --level) {
        const auto up = pickAbove(mx, my, hit, storeyBaseHeight(level));
        if (!up) continue;
        const int x = tileOf(up->first), z = tileOf(up->second);
        const auto f = m.floor.find(levelKey(x, z, level));
        if (f == m.floor.end() || world.has<Blueprint>(f->second) || renderer.tileCutAway(x, z, level)) continue;
        session.buildLevel = level;
        return up;
    }
    return hit;
}

// Upper floors: every tile of your finished, walled foundations that is open for one (blue), and while dragging, the
// tiles that will get one (green) or can't (red)
void App::markFloorTiles(BuildPreview& bp, int tx, int tz) {
    const PropKind kind = session.buildPick.kind;
    const int level = session.floorLevel;
    const float above = (level + 1) * FLOOR_HEIGHT;
    // Open tiles near the pointer: those of your walled buildings that may take a floor over this one
    std::unordered_set<long long> open;
    for (ObjectId fe : world.view<Foundation>()) {
        const Owner* fo = world.get<Owner>(fe);
        if (world.has<Blueprint>(fe) || !fo || fo->faction != FACTION_PLAYER) continue;
        const Foundation* f = world.get<Foundation>(fe);
        if (std::abs(f->x0 - tx) > 24 || std::abs(f->z0 - tz) > 24) continue;
        for (int z = f->z0; z < f->z0 + f->h; ++z) {
            for (int x = f->x0; x < f->x0 + f->w; ++x) {
                if (checkPiece(world, kind, x, z, FACTION_PLAYER, level) != PieceCheck::Ok) continue;
                open.insert(tileKey(x, z));
                bp.marks.push_back({ x, z, terrain.heightAt(x + 0.5f, z + 0.5f) + above + 0.16f, 0 });
            }
        }
    }
    const int x0 = session.floorDrag ? std::min(session.dragTileX, tx) : tx, x1 = session.floorDrag ? std::max(session.dragTileX, tx) : tx;
    const int z0 = session.floorDrag ? std::min(session.dragTileZ, tz) : tz, z1 = session.floorDrag ? std::max(session.dragTileZ, tz) : tz;
    for (int z = z0; z <= z1; ++z) {
        for (int x = x0; x <= x1; ++x) {
            const bool ok = open.count(tileKey(x, z)) && checkPiece(world, kind, x, z, FACTION_PLAYER, level) == PieceCheck::Ok;
            bp.marks.push_back({ x, z, terrain.heightAt(x + 0.5f, z + 0.5f) + above + 0.18f, ok ? 1 : 2 });
        }
    }
}

void App::drawFoundationSize() {
    // Building on an upper floor: which one, beside the cursor
    if (session.buildPick.placing() && !session.foundationDrag && !session.floorDrag && !propInfo(session.buildPick.kind).foundation) {
        const float s = uiScaleFor((float)winH);
        std::string text = langf("MISC_BUILD_ON_FLOOR", { std::to_string(session.buildLevel + 1) });
        if (propInfo(session.buildPick.kind).stairs) text += "  " + lang("MISC_STAIRS_TURN");
        const float pad = 6.0f * s, th = renderer.listTextHeight("Ag"), tw = renderer.listTextWidth(text);
        const Rect box{ std::floor((float)mouseX + 18.0f * s), std::floor((float)mouseY + 18.0f * s), tw + 2.0f * pad, th + 2.0f * pad };
        renderer.drawRect(box, RGBA{ 0.0f, 0.0f, 0.0f, 0.65f });
        renderer.drawListText(text, box.x + pad, box.y + pad, RGBA{ 0.75f, 0.85f, 1.0f, 1.0f });
        return;
    }
    // While a foundation or upper floors are dragged out: the size beside the cursor, as columns x rows
    if ((!session.foundationDrag && !session.floorDrag) || !session.buildPick.placing()) return;
    const auto hit = buildPickAt((float)mouseX, (float)mouseY);
    if (!hit) return;
    const int tx = tileOf(hit->first), tz = tileOf(hit->second);
    const int cols = std::abs(tx - session.dragTileX) + 1, rows = std::abs(tz - session.dragTileZ) + 1;
    const PieceCheck why = session.floorDrag ? PieceCheck::Ok : checkFoundation(world, terrain, session.dragTileX, session.dragTileZ, tx, tz);
    const bool ok = why == PieceCheck::Ok;
    const std::string text = std::to_string(cols) + "x" + std::to_string(rows);
    const float s = uiScaleFor((float)winH);
    const float pad = 6.0f * s, th = renderer.listTextHeight("Ag"), tw = renderer.listTextWidth(text);
    const Rect box{ std::floor((float)mouseX + 18.0f * s), std::floor((float)mouseY + 18.0f * s), tw + 2.0f * pad, th + 2.0f * pad };
    renderer.drawRect(box, RGBA{ 0.0f, 0.0f, 0.0f, 0.65f });
    renderer.drawListText(text, box.x + pad, box.y + pad, ok ? RGBA{ 0.45f, 1.0f, 0.5f, 1.0f } : RGBA{ 1.0f, 0.4f, 0.4f, 1.0f });
    // Red: the first reason it can't be laid there, under the size
    if (ok) return;
    const std::string reason = lang(pieceRefusalKey(why));
    if (reason.empty()) return;
    const float rw = renderer.listTextWidth(reason);
    const float rx = std::floor(std::max(4.0f * s, std::min(box.x, (float)winW - rw - 2.0f * pad - 4.0f * s)));
    const Rect line{ rx, box.y + box.h + 4.0f * s, rw + 2.0f * pad, th + 2.0f * pad };
    renderer.drawRect(line, RGBA{ 0.0f, 0.0f, 0.0f, 0.65f });
    renderer.drawListText(reason, line.x + pad, line.y + pad, RGBA{ 1.0f, 0.4f, 0.4f, 1.0f });
}

bool App::buildPressAt(float gx, float gz) {
    const PropInfo& info = propInfo(session.buildPick.kind);
    const int tx = tileOf(gx), tz = tileOf(gz);
    if (info.foundation) {
        session.foundationDrag = true;
        session.dragTileX = tx;
        session.dragTileZ = tz;
        return true;
    }
    if (info.upperFloor) {
        session.floorDrag = true;
        session.dragTileX = tx;
        session.dragTileZ = tz;
        return true;
    }
    if (info.onFoundation) {
        const PieceCheck why = checkPiece(world, session.buildPick.kind, tx, tz, FACTION_PLAYER, session.buildLevel, session.buildDir);
        if (why == PieceCheck::Ok) {
            placePiece(world, terrain, session.buildPick.kind, tx, tz, FACTION_PLAYER, (BuildMaterial)session.buildMaterial, session.buildLevel,
                       session.buildDir);
            playSfx(Sfx::UiClick);
        } else {
            refuse(lang(pieceRefusalKey(why)));
        }
        session.paintPiece = true;
        session.lastPaintTile = tileKey(tx, tz);
        return true;
    }
    return false;
}

bool App::buildRelease() {
    session.paintPiece = false;
    if (session.floorDrag) {
        // Upper floors on every tile of the rectangle that may take one
        session.floorDrag = false;
        float mx = 0.0f, my = 0.0f;
        SDL_GetMouseState(&mx, &my);
        const auto hit = buildPickAt(mx, my);
        if (!hit || !session.buildPick.placing()) return true;
        const int x1 = tileOf(hit->first), z1 = tileOf(hit->second);
        int laid = 0;
        PieceCheck why = PieceCheck::Ok;
        for (int z = std::min(session.dragTileZ, z1); z <= std::max(session.dragTileZ, z1); ++z) {
            for (int x = std::min(session.dragTileX, x1); x <= std::max(session.dragTileX, x1); ++x) {
                const PieceCheck c = checkPiece(world, session.buildPick.kind, x, z, FACTION_PLAYER, session.floorLevel);
                if (c != PieceCheck::Ok) {
                    why = c;
                    continue;
                }
                placePiece(world, terrain, session.buildPick.kind, x, z, FACTION_PLAYER, (BuildMaterial)session.buildMaterial, session.floorLevel);
                ++laid;
            }
        }
        if (laid > 0) playSfx(Sfx::UiOrder);
        else refuse(lang(pieceRefusalKey(why)));
        return true;
    }
    if (!session.foundationDrag) return false;
    session.foundationDrag = false;
    float mx = 0.0f, my = 0.0f;
    SDL_GetMouseState(&mx, &my);
    const auto hit = buildPickAt(mx, my);
    if (!hit) return true;
    const int x1 = tileOf(hit->first), z1 = tileOf(hit->second);
    const PieceCheck why = checkFoundation(world, terrain, session.dragTileX, session.dragTileZ, x1, z1);
    if (why != PieceCheck::Ok) {
        refuse(lang(pieceRefusalKey(why)));
        return true;
    }
    placeFoundation(world, terrain, session.dragTileX, session.dragTileZ, x1, z1, FACTION_PLAYER, (BuildMaterial)session.buildMaterial);
    playSfx(Sfx::UiOrder);
    return true;
}

void App::updateBuildPaint() {
    if (!session.paintPiece) return;
    if (!session.buildPick.placing() || !propInfo(session.buildPick.kind).onFoundation || propInfo(session.buildPick.kind).stairs ||
        !(SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_LMASK)) {
        session.paintPiece = false;
        return;
    }
    float mx = 0.0f, my = 0.0f;
    SDL_GetMouseState(&mx, &my);
    const auto hit = buildPickAt(mx, my);
    if (!hit) return;
    const int tx = tileOf(hit->first), tz = tileOf(hit->second);
    if (tileKey(tx, tz) == session.lastPaintTile) return;
    session.lastPaintTile = tileKey(tx, tz);
    // Dragging lays the piece on each tile it may go on, and passes over the rest
    if (checkPiece(world, session.buildPick.kind, tx, tz, FACTION_PLAYER, session.buildLevel) == PieceCheck::Ok) {
        placePiece(world, terrain, session.buildPick.kind, tx, tz, FACTION_PLAYER, (BuildMaterial)session.buildMaterial, session.buildLevel);
    }
}

bool App::previewBuilding(BuildPreview& bp, float gx, float gz) {
    const PropInfo& info = propInfo(session.buildPick.kind);
    if (!info.foundation && !info.onFoundation) return false;
    const int tx = tileOf(gx), tz = tileOf(gz);
    if (info.upperFloor) {
        markFloorTiles(bp, tx, tz);
        return true;
    }
    if (info.stairs) {
        // Their three steps, green where they may go and red where not, each at its height (the top one is a tile of the
        // floor above)
        const int level = session.buildLevel, d = session.buildDir;
        const bool ok = checkPiece(world, session.buildPick.kind, tx, tz, FACTION_PLAYER, level, d) == PieceCheck::Ok;
        const float rise = storeyBaseHeight(level + 1) - storeyBaseHeight(level);
        for (int i = -1; i <= 1; ++i) {
            const int x = tx + i * STAIRS_DX[d], z = tz + i * STAIRS_DZ[d];
            const float ground = terrain.heightAt(x + 0.5f, z + 0.5f) + storeyBaseHeight(level);
            bp.marks.push_back({ x, z, ground + 0.06f + rise * (float)(i + 2) / 3.0f, ok ? 1 : 2 });
        }
        return true;
    }
    if (info.foundation && session.foundationDrag) {
        bp.rect = true;
        bp.rx0 = std::min(session.dragTileX, tx);
        bp.rx1 = std::max(session.dragTileX, tx);
        bp.rz0 = std::min(session.dragTileZ, tz);
        bp.rz1 = std::max(session.dragTileZ, tz);
        bp.valid = checkFoundation(world, terrain, session.dragTileX, session.dragTileZ, tx, tz) == PieceCheck::Ok;
        return true;
    }
    if (!info.foundation) {
        // A piece on a floor: the outline of its tile on that floor's top (a foundation's deck on the ground floor),
        // green where it may go and red where not
        const bool ok = checkPiece(world, session.buildPick.kind, tx, tz, FACTION_PLAYER, session.buildLevel) == PieceCheck::Ok;
        const int level = session.buildLevel;
        const float top = level > 0 ? storeyBaseHeight(level) : foundationAt(world, tx, tz) != NO_OBJECT ? DECK_HEIGHT : 0.0f;
        bp.marks.push_back({ tx, tz, terrain.heightAt(tx + 0.5f, tz + 0.5f) + top + 0.02f, ok ? 1 : 2, true });
        return true;
    }
    bp.on = true;
    bp.kind = session.buildPick.kind;
    bp.x = tx + 0.5f;
    bp.z = tz + 0.5f;
    bp.valid = foundationAt(world, tx, tz) == NO_OBJECT && terrain.walkable(tx, tz);
    return true;
}
