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

// Where the pointer points for building: the ground, or for an upper floor, the height of the floor above it, so the
// tiles dragged over are the ones seen at that height
std::optional<std::pair<float, float>> App::buildPickAt(float mx, float my) {
    auto hit = pickGround(cam, terrain, mx, my, winW, winH);
    if (!hit || !session.buildPick.placing() || !propInfo(session.buildPick.kind).upperFloor) return hit;
    for (int i = 0; i < 2; ++i) {
        const auto up = pickPlane(cam, mx, my, winW, winH, terrain.heightAt(hit->first, hit->second) + FLOOR_HEIGHT);
        if (!up) break;
        hit = up;
    }
    return hit;
}

// Upper floors: every tile of your finished, walled foundations that is open for one (blue), and while dragging, the
// tiles that will get one (green) or can't (red)
void App::markFloorTiles(BuildPreview& bp, int tx, int tz) {
    const PropKind kind = session.buildPick.kind;
    std::unordered_set<long long> taken;
    for (ObjectId e : world.view<Prop, Transform>()) {
        const PropInfo& info = propInfo(world.get<Prop>(e)->kind);
        if (!info.upperFloor && !info.ladder) continue;
        const Transform* tr = world.get<Transform>(e);
        taken.insert(tileKey(tileOf(tr->x), tileOf(tr->z)));
    }
    std::unordered_set<long long> open;
    for (ObjectId fe : world.view<Foundation>()) {
        const Owner* fo = world.get<Owner>(fe);
        if (world.has<Blueprint>(fe) || !fo || fo->faction != FACTION_PLAYER || !edgesClosed(world, fe)) continue;
        const Foundation* f = world.get<Foundation>(fe);
        for (int z = f->z0; z < f->z0 + f->h; ++z) {
            for (int x = f->x0; x < f->x0 + f->w; ++x) {
                if (taken.count(tileKey(x, z))) continue;
                open.insert(tileKey(x, z));
                bp.marks.push_back({ x, z, terrain.heightAt(x + 0.5f, z + 0.5f) + FLOOR_HEIGHT + 0.16f, 0 });
            }
        }
    }
    const int x0 = session.floorDrag ? std::min(session.dragTileX, tx) : tx, x1 = session.floorDrag ? std::max(session.dragTileX, tx) : tx;
    const int z0 = session.floorDrag ? std::min(session.dragTileZ, tz) : tz, z1 = session.floorDrag ? std::max(session.dragTileZ, tz) : tz;
    for (int z = z0; z <= z1; ++z) {
        for (int x = x0; x <= x1; ++x) {
            const bool ok = open.count(tileKey(x, z)) && checkPiece(world, kind, x, z) == PieceCheck::Ok;
            bp.marks.push_back({ x, z, terrain.heightAt(x + 0.5f, z + 0.5f) + FLOOR_HEIGHT + 0.18f, ok ? 1 : 2 });
        }
    }
}

void App::drawFoundationSize() {
    // While a foundation or upper floors are dragged out: the size beside the cursor, as columns x rows
    if ((!session.foundationDrag && !session.floorDrag) || !session.buildPick.placing()) return;
    const auto hit = buildPickAt((float)mouseX, (float)mouseY);
    if (!hit) return;
    const int tx = tileOf(hit->first), tz = tileOf(hit->second);
    const int cols = std::abs(tx - session.dragTileX) + 1, rows = std::abs(tz - session.dragTileZ) + 1;
    const bool ok = session.floorDrag || checkFoundation(world, terrain, session.dragTileX, session.dragTileZ, tx, tz) == PieceCheck::Ok;
    const std::string text = std::to_string(cols) + "x" + std::to_string(rows);
    const float s = uiScaleFor((float)winH);
    const float pad = 6.0f * s, th = renderer.listTextHeight("Ag"), tw = renderer.listTextWidth(text);
    const Rect box{ std::floor((float)mouseX + 18.0f * s), std::floor((float)mouseY + 18.0f * s), tw + 2.0f * pad, th + 2.0f * pad };
    renderer.drawRect(box, RGBA{ 0.0f, 0.0f, 0.0f, 0.65f });
    renderer.drawListText(text, box.x + pad, box.y + pad, ok ? RGBA{ 0.45f, 1.0f, 0.5f, 1.0f } : RGBA{ 1.0f, 0.4f, 0.4f, 1.0f });
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
        const PieceCheck why = checkPiece(world, session.buildPick.kind, tx, tz);
        if (why == PieceCheck::Ok) {
            placePiece(world, terrain, session.buildPick.kind, tx, tz, FACTION_PLAYER, (BuildMaterial)session.buildMaterial);
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
                const PieceCheck c = checkPiece(world, session.buildPick.kind, x, z);
                if (c != PieceCheck::Ok) {
                    why = c;
                    continue;
                }
                placePiece(world, terrain, session.buildPick.kind, x, z, FACTION_PLAYER, (BuildMaterial)session.buildMaterial);
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
    if (!session.buildPick.placing() || !propInfo(session.buildPick.kind).onFoundation ||
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
    if (checkPiece(world, session.buildPick.kind, tx, tz) == PieceCheck::Ok) {
        placePiece(world, terrain, session.buildPick.kind, tx, tz, FACTION_PLAYER, (BuildMaterial)session.buildMaterial);
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
    if (info.foundation && session.foundationDrag) {
        bp.rect = true;
        bp.rx0 = std::min(session.dragTileX, tx);
        bp.rx1 = std::max(session.dragTileX, tx);
        bp.rz0 = std::min(session.dragTileZ, tz);
        bp.rz1 = std::max(session.dragTileZ, tz);
        bp.valid = checkFoundation(world, terrain, session.dragTileX, session.dragTileZ, tx, tz) == PieceCheck::Ok;
        return true;
    }
    bp.on = true;
    bp.kind = session.buildPick.kind;
    bp.x = tx + 0.5f;
    bp.z = tz + 0.5f;
    if (info.foundation) {
        bp.valid = foundationAt(world, tx, tz) == NO_OBJECT && terrain.walkable(tx, tz);
    } else {
        bp.valid = checkPiece(world, session.buildPick.kind, tx, tz) == PieceCheck::Ok;
    }
    return true;
}
