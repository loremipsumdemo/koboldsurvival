#include "app.h"
#include "factions.h"

using namespace play;

// The 16 colors, in the order of the loop (data/factions.txt has them as FACTION_<NAME>):
//   Ember #FF4000, Solar #EFDF00, Amber #AF8010, Dawn #FFCFAF, Umber #704020, Thicket #305000, Verdant #00FF40, Jade #509F70,
//   Tidal #007080, Glacier #00EFEF, Zephyr #40AFFF, Cobalt #1000EF, Mystic #9F70FF, Veil #EFCFFF, Garnet #8F0050, Blossom #FF508F
// Character creation's faction color (factions.h): the window of the 16 colors, opened from the party screen's Faction button. The one picked
// is yours, and no other faction can have it: the other 15 are the colors you did not pick. A random one is drawn if you pick none.

namespace {
struct FactionPickLayout {
    Rect box, title, close, field, hint, random;
    std::vector<Rect> cells;
};

FactionPickLayout layoutFactionPick(int W, int H) {
    const float s = uiScaleFor((float)H);
    const int cols = 4, rows = PALETTE_COUNT / cols;
    const float pad = 14.0f * s, gap = 8.0f * s, line = 30.0f * s, btnH = 34.0f * s, hintH = 78.0f * s;
    const float fixedH = pad + line + gap + line + gap + hintH + gap + btnH + pad;
    const float cellH = std::min(64.0f * s, std::max(32.0f * s, ((float)H - 20.0f * s - fixedH - (rows - 1) * gap) / rows));
    const float cellW = std::min(140.0f * s, std::max(80.0f * s, ((float)W - 20.0f * s - 2.0f * pad - (cols - 1) * gap) / cols));
    FactionPickLayout L;
    const float w = cols * cellW + (cols - 1) * gap + 2.0f * pad;
    const float h = fixedH + rows * cellH + (rows - 1) * gap;
    L.box = Rect{ std::floor((W - w) * 0.5f), std::floor((H - h) * 0.5f), w, h };
    L.title = Rect{ L.box.x + pad, L.box.y + pad, w - 2.0f * pad - line, line };
    L.close = Rect{ L.box.x + w - pad - line, L.box.y + pad, line, line };
    L.field = Rect{ L.box.x + pad, L.title.y + line + gap, w - 2.0f * pad, line };
    const float top = L.field.y + line + gap;
    for (int i = 0; i < PALETTE_COUNT; ++i) {
        L.cells.push_back(Rect{ L.box.x + pad + (float)(i % cols) * (cellW + gap), top + (float)(i / cols) * (cellH + gap), cellW, cellH });
    }
    L.hint = Rect{ L.box.x + pad, top + rows * (cellH + gap), w - 2.0f * pad, hintH };
    L.random = Rect{ L.box.x + pad, L.box.y + h - pad - btnH, w - 2.0f * pad, btnH };
    return L;
}

// The color's own name (Ember), from the faction's key (FACTION_EMBER gives COLORNAME_EMBER)
std::string colorName(const FactionDef& f) {
    const std::string prefix = "FACTION_";
    return lang(f.nameKey.compare(0, prefix.size(), prefix) == 0 ? "COLORNAME_" + f.nameKey.substr(prefix.size()) : f.nameKey);
}

void popFrontUtf8(std::string& s) {
    size_t n = 1;
    while (n < s.size() && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) ++n;
    s.erase(0, n);
}

// Dark text on a light color, light on a dark one
RGBA textOn(const FactionColor& c) {
    const float lum = 0.299f * c.r + 0.587f * c.g + 0.114f * c.b;
    return lum > 0.58f ? RGBA{ 0.07f, 0.07f, 0.07f, 1.0f } : RGBA{ 1.0f, 1.0f, 1.0f, 1.0f };
}
}

std::string App::factionButtonText() {
    const FactionDef* f = factionPick >= 0 ? paletteFaction(factionPick) : nullptr;
    const std::string typed = cleanName(factionNameText);
    return langf("UILABEL_FACTION_COLOR_BUTTON", { !typed.empty() ? typed : f ? lang(f->nameKey) : lang("UILABEL_FACTION_COLOR_RANDOM") });
}

void App::stopFactionNameEdit() {
    if (factionNameEditing) SDL_StopTextInput(window);
    factionNameEditing = false;
    factionNameText = cleanName(factionNameText);
}

void App::openFactionPick() {
    factionPickOpen = true;
    patronOpen = false;
    playSfx(Sfx::UiMenuOpen);
}

// The slot a new game takes: the one picked, or a random one. Refused slots (none the table has) fall back to a random one that is.
int App::resolveFactionPick() {
    if (factionPick >= 0 && paletteFaction(factionPick)) return factionPick;
    std::vector<int> have;
    for (int k = 0; k < PALETTE_COUNT; ++k) {
        if (paletteFaction(k)) have.push_back(k);
    }
    return have.empty() ? PALETTE_DEFAULT_SLOT : have[(size_t)rng.range(0, (int)have.size() - 1)];
}

bool App::pressOnFactionPick() {
    if (!factionPickOpen || screen != Screen::PartySetup) return false;
    if (e.button.button != SDL_BUTTON_LEFT) return true;
    const float x = e.button.x, y = e.button.y;
    const FactionPickLayout L = layoutFactionPick(winW, winH);
    if (L.field.contains(x, y)) {
        if (!factionNameEditing) SDL_StartTextInput(window);
        factionNameEditing = true;
        return true;
    }
    stopFactionNameEdit();
    if (L.close.contains(x, y) || !L.box.contains(x, y)) {
        factionPickOpen = false;
        playSfx(Sfx::UiBack);
        return true;
    }
    if (L.random.contains(x, y)) {
        factionPick = -1;
        factionPickOpen = false;
        playSfx(Sfx::UiClick);
        return true;
    }
    for (size_t i = 0; i < L.cells.size(); ++i) {
        if (!L.cells[i].contains(x, y) || !paletteFaction((int)i)) continue;
        factionPick = (int)i;
        factionPickOpen = false;
        playSfx(Sfx::UiClick);
        return true;
    }
    return true;
}

void App::drawFactionPick() {
    if (!factionPickOpen || screen != Screen::PartySetup) return;
    const FactionPickLayout L = layoutFactionPick(winW, winH);
    const float s = uiScaleFor((float)winH), textH = renderer.listTextHeight("Ag");
    const RGBA gold{ 1.0f, 0.78f, 0.30f, 1.0f }, dim{ 0.80f, 0.72f, 0.64f, 1.0f };
    renderer.drawThemedPanel(L.box);
    renderer.drawListText(renderer.fitListText(lang("UILABEL_FACTION_COLOR_TITLE"), L.title.w), L.title.x, L.title.y + (L.title.h - textH) * 0.5f, gold);
    renderer.drawCloseButton(L.close);
    renderer.drawRect(L.field, factionNameEditing ? RGBA{ 0.30f, 0.20f, 0.12f, 1.0f } : RGBA{ 0.14f, 0.09f, 0.08f, 1.0f });
    const float fieldPad = 8.0f * s;
    if (factionNameText.empty() && !factionNameEditing) {
        const FactionDef* mine = factionPick >= 0 ? paletteFaction(factionPick) : nullptr;
        const std::string shown = renderer.fitListText(mine ? lang(mine->nameKey) : lang("UILABEL_FACTION_COLOR_RANDOM"), L.field.w - 2.0f * fieldPad);
        renderer.drawListText(shown, L.field.x + fieldPad, L.field.y + (L.field.h - textH) * 0.5f, dim);
    } else {
        std::string shown = factionNameText + (factionNameEditing && (SDL_GetTicks() / 500) % 2 ? "|" : "");
        while (utf8Length(shown) > 1 && renderer.listTextWidth(shown) > L.field.w - 2.0f * fieldPad) popFrontUtf8(shown);   // the end being typed stays in view
        renderer.drawListText(shown, L.field.x + fieldPad, L.field.y + (L.field.h - textH) * 0.5f, RGBA{ 1.0f, 1.0f, 1.0f, 1.0f });
    }
    const float mx = (float)mouseX, my = (float)mouseY;
    int hover = -1;
    for (size_t i = 0; i < L.cells.size(); ++i) {
        const FactionDef* f = paletteFaction((int)i);
        if (!f) continue;
        const Rect& c = L.cells[i];
        const bool on = (int)i == factionPick;
        if (c.contains(mx, my)) hover = (int)i;
        renderer.drawRect(Rect{ c.x - 3.0f * s, c.y - 3.0f * s, c.w + 6.0f * s, c.h + 6.0f * s }, on ? gold : RGBA{ 0.30f, 0.20f, 0.35f, 1.0f });
        renderer.drawRect(c, RGBA{ f->color.r, f->color.g, f->color.b, 1.0f });
        if (hover == (int)i && !on) renderer.drawRect(c, RGBA{ 1.0f, 1.0f, 1.0f, 0.16f });
        const RGBA ink = textOn(f->color);
        const std::string name = renderer.fitListText(colorName(*f), c.w - 8.0f * s);
        renderer.drawListText(name, c.x + (c.w - renderer.listTextWidth(name)) * 0.5f, c.y + (c.h - textH) * 0.5f, ink);
    }
    float hy = L.hint.y;
    for (const std::string& ln : renderer.wrapListText(lang("UILABEL_FACTION_COLOR_TIP"), L.hint.w)) {
        renderer.drawListText(ln, L.hint.x, hy, dim);
        hy += textH;
    }
    renderer.drawButton(L.random, lang("UILABEL_FACTION_COLOR_RANDOM"), true, factionPick < 0 || L.random.contains(mx, my));
    if (hover < 0) return;

    // Who the one under the pointer is allied with, and who it is at war with, from its place on the loop
    const FactionDef* f = paletteFaction(hover);
    std::string allies, enemies;
    for (int k = 0; k < PALETTE_COUNT; ++k) {
        const FactionDef* o = paletteFaction(k);
        if (k == hover || !o) continue;
        const int rep = loopReputation(hover, k);
        if (rep >= REP_ALLIED) allies += (allies.empty() ? "" : ", ") + colorName(*o);
        else if (rep <= REP_HOSTILE) enemies += (enemies.empty() ? "" : ", ") + colorName(*o);
    }
    std::vector<std::pair<std::string, RGBA>> lines;
    lines.push_back({ lang(f->nameKey), gold });
    const float textW = 260.0f * s;
    for (const std::string& ln : renderer.wrapListText(langf("UILABEL_FACTION_COLOR_ALLIES", { allies }), textW)) lines.push_back({ ln, RGBA{ 0.5f, 0.95f, 0.5f, 1.0f } });
    for (const std::string& ln : renderer.wrapListText(langf("UILABEL_FACTION_COLOR_ENEMIES", { enemies }), textW)) lines.push_back({ ln, RGBA{ 1.0f, 0.42f, 0.42f, 1.0f } });
    const float pad = 8.0f * s;
    const float bw = textW + 2.0f * pad, bh = (float)lines.size() * textH + 2.0f * pad;
    const float bx = std::clamp(mx + 18.0f * s, 4.0f, std::max(4.0f, (float)winW - bw - 4.0f));
    const float by = std::clamp(my + 18.0f * s, 4.0f, std::max(4.0f, (float)winH - bh - 4.0f));
    renderer.drawRect(Rect{ bx - 1.5f * s, by - 1.5f * s, bw + 3.0f * s, bh + 3.0f * s }, RGBA{ 0.55f, 0.35f, 0.70f, 1.0f });
    renderer.drawRect(Rect{ bx, by, bw, bh }, RGBA{ 0.07f, 0.04f, 0.11f, 0.97f });
    float ty = by + pad;
    for (const auto& [text, tint] : lines) {
        renderer.drawListText(text, bx + pad, ty, tint);
        ty += textH;
    }
}
