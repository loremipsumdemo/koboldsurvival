#include "app.h"
#include <algorithm>
#include "commerce.h"
#include "inventory.h"
#include "systems.h"

using namespace play;

// A Stall's Stock window. Down the left, every store of yours in your claim, sorted by kind, by how full, or by how full
// for its size; on the right, what the store picked holds. Clicking a stack orders it hauled to the Stall (once, by a
// unit with Auto-Haul to Stalls). Along the bottom, the kinds of item the Stall takes as payment.

namespace {
struct StallLayout {
    Rect box, title, close;
    Rect sortKind, sortFull, sortRatio;
    Rect list;
    std::vector<Rect> rows;
    Rect items;
    std::vector<Rect> cells;
    Rect orders, clearOrders;
    Rect payTitle;
    std::vector<Rect> pay;
    int rowsShown = 0, cellsShown = 0, cols = 1;
};

StallLayout layoutStall(int W, int H) {
    const float s = uiScaleFor((float)H);
    const float pad = 14.0f * s, gap = 8.0f * s, line = 28.0f * s, rowH = 30.0f * s, btnH = 30.0f * s;
    StallLayout L;
    const float boxW = std::min((float)W - 32.0f * s, 900.0f * s);
    const float boxH = std::min((float)H - 32.0f * s, 620.0f * s);
    L.box = Rect{ std::floor((W - boxW) * 0.5f), std::floor((H - boxH) * 0.5f), boxW, boxH };
    const float x = L.box.x + pad, w = boxW - 2.0f * pad;
    float y = L.box.y + pad;
    L.title = Rect{ x, y, w - line, line };
    L.close = Rect{ x + w - line, y, line, line };
    y += line + gap;
    const float third = (w * 0.4f - 2.0f * gap) / 3.0f;
    L.sortKind = Rect{ x, y, third, btnH };
    L.sortFull = Rect{ x + third + gap, y, third, btnH };
    L.sortRatio = Rect{ x + 2.0f * (third + gap), y, third, btnH };
    y += btnH + gap;
    const int cats = (int)StoreCategory::Count;
    const float payRowH = 26.0f * s;
    const int payCols = 6;
    const float payH = line + ((cats + payCols - 1) / payCols) * (payRowH + 4.0f * s);
    const float bottom = L.box.y + boxH - pad - payH - gap - btnH - gap;
    L.list = Rect{ x, y, w * 0.4f, bottom - y };
    L.rowsShown = std::max(1, (int)(L.list.h / (rowH + 2.0f * s)));
    for (int i = 0; i < L.rowsShown; ++i) L.rows.push_back(Rect{ L.list.x, L.list.y + i * (rowH + 2.0f * s), L.list.w, rowH });
    L.items = Rect{ x + w * 0.4f + gap, y, w * 0.6f - gap, bottom - y };
    const float cell = ITEM_CELL_PX, cellGap = 4.0f * s;
    L.cols = std::max(1, (int)((L.items.w - 8.0f * s) / (cell + cellGap)));
    const int rowsOfCells = std::max(1, (int)((L.items.h - 8.0f * s) / (cell + cellGap)));
    L.cellsShown = L.cols * rowsOfCells;
    for (int i = 0; i < L.cellsShown; ++i) {
        L.cells.push_back(Rect{ L.items.x + 4.0f * s + (i % L.cols) * (cell + cellGap), L.items.y + 4.0f * s + (i / L.cols) * (cell + cellGap), cell, cell });
    }
    y = bottom + gap;
    L.orders = Rect{ x, y, w * 0.6f, btnH };
    L.clearOrders = Rect{ x + w - 160.0f * s, y, 160.0f * s, btnH };
    y += btnH + gap;
    L.payTitle = Rect{ x, y, w, line };
    y += line;
    const float pw = (w - (payCols - 1) * 4.0f * s) / payCols;
    for (int i = 0; i < cats; ++i) {
        L.pay.push_back(Rect{ x + (i % payCols) * (pw + 4.0f * s), y + (i / payCols) * (payRowH + 4.0f * s), pw, payRowH });
    }
    return L;
}

double fillRatio(const Storage& st) {
    if (st.capacity > 0) return (double)storedCount(st) / (double)st.capacity;
    int used = 0;
    for (const ItemStack& s : st.slots) used += s.item > 0 ? 1 : 0;
    return st.slots.empty() ? 0.0 : (double)used / (double)st.slots.size();
}

// Your stores in your claim (not the Stall itself, nor an Altar), in the order picked
std::vector<ObjectId> stallSources(World& w, ObjectId stall, int sort) {
    const std::vector<Claim> claims = claimsOf(w);
    std::vector<ObjectId> out;
    for (ObjectId p : w.view<Storage, Prop, Transform>()) {
        if (p == stall || w.has<Blueprint>(p)) continue;
        const PropInfo& info = propInfo(w.get<Prop>(p)->kind);
        if (info.claimsLand || info.stall) continue;
        const Owner* own = w.get<Owner>(p);
        const Transform* tr = w.get<Transform>(p);
        if (own && own->faction != FACTION_PLAYER) continue;
        if (!claimAt(claims, tr->x, tr->z, FACTION_PLAYER)) continue;
        out.push_back(p);
    }
    std::sort(out.begin(), out.end(), [&](ObjectId a, ObjectId b) {
        const Storage& sa = *w.get<Storage>(a);
        const Storage& sb = *w.get<Storage>(b);
        if (sort == 1 && storedCount(sa) != storedCount(sb)) return storedCount(sa) > storedCount(sb);
        if (sort == 2 && fillRatio(sa) != fillRatio(sb)) return fillRatio(sa) > fillRatio(sb);
        const int ka = idx(w.get<Prop>(a)->kind), kb = idx(w.get<Prop>(b)->kind);
        if (ka != kb) return ka < kb;
        return a < b;
    });
    return out;
}

// The store's filled slots, in order
std::vector<int> filledSlots(const Storage& st) {
    std::vector<int> out;
    for (int i = 0; i < (int)st.slots.size(); ++i) {
        if (st.slots[(size_t)i].item > 0 && st.slots[(size_t)i].count > 0) out.push_back(i);
    }
    return out;
}
}

void App::openStall(ObjectId stall) {
    if (!world.has<StallStock>(stall)) return;
    stallUi = StallUi{};
    stallUi.open = true;
    stallUi.stall = stall;
    const std::vector<ObjectId> sources = stallSources(world, stall, 0);
    if (!sources.empty()) stallUi.store = sources.front();
    session.contextMenu.open = false;
    itemMenu.open = false;
    playSfx(Sfx::UiMenuOpen);
}

void App::scrollStall(float mx, float my, float wheel) {
    if (!stallUi.open) return;
    const StallLayout L = layoutStall(winW, winH);
    const int step = wheel > 0.0f ? -1 : wheel < 0.0f ? 1 : 0;
    if (L.list.contains(mx, my)) {
        const int n = (int)stallSources(world, stallUi.stall, stallUi.sort).size();
        stallUi.listScroll = std::clamp(stallUi.listScroll + step, 0, std::max(0, n - L.rowsShown));
    } else if (L.items.contains(mx, my)) {
        const Storage* st = world.get<Storage>(stallUi.store);
        const int n = st ? (int)filledSlots(*st).size() : 0;
        const int rows = (n + L.cols - 1) / L.cols, shownRows = L.cellsShown / L.cols;
        stallUi.itemScroll = std::clamp(stallUi.itemScroll + step, 0, std::max(0, rows - shownRows));
    }
}

bool App::pressOnStall() {
    if (!stallUi.open) return false;
    StallStock* ss = world.get<StallStock>(stallUi.stall);
    if (!ss) {
        stallUi = StallUi{};
        return true;
    }
    const float x = mouseX, y = mouseY;
    const StallLayout L = layoutStall(winW, winH);
    if (L.close.contains(x, y) || !L.box.contains(x, y)) {
        stallUi = StallUi{};
        playSfx(Sfx::UiBack);
        return true;
    }
    const Rect sorts[3] = { L.sortKind, L.sortFull, L.sortRatio };
    for (int i = 0; i < 3; ++i) {
        if (!sorts[i].contains(x, y)) continue;
        stallUi.sort = i;
        stallUi.listScroll = 0;
        playSfx(Sfx::UiClick);
        return true;
    }
    const std::vector<ObjectId> sources = stallSources(world, stallUi.stall, stallUi.sort);
    for (size_t i = 0; i < L.rows.size(); ++i) {
        const size_t at = (size_t)stallUi.listScroll + i;
        if (at >= sources.size() || !L.rows[i].contains(x, y)) continue;
        stallUi.store = sources[at];
        stallUi.itemScroll = 0;
        playSfx(Sfx::UiClick);
        return true;
    }
    if (const Storage* st = world.get<Storage>(stallUi.store)) {
        const std::vector<int> filled = filledSlots(*st);
        for (size_t i = 0; i < L.cells.size(); ++i) {
            const size_t at = (size_t)(stallUi.itemScroll * L.cols) + i;
            if (at >= filled.size() || !L.cells[i].contains(x, y)) continue;
            const ItemStack stack = st->slots[(size_t)filled[at]];
            if (requestStallHaul(world, stallUi.stall, stallUi.store, stack.item, stack.count)) {
                showNotice(langf("MISC_STALL_ORDERED", { std::to_string(stack.count), itemNameOf(stack.item) }));
                playSfx(Sfx::UiOrder);
            } else {
                refuse(lang("MISC_STALL_ALREADY_ORDERED"));
            }
            return true;
        }
    }
    if (L.clearOrders.contains(x, y) && !ss->requests.empty()) {
        ss->requests.erase(std::remove_if(ss->requests.begin(), ss->requests.end(), [](const StallRequest& r) { return r.takenBy == NO_OBJECT; }),
                           ss->requests.end());
        playSfx(Sfx::UiClick);
        return true;
    }
    for (size_t i = 0; i < L.pay.size(); ++i) {
        if (!L.pay[i].contains(x, y)) continue;
        ss->payment ^= storeBit((StoreCategory)i);
        playSfx(Sfx::UiToggle);
        return true;
    }
    return true;
}

void App::drawStall() {
    if (!stallUi.open) return;
    const StallStock* ss = world.get<StallStock>(stallUi.stall);
    if (!ss) {
        stallUi = StallUi{};
        return;
    }
    const StallLayout L = layoutStall(winW, winH);
    const float s = uiScaleFor((float)winH);
    const float th = renderer.listTextHeight("Ag");
    auto mid = [&](const Rect& r) { return r.y + (r.h - th) * 0.5f; };
    renderer.drawScrim();
    renderer.drawThemedPanel(L.box);
    renderer.drawListText(langf("UILABEL_STOCK_STALL_TITLE", { propDisplayName(world, stallUi.stall) }), L.title.x, mid(L.title), theme::GOLD);
    renderer.drawButton(L.close, "X", true, L.close.contains(mouseX, mouseY));
    const Rect sorts[3] = { L.sortKind, L.sortFull, L.sortRatio };
    const char* sortKeys[3] = { "UILABEL_SORT_KIND", "UILABEL_SORT_FULLEST", "UILABEL_SORT_RATIO" };
    for (int i = 0; i < 3; ++i) renderer.drawButton(sorts[i], lang(sortKeys[i]), stallUi.sort != i, sorts[i].contains(mouseX, mouseY));

    // The stores
    renderer.drawRect(L.list, RGBA{ 0.0f, 0.0f, 0.0f, 0.35f });
    const std::vector<ObjectId> sources = stallSources(world, stallUi.stall, stallUi.sort);
    stallUi.listScroll = std::clamp(stallUi.listScroll, 0, std::max(0, (int)sources.size() - L.rowsShown));
    if (sources.empty()) renderer.drawListText(lang("UILABEL_STALL_NO_STORES"), L.list.x + 6.0f * s, L.list.y + 6.0f * s, theme::TEXT_DIM);
    for (size_t i = 0; i < L.rows.size(); ++i) {
        const size_t at = (size_t)stallUi.listScroll + i;
        if (at >= sources.size()) break;
        const ObjectId p = sources[at];
        const Rect& r = L.rows[i];
        const bool picked = p == stallUi.store, hot = r.contains(mouseX, mouseY);
        renderer.drawRect(r, picked ? RGBA{ 0.45f, 0.18f, 0.20f, 0.95f } : hot ? RGBA{ 0.32f, 0.13f, 0.16f, 0.95f } : RGBA{ 0.20f, 0.08f, 0.10f, 0.9f });
        const Storage& st = *world.get<Storage>(p);
        const std::string fill = st.capacity > 0 ? std::to_string(storedCount(st)) + "/" + std::to_string(st.capacity)
                                                 : std::to_string((int)std::lround(fillRatio(st) * 100.0)) + "%";
        renderer.drawListText(propDisplayName(world, p), r.x + 6.0f * s, mid(r), RGBA{ 1.0f, 1.0f, 1.0f, 1.0f });
        renderer.drawListText(fill, r.x + r.w - renderer.listTextWidth(fill) - 6.0f * s, mid(r), theme::TEXT_DIM);
    }

    // What the picked store holds: click to order it to the Stall
    renderer.drawRect(L.items, RGBA{ 0.0f, 0.0f, 0.0f, 0.35f });
    int hoverItem = 0, hoverCount = 0;
    if (const Storage* st = world.get<Storage>(stallUi.store)) {
        const std::vector<int> filled = filledSlots(*st);
        if (filled.empty()) renderer.drawListText(lang("UILABEL_STALL_STORE_EMPTY"), L.items.x + 6.0f * s, L.items.y + 6.0f * s, theme::TEXT_DIM);
        for (size_t i = 0; i < L.cells.size(); ++i) {
            const size_t at = (size_t)(stallUi.itemScroll * L.cols) + i;
            if (at >= filled.size()) break;
            const ItemStack& stack = st->slots[(size_t)filled[at]];
            bool ordered = false;
            for (const StallRequest& r : ss->requests) ordered = ordered || (r.from == stallUi.store && r.item == stack.item);
            renderer.drawItemCell(L.cells[i], stack.item, stack.count, ordered ? 0.45f : 1.0f);
            if (L.cells[i].contains(mouseX, mouseY)) {
                hoverItem = stack.item;
                hoverCount = stack.count;
            }
        }
    }
    const std::string orders = langf("UILABEL_STALL_ORDERS", { std::to_string(ss->requests.size()) }) +
                               (stallManned(world, stallUi.stall) != NO_OBJECT ? "  " + lang("MISC_STALL_MANNED") : "  " + lang("MISC_STALL_UNMANNED"));
    renderer.drawListText(orders, L.orders.x, mid(L.orders), theme::TEXT_DIM);
    renderer.drawButton(L.clearOrders, lang("UILABEL_STALL_CLEAR_ORDERS"), !ss->requests.empty(), L.clearOrders.contains(mouseX, mouseY));

    // Payment it takes
    renderer.drawListText(lang("UILABEL_STALL_PAYMENT"), L.payTitle.x, mid(L.payTitle), theme::GOLD);
    for (size_t i = 0; i < L.pay.size(); ++i) {
        const bool on = (ss->payment & storeBit((StoreCategory)i)) != 0;
        const Rect& r = L.pay[i];
        renderer.drawRect(r, on ? RGBA{ 0.25f, 0.45f, 0.22f, 0.95f } : RGBA{ 0.20f, 0.08f, 0.10f, 0.9f });
        renderer.drawListText(lang(STORE_CATEGORY_KEYS[i]), r.x + 6.0f * s, mid(r), on ? RGBA{ 1.0f, 1.0f, 1.0f, 1.0f } : theme::TEXT_DIM);
    }
    if (hoverItem != 0) renderer.showItemTooltip(hoverItem, hoverCount, (float)mouseX, (float)mouseY);
}
