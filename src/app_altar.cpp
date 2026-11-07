#include "app.h"
#include "claims.h"
#include "squads.h"

using namespace play;

namespace {
int columnOf(World& w, ObjectId u) {
    return w.has<Citizen>(u) ? ALTAR_CITIZEN : ALTAR_MILITARY;
}
}

void App::openAltar() {
    altar = AltarUi{};
    altar.open = true;
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
    if (e.button.button != SDL_BUTTON_LEFT) return true;
    const float x = e.button.x, y = e.button.y;
    const std::array<std::vector<ObjectId>, 2> cols = altarColumns();
    const AltarLayout L = layoutAltar(winW, winH, { (int)cols[0].size(), (int)cols[1].size() }, altar.scroll);
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
    if (shown != NO_OBJECT) renderer.drawUnitCard(world, shown, layoutUnitCard(beside, winW, winH, STAT_GROUP_COUNT + STAT_COUNT));
}
