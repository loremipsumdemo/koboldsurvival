#include "app.h"
#include "claims.h"
#include "squads.h"
#include "ghosts.h"

using namespace play;

namespace {
int columnOf(World& w, ObjectId u) {
    return w.has<Citizen>(u) ? ALTAR_CITIZEN : ALTAR_MILITARY;
}
}

void App::openAltar() {
    altar = AltarUi{};
    altar.open = true;
    ghostsUi = GhostsUi{};
    selectMenuOpen = false;
    session.contextMenu.open = false;
    itemMenu.open = false;
    stopSquadRename(true);
    playSfx(Sfx::UiMenuOpen);
}

// Each column's units as they will be after Apply: the units moving in come first, shown as ghosts
std::array<std::vector<ObjectId>, 2> App::altarColumns() {
    for (auto it = altar.moves.begin(); it != altar.moves.end();) {
        if (!world.has<Entity>(it->first) || (!world.has<Commandable>(it->first) && !world.has<Citizen>(it->first))) it = altar.moves.erase(it);
        else ++it;
    }
    std::array<std::vector<ObjectId>, 2> cols;
    for (const auto& [u, to] : altar.moves) cols[(size_t)to].push_back(u);
    std::vector<ObjectId> military = militaryUnits(world);
    std::stable_sort(military.begin(), military.end(), [&](ObjectId a, ObjectId b) { return squadOf(world, a) < squadOf(world, b); });
    for (ObjectId u : military) {
        if (!altar.moves.count(u)) cols[ALTAR_MILITARY].push_back(u);
    }
    for (ObjectId u : citizenUnits(world)) {
        if (!altar.moves.count(u)) cols[ALTAR_CITIZEN].push_back(u);
    }
    return cols;
}

bool App::pressOnAltar() {
    if (screen != Screen::Playing || !altar.open) return false;
    const float x = e.button.x, y = e.button.y;
    const std::array<std::vector<ObjectId>, 2> cols = altarColumns();
    const AltarLayout L = layoutAltar(winW, winH, { (int)cols[0].size(), (int)cols[1].size() }, altar.scroll);
    if (altar.menuUnit != NO_OBJECT) {
        const ItemMenuLayout menu = renderer.itemMenuLayout(altar.menuX, altar.menuY, 1, winW, winH);
        const ObjectId u = altar.menuUnit;
        altar.menuUnit = NO_OBJECT;
        if (e.button.button == SDL_BUTTON_LEFT && itemMenuButtonAt(menu, x, y) == 0) {
            startRename(u);
            return true;
        }
        if (menu.box.contains(x, y)) return true;
    }
    if (e.button.button == SDL_BUTTON_RIGHT) {
        for (int c = 0; c < 2; ++c) {
            for (const AltarRow& r : L.rows[(size_t)c]) {
                if (!r.portrait.contains(x, y) || r.index >= (int)cols[(size_t)c].size()) continue;
                altar.menuUnit = cols[(size_t)c][(size_t)r.index];
                altar.menuX = x;
                altar.menuY = y;
                playSfx(Sfx::UiMenuOpen);
                return true;
            }
        }
        return true;
    }
    if (e.button.button != SDL_BUTTON_LEFT) return true;
    if (L.close.contains(x, y) || L.cancel.contains(x, y)) {
        altar = AltarUi{};
        playSfx(Sfx::UiBack);
        return true;
    }
    if (L.apply.contains(x, y)) {
        if (!altar.moves.empty()) applyAltarMoves();
        return true;
    }
    if (!L.box.contains(x, y)) {
        altar.pinned = NO_OBJECT;
        return true;
    }
    for (int c = 0; c < 2; ++c) {
        for (const AltarRow& r : L.rows[(size_t)c]) {
            if (!r.row.contains(x, y) || r.index >= (int)cols[(size_t)c].size()) continue;
            const ObjectId u = cols[(size_t)c][(size_t)r.index];
            if (!r.move.contains(x, y)) {
                altar.pinned = altar.pinned == u ? NO_OBJECT : u;
                playSfx(Sfx::UiClick);
                return true;
            }
            // Undo a move, or start one if the column it goes to has room
            if (altar.moves.erase(u)) {
                playSfx(Sfx::UiClick);
                return true;
            }
            const int to = 1 - c;
            int military = (int)cols[ALTAR_MILITARY].size(), citizens = (int)cols[ALTAR_CITIZEN].size();
            if (to == ALTAR_CITIZEN && citizens + 1 > settings.citizenHardCap) {
                refuse(langf("MISC_CITIZENS_HARD_CAP", { std::to_string(settings.citizenHardCap) }));
                return true;
            }
            if (to == ALTAR_MILITARY && military + 1 > MILITARY_MAX) {
                refuse(langf("MISC_MILITARY_FULL", { std::to_string(MILITARY_MAX) }));
                return true;
            }
            altar.moves[u] = to;
            playSfx(Sfx::UiClick);
            return true;
        }
    }
    return true;
}

void App::applyAltarMoves() {
    // Citizens first, so their squad places are free for the units joining the military
    int toCitizens = 0, toMilitary = 0, noRoom = 0;
    for (const auto& [u, to] : altar.moves) {
        if (to != ALTAR_CITIZEN || columnOf(world, u) == ALTAR_CITIZEN) continue;
        makeCitizen(world, u);
        ++toCitizens;
    }
    for (const auto& [u, to] : altar.moves) {
        if (to != ALTAR_MILITARY || columnOf(world, u) == ALTAR_MILITARY) continue;
        if (makeMilitary(world, u, world.resource<SquadBook>().current)) ++toMilitary;
        else ++noRoom;
    }
    altar.moves.clear();
    altar.pinned = NO_OBJECT;
    if (noRoom > 0) refuse(langf("MISC_MILITARY_FULL", { std::to_string(MILITARY_MAX) }));
    else playSfx(Sfx::UiClick);
    std::string done = langf("MISC_ALTAR_APPLIED", { std::to_string(toMilitary), std::to_string(toCitizens) });
    if ((int)citizenUnits(world).size() > citizenSoftLimit(settings)) {
        done += "  " + langf("MISC_CITIZENS_OVER_SOFT_CAP", { std::to_string(citizenSoftLimit(settings)) });
    }
    showNotice(done);
}

void App::drawAltar() {
    if (!altar.open) return;
    const std::array<std::vector<ObjectId>, 2> cols = altarColumns();
    const AltarLayout L = layoutAltar(winW, winH, { (int)cols[0].size(), (int)cols[1].size() }, altar.scroll);
    altar.scroll = L.first;
    AltarTotals totals;
    totals.military = (int)cols[ALTAR_MILITARY].size();
    totals.citizens = (int)cols[ALTAR_CITIZEN].size();
    totals.softCap = citizenSoftLimit(settings);
    totals.hardCap = settings.citizenHardCap;
    renderer.drawAltarRoster(world, L, cols, altar.moves, totals, altar.pinned, mouseX, mouseY);

    // The card of the pinned unit, or else of the unit under the mouse
    ObjectId shown = NO_OBJECT;
    Rect beside{};
    for (int c = 0; c < 2; ++c) {
        for (const AltarRow& r : L.rows[(size_t)c]) {
            if (r.index >= (int)cols[(size_t)c].size()) continue;
            const ObjectId u = cols[(size_t)c][(size_t)r.index];
            if (u == altar.pinned || (altar.pinned == NO_OBJECT && r.row.contains(mouseX, mouseY) && !r.move.contains(mouseX, mouseY))) {
                shown = u;
                beside = r.row;
            }
        }
    }
    if (shown == NO_OBJECT && altar.pinned != NO_OBJECT && world.has<Entity>(altar.pinned)) {
        shown = altar.pinned;   // scrolled out of sight: beside the roster
        beside = L.box;
    }
    if (shown != NO_OBJECT && altar.menuUnit == NO_OBJECT) {
        renderer.drawUnitCard(world, shown, layoutUnitCard(beside, winW, winH, STAT_GROUP_COUNT + STAT_COUNT));
    }
    if (altar.menuUnit != NO_OBJECT) {
        renderer.drawItemMenu(renderer.itemMenuLayout(altar.menuX, altar.menuY, 1, winW, winH), { lang("UILABEL_RENAME") }, mouseX, mouseY);
    }
}

void App::openGhosts(ObjectId altarProp) {
    ghostsUi = GhostsUi{};
    ghostsUi.open = true;
    ghostsUi.altar = altarProp;
    altar = AltarUi{};
    selectMenuOpen = false;
    session.contextMenu.open = false;
    itemMenu.open = false;
    playSfx(Sfx::UiMenuOpen);
}

// The Altar's bones, one row per kind
std::vector<ItemStack> App::ghostBones() {
    std::vector<ItemStack> out;
    if (const Storage* st = world.get<Storage>(ghostsUi.altar)) {
        for (const ItemStack& s : st->slots) {
            if (s.count <= 0 || !isBones(s.item)) continue;
            auto same = std::find_if(out.begin(), out.end(), [&](const ItemStack& o) { return o.item == s.item; });
            if (same != out.end()) same->count += s.count;
            else out.push_back(s);
        }
    }
    return out;
}

// The Ghosts waiting longest first, then those already appeased
std::vector<size_t> App::ghostOrder() {
    const std::vector<Ghost>& ghosts = world.resource<Graveyard>().ghosts;
    std::vector<size_t> order(ghosts.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        if (ghosts[a].appeased != ghosts[b].appeased) return !ghosts[a].appeased;
        return !ghosts[a].appeased && ghosts[a].hours > ghosts[b].hours;
    });
    return order;
}

bool App::pressOnGhosts() {
    if (screen != Screen::Playing || !ghostsUi.open) return false;
    if (!world.has<Prop>(ghostsUi.altar)) {
        ghostsUi = GhostsUi{};
        return false;
    }
    if (e.button.button != SDL_BUTTON_LEFT) return true;
    const float x = e.button.x, y = e.button.y;
    const std::vector<ItemStack> bones = ghostBones();
    const std::vector<size_t> order = ghostOrder();
    const AltarLayout L = layoutAltar(winW, winH, { (int)bones.size(), (int)order.size() }, ghostsUi.scroll);
    if (L.close.contains(x, y) || L.cancel.contains(x, y)) {
        ghostsUi = GhostsUi{};
        playSfx(Sfx::UiBack);
        return true;
    }
    if (L.apply.contains(x, y)) {
        int done = 0;
        for (size_t i : order) done += appeaseGhost(world, ghostsUi.altar, i) ? 1 : 0;
        if (done > 0) playSfx(Sfx::UiClick);
        else refuse(lang(bonesIn(world, ghostsUi.altar) > 0 ? "MISC_GHOSTS_ALL_APPEASED" : "MISC_GHOSTS_NO_BONES"));
        return true;
    }
    for (const AltarRow& r : L.rows[1]) {
        if (r.index >= (int)order.size() || !ghostButton(r, winH).contains(x, y)) continue;
        if (world.resource<Graveyard>().ghosts[order[(size_t)r.index]].appeased) return true;
        if (appeaseGhost(world, ghostsUi.altar, order[(size_t)r.index])) playSfx(Sfx::UiClick);
        else refuse(lang("MISC_GHOSTS_NO_BONES"));
        return true;
    }
    return true;
}

void App::drawGhosts() {
    if (!ghostsUi.open) return;
    if (!world.has<Prop>(ghostsUi.altar)) {
        ghostsUi = GhostsUi{};
        return;
    }
    const std::vector<ItemStack> bones = ghostBones();
    const std::vector<size_t> order = ghostOrder();
    const AltarLayout L = layoutAltar(winW, winH, { (int)bones.size(), (int)order.size() }, ghostsUi.scroll);
    ghostsUi.scroll = L.first;
    renderer.drawGhostsWindow(world, L, bones, order, bonesIn(world, ghostsUi.altar), mouseX, mouseY);
}
