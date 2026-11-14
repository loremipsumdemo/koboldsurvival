#include "app.h"
#include "gates.h"
#include "schedule.h"

using namespace play;

// Faction Info: any faction's settings and setup, from its Altar's menu, to look at. An NPC faction is run by its NPC Avatar, which makes
// the decisions a player would; everything shown is what it has set, as Faction Rules and Manage Units show it for yours.

namespace {
struct InfoRow {
    std::string label, value;
    bool heading = false;
};

std::string hourText(int h) {
    char buf[8];
    std::snprintf(buf, sizeof buf, "%02d", h);
    return buf;
}

std::vector<InfoRow> infoRows(World& w, int faction) {
    std::vector<InfoRow> rows;
    const bool yours = faction == FACTION_PLAYER;
    rows.push_back({ lang("UILABEL_INFO_CONTROL"), lang(yours ? "UILABEL_INFO_PLAYER" : "UILABEL_INFO_AVATAR"), false });
    for (const Claim& c : claimsOf(w)) {
        if (c.owner == faction && !c.camp) {
            rows.push_back({ lang("UILABEL_INFO_CLAIM"), std::to_string((int)std::lround(c.radius)), false });
            break;
        }
    }
    int citizens = 0, fortified = 0, military = 0;
    for (ObjectId u : w.view<Entity, Faction>()) {
        if (factionOf(w, u) != faction || w.has<Mob>(u) || w.has<Corpse>(u)) continue;
        if (w.has<Fortified>(u)) ++fortified;
        else if (w.has<Citizen>(u)) ++citizens;
        else ++military;
    }
    rows.push_back({ lang("UILABEL_INFO_UNITS"), std::to_string(citizens + fortified + military), false });
    rows.push_back({ lang("UILABEL_INFO_CITIZENS"), std::to_string(citizens), false });
    rows.push_back({ lang("UILABEL_INFO_FORTIFIED"), std::to_string(fortified), false });
    rows.push_back({ lang("UILABEL_INFO_MILITARY"), std::to_string(military), false });
    rows.push_back({ lang("UILABEL_INFO_GATES"), std::to_string(ownGates(w, faction).size()), false });
    std::string closed;
    for (int h = 0; h < SCHEDULE_HOURS;) {
        if (!perimeterClosedAtHour(w, h, faction)) {
            ++h;
            continue;
        }
        const int from = h;
        while (h < SCHEDULE_HOURS && perimeterClosedAtHour(w, h, faction)) ++h;
        if (!closed.empty()) closed += ", ";
        closed += hourText(from) + ":00-" + hourText(h - 1) + ":59";
    }
    rows.push_back({ lang("UILABEL_INFO_GATE_HOURS"), closed.empty() ? std::string(lang("UILABEL_RULE_NEVER")) : closed, false });
    rows.push_back({ lang("UILABEL_INFO_RULES"), "", true });
    const FactionRules& rules = factionRules(w, faction);
    const int shown = yours ? PLAYER_RULE_ROWS : (int)RuleRow::Count;
    for (int i = 0; i < shown; ++i) rows.push_back({ lang(ruleRowKey((RuleRow)i)), ruleValueText(rules, (RuleRow)i), false });
    return rows;
}

struct InfoLayout {
    Rect box, title, close, note;
    float rowH = 0.0f, pad = 0.0f, valueX = 0.0f, firstY = 0.0f;
};

InfoLayout layoutInfo(int W, int H, int rowCount, float textH) {
    const float s = uiScaleFor((float)H);
    InfoLayout L;
    L.pad = 14.0f * s;
    L.rowH = std::max(textH + 4.0f * s, 22.0f * s);
    const float line = 30.0f * s, gap = 6.0f * s, noteH = 3.0f * (textH + 2.0f);
    const float w = std::min((float)W - 20.0f * s, 560.0f * s);
    const float h = std::min((float)H - 20.0f * s, L.pad + line + gap + rowCount * L.rowH + gap + noteH + L.pad);
    L.box = Rect{ std::floor((W - w) * 0.5f), std::floor((H - h) * 0.5f), w, h };
    L.title = Rect{ L.box.x + L.pad, L.box.y + L.pad, w - 2.0f * L.pad - line, line };
    L.close = Rect{ L.box.x + w - L.pad - line, L.box.y + L.pad, line, line };
    L.firstY = L.title.y + line + gap;
    L.valueX = L.box.x + L.pad + (w - 2.0f * L.pad) * 0.5f;
    L.note = Rect{ L.box.x + L.pad, L.box.y + h - L.pad - noteH, w - 2.0f * L.pad, noteH };
    return L;
}
}

void App::openFactionInfo(int faction) {
    factionInfoOpen = true;
    factionInfoFaction = faction;
    session.contextMenu.open = false;
    itemMenu.open = false;
    playSfx(Sfx::UiMenuOpen);
}

bool App::pressOnFactionInfo() {
    if (screen != Screen::Playing || !factionInfoOpen) return false;
    if (e.button.button != SDL_BUTTON_LEFT) return true;
    const float x = e.button.x, y = e.button.y;
    const InfoLayout L = layoutInfo(winW, winH, (int)infoRows(world, factionInfoFaction).size(), renderer.listTextHeight("Ag"));
    if (L.close.contains(x, y) || !L.box.contains(x, y)) {
        factionInfoOpen = false;
        playSfx(Sfx::UiBack);
    }
    return true;
}

void App::drawFactionInfo() {
    if (screen != Screen::Playing || !factionInfoOpen) return;
    const std::vector<InfoRow> rows = infoRows(world, factionInfoFaction);
    const float textH = renderer.listTextHeight("Ag");
    const InfoLayout L = layoutInfo(winW, winH, (int)rows.size(), textH);
    const float s = uiScaleFor((float)winH);
    const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f }, gold{ 1.0f, 0.78f, 0.30f, 1.0f }, dim{ 0.80f, 0.72f, 0.64f, 1.0f };
    renderer.drawThemedPanel(L.box);
    renderer.drawListText(renderer.fitListText(langf("UILABEL_FACTION_INFO_TITLE", { factionName(factionInfoFaction) }) + " (" + lang("UILABEL_VIEW_ONLY") + ")", L.title.w),
                          L.title.x, L.title.y + (L.title.h - textH) * 0.5f, gold);
    renderer.drawCloseButton(L.close);
    float y = L.firstY;
    const float right = L.box.x + L.box.w - L.pad;
    for (const InfoRow& r : rows) {
        if (y + L.rowH > L.note.y) break;
        if (r.heading) {
            renderer.drawRect(Rect{ L.box.x + L.pad, y, L.box.w - 2.0f * L.pad, L.rowH }, RGBA{ 0.30f, 0.20f, 0.08f, 0.8f });
            renderer.drawListText(r.label, L.box.x + L.pad + 6.0f * s, y + (L.rowH - textH) * 0.5f, gold);
        } else {
            renderer.drawListText(renderer.fitListText(r.label, L.valueX - L.box.x - L.pad - 8.0f * s), L.box.x + L.pad + 6.0f * s, y + (L.rowH - textH) * 0.5f, white);
            const std::string v = renderer.fitListText(r.value, right - L.valueX - 6.0f * s);
            renderer.drawListText(v, L.valueX, y + (L.rowH - textH) * 0.5f, gold);
        }
        y += L.rowH;
    }
    if (factionInfoFaction != FACTION_PLAYER) {
        float ny = L.note.y;
        for (const std::string& line : renderer.wrapListText(lang("UILABEL_INFO_AVATAR_NOTE"), L.note.w)) {
            if (ny + textH > L.note.y + L.note.h) break;
            renderer.drawListText(line, L.note.x, ny, dim);
            ny += textH + 2.0f;
        }
    }
}
