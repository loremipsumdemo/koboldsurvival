#include "app.h"
#include "claims.h"
#include "squads.h"
#include "ghosts.h"
#include "behavior_groups.h"

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
    squadsUi = SquadsUi{};
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
    // A citizen's behavior group: the next one on a click, the one before on a right-click
    if (e.button.button == SDL_BUTTON_LEFT || e.button.button == SDL_BUTTON_RIGHT) {
        for (const AltarRow& r : L.rows[ALTAR_CITIZEN]) {
            if (!r.group.contains(x, y) || r.index >= (int)cols[ALTAR_CITIZEN].size()) continue;
            const ObjectId u = cols[ALTAR_CITIZEN][(size_t)r.index];
            const Citizen* c = world.get<Citizen>(u);
            const int count = (int)behaviorGroups(world).groups.size();
            if (!c || altar.moves.count(u) || count <= 0) return true;
            const int step = e.button.button == SDL_BUTTON_LEFT ? 1 : count - 1;
            setCitizenGroup(world, u, (c->group + step) % count);
            playSfx(Sfx::UiClick);
            return true;
        }
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

// ---- Manage Squads ----

void App::openManageSquads() {
    squadsUi = SquadsUi{};
    squadsUi.open = true;
    // The squad the Squad tab shows on the left, and the next one on the right
    const int current = std::clamp(world.resource<SquadBook>().current, 0, SQUAD_COUNT - 1);
    squadsUi.view = { current, (current + 1) % SQUAD_COUNT };
    altar = AltarUi{};
    ghostsUi = GhostsUi{};
    selectMenuOpen = false;
    session.contextMenu.open = false;
    itemMenu.open = false;
    stopSquadRename(true);
    playSfx(Sfx::UiMenuOpen);
}

// The squad a unit will be in once the moves are applied (SQUAD_UNSORTED for one put aside)
int App::squadsTarget(ObjectId u) {
    const auto it = squadsUi.moves.find(u);
    return it != squadsUi.moves.end() ? it->second : squadOf(world, u);
}

// How many units a squad (or Unsorted) will have once the moves are applied
int App::squadsCount(int squad) {
    int n = 0;
    for (ObjectId u : militaryUnits(world)) n += squadsTarget(u) == squad ? 1 : 0;
    return n;
}

// Each column's units as they will be after Apply: the units moving in come first, shown as ghosts
std::array<std::vector<ObjectId>, 2> App::squadsColumns() {
    for (auto it = squadsUi.moves.begin(); it != squadsUi.moves.end();) {
        if (!world.has<Entity>(it->first) || !world.has<Commandable>(it->first) || it->second == squadOf(world, it->first)) {
            it = squadsUi.moves.erase(it);
        } else {
            ++it;
        }
    }
    std::array<std::vector<ObjectId>, 2> cols;
    const std::vector<ObjectId> military = militaryUnits(world);
    for (int c = 0; c < 2; ++c) {
        const int view = squadsUi.view[(size_t)c];
        for (const auto& [u, to] : squadsUi.moves) {
            if (to == view) cols[(size_t)c].push_back(u);
        }
        for (ObjectId u : military) {
            if (!squadsUi.moves.count(u) && squadOf(world, u) == view) cols[(size_t)c].push_back(u);
        }
    }
    return cols;
}

bool App::pressOnManageSquads() {
    if (screen != Screen::Playing || !squadsUi.open) return false;
    const float x = e.button.x, y = e.button.y;
    const std::array<std::vector<ObjectId>, 2> cols = squadsColumns();
    const SquadsLayout SL = layoutSquads(winW, winH, { (int)cols[0].size(), (int)cols[1].size() }, squadsUi.scroll);
    const AltarLayout& L = SL.roster;
    if (e.button.button == SDL_BUTTON_RIGHT) {
        // A right-click on a portrait renames that unit, as in the roster
        for (int c = 0; c < 2; ++c) {
            for (const AltarRow& r : L.rows[(size_t)c]) {
                if (!r.portrait.contains(x, y) || r.index >= (int)cols[(size_t)c].size()) continue;
                startRename(cols[(size_t)c][(size_t)r.index]);
                return true;
            }
        }
        return true;
    }
    if (e.button.button != SDL_BUTTON_LEFT) return true;
    if (L.close.contains(x, y) || L.cancel.contains(x, y)) {
        squadsUi = SquadsUi{};
        playSfx(Sfx::UiBack);
        return true;
    }
    if (L.apply.contains(x, y)) {
        applySquadMoves();
        return true;
    }
    // The arrows: the column shows the next or the one before of Unsorted and the nine squads, never the other
    // column's
    for (int c = 0; c < 2; ++c) {
        const bool prev = SL.prev[(size_t)c].contains(x, y), next = SL.next[(size_t)c].contains(x, y);
        if (!prev && !next) continue;
        constexpr int CHOICES = SQUAD_COUNT + 1;   // Unsorted, then the squads
        int at = squadsUi.view[(size_t)c] + 1;
        do {
            at = (at + (next ? 1 : CHOICES - 1)) % CHOICES;
        } while (at - 1 == squadsUi.view[(size_t)(1 - c)]);
        squadsUi.view[(size_t)c] = at - 1;
        squadsUi.scroll[(size_t)c] = 0;
        playSfx(Sfx::UiClick);
        return true;
    }
    if (!L.box.contains(x, y)) {
        squadsUi.pinned = NO_OBJECT;
        return true;
    }
    for (int c = 0; c < 2; ++c) {
        for (const AltarRow& r : L.rows[(size_t)c]) {
            if (!r.row.contains(x, y) || r.index >= (int)cols[(size_t)c].size()) continue;
            const ObjectId u = cols[(size_t)c][(size_t)r.index];
            if (!r.move.contains(x, y)) {
                squadsUi.pinned = squadsUi.pinned == u ? NO_OBJECT : u;
                playSfx(Sfx::UiClick);
                return true;
            }
            // Across to the other column's squad: back where it was undoes the move. Unsorted takes any number.
            const int to = squadsUi.view[(size_t)(1 - c)];
            if (to != SQUAD_UNSORTED && squadsCount(to) >= SQUAD_SIZE) {
                refuse(langf("MISC_SQUAD_FULL", { std::to_string(to + 1), std::to_string(SQUAD_SIZE) }));
                return true;
            }
            if (to == squadOf(world, u)) squadsUi.moves.erase(u);
            else squadsUi.moves[u] = to;
            playSfx(Sfx::UiClick);
            return true;
        }
    }
    return true;
}

void App::scrollManageSquads(float x, float y, float notches) {
    const std::array<std::vector<ObjectId>, 2> cols = squadsColumns();
    const SquadsLayout SL = layoutSquads(winW, winH, { (int)cols[0].size(), (int)cols[1].size() }, squadsUi.scroll);
    for (int c = 0; c < 2; ++c) {
        if (!SL.roster.lists[(size_t)c].contains(x, y)) continue;
        const int step = notches > 0.0f ? -3 : notches < 0.0f ? 3 : 0;
        squadsUi.scroll[(size_t)c] = std::clamp(SL.roster.first[(size_t)c] + step, 0, altarMaxScroll(SL.roster, c, (int)cols[(size_t)c].size()));
    }
}

void App::applySquadMoves() {
    squadsColumns();   // drops moves of units gone since
    if (squadsUi.moves.empty()) return;
    const int unsorted = squadsCount(SQUAD_UNSORTED);
    if (unsorted > 0) {
        refuse(langf("MISC_SQUADS_UNSORTED", { std::to_string(unsorted) }));
        return;
    }
    // Every squad's count was kept to SQUAD_SIZE as units were moved, so they all go at once (moving them one by one
    // could find a squad full until another unit has left it)
    const int moved = (int)squadsUi.moves.size();
    for (const auto& [u, to] : squadsUi.moves) world.add(u, SquadMember{ to });
    squadsUi.moves.clear();
    squadsUi.pinned = NO_OBJECT;
    showNotice(langf("MISC_SQUADS_APPLIED", { std::to_string(moved) }));
    playSfx(Sfx::UiClick);
}

void App::drawManageSquads() {
    if (!squadsUi.open) return;
    const std::array<std::vector<ObjectId>, 2> cols = squadsColumns();
    const SquadsLayout SL = layoutSquads(winW, winH, { (int)cols[0].size(), (int)cols[1].size() }, squadsUi.scroll);
    squadsUi.scroll = SL.roster.first;
    Renderer::SquadsRosterText text;
    for (int c = 0; c < 2; ++c) {
        const int view = squadsUi.view[(size_t)c];
        const int n = squadsCount(view);
        if (view == SQUAD_UNSORTED) {
            text.heads[(size_t)c] = lang("UILABEL_SQUADS_UNSORTED");
            text.counts[(size_t)c] = std::to_string(n);
        } else {
            text.heads[(size_t)c] = squadLabel(world, view);
            text.counts[(size_t)c] = std::to_string(n) + "/" + std::to_string(SQUAD_SIZE);
            text.full[(size_t)c] = n >= SQUAD_SIZE;
        }
    }
    const int unsorted = squadsCount(SQUAD_UNSORTED);
    text.moveCount = (int)squadsUi.moves.size();
    text.canApply = text.moveCount > 0 && unsorted == 0;
    text.warn = unsorted > 0;
    text.totals = unsorted > 0 ? langf("MISC_SQUADS_UNSORTED", { std::to_string(unsorted) }) : lang("UILABEL_SQUADS_HOW");
    renderer.drawSquadsRoster(world, SL, cols, squadsUi.moves, text, squadsUi.pinned, mouseX, mouseY);

    // The card of the pinned unit, or else of the unit under the mouse
    ObjectId shown = NO_OBJECT;
    Rect beside{};
    for (int c = 0; c < 2; ++c) {
        for (const AltarRow& r : SL.roster.rows[(size_t)c]) {
            if (r.index >= (int)cols[(size_t)c].size()) continue;
            const ObjectId u = cols[(size_t)c][(size_t)r.index];
            if (u == squadsUi.pinned || (squadsUi.pinned == NO_OBJECT && r.row.contains(mouseX, mouseY) && !r.move.contains(mouseX, mouseY))) {
                shown = u;
                beside = r.row;
            }
        }
    }
    if (shown == NO_OBJECT && squadsUi.pinned != NO_OBJECT && world.has<Entity>(squadsUi.pinned)) {
        shown = squadsUi.pinned;
        beside = SL.roster.box;
    }
    if (shown != NO_OBJECT) renderer.drawUnitCard(world, shown, layoutUnitCard(beside, winW, winH, STAT_GROUP_COUNT + STAT_COUNT));
}

void App::followSelectionSquad() {
    std::vector<ObjectId> now = selectedUnits(world);
    std::sort(now.begin(), now.end());
    if (now == lastSelection) return;
    lastSelection = now;
    SquadBook& book = world.resource<SquadBook>();
    if (!squadUnits(world, book.current).empty()) return;
    // The squad most of the selected units are in
    std::array<int, SQUAD_COUNT> counts{};
    for (ObjectId u : now) {
        if (const int s = squadOf(world, u); s >= 0) ++counts[(size_t)s];
    }
    const auto best = std::max_element(counts.begin(), counts.end());
    if (*best <= 0) return;
    book.current = (int)(best - counts.begin());
    unitPanelRow = 0;
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
