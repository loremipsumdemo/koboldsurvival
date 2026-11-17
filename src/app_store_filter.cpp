#include "app.h"
#include "systems.h"

using namespace play;

// A general store's Allowed Items window: a row per kind of item, each switched on or off, with Default (all but Wood,
// Stone and bones), All and None below

namespace {
struct StoreFilterLayout {
    Rect box, title, close;
    std::vector<Rect> rows;
    Rect useDefault, all, none;
};

StoreFilterLayout layoutStoreFilter(int W, int H) {
    const float s = uiScaleFor((float)H);
    const float pad = 14.0f * s, gap = 8.0f * s, line = 28.0f * s, rowH = 30.0f * s, rowGap = 3.0f * s, btnH = 32.0f * s;
    StoreFilterLayout L;
    const int n = (int)StoreCategory::Count;
    const float boxW = std::min((float)W - 32.0f * s, 360.0f * s);
    const float boxH = pad * 2.0f + line + gap + n * (rowH + rowGap) + gap + btnH;
    L.box = Rect{ std::floor((W - boxW) * 0.5f), std::floor(std::max(16.0f * s, (H - boxH) * 0.5f)), boxW, boxH };
    const float x = L.box.x + pad, w = boxW - 2.0f * pad;
    float y = L.box.y + pad;
    layoutTitleClose(L.box, pad, line, L.title, L.close);
    y += line + gap;
    for (int i = 0; i < n; ++i) L.rows.push_back(Rect{ x, y + i * (rowH + rowGap), w, rowH });
    y += n * (rowH + rowGap) + gap;
    const float third = (w - 2.0f * gap) / 3.0f;
    L.useDefault = Rect{ x, y, third, btnH };
    L.all = Rect{ x + third + gap, y, third, btnH };
    L.none = Rect{ x + 2.0f * (third + gap), y, third, btnH };
    return L;
}
}

void App::openStoreFilter(ObjectId prop) {
    const Prop* p = world.get<Prop>(prop);
    if (!p || !hasStoreFilter(p->kind)) return;
    storeFilterUi = StoreFilterUi{ true, prop };
    session.contextMenu.open = false;
    itemMenu.open = false;
    playSfx(Sfx::UiMenuOpen);
}

bool App::pressOnStoreFilter() {
    if (!storeFilterUi.open) return false;
    const ObjectId prop = storeFilterUi.prop;
    if (!world.has<Prop>(prop)) {
        storeFilterUi = StoreFilterUi{};
        return true;
    }
    const float x = mouseX, y = mouseY;
    const StoreFilterLayout L = layoutStoreFilter(winW, winH);
    if (dismissAt(L.box, L.close, x, y)) {
        storeFilterUi = StoreFilterUi{};
        playSfx(Sfx::UiBack);
        return true;
    }
    std::uint32_t allowed = storeFilterOf(world, prop);
    const std::uint32_t before = allowed;
    for (size_t i = 0; i < L.rows.size(); ++i) {
        if (L.rows[i].contains(x, y)) allowed ^= storeBit((StoreCategory)i);
    }
    if (L.useDefault.contains(x, y)) allowed = STORE_DEFAULT;
    if (L.all.contains(x, y)) allowed = STORE_ALL;
    if (L.none.contains(x, y)) allowed = 0;
    if (allowed != before) {
        if (allowed == STORE_DEFAULT) world.remove<StoreFilter>(prop);
        else world.add(prop, StoreFilter{ allowed });
        playSfx(Sfx::UiClick);
    }
    return true;
}

void App::drawStoreFilter() {
    if (!storeFilterUi.open) return;
    const Prop* p = world.get<Prop>(storeFilterUi.prop);
    if (!p) {
        storeFilterUi = StoreFilterUi{};
        return;
    }
    const StoreFilterLayout L = layoutStoreFilter(winW, winH);
    const std::uint32_t allowed = storeFilterOf(world, storeFilterUi.prop);
    renderer.drawWindowFrame(L.box, L.title, L.close, langf("UILABEL_STORE_FILTER_TITLE", { lang(propInfo(p->kind).nameKey) }));
    for (size_t i = 0; i < L.rows.size(); ++i) {
        renderer.drawCheckRow(L.rows[i], lang(STORE_CATEGORY_KEYS[i]), (allowed & storeBit((StoreCategory)i)) != 0);
    }
    renderer.drawButton(L.useDefault, lang("UILABEL_STORE_DEFAULT"), allowed != STORE_DEFAULT);
    renderer.noteTip(lang("UILABEL_STORE_DEFAULT_TIP"), L.useDefault);
    renderer.drawButton(L.all, lang("UILABEL_STORE_ALL"), allowed != STORE_ALL);
    renderer.drawButton(L.none, lang("UILABEL_STORE_NONE"), allowed != 0);
}
