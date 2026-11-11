#include "app.h"
#include "recipes.h"

using namespace play;

// Two windows over one list of recipes:
//  - Research, from a Research Table's right-click menu: the recipes that cost Gizmos, each unlocked for your whole
//    faction by spending the Gizmos the table holds.
//  - Crafting Order, from an Advanced Crafting prop's: the recipes your faction knows that are made at it. One is
//    picked at a time, with the stock of what it makes to keep (0, Off: it is made only when a unit is told to Use it).

namespace {
struct CraftLayout {
    Rect box, title, close, header, list, help;
    Rect keepLabel, keepDown, keepBox, keepUp, stock;
    std::vector<Rect> rows, buttons;
};

CraftLayout layoutCraft(int W, int H, bool research) {
    const float s = uiScaleFor((float)H);
    const float pad = 14.0f * s, gap = 6.0f * s, line = 30.0f * s, small = 30.0f * s;
    const float w = std::min((float)W - 20.0f * s, 680.0f * s);
    const float h = std::min((float)H - 60.0f * s, 600.0f * s);
    CraftLayout L;
    L.box = Rect{ std::floor((W - w) * 0.5f), std::floor((H - h) * 0.5f), w, h };
    const float x = L.box.x + pad, inner = w - 2.0f * pad;
    L.title = Rect{ x, L.box.y + pad, inner - line, line };
    L.close = Rect{ L.box.x + w - pad - line, L.box.y + pad, line, line };
    L.header = Rect{ x, L.title.y + line + gap, inner, line };
    const float helpH = 2.0f * 20.0f * s;
    L.help = Rect{ x, L.box.y + h - pad - helpH, inner, helpH };
    float bottom = L.help.y - gap;
    if (!research) {
        // Keep in stock: the label, then - N +, then what the stores hold now
        const float y = bottom - line;
        const float labelW = 150.0f * s, box = 60.0f * s;
        L.keepLabel = Rect{ x, y, labelW, line };
        L.keepDown = Rect{ x + labelW + gap, y, small, line };
        L.keepBox = Rect{ L.keepDown.x + small + gap, y, box, line };
        L.keepUp = Rect{ L.keepBox.x + box + gap, y, small, line };
        L.stock = Rect{ L.keepUp.x + small + 2.0f * gap, y, x + inner - (L.keepUp.x + small + 2.0f * gap), line };
        bottom = y - gap;
    }
    const float top = L.header.y + line + gap;
    L.list = Rect{ x, top, inner, std::max(0.0f, bottom - top) };
    const float rowH = 30.0f * s, rowGap = 2.0f * s, btnW = 92.0f * s;
    const int shown = std::max(1, (int)((L.list.h + rowGap) / (rowH + rowGap)));
    for (int i = 0; i < shown; ++i) {
        const Rect r{ x, top + i * (rowH + rowGap), inner, rowH };
        L.rows.push_back(r);
        L.buttons.push_back(Rect{ r.x + r.w - btnW - 2.0f * s, r.y + 2.0f * s, btnW, r.h - 4.0f * s });
    }
    return L;
}
}

// The recipes the window lists, as places in recipes(): for Research every recipe that costs Gizmos, grouped by the
// prop it is made at; for a Crafting Order those of its prop that your faction knows
std::vector<int> App::craftRows() {
    std::vector<int> out;
    const std::vector<Recipe>& all = recipes();
    const Prop* p = world.get<Prop>(craftUi.prop);
    if (!p) return out;
    for (int i = 0; i < (int)all.size(); ++i) {
        if (craftUi.research) {
            if (all[(size_t)i].gizmos > 0) out.push_back(i);
        } else if (all[(size_t)i].station == p->kind && recipeUnlocked(world, FACTION_PLAYER, i)) {
            out.push_back(i);
        }
    }
    if (craftUi.research) {
        std::stable_sort(out.begin(), out.end(), [&](int a, int b) { return idx(all[(size_t)a].station) < idx(all[(size_t)b].station); });
    }
    return out;
}

void App::openCraftWindow(ObjectId prop, bool research) {
    craftUi = CraftUi{};
    craftUi.open = true;
    craftUi.prop = prop;
    craftUi.research = research;
    session.contextMenu.open = false;
    itemMenu.open = false;
    playSfx(Sfx::UiMenuOpen);
}

bool App::pressOnCraftWindow() {
    if (screen != Screen::Playing || !craftUi.open) return false;
    if (!world.has<Prop>(craftUi.prop)) {
        craftUi = CraftUi{};
        return false;
    }
    if (e.button.button != SDL_BUTTON_LEFT) return true;
    const float x = e.button.x, y = e.button.y;
    const CraftLayout L = layoutCraft(winW, winH, craftUi.research);
    if (L.close.contains(x, y) || !L.box.contains(x, y)) {
        craftUi = CraftUi{};
        playSfx(Sfx::UiBack);
        return true;
    }
    const std::vector<int> rows = craftRows();
    craftUi.scroll = std::clamp(craftUi.scroll, 0, std::max(0, (int)rows.size() - (int)L.rows.size()));
    const CraftOrder order = world.has<CraftOrder>(craftUi.prop) ? *world.get<CraftOrder>(craftUi.prop) : CraftOrder{};
    if (!craftUi.research) {
        const int step = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0 ? 10 : 1;
        if (L.keepDown.contains(x, y) || L.keepUp.contains(x, y)) {
            const int keep = std::clamp(order.keep + (L.keepUp.contains(x, y) ? step : -step), 0, 999);
            world.add(craftUi.prop, CraftOrder{ order.recipe, keep });
            playSfx(keep != order.keep ? Sfx::UiClick : Sfx::UiError);
            return true;
        }
    }
    for (size_t i = 0; i < L.rows.size() && (size_t)craftUi.scroll + i < rows.size(); ++i) {
        if (!L.rows[i].contains(x, y)) continue;
        const int recipe = rows[(size_t)craftUi.scroll + i];
        if (craftUi.research) {
            if (!L.buttons[i].contains(x, y)) return true;
            if (researchRecipe(world, craftUi.prop, recipe, FACTION_PLAYER)) playSfx(Sfx::UiClick);
            else playSfx(Sfx::UiError);
        } else {
            // One at a time: picking the one picked already puts it down
            setCraftOrder(world, craftUi.prop, order.recipe == recipe ? -1 : recipe, order.keep);
            playSfx(Sfx::UiSelect);
        }
        return true;
    }
    return true;
}

void App::scrollCraftWindow(float notches) {
    const CraftLayout L = layoutCraft(winW, winH, craftUi.research);
    const int most = std::max(0, (int)craftRows().size() - (int)L.rows.size());
    craftUi.scroll = std::clamp(craftUi.scroll - (int)notches * 2, 0, most);
}

void App::drawCraftWindow() {
    if (screen != Screen::Playing || !craftUi.open) return;
    const Prop* p = world.get<Prop>(craftUi.prop);
    if (!p) {
        craftUi = CraftUi{};
        return;
    }
    const CraftLayout L = layoutCraft(winW, winH, craftUi.research);
    const float s = uiScaleFor((float)winH);
    const float textH = renderer.listTextHeight("Ag");
    const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f }, gold{ 1.0f, 0.78f, 0.30f, 1.0f }, dim{ 0.80f, 0.72f, 0.64f, 1.0f };
    const RGBA green{ 0.5f, 0.95f, 0.5f, 1.0f };
    renderer.drawThemedPanel(L.box);
    const std::string title = craftUi.research ? lang("UILABEL_RESEARCH_TITLE") : langf("UILABEL_CRAFT_ORDER_TITLE", { lang(propInfo(p->kind).nameKey) });
    renderer.drawListText(renderer.fitListText(title, L.title.w), L.title.x, L.title.y + (L.title.h - textH) * 0.5f, gold);
    renderer.drawCloseButton(L.close);

    const std::vector<Recipe>& all = recipes();
    const std::vector<int> rows = craftRows();
    craftUi.scroll = std::clamp(craftUi.scroll, 0, std::max(0, (int)rows.size() - (int)L.rows.size()));
    const CraftOrder order = world.has<CraftOrder>(craftUi.prop) ? *world.get<CraftOrder>(craftUi.prop) : CraftOrder{};
    const int gizmos = craftUi.research ? gizmosIn(world, craftUi.prop) : 0;

    std::string header;
    if (craftUi.research) header = langf("UILABEL_RESEARCH_GIZMOS", { std::to_string(gizmos) });
    else if (rows.empty()) header = lang("UILABEL_CRAFT_NONE_KNOWN");
    else if (const char* why = craftBlock(world, craftUi.prop)) header = lang(why);
    renderer.drawListText(renderer.fitListText(header, L.header.w), L.header.x, L.header.y + (L.header.h - textH) * 0.5f,
                          craftUi.research ? green : dim);

    for (size_t i = 0; i < L.rows.size() && (size_t)craftUi.scroll + i < rows.size(); ++i) {
        const int at = rows[(size_t)craftUi.scroll + i];
        const Recipe& r = all[(size_t)at];
        const Rect& row = L.rows[i];
        const Rect& btn = L.buttons[i];
        const bool hot = row.contains(mouseX, mouseY);
        const bool known = recipeUnlocked(world, FACTION_PLAYER, at);
        const bool picked = !craftUi.research && order.recipe == at;
        renderer.drawRect(row, picked ? RGBA{ 0.45f, 0.30f, 0.14f, 1.0f } : RGBA{ 0.16f, 0.09f, 0.08f, hot ? 0.95f : 0.6f });
        std::string text = recipeName(r);
        if (craftUi.research) text = lang(propInfo(r.station).nameKey) + ": " + text;
        text += "  -  " + langf("UILABEL_CRAFT_TAKES", { recipeInputsText(r) });
        float right = btn.x - 8.0f * s;
        if (craftUi.research && !known) {
            const std::string cost = langf("UILABEL_RESEARCH_COST", { std::to_string(r.gizmos) });
            const float cw = renderer.listTextWidth(cost);
            renderer.drawListText(cost, right - cw, row.y + (row.h - textH) * 0.5f, gizmos >= r.gizmos ? green : RGBA{ 1.0f, 0.5f, 0.45f, 1.0f });
            right -= cw + 10.0f * s;
        }
        const std::string shown = renderer.fitListText(text, right - (row.x + 8.0f * s));
        renderer.drawListText(shown, row.x + 8.0f * s, row.y + (row.h - textH) * 0.5f, picked || (craftUi.research && known) ? gold : white);
        if (shown != text && hot) renderer.noteTip(text, row);
        if (craftUi.research) {
            if (known) renderer.drawListText(lang("UILABEL_RESEARCH_KNOWN"), btn.x + 6.0f * s, btn.y + (btn.h - textH) * 0.5f, green);
            else renderer.drawButton(btn, lang("UILABEL_RESEARCH_UNLOCK"), gizmos >= r.gizmos, btn.contains(mouseX, mouseY));
        } else {
            renderer.drawButton(btn, lang(picked ? "UILABEL_CRAFT_PICKED" : "UILABEL_CRAFT_PICK"), true, hot);
        }
    }

    if (!craftUi.research) {
        renderer.drawListText(renderer.fitListText(lang("UILABEL_CRAFT_KEEP"), L.keepLabel.w), L.keepLabel.x,
                              L.keepLabel.y + (L.keepLabel.h - textH) * 0.5f, white);
        renderer.drawButton(L.keepDown, "-", order.keep > 0, L.keepDown.contains(mouseX, mouseY));
        renderer.drawRect(L.keepBox, RGBA{ 0.14f, 0.09f, 0.08f, 1.0f });
        const std::string keep = order.keep > 0 ? std::to_string(order.keep) : std::string(lang("UILABEL_CRAFT_KEEP_OFF"));
        renderer.drawListText(keep, L.keepBox.x + (L.keepBox.w - renderer.listTextWidth(keep)) * 0.5f,
                              L.keepBox.y + (L.keepBox.h - textH) * 0.5f, gold);
        renderer.drawButton(L.keepUp, "+", order.keep < 999, L.keepUp.contains(mouseX, mouseY));
        if (order.recipe >= 0 && order.recipe < (int)all.size()) {
            const int have = factionStock(world, FACTION_PLAYER, all[(size_t)order.recipe].item);
            renderer.drawListText(renderer.fitListText(langf("UILABEL_CRAFT_STOCK", { std::to_string(have) }), L.stock.w), L.stock.x,
                                  L.stock.y + (L.stock.h - textH) * 0.5f, order.keep > 0 && have < order.keep ? green : dim);
        }
    }
    float y = L.help.y;
    for (const std::string& line : renderer.wrapListText(lang(craftUi.research ? "UILABEL_RESEARCH_HELP" : "UILABEL_CRAFT_ORDER_HELP"), L.help.w)) {
        if (y + textH > L.help.y + L.help.h + 2.0f) break;
        renderer.drawListText(line, L.help.x, y, dim);
        y += textH + 2.0f;
    }
}
