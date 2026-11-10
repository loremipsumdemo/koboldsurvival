#include "app.h"

using namespace play;

// Faction Rules: your faction's rules for those outside it, one row each with < and > to step its value. A change takes
// hold at once, and the save keeps it (factionrules). The other factions' rules are data/factions.txt's.

namespace {
struct FactionRulesLayout {
    Rect box, title, close, reset;
    std::vector<Rect> labels, down, value, up;
    Rect help;
};

FactionRulesLayout layoutFactionRules(int W, int H) {
    const float s = uiScaleFor((float)H);
    const float pad = 14.0f * s, gap = 6.0f * s, line = 30.0f * s, small = 30.0f * s;
    const int rows = (int)RuleRow::Count;
    const float helpH = 3.0f * 20.0f * s;
    const float w = std::min((float)W - 20.0f * s, 560.0f * s);
    const float h = pad + line + gap + rows * (line + gap) + helpH + gap + line + pad;
    FactionRulesLayout L;
    L.box = Rect{ std::floor((W - w) * 0.5f), std::floor((H - h) * 0.5f), w, h };
    L.title = Rect{ L.box.x + pad, L.box.y + pad, w - 2.0f * pad - line, line };
    L.close = Rect{ L.box.x + w - pad - line, L.box.y + pad, line, line };
    float y = L.title.y + line + gap;
    const float labelW = (w - 2.0f * pad) * 0.45f;
    const float valueX = L.box.x + pad + labelW + gap;
    const float right = L.box.x + w - pad;
    for (int i = 0; i < rows; ++i) {
        L.labels.push_back(Rect{ L.box.x + pad, y, labelW, line });
        L.down.push_back(Rect{ valueX, y, small, line });
        L.up.push_back(Rect{ right - small, y, small, line });
        L.value.push_back(Rect{ valueX + small + gap, y, right - small - gap - (valueX + small + gap), line });
        y += line + gap;
    }
    L.help = Rect{ L.box.x + pad, y, w - 2.0f * pad, helpH };
    y += helpH + gap;
    const float resetW = 200.0f * s;
    L.reset = Rect{ L.box.x + (w - resetW) * 0.5f, y, resetW, line };
    return L;
}
}

void App::openFactionRules() {
    factionRulesOpen = true;
    session.contextMenu.open = false;
    itemMenu.open = false;
    playSfx(Sfx::UiMenuOpen);
}

bool App::pressOnFactionRules() {
    if (screen != Screen::Playing || !factionRulesOpen) return false;
    const float x = e.button.x, y = e.button.y;
    const FactionRulesLayout L = layoutFactionRules(winW, winH);
    if (e.button.button != SDL_BUTTON_LEFT) return true;
    if (L.close.contains(x, y) || !L.box.contains(x, y)) {
        factionRulesOpen = false;
        playSfx(Sfx::UiBack);
        return true;
    }
    FactionRules rules = factionRules(world, FACTION_PLAYER);
    if (L.reset.contains(x, y)) {
        const FactionDef* f = findFaction(FACTION_PLAYER);
        setFactionRules(world, FACTION_PLAYER, f ? f->rules : FactionRules{});
        playSfx(Sfx::UiClick);
        return true;
    }
    // Shift steps the markup by 25 percent
    const int step = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0 ? 5 : 1;
    for (int i = 0; i < (int)RuleRow::Count; ++i) {
        const RuleRow row = (RuleRow)i;
        const bool down = L.down[(size_t)i].contains(x, y), up = L.up[(size_t)i].contains(x, y) || L.value[(size_t)i].contains(x, y);
        if (!down && !up) continue;
        stepRule(rules, row, (up ? 1 : -1) * (row == RuleRow::Markup ? step : 1));
        setFactionRules(world, FACTION_PLAYER, rules);
        playSfx(Sfx::UiClick);
        return true;
    }
    return true;
}

void App::drawFactionRules() {
    if (screen != Screen::Playing || !factionRulesOpen) return;
    const FactionRulesLayout L = layoutFactionRules(winW, winH);
    const float s = uiScaleFor((float)winH);
    const float textH = renderer.listTextHeight("Ag");
    const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f }, gold{ 1.0f, 0.78f, 0.30f, 1.0f }, dim{ 0.80f, 0.72f, 0.64f, 1.0f };
    renderer.drawThemedPanel(L.box);
    renderer.drawListText(renderer.fitListText(langf("UILABEL_FACTION_RULES_TITLE", { factionName(FACTION_PLAYER) }), L.title.w),
                          L.title.x, L.title.y + (L.title.h - textH) * 0.5f, gold);
    renderer.drawCloseButton(L.close);
    const FactionRules& rules = factionRules(world, FACTION_PLAYER);
    std::string tip;
    for (int i = 0; i < (int)RuleRow::Count; ++i) {
        const RuleRow row = (RuleRow)i;
        const Rect& lr = L.labels[(size_t)i];
        const Rect& vr = L.value[(size_t)i];
        const bool hot = lr.contains(mouseX, mouseY) || vr.contains(mouseX, mouseY) || L.down[(size_t)i].contains(mouseX, mouseY) ||
                         L.up[(size_t)i].contains(mouseX, mouseY);
        if (hot) tip = lang(ruleRowTipKey(row));
        renderer.drawListText(renderer.fitListText(lang(ruleRowKey(row)), lr.w), lr.x, lr.y + (lr.h - textH) * 0.5f, hot ? gold : white);
        renderer.drawButton(L.down[(size_t)i], "<", true, L.down[(size_t)i].contains(mouseX, mouseY));
        renderer.drawRect(vr, RGBA{ 0.14f, 0.09f, 0.08f, 1.0f });
        const std::string value = renderer.fitListText(ruleValueText(rules, row), vr.w - 12.0f * s);
        renderer.drawListText(value, vr.x + (vr.w - renderer.listTextWidth(value)) * 0.5f, vr.y + (vr.h - textH) * 0.5f, gold);
        renderer.drawButton(L.up[(size_t)i], ">", true, L.up[(size_t)i].contains(mouseX, mouseY));
    }
    float y = L.help.y;
    for (const std::string& line : renderer.wrapListText(tip.empty() ? lang("UILABEL_FACTION_RULES_HELP") : tip, L.help.w)) {
        if (y + textH > L.help.y + L.help.h) break;
        renderer.drawListText(line, L.help.x, y, dim);
        y += textH + 2.0f;
    }
    const FactionDef* f = findFaction(FACTION_PLAYER);
    const bool changed = f && rules != f->rules;
    renderer.drawButton(L.reset, lang("UILABEL_FACTION_RULES_RESET"), changed, L.reset.contains(mouseX, mouseY));
}
