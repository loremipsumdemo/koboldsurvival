#include "app.h"
#include <filesystem>
#include "scenario.h"
#include "world_gen.h"

using namespace play;

// ---- Add Custom Scenario: the screen that makes a scenario (scenario.h) and saves it as a file in MAP/SCENARIO

ScenarioEditLayout layoutScenarioEdit(int W, int H, bool factionScope, int rowCount, int rowScroll, int fileCount, int fileScroll) {
    const float s = uiScaleFor(H);
    const float m = 16.0f * s, rowH = 32.0f * s, gap = 8.0f * s, pitch = rowH + 6.0f * s;
    ScenarioEditLayout L;
    const float rightW = 280.0f * s;
    const float availW = (float)W - rightW - 3.0f * m;
    const float panelW = std::min(availW, 780.0f * s);
    L.panel = Rect{ m + (availW - panelW) * 0.5f, m, panelW, (float)H - 2.0f * m };
    const float x = L.panel.x + 14.0f * s, w = L.panel.w - 28.0f * s, labelW = 110.0f * s;
    L.title = Rect{ x, L.panel.y + 12.0f * s, w, 26.0f * s };
    float y = L.panel.y + 46.0f * s;
    L.nameBox = Rect{ x + labelW, y, w - labelW, rowH };
    y += rowH + gap;
    L.aboutBox = Rect{ x + labelW, y, w - labelW, rowH };
    y += rowH + gap;
    const float quarterW = (w - 3.0f * gap) / 4.0f;
    for (int i = 0; i < 4; ++i) L.scopes[i] = Rect{ x + (float)i * (quarterW + gap), y, quarterW, rowH };
    y += rowH + gap;
    if (factionScope) {
        L.slotDown = Rect{ x + labelW, y, rowH, rowH };
        L.slotBox = Rect{ x + labelW + rowH, y, w - labelW - 2.0f * rowH, rowH };
        L.slotUp = Rect{ x + w - rowH, y, rowH, rowH };
        y += rowH + gap;
    }
    for (int i = 0; i < 4; ++i) L.tabs[i] = Rect{ x + (float)i * (quarterW + gap), y, quarterW, rowH };
    y += rowH + gap;
    const float btnH = 44.0f * s, halfW = (w - gap) * 0.5f;
    L.save = Rect{ x, L.panel.y + L.panel.h - btnH - 14.0f * s, halfW, btnH };
    L.back = Rect{ x + halfW + gap, L.save.y, halfW, btnH };
    L.info = Rect{ x, L.save.y - gap - 22.0f * s, w, 22.0f * s };
    const float listBottom = L.info.y - gap * 0.5f;
    L.list = Rect{ x, y, w, std::max(0.0f, listBottom - y) };
    L.shown = std::max(1, (int)std::floor((L.list.h + 6.0f * s) / pitch));
    L.first = std::clamp(rowScroll, 0, std::max(0, rowCount - L.shown));
    L.bar = layoutRowBar(L.list.x + L.list.w - 8.0f * s, L.list.y, std::max(0.0f, (float)L.shown * pitch - 6.0f * s), s, rowCount, L.shown, L.first);
    const float right = L.list.x + L.list.w - (L.bar.on() ? 14.0f * s : 0.0f);
    const float valueW = 170.0f * s, small = 4.0f * s;
    for (int i = L.first; i < rowCount && i < L.first + L.shown; ++i) {
        ScenarioEditLayout::Line line;
        const float ly = L.list.y + (float)(i - L.first) * pitch;
        line.box = Rect{ L.list.x, ly, right - L.list.x, rowH };
        line.reset = Rect{ right - rowH, ly, rowH, rowH };
        line.up = Rect{ line.reset.x - small - rowH, ly, rowH, rowH };
        line.value = Rect{ line.up.x - valueW, ly, valueW, rowH };
        line.down = Rect{ line.value.x - rowH, ly, rowH, rowH };
        line.label = Rect{ L.list.x, ly, std::max(0.0f, line.down.x - 6.0f * s - L.list.x), rowH };
        L.lines.push_back(line);
    }
    // The scenarios saved, down the right
    L.filesBox = Rect{ (float)W - m - rightW, m, rightW, (float)H - 2.0f * m };
    L.fresh = Rect{ L.filesBox.x + 10.0f * s, L.filesBox.y + 40.0f * s, rightW - 20.0f * s, rowH };
    const float fileRow = 30.0f * s, filePitch = fileRow + 4.0f * s;
    const float filesTop = L.fresh.y + L.fresh.h + gap;
    const int fileShown = std::max(0, (int)((L.filesBox.y + L.filesBox.h - 14.0f * s - filesTop + 4.0f * s) / filePitch));
    L.fileFirst = std::clamp(fileScroll, 0, std::max(0, fileCount - fileShown));
    L.fileBar = layoutRowBar(L.filesBox.x + rightW - 10.0f * s - 8.0f * s, filesTop, std::max(0.0f, (float)fileShown * filePitch - 4.0f * s), s,
                             fileCount, fileShown, L.fileFirst);
    const float fileW = rightW - 20.0f * s - fileRow - 4.0f * s - (L.fileBar.on() ? 12.0f * s : 0.0f);
    for (int i = L.fileFirst; i < fileCount && i < L.fileFirst + fileShown; ++i) {
        const float fy = filesTop + (float)(i - L.fileFirst) * filePitch;
        L.fileRows.push_back(Rect{ L.filesBox.x + 10.0f * s, fy, fileW, fileRow });
        L.fileDelete.push_back(Rect{ L.filesBox.x + 10.0f * s + fileW + 4.0f * s, fy, fileRow, fileRow });
    }
    return L;
}

namespace {
// One line of the list of rows: a number of a faction's start, how many of a kind of animal or of prop, or a faction rule
struct EditRow {
    enum Kind { Start, Mob, Prop, Rule } kind = Start;
    int index = 0;      // a StartRow, or a RuleRow
    std::string key;    // a mob's or a prop's key in the file
    std::string label, tip;
};

EditRow startLine(StartRow r) {
    EditRow e;
    e.kind = EditRow::Start;
    e.index = (int)r;
    e.label = lang(startRowLabelKey(r));
    e.tip = lang(startRowTipKey(r));
    return e;
}

std::vector<EditRow> editRows(ScenarioTab tab, ScenarioScope scope) {
    std::vector<EditRow> out;
    const bool player = scope == ScenarioScope::Player;
    switch (tab) {
    case ScenarioTab::Units:
        out.push_back(startLine(StartRow::Military));
        out.push_back(startLine(StartRow::Fortified));
        out.push_back(startLine(StartRow::Citizens));
        break;
    case ScenarioTab::Mobs:
        out.push_back(startLine(StartRow::Caged));
        for (MobKind kind : regularMobKinds()) {
            EditRow e;
            e.kind = EditRow::Mob;
            e.key = mobInfo(kind).key;
            e.label = lang(mobInfo(kind).nameKey);
            e.tip = lang("UILABEL_SCENARIO_MOB_TIP");
            out.push_back(e);
        }
        break;
    case ScenarioTab::Props:
        out.push_back(startLine(StartRow::Camp));
        out.push_back(startLine(StartRow::Extras));
        out.push_back(startLine(StartRow::Stock));
        for (PropKind kind : scenarioPropKinds()) {
            EditRow e;
            e.kind = EditRow::Prop;
            e.key = propInfo(kind).key;
            e.label = lang(propInfo(kind).nameKey);
            e.tip = lang("UILABEL_SCENARIO_PROP_TIP");
            out.push_back(e);
        }
        break;
    case ScenarioTab::Rules:
        // How a faction that runs itself stands with you at the start comes first
        if (!player) out.push_back(startLine(StartRow::Standing));
        for (int i = 0; i < (int)RuleRow::Count; ++i) {
            const RuleRow row = (RuleRow)i;
            // Yours are the rules for outsiders; an NPC faction's all but the two shares its unit counts make
            if (player ? i >= PLAYER_RULE_ROWS : (row == RuleRow::Fortified || row == RuleRow::Military)) continue;
            EditRow e;
            e.kind = EditRow::Rule;
            e.index = i;
            e.label = lang(ruleRowKey(row));
            e.tip = lang(ruleRowTipKey(row));
            out.push_back(e);
        }
        break;
    case ScenarioTab::Count:
        break;
    }
    return out;
}

bool appliesToPlayer(const EditRow& r) {
    if (r.kind == EditRow::Start) return r.index != (int)StartRow::Standing;
    if (r.kind == EditRow::Rule) return r.index < PLAYER_RULE_ROWS;
    return true;
}

// What a scope shows of one faction: the blocks that are its own, over the defaults
FactionStart shownStart(const ScenarioEditUi& ui, bool player) {
    Scenario view;
    int id = FACTION_PLAYER;
    if (player) {
        view.player = ui.scenario.player;
    } else {
        const int slot = ui.scope == ScenarioScope::Faction ? std::clamp(ui.slot, 0, PALETTE_COUNT - 1) : 0;
        view.npc = ui.scenario.npc;
        if (ui.scope == ScenarioScope::Faction) view.factions[slot] = ui.scenario.factions[slot];
        id = paletteFactionId(slot);
    }
    return scenarioStart(view, id);
}

// The block a scope's rows are read from and written to, for the player's side or the other
const ScenarioBlock& blockFor(const ScenarioEditUi& ui, bool player) {
    if (player) return ui.scenario.player;
    return ui.scope == ScenarioScope::Faction ? ui.scenario.factions[std::clamp(ui.slot, 0, PALETTE_COUNT - 1)] : ui.scenario.npc;
}

ScenarioBlock& blockFor(ScenarioEditUi& ui, bool player) {
    return const_cast<ScenarioBlock&>(blockFor(static_cast<const ScenarioEditUi&>(ui), player));
}

// The sides an edit in the scope writes to: yours, the others', or both
std::vector<bool> sidesOf(const ScenarioEditUi& ui, const EditRow& r) {
    switch (ui.scope) {
    case ScenarioScope::Player: return { true };
    case ScenarioScope::All: return appliesToPlayer(r) ? std::vector<bool>{ true, false } : std::vector<bool>{ false };
    default: return { false };
    }
}

int rowValue(const FactionStart& st, const EditRow& r) {
    switch (r.kind) {
    case EditRow::Start:
        switch ((StartRow)r.index) {
        case StartRow::Military: return st.military;
        case StartRow::Fortified: return st.fortified;
        case StartRow::Citizens: return st.citizens;
        case StartRow::Caged: return st.caged;
        case StartRow::Camp: return (int)st.camp;
        case StartRow::Extras: return st.extras;
        case StartRow::Stock: return st.stock;
        case StartRow::Standing: return st.standing;
        case StartRow::Count: break;
        }
        return 0;
    case EditRow::Mob:
        for (const auto& m : st.mobs) {
            if (mobInfo(m.first).key == r.key) return m.second;
        }
        return 0;
    case EditRow::Prop:
        for (const auto& p : st.props) {
            if (propInfo(p.first).key == r.key) return p.second;
        }
        return 0;
    case EditRow::Rule: {
        const auto it = st.rules.find(r.index);
        return it != st.rules.end() ? it->second : -1;
    }
    }
    return 0;
}

std::string ruleText(int row, int value) {
    FactionRules rules;
    setRule(rules, (RuleRow)row, value);
    return ruleValueText(rules, (RuleRow)row);
}

// What a faction's table says of a rule, for a row the scenario leaves alone
int tableRule(const ScenarioEditUi& ui, bool player, int row) {
    const FactionDef* f = player ? findFaction(FACTION_PLAYER) : paletteFaction(ui.scope == ScenarioScope::Faction ? std::clamp(ui.slot, 0, PALETTE_COUNT - 1) : 0);
    return ruleCurrent(f ? f->rules : FactionRules{}, (RuleRow)row);
}

struct RowShown {
    std::string text;
    bool set = false;
};

RowShown showRow(const ScenarioEditUi& ui, const EditRow& r, bool player) {
    const FactionStart st = shownStart(ui, player);
    const ScenarioBlock& block = blockFor(ui, player);
    RowShown out;
    const int v = rowValue(st, r);
    switch (r.kind) {
    case EditRow::Start:
        out.set = block.rows.count(r.index) > 0;
        if ((StartRow)r.index == StartRow::Camp) out.text = lang(startCampKey(v));
        else if ((StartRow)r.index == StartRow::Extras && v < 0) out.text = lang("UILABEL_SCENARIO_RANDOM");
        else if ((StartRow)r.index == StartRow::Standing) out.text = v == STANDING_BY_LOOP ? lang("UILABEL_SCENARIO_BY_LOOP") : (v > 0 ? "+" : "") + std::to_string(v);
        else out.text = std::to_string(v);
        break;
    case EditRow::Mob:
        out.set = block.mobs.count(r.key) > 0;
        out.text = std::to_string(v);
        break;
    case EditRow::Prop:
        out.set = block.props.count(r.key) > 0;
        out.text = std::to_string(v);
        break;
    case EditRow::Rule:
        out.set = block.rules.count(r.index) > 0;
        if (v >= 0) out.text = ruleText(r.index, v);
        else if (ui.scope == ScenarioScope::Faction || player) out.text = ruleText(r.index, tableRule(ui, player, r.index));
        else out.text = lang("UILABEL_SCENARIO_EACH_OWN");
        break;
    }
    return out;
}

// What a row reads in the scope: for All, what both sides read when they agree
RowShown showScope(const ScenarioEditUi& ui, const EditRow& r) {
    if (ui.scope == ScenarioScope::Player) return showRow(ui, r, true);
    if (ui.scope != ScenarioScope::All || !appliesToPlayer(r)) return showRow(ui, r, false);
    const RowShown a = showRow(ui, r, true), b = showRow(ui, r, false);
    RowShown out = b;
    out.set = a.set && b.set;
    if (a.text != b.text) out.text = lang("UILABEL_SCENARIO_MIXED");
    return out;
}

void forgetRow(ScenarioBlock& b, const EditRow& r) {
    switch (r.kind) {
    case EditRow::Start: b.rows.erase(r.index); break;
    case EditRow::Mob: b.mobs.erase(r.key); break;
    case EditRow::Prop: b.props.erase(r.key); break;
    case EditRow::Rule: b.rules.erase(r.index); break;
    }
}

// Steps a row up or down in the scope: `by` steps (a row's own size, startRowStep; Camp and a rule go round their words). False if nothing moved.
bool stepRow(ScenarioEditUi& ui, const EditRow& r, int by) {
    bool moved = false;
    const int dir = by > 0 ? 1 : -1;
    const std::vector<bool> sides = sidesOf(ui, r);
    // In the All scope the two sides read alike, or Mixed: a step from Mixed starts from the others' value and sets both to what it comes to
    constexpr int NOT_UNIFIED = -100000;
    int unified = NOT_UNIFIED;
    if (ui.scope == ScenarioScope::All && sides.size() == 2) {
        const int yours = rowValue(shownStart(ui, true), r), others = rowValue(shownStart(ui, false), r);
        if (yours != others) unified = r.kind == EditRow::Rule && others < 0 ? ruleCurrent(FactionRules{}, (RuleRow)r.index) : others;
    }
    for (const bool player : sides) {
        const FactionStart st = shownStart(ui, player);
        ScenarioBlock& block = blockFor(ui, player);
        const int v = unified != NOT_UNIFIED ? unified : rowValue(st, r);
        switch (r.kind) {
        case EditRow::Start: {
            const StartRow row = (StartRow)r.index;
            const int least = startRowLeast(row, player), most = startRowMost(row, player);
            if (row == StartRow::Camp) {
                int n = v + dir;
                n = n > most ? least : n < least ? most : n;
                block.rows[r.index] = n;
                moved = true;
                break;
            }
            // A step is the row's own size (a standing by 5, a stock by 50); a standing the scenario leaves to the loop starts from neutral
            const int base = row == StartRow::Standing && v == STANDING_BY_LOOP ? 0 : v;
            int n = base + by * startRowStep(row);
            if (row == StartRow::Extras && n < 0 && !player) {
                // Below none, back to the random ones the camps have always had
                if (block.rows.erase(r.index) > 0) moved = true;
                break;
            }
            n = std::clamp(n, least, most);
            if (player && by > 0 && (row == StartRow::Military || row == StartRow::Fortified || row == StartRow::Citizens)) {
                // Your party holds only so many, its animals with it
                n = std::min(n, v + std::max(0, PARTY_MAX_UNITS - st.people() - st.mobCount()));
            }
            if (n == v) break;
            block.rows[r.index] = n;
            moved = true;
            break;
        }
        case EditRow::Mob: {
            int n = std::clamp(v + by, 0, SCENARIO_MOST_MOBS);
            if (player && by > 0) n = std::min(n, v + std::max(0, PARTY_MAX_UNITS - st.people() - st.mobCount()));
            if (n == v) break;
            block.mobs[r.key] = n;
            moved = true;
            break;
        }
        case EditRow::Prop: {
            const int n = std::clamp(v + by, 0, SCENARIO_MOST_MOBS);
            if (n == v) break;
            block.props[r.key] = n;
            moved = true;
            break;
        }
        case EditRow::Rule: {
            // From what the row reads now; a row the scenario leaves alone starts from its faction's table (the NPC block's, from the plain defaults)
            const bool tableKnown = (player || ui.scope == ScenarioScope::Faction) && unified == NOT_UNIFIED;
            const int base = v >= 0 ? v : (tableKnown ? tableRule(ui, player, r.index) : ruleCurrent(FactionRules{}, (RuleRow)r.index));
            FactionRules rules;
            setRule(rules, (RuleRow)r.index, base);
            stepRule(rules, (RuleRow)r.index, dir);
            block.rules[r.index] = ruleCurrent(rules, (RuleRow)r.index);
            moved = true;
            break;
        }
        }
    }
    // A row set for every faction is every faction's: no color keeps an older value of its own
    if (moved && ui.scope == ScenarioScope::All) {
        for (ScenarioBlock& f : ui.scenario.factions) forgetRow(f, r);
    }
    return moved;
}

void resetRow(ScenarioEditUi& ui, const EditRow& r) {
    for (const bool player : sidesOf(ui, r)) forgetRow(blockFor(ui, player), r);
    if (ui.scope == ScenarioScope::All) {
        for (ScenarioBlock& f : ui.scenario.factions) forgetRow(f, r);
    }
}

std::string infoText(const ScenarioEditUi& ui) {
    const FactionStart mine = shownStart(ui, true), others = shownStart(ui, false);
    switch (ui.scope) {
    case ScenarioScope::Player:
        return langf("UILABEL_SCENARIO_INFO_PLAYER", { std::to_string(mine.people()), std::to_string(mine.mobCount()), std::to_string(PARTY_MAX_UNITS) });
    case ScenarioScope::All:
        return langf("UILABEL_SCENARIO_INFO_ALL", { std::to_string(others.people()), std::to_string(mine.people()), std::to_string(mine.mobCount()) });
    default:
        return langf("UILABEL_SCENARIO_INFO_NPC", { std::to_string(others.people()), std::to_string(others.mobCount()) });
    }
}

std::string scopeTipKey(int scope) {
    static const char* const KEYS[] = { "UILABEL_SCENARIO_SCOPE_ALL_TIP", "UILABEL_SCENARIO_SCOPE_NPC_TIP", "UILABEL_SCENARIO_SCOPE_PLAYER_TIP",
                                        "UILABEL_SCENARIO_SCOPE_FACTION_TIP" };
    return KEYS[std::clamp(scope, 0, 3)];
}

constexpr float TEXT_PAD = 8.0f;
}

// A new scenario starts as the Outpost is, with each of its numbers written out so it can be seen and changed from there
static Scenario outpostCopy() {
    Scenario s = outpostScenario();
    s.builtin = false;
    for (const bool player : { true, false }) {
        const FactionStart st = defaultStart(player ? FACTION_PLAYER : paletteFactionId(0));
        ScenarioBlock& b = player ? s.player : s.npc;
        b.rows[(int)StartRow::Military] = st.military;
        b.rows[(int)StartRow::Fortified] = st.fortified;
        b.rows[(int)StartRow::Citizens] = st.citizens;
        b.rows[(int)StartRow::Caged] = st.caged;
        b.rows[(int)StartRow::Camp] = (int)st.camp;
        b.rows[(int)StartRow::Stock] = st.stock;
        if (player) b.rows[(int)StartRow::Extras] = st.extras;
    }
    return s;
}

void App::openScenarioEditor() {
    scenarioEdit = ScenarioEditUi{};
    scenarioEdit.scenario = outpostCopy();
    scenarioEdit.files = listScenarioFiles();
    scenarioEdit.name = lang("UILABEL_SCENARIO_DEFAULT_NAME");
    scenarioEdit.savedText = scenarioToText(scenarioEdit.scenario);
    screen = Screen::ScenarioEdit;
}

void App::stopScenarioText() {
    if (scenarioEdit.editingName || scenarioEdit.editingAbout) SDL_StopTextInput(window);
    scenarioEdit.editingName = false;
    scenarioEdit.editingAbout = false;
}

std::string App::scenarioTitle(const Scenario& s) const {
    return s.builtin ? lang("SCENARIO_OUTPOST_NAME") : s.name;
}

std::string App::scenarioAbout(const Scenario& s) const {
    if (s.builtin) return lang("SCENARIO_OUTPOST_DESC");
    const FactionStart mine = scenarioStart(s, FACTION_PLAYER);
    const FactionStart others = scenarioStart(s, paletteFactionId(0));
    const std::string summary = langf("SCENARIO_SUMMARY", { std::to_string(mine.military), std::to_string(mine.citizens), std::to_string(mine.mobCount()),
                                                             std::to_string(others.people()) });
    return s.about.empty() ? summary : s.about + "  " + summary;
}

// ---- Drawing

void App::drawScenarioEditScreen() {
    ScenarioEditUi& ui = scenarioEdit;
    const std::vector<EditRow> rows = editRows(ui.tab, ui.scope);
    const ScenarioEditLayout L = layoutScenarioEdit(winW, winH, ui.scope == ScenarioScope::Faction, (int)rows.size(), ui.scroll, (int)ui.files.size(), ui.fileScroll);
    ui.scroll = L.first;
    ui.fileScroll = L.fileFirst;
    const float s = uiScaleFor(winH);
    const bool asking = ui.ask != ScenarioAsk::None;
    const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f }, gray{ 0.80f, 0.72f, 0.64f, 1.0f }, gold{ 1.0f, 0.78f, 0.30f, 1.0f }, dull{ 0.62f, 0.56f, 0.50f, 1.0f };
    renderer.beginUi(winW, winH);
    drawMenuBackdrop();
    renderer.drawThemedPanel(L.panel);
    const float textH = renderer.listTextHeight("Ag");
    renderer.drawListText(lang("UILABEL_ADD_CUSTOM_SCENARIO"), L.title.x, L.title.y, gold);
    const auto label = [&](const std::string& text, const Rect& box) {
        renderer.drawListText(text, L.panel.x + 14.0f * s, box.y + (box.h - textH) * 0.5f, gray);
    };
    const auto field = [&](const Rect& r, const std::string& text, bool active) {
        renderer.drawField(r, active);
        renderer.drawListText(renderer.fitListText(text + (active ? "|" : ""), r.w - 2.0f * TEXT_PAD * s), r.x + TEXT_PAD * s, r.y + (r.h - textH) * 0.5f, white);
    };
    label(lang("UILABEL_NAME"), L.nameBox);
    field(L.nameBox, ui.name, ui.editingName);
    label(lang("UILABEL_SCENARIO_ABOUT"), L.aboutBox);
    field(L.aboutBox, ui.scenario.about, ui.editingAbout);

    static const char* const SCOPE_KEYS[] = { "UILABEL_SCENARIO_SCOPE_ALL", "UILABEL_SCENARIO_SCOPE_NPC", "UILABEL_SCENARIO_SCOPE_PLAYER",
                                              "UILABEL_SCENARIO_SCOPE_FACTION" };
    static const char* const TAB_KEYS[] = { "UILABEL_SCENARIO_TAB_UNITS", "UILABEL_SCENARIO_TAB_MOBS", "UILABEL_SCENARIO_TAB_PROPS",
                                            "UILABEL_SCENARIO_TAB_RULES" };
    for (int i = 0; i < 4; ++i) {
        renderer.drawToggleButton(L.scopes[i], lang(SCOPE_KEYS[i]), (int)ui.scope == i);
        if (!asking) renderer.noteTip(lang(scopeTipKey(i)), L.scopes[i]);
        renderer.drawToggleButton(L.tabs[i], lang(TAB_KEYS[i]), (int)ui.tab == i);
    }
    if (ui.scope == ScenarioScope::Faction) {
        const int id = paletteFactionId(std::clamp(ui.slot, 0, PALETTE_COUNT - 1));
        label(lang("UILABEL_SCENARIO_SCOPE_FACTION"), L.slotBox);
        renderer.drawButton(L.slotDown, "<", true);
        renderer.drawField(L.slotBox, false);
        const FactionColor c = factionColor(id);
        const float sw = L.slotBox.h * 0.5f;
        renderer.drawRect(Rect{ L.slotBox.x + 8.0f * s, L.slotBox.y + (L.slotBox.h - sw) * 0.5f, sw, sw }, RGBA{ c.r, c.g, c.b, 1.0f });
        renderer.drawListText(renderer.fitListText(factionName(id), L.slotBox.w - sw - 24.0f * s), L.slotBox.x + 16.0f * s + sw, L.slotBox.y + (L.slotBox.h - textH) * 0.5f, white);
        renderer.drawButton(L.slotUp, ">", true);
    }

    const float hoverX = asking ? -1.0f : mouseX, hoverY = asking ? -1.0f : mouseY;
    for (size_t i = 0; i < L.lines.size(); ++i) {
        const ScenarioEditLayout::Line& line = L.lines[i];
        const EditRow& r = rows[(size_t)L.first + i];
        const RowShown shown = showScope(ui, r);
        renderer.drawListText(renderer.fitListText(r.label, line.label.w), line.label.x, line.label.y + (line.label.h - textH) * 0.5f, shown.set ? gold : white);
        if (!asking && !r.tip.empty()) renderer.noteTip(r.tip, line.label);
        renderer.drawButton(line.down, "-", true);
        renderer.drawField(line.value, false);
        renderer.drawListText(renderer.fitListText(shown.text, line.value.w - 2.0f * TEXT_PAD * s), line.value.x + TEXT_PAD * s,
                              line.value.y + (line.value.h - textH) * 0.5f, shown.set ? white : dull);
        renderer.drawButton(line.up, "+", true);
        renderer.drawButton(line.reset, "x", shown.set);
    }
    drawListBar(L.bar);
    renderer.drawListText(renderer.fitListText(infoText(ui), L.info.w), L.info.x, L.info.y + (L.info.h - textH) * 0.5f, gray);
    renderer.drawButton(L.save, lang("UILABEL_SCENARIO_SAVE"), !safeFileName(ui.name).empty());
    renderer.drawButton(L.back, lang("UILABEL_RETURN_TO_TITLE"), true);

    // The scenarios saved
    renderer.drawThemedPanel(L.filesBox);
    renderer.drawListText(lang("UILABEL_SCENARIO_SAVED"), L.filesBox.x + 10.0f * s, L.filesBox.y + 12.0f * s, gray);
    renderer.drawButton(L.fresh, lang("UILABEL_SCENARIO_NEW"), true);
    for (size_t i = 0; i < L.fileRows.size(); ++i) {
        const Rect& row = L.fileRows[i];
        const std::string& name = ui.files[(size_t)L.fileFirst + i];
        renderer.drawListButton(row, "", row.contains(hoverX, hoverY));
        const std::string shown = renderer.fitListText(name, row.w - 16.0f * s);
        if (!asking && shown != name) renderer.noteTip(name, row);
        renderer.drawListText(shown, row.x + 8.0f * s, row.y + (row.h - textH) * 0.5f, name == ui.loadedName ? gold : white);
        renderer.drawListButton(L.fileDelete[i], "X", L.fileDelete[i].contains(hoverX, hoverY));
    }
    drawListBar(L.fileBar);

    if (asking) {
        dimScreen(0.45f);
        std::vector<std::pair<std::string, RGBA>> lines;
        lines.push_back({ langf(ui.ask == ScenarioAsk::Delete ? "MISC_SCENARIO_DELETE_Q" : "MISC_SCENARIO_OVERWRITE_Q", { ui.askName }), white });
        const MapQuestionLayout D = layoutMapQuestion(winW, winH, (int)lines.size());
        renderer.drawThemedPanel(D.box);
        for (size_t i = 0; i < lines.size(); ++i) renderer.drawUiTextInBox(lines[i].first, D.lines[i], lines[i].second);
        renderer.drawButton(D.confirm, lang(ui.ask == ScenarioAsk::Delete ? "UILABEL_DELETE" : "UILABEL_GEN_OVERWRITE"), true);
        renderer.drawButton(D.cancel, lang("UILABEL_CANCEL"), true);
    }
    renderer.endUi();
}

// ---- Input

// Writes the scenario to its file in MAP/SCENARIO. One whose name is another file's is only written over after a question, which this asks
// (overwriteConfirmed: it was answered).
void App::scenarioSave(bool overwriteConfirmed) {
    ScenarioEditUi& ui = scenarioEdit;
    const std::string name = safeFileName(ui.name);
    if (name.empty()) {
        refuse(lang("MISC_SCENARIO_NEEDS_NAME"));
        return;
    }
    ui.name = name;
    std::error_code ec;
    const bool exists = std::filesystem::exists(std::filesystem::u8path(scenarioFilePath(name)), ec);
    if (exists && name != ui.loadedName && !overwriteConfirmed) {
        ui.ask = ScenarioAsk::Overwrite;
        ui.askName = name;
        playSfx(Sfx::UiClick);
        return;
    }
    std::string err;
    ui.scenario.name = name;
    if (!writeScenarioFile(name, ui.scenario, err)) {
        refuse(err);
        return;
    }
    ui.loadedName = name;
    ui.files = listScenarioFiles();
    ui.savedText = scenarioToText(ui.scenario);
    playSfx(Sfx::UiSaved);
    showNotice(langf("MISC_SCENARIO_SAVED", { name }));
}

void App::scenarioEditClick(float x, float y) {
    ScenarioEditUi& ui = scenarioEdit;
    const std::vector<EditRow> rows = editRows(ui.tab, ui.scope);
    const ScenarioEditLayout L = layoutScenarioEdit(winW, winH, ui.scope == ScenarioScope::Faction, (int)rows.size(), ui.scroll, (int)ui.files.size(), ui.fileScroll);

    // A question over the screen takes every click
    if (ui.ask != ScenarioAsk::None) {
        const MapQuestionLayout D = layoutMapQuestion(winW, winH, 1);
        if (D.confirm.contains(x, y)) {
            const ScenarioAsk question = ui.ask;
            const std::string name = ui.askName;
            ui.ask = ScenarioAsk::None;
            if (question == ScenarioAsk::Delete) {
                deleteScenarioFile(name);
                if (ui.loadedName == name) ui.loadedName.clear();
                ui.files = listScenarioFiles();
                playSfx(Sfx::UiClick);
            } else {
                ui.name = name;
                scenarioSave(true);
            }
        } else if (D.cancel.contains(x, y) || !D.box.contains(x, y)) {
            ui.ask = ScenarioAsk::None;
            playSfx(Sfx::UiBack);
        }
        return;
    }
    if (pressListBar(L.bar, x, y, [this](float top) {
            ScenarioEditUi& u = scenarioEdit;
            const std::vector<EditRow> now = editRows(u.tab, u.scope);
            const ScenarioEditLayout M = layoutScenarioEdit(winW, winH, u.scope == ScenarioScope::Faction, (int)now.size(), u.scroll, (int)u.files.size(), u.fileScroll);
            u.scroll = rowBarFirst(M.bar, (int)now.size(), M.shown, top);
        })) return;
    if (pressListBar(L.fileBar, x, y, [this](float top) {
            ScenarioEditUi& u = scenarioEdit;
            const std::vector<EditRow> now = editRows(u.tab, u.scope);
            const ScenarioEditLayout M = layoutScenarioEdit(winW, winH, u.scope == ScenarioScope::Faction, (int)now.size(), u.scroll, (int)u.files.size(), u.fileScroll);
            u.fileScroll = rowBarFirst(M.fileBar, (int)u.files.size(), (int)M.fileRows.size(), top);
        })) return;

    // The two text fields
    if (L.nameBox.contains(x, y) || L.aboutBox.contains(x, y)) {
        const bool name = L.nameBox.contains(x, y);
        if (!ui.editingName && !ui.editingAbout) SDL_StartTextInput(window);
        ui.editingName = name;
        ui.editingAbout = !name;
        playSfx(Sfx::UiClick);
        return;
    }
    stopScenarioText();

    for (int i = 0; i < 4; ++i) {
        if (L.scopes[i].contains(x, y)) {
            if ((int)ui.scope != i) ui.scroll = 0;
            ui.scope = (ScenarioScope)i;
            playSfx(Sfx::UiTab);
            return;
        }
        if (L.tabs[i].contains(x, y)) {
            if ((int)ui.tab != i) ui.scroll = 0;
            ui.tab = (ScenarioTab)i;
            playSfx(Sfx::UiTab);
            return;
        }
    }
    if (ui.scope == ScenarioScope::Faction && (L.slotDown.contains(x, y) || L.slotUp.contains(x, y))) {
        ui.slot = (ui.slot + (L.slotUp.contains(x, y) ? 1 : PALETTE_COUNT - 1)) % PALETTE_COUNT;
        playSfx(Sfx::UiClick);
        return;
    }
    for (size_t i = 0; i < L.lines.size(); ++i) {
        const ScenarioEditLayout::Line& line = L.lines[i];
        const EditRow& r = rows[(size_t)L.first + i];
        const int step = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0 ? 5 : 1;
        if (line.down.contains(x, y) || line.up.contains(x, y)) {
            if (stepRow(ui, r, line.up.contains(x, y) ? step : -step)) playSfx(Sfx::UiClick);
            else playSfx(Sfx::UiError);
            return;
        }
        if (line.reset.contains(x, y)) {
            if (showScope(ui, r).set) {
                resetRow(ui, r);
                playSfx(Sfx::UiClick);
            }
            return;
        }
    }
    if (L.save.contains(x, y)) {
        scenarioSave(false);
        return;
    }
    if (L.back.contains(x, y)) {
        playSfx(Sfx::UiBack);
        screen = Screen::Title;
        return;
    }
    if (L.fresh.contains(x, y)) {
        const std::vector<std::string> files = ui.files;
        const ScenarioScope scope = ui.scope;
        ui = ScenarioEditUi{};
        ui.files = files;
        ui.scope = scope;
        ui.scenario = outpostCopy();
        ui.name = lang("UILABEL_SCENARIO_DEFAULT_NAME");
        ui.savedText = scenarioToText(ui.scenario);
        playSfx(Sfx::UiClick);
        return;
    }
    for (size_t i = 0; i < L.fileRows.size(); ++i) {
        const std::string name = ui.files[(size_t)L.fileFirst + i];
        if (L.fileDelete[i].contains(x, y)) {
            ui.ask = ScenarioAsk::Delete;
            ui.askName = name;
            playSfx(Sfx::UiClick);
            return;
        }
        if (L.fileRows[i].contains(x, y)) {
            Scenario opened;
            std::string err;
            if (!readScenarioFile(name, opened, err)) {
                refuse(langf("MISC_LOAD_FAILED", { name, err }));
                return;
            }
            ui.scenario = opened;
            ui.name = name;
            ui.loadedName = name;
            ui.savedText = scenarioToText(opened);
            ui.scroll = 0;
            playSfx(Sfx::UiClick);
            return;
        }
    }
}

void App::scenarioEditText(const char* text) {
    ScenarioEditUi& ui = scenarioEdit;
    if (ui.ask != ScenarioAsk::None) return;
    if (ui.editingName) appendUtf8Limited(ui.name, text, SCENARIO_NAME_CHARS);
    else if (ui.editingAbout) appendUtf8Limited(ui.scenario.about, text, SCENARIO_ABOUT_CHARS);
}

void App::scenarioEditKey() {
    ScenarioEditUi& ui = scenarioEdit;
    const SDL_Keycode k = e.key.key;
    if (ui.ask != ScenarioAsk::None) {
        if (k == SDLK_ESCAPE) {
            ui.ask = ScenarioAsk::None;
            playSfx(Sfx::UiBack);
        }
        return;
    }
    if (ui.editingName || ui.editingAbout) {
        if (k == SDLK_BACKSPACE) popUtf8Char(ui.editingName ? ui.name : ui.scenario.about);
        else if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_ESCAPE) stopScenarioText();
        return;
    }
    if (k == SDLK_ESCAPE) {
        playSfx(Sfx::UiBack);
        screen = Screen::Title;
    }
}

void App::scenarioEditWheel(float x, float y, float wheel) {
    ScenarioEditUi& ui = scenarioEdit;
    if (ui.ask != ScenarioAsk::None) return;
    const std::vector<EditRow> rows = editRows(ui.tab, ui.scope);
    const ScenarioEditLayout L = layoutScenarioEdit(winW, winH, ui.scope == ScenarioScope::Faction, (int)rows.size(), ui.scroll, (int)ui.files.size(), ui.fileScroll);
    const int step = wheel > 0.0f ? -1 : wheel < 0.0f ? 1 : 0;
    if (L.filesBox.contains(x, y)) {
        ui.fileScroll = std::clamp(L.fileFirst + step, 0, std::max(0, (int)ui.files.size() - (int)L.fileRows.size()));
    } else if (L.panel.contains(x, y)) {
        ui.scroll = std::clamp(L.first + step, 0, std::max(0, (int)rows.size() - L.shown));
    }
}
