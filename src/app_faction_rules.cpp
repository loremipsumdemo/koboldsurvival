#include "app.h"

using namespace play;

// The Faction tab's Settings: your faction's name and rules for those outside it, a row each. A row's Edit button opens a menu of
// everything it can be (and Custom, for a percent, to type an exact one); the choice takes hold at once, and the save keeps it
// (factionrules, factionname). The other factions' rules are data/factions.txt's.

namespace {
static_assert(FACTION_SETTING_ROWS == 1 + PLAYER_RULE_ROWS, "the Faction tab has a row for the name and one for each of your rules");
const char* const BULLET = "\xE2\x80\xA2 ";
}

void App::stopFactionRulesNaming() {
    if (factionRulesNaming) SDL_StopTextInput(window);
    factionRulesNaming = false;
    factionRulesName = playerFactionName();
}

// The faction's name from its color alone, ignoring what you typed
std::string App::colorFactionName() {
    const FactionDef* f = paletteFaction(playerSlot());
    return f ? lang(f->nameKey) : lang("FACTION_PLAYER");
}

// What the menu lists: the name's two choices, or a rule's every value (the current one marked) and Custom for a percent
std::vector<std::string> App::ruleMenuLabels() {
    std::vector<std::string> out;
    if (ruleMenu.row == 0) {
        out.push_back(lang("UILABEL_FACTION_NAME_RENAME"));
        out.push_back(langf("UILABEL_FACTION_NAME_DEFAULT", { colorFactionName() }));
        return out;
    }
    const RuleRow row = (RuleRow)(ruleMenu.row - 1);
    const int now = ruleCurrent(factionRules(world, FACTION_PLAYER), row);
    bool matched = false;
    for (const RuleChoice& c : ruleChoices(row)) {
        const bool on = c.value == now;
        matched = matched || on;
        out.push_back(std::string(on ? BULLET : "") + c.label);
    }
    if (ruleIsNumber(row)) out.push_back(std::string(matched ? "" : BULLET) + lang("UILABEL_RULE_CUSTOM"));
    return out;
}

ItemMenuLayout App::ruleMenuLayout() {
    const UnitPanelLayout pl = squadPanelLayout(winW, winH);
    const size_t i = (size_t)std::clamp(ruleMenu.row, 0, FACTION_SETTING_ROWS - 1);
    const Rect b = i < pl.settingRows.size() ? pl.settingRows[i].edit : Rect{};
    const std::vector<std::string> labels = ruleMenuLabels();
    ItemMenuLayout L = renderer.itemMenuLayout(b.x, b.y + b.h, (int)labels.size(), winW, winH, labels);
    // No room below the button: open upward instead
    if (L.box.y < b.y + b.h - 0.5f) L = renderer.itemMenuLayout(b.x, b.y - L.box.h, (int)labels.size(), winW, winH, labels);
    return L;
}

void App::runRuleMenu(int choice) {
    if (ruleMenu.row == 0) {
        if (choice == 0) {
            factionRulesName = playerFactionName();
            if (!factionRulesNaming) SDL_StartTextInput(window);
            factionRulesNaming = true;
        } else {
            setPlayerFactionName(std::string());
            factionRulesName.clear();
        }
        playSfx(Sfx::UiClick);
        return;
    }
    const RuleRow row = (RuleRow)(ruleMenu.row - 1);
    const std::vector<RuleChoice> choices = ruleChoices(row);
    if (choice >= (int)choices.size()) {
        // Custom: type the percent
        percentPrompt = PercentPrompt{};
        percentPrompt.open = true;
        percentPrompt.faction = true;
        percentPrompt.ruleRow = (int)row;
        percentPrompt.text = std::to_string(ruleCurrent(factionRules(world, FACTION_PLAYER), row));
        SDL_StartTextInput(window);
        playSfx(Sfx::UiClick);
        return;
    }
    FactionRules rules = factionRules(world, FACTION_PLAYER);
    setRule(rules, row, choices[(size_t)choice].value);
    setFactionRules(world, FACTION_PLAYER, rules);
    playSfx(Sfx::UiClick);
}

// A click while the menu is open: it picks, or closes it; a click on the menu or the button that opened it is not passed on
bool App::pressOnRuleMenu() {
    if (screen != Screen::Playing || !ruleMenu.open) return false;
    const ItemMenuLayout L = ruleMenuLayout();
    const int hit = e.button.button == SDL_BUTTON_LEFT ? itemMenuButtonAt(L, e.button.x, e.button.y) : -1;
    const UnitPanelLayout pl = squadPanelLayout(winW, winH);
    const size_t i = (size_t)std::clamp(ruleMenu.row, 0, FACTION_SETTING_ROWS - 1);
    const bool onButton = i < pl.settingRows.size() && pl.settingRows[i].edit.contains(e.button.x, e.button.y);
    ruleMenu.open = false;
    if (hit >= 0) {
        runRuleMenu(hit);
        return true;
    }
    return onButton || L.box.contains(e.button.x, e.button.y);
}

// A click in the Settings rows: an Edit button opens its menu (or, while the name is being typed, finishes it)
bool App::pressOnFactionSettings(const UnitPanelLayout& fp, float x, float y) {
    for (size_t i = 0; i < fp.settingRows.size(); ++i) {
        if (!fp.settingRows[i].edit.contains(x, y)) continue;
        if (i == 0 && factionRulesNaming) {
            stopFactionRulesNaming();
            playSfx(Sfx::UiClick);
            return true;
        }
        stopFactionRulesNaming();
        ruleMenu.open = true;
        ruleMenu.row = (int)i;
        playSfx(Sfx::UiMenuOpen);
        return true;
    }
    return false;
}

void App::drawRuleMenu() {
    if (screen != Screen::Playing || !ruleMenu.open) return;
    renderer.drawItemMenu(ruleMenuLayout(), ruleMenuLabels(), mouseX, mouseY);
}
