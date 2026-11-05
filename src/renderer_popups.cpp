// Renderer: dialogs over the game: talk, trade, storage, loot, Optimize All, Disband, the prompts, stats and item menus.
#include "renderer_internal.h"

using namespace render_detail;

namespace {

// The value bar's colour at t from 0 to 1: red at the start, yellow halfway, green at the end
RGBA valueBarColor(float t) {
    const RGBA red{ 0.85f, 0.20f, 0.20f, 1.0f }, yellow{ 0.95f, 0.85f, 0.20f, 1.0f }, green{ 0.25f, 0.85f, 0.30f, 1.0f };
    const RGBA a = t < 0.5f ? red : yellow;
    const RGBA b = t < 0.5f ? yellow : green;
    const float u = t < 0.5f ? t * 2.0f : (t - 0.5f) * 2.0f;
    return RGBA{ a.r + (b.r - a.r) * u, a.g + (b.g - a.g) * u, a.b + (b.b - a.b) * u, 1.0f };
}

} // namespace

void Renderer::drawDialogue(const DialogueLayout& L, const std::vector<SpokenLine>& convo,
                            const std::vector<DialogueOption>& options, float mx, float my) {
    const float s = uiH_ / 720.0f;
    const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f };
    const RGBA playerTint{ 0.72f, 0.88f, 1.0f, 1.0f };
    const RGBA rowBg{ 0.14f, 0.06f, 0.075f, 0.92f };
    const RGBA plate{ 0.588f, 0.196f, 0.259f, 1.0f };
    const Rect& b = L.box;
    drawRect(Rect{ b.x - 1.5f * s, b.y - 1.5f * s, b.w + 3.0f * s, b.h + 3.0f * s }, RGBA{ 0.910f, 0.376f, 0.431f, 1.0f });
    drawRect(b, RGBA{ 0.075f, 0.03f, 0.04f, 0.96f });

    // The lines: the last ones said, the NPC's portrait on the left and the player's on the right
    const float lineH = listTextHeight("Ag");
    const size_t shownFrom = convo.size() > L.lines.size() ? convo.size() - L.lines.size() : 0;
    for (size_t i = 0; i < L.lines.size() && shownFrom + i < convo.size(); ++i) {
        const SpokenLine& ln = convo[shownFrom + i];
        const Rect& r = L.lines[i];
        drawRect(r, rowBg);
        const float pic = r.h - 10.0f * s;
        const Rect picRect = ln.player ? Rect{ r.x + r.w - pic - 5.0f * s, r.y + 5.0f * s, pic, pic }
                                       : Rect{ r.x + 5.0f * s, r.y + 5.0f * s, pic, pic };
        drawRect(Rect{ picRect.x - 1.0f * s, picRect.y - 1.0f * s, picRect.w + 2.0f * s, picRect.h + 2.0f * s }, plate);
        drawUnitPicture(ln.sprite, picRect);
        const float textX = ln.player ? r.x + 10.0f * s : picRect.x + picRect.w + 10.0f * s;
        const float textW = r.w - pic - 24.0f * s;
        const std::vector<std::string> wrapped = wrapListText(ln.text, textW);
        for (size_t j = 0; j < wrapped.size() && (float)j * lineH < r.h - 8.0f * s; ++j) {
            drawListText(fitText(wrapped[j], textW, false), textX, r.y + 6.0f * s + (float)j * lineH,
                         ln.player ? playerTint : white);
        }
    }

    // The options the player can say, then End Dialogue, which is always last
    for (size_t i = 0; i < L.options.size() && i < options.size(); ++i) {
        const Rect& r = L.options[i];
        drawButton(r, fitText(options[i].prompt, r.w - 16.0f * s, false), true, r.contains(mx, my));
    }
    drawButton(L.end, lang("UILABEL_END_DIALOGUE"), true, L.end.contains(mx, my));
}

void Renderer::drawTrade(const TradeLayout& L, const std::string& npcName, const std::string& yourName,
                         const Inventory* npcInv, const std::vector<TradeGhost>& theirs,
                         const std::vector<TradeGhost>& yours, int dragFrom, const TradeStatus& status, bool showValue,
                         float mx, float my) {
    const float s = uiH_ / 720.0f;
    const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f };
    const RGBA gray{ 0.74f, 0.66f, 0.59f, 1.0f };
    const RGBA heading{ 0.961f, 0.788f, 0.459f, 1.0f };
    const RGBA panel{ 0.085f, 0.035f, 0.045f, 0.95f };
    const RGBA bar{ 0.588f, 0.196f, 0.259f, 1.0f };
    const RGBA well{ 0.055f, 0.02f, 0.03f, 1.0f };
    const RGBA plate{ 0.14f, 0.06f, 0.075f, 1.0f };
    const RGBA plateHot{ 0.26f, 0.16f, 0.07f, 1.0f };
    const float th = listTextHeight("Ag");
    const float pad = 10.0f * s, line = 20.0f * s;

    // The Interaction Panel: the NPC's name, and its inventory
    drawRect(L.interaction, panel);
    drawRect(L.interBar, bar);
    drawListText(lang("UILABEL_INTERACTION_PANEL"), L.interBar.x + 6.0f * s, L.interBar.y + (L.interBar.h - th) * 0.5f, white);
    const float nameY = L.interBar.y + L.interBar.h + pad;
    drawListText(fitText(npcName, L.interaction.w - 2.0f * pad, false), L.interaction.x + pad, nameY, white);
    drawListText(lang("UILABEL_INVENTORY"), L.interaction.x + pad, nameY + line, heading);
    drawRect(L.npcGrid, well);
    int hoverNpc = -1;
    for (size_t i = 0; i < L.npcSlots.size(); ++i) {
        const Rect& r = L.npcSlots[i];
        const bool hot = r.contains(mx, my);
        drawRect(r, hot ? plateHot : plate);
        if (!npcInv || i >= (size_t)INVENTORY_SLOTS) continue;
        const ItemStack& st = npcInv->slots[i];
        if (st.item == 0 || (int)i == dragFrom) continue;
        drawItemCell(r, st.item, st.count);
        if (hot) hoverNpc = (int)i;
    }

    // The Trade Panel: a box for each side, its items in cells, the total under it
    drawRect(L.tradePanel, panel);
    drawRect(L.tradeBar, bar);
    drawListText(lang("UILABEL_TRADE_PANEL"), L.tradeBar.x + 6.0f * s, L.tradeBar.y + (L.tradeBar.h - th) * 0.5f, white);
    auto side = [&](const Rect& box, const std::vector<Rect>& cells, const std::vector<TradeGhost>& ghosts,
                    const std::string& hint, const Rect& total) {
        drawRect(box, well);
        for (size_t i = 0; i < cells.size(); ++i) {
            drawRect(cells[i], plate);
            if (i < ghosts.size()) drawItemCell(cells[i], ghosts[i].stack.item, ghosts[i].stack.count);
        }
        // The hint shows while the side is empty, centred in its box
        if (ghosts.empty()) {
            const std::vector<std::string> lines = wrapListText(hint, box.w - 2.0f * pad);
            float ty = box.y + (box.h - (float)lines.size() * th) * 0.5f;
            for (const std::string& ln : lines) {
                const std::string shown = fitText(ln, box.w - 2.0f * pad, false);
                drawListText(shown, box.x + (box.w - listTextWidth(shown)) * 0.5f, ty, gray);
                ty += th;
            }
        }
        drawListText(fitText(langf("UILABEL_TOTAL_VALUE", { std::to_string(tradeValueOf(ghosts)) }), total.w, false), total.x, total.y, white);
    };
    // Whose side is whose: each unit's name over its box
    auto nameOver = [&](const Rect& r, const std::string& name) {
        const std::string shown = fitText(name, r.w, false);
        if (shown != name) noteTip(name, r);
        drawListText(shown, r.x, r.y + (r.h - th) * 0.5f, heading);
    };
    nameOver(L.theirName, npcName);
    nameOver(L.yourName, yourName);
    side(L.theirBox, L.theirCells, theirs, lang("UILABEL_TRADE_DROP_THEIRS"), L.theirTotal);
    side(L.yourBox, L.yourCells, yours, lang("UILABEL_TRADE_DROP_YOURS"), L.yourTotal);

    // The value bar: red while the offer is far from the price, yellow, then green once it is met. A free trade between
    // your own units has no price, so no bar.
    if (showValue) drawRect(L.valueBar, well);
    const float fillW = L.valueBar.w * std::clamp(status.progress, 0.0f, 1.0f);
    const int slices = showValue ? 24 : 0;
    for (int i = 0; i < slices; ++i) {
        const float x0 = L.valueBar.x + L.valueBar.w * (float)i / slices;
        const float x1 = L.valueBar.x + L.valueBar.w * (float)(i + 1) / slices;
        if (x0 >= L.valueBar.x + fillW) break;
        drawRect(Rect{ x0, L.valueBar.y, std::min(x1, L.valueBar.x + fillW) - x0, L.valueBar.h },
                 valueBarColor((i + 0.5f) / slices));
    }
    drawListText(fitText(status.text, L.statusLine.w, false), L.statusLine.x, L.statusLine.y,
                 status.ok ? RGBA{ 0.6f, 1.0f, 0.6f, 1.0f } : RGBA{ 1.0f, 0.72f, 0.55f, 1.0f });

    drawButton(L.cancelBtn, lang("UILABEL_CANCEL"), true, L.cancelBtn.contains(mx, my));
    drawButton(L.tradeBtn, lang("UILABEL_TRADE"), status.ok, status.ok && L.tradeBtn.contains(mx, my));

    if (hoverNpc >= 0 && npcInv) {
        const ItemStack& st = npcInv->slots[hoverNpc];
        drawItemTooltip(st.item, st.count, mx, my);
    }
    // Item ghosts show their item's tooltip on hover, as items do elsewhere
    for (size_t i = 0; i < L.theirCells.size() && i < theirs.size(); ++i) {
        if (theirs[i].stack.item != 0 && L.theirCells[i].contains(mx, my)) drawItemTooltip(theirs[i].stack.item, theirs[i].stack.count, mx, my);
    }
    for (size_t i = 0; i < L.yourCells.size() && i < yours.size(); ++i) {
        if (yours[i].stack.item != 0 && L.yourCells[i].contains(mx, my)) drawItemTooltip(yours[i].stack.item, yours[i].stack.count, mx, my);
    }
}

void Renderer::drawStorage(const StorageLayout& L, const std::string& propName, const std::string& hint,
                           const Storage* storage, const Output* output, const std::string& status, int dragFrom,
                           float mx, float my, const std::string& toggleLabel) {
    const float s = uiH_ / 720.0f;
    const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f };
    const RGBA gray{ 0.74f, 0.66f, 0.59f, 1.0f };
    const RGBA heading{ 0.961f, 0.788f, 0.459f, 1.0f };
    const RGBA amber{ 0.91f, 0.65f, 0.24f, 1.0f };
    const RGBA bar{ 0.588f, 0.196f, 0.259f, 1.0f };
    const RGBA well{ 0.055f, 0.02f, 0.03f, 1.0f };
    const RGBA plate{ 0.14f, 0.06f, 0.075f, 1.0f };
    const RGBA plateHot{ 0.26f, 0.16f, 0.07f, 1.0f };
    const float th = listTextHeight("Ag");

    drawThemedPanel(L.panel);
    drawRect(L.bar, bar);
    drawListText(fitText(lang("UILABEL_INTERACTION_PANEL"), L.bar.w - 12.0f * s, false), L.bar.x + 6.0f * s,
                 L.bar.y + (L.bar.h - th) * 0.5f, white);
    const std::string name = fitText(propName, L.name.w, false);
    if (name != propName) noteTip(propName, L.name);
    drawListText(name, L.name.x, L.name.y, white);
    drawListText(fitText(lang(L.output ? "UILABEL_INPUT" : "UILABEL_INVENTORY"), L.heading.w, false), L.heading.x,
                 L.heading.y, heading);
    // One grid of cells. first is the panel slot of its first cell.
    ItemStack hoverStack{};
    auto grid = [&](const Rect& area, const std::vector<Rect>& cells, const ItemStack* stacks, int first) {
        drawRect(area, well);
        for (size_t i = 0; i < cells.size(); ++i) {
            const Rect& r = cells[i];
            const bool hot = r.contains(mx, my);
            drawRect(r, hot ? plateHot : plate);
            if (!stacks || i >= (size_t)STORAGE_SLOTS) continue;
            const ItemStack& st = stacks[i];
            if (st.item == 0 || first + (int)i == dragFrom) continue;
            drawItemCell(r, st.item, st.count);
            if (hot) hoverStack = st;
        }
    };
    grid(L.grid, L.slots, storage ? storage->slots.data() : nullptr, 0);
    if (L.output) {
        drawListText(fitText(lang("UILABEL_OUTPUT"), L.outHeading.w, false), L.outHeading.x, L.outHeading.y, heading);
        grid(L.outGrid, L.outSlots, output ? output->slots.data() : nullptr, STORAGE_SLOTS);
        const std::string line = fitText(status, L.status.w, false);
        if (line != status) noteTip(status, L.status);
        drawListText(line, L.status.x, L.status.y + (L.status.h - th) * 0.5f, amber);
    }
    float ty = L.hint.y;
    for (const std::string& ln : wrapListText(hint, L.hint.w)) {
        if (ty + th > L.hint.y + L.hint.h + 0.5f) break;
        drawListText(ln, L.hint.x, ty, gray);
        ty += th;
    }
    if (L.lockToggle && !toggleLabel.empty()) drawButton(L.lockBtn, toggleLabel, true, L.lockBtn.contains(mx, my));
    drawButton(L.closeBtn, lang("UILABEL_CLOSE"), true, L.closeBtn.contains(mx, my));
    if (hoverStack.item != 0 && dragFrom < 0) drawItemTooltip(hoverStack.item, hoverStack.count, mx, my);
}

void Renderer::drawLoot(const LootLayout& L, const std::string& name, const Equipment* worn, const Inventory* inv,
                        float mx, float my) {
    const float s = uiH_ / 720.0f;
    const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f };
    const RGBA gray{ 0.74f, 0.66f, 0.59f, 1.0f };
    const RGBA heading{ 0.961f, 0.788f, 0.459f, 1.0f };
    const RGBA bar{ 0.588f, 0.196f, 0.259f, 1.0f };
    const RGBA well{ 0.055f, 0.02f, 0.03f, 1.0f };
    const RGBA plate{ 0.14f, 0.06f, 0.075f, 1.0f };
    const RGBA plateHot{ 0.26f, 0.16f, 0.07f, 1.0f };
    const float th = listTextHeight("Ag");
    drawThemedPanel(L.panel);
    drawRect(L.bar, bar);
    drawListText(fitText(lang("UILABEL_INTERACTION_PANEL"), L.bar.w - 12.0f * s, false), L.bar.x + 6.0f * s,
                 L.bar.y + (L.bar.h - th) * 0.5f, white);
    const std::string shown = fitText(name, L.name.w, false);
    if (shown != name) noteTip(name, L.name);
    drawListText(shown, L.name.x, L.name.y, white);
    int hoverItem = 0, hoverCount = 0;
    // Gear: each slot shows its item, or its name dimmed when empty
    drawListText(lang("UILABEL_EQUIPMENT"), L.gearHeading.x, L.gearHeading.y, heading);
    drawRect(L.gearGrid, well);
    for (size_t i = 0; i < L.gear.size() && i < (size_t)GEAR_SLOTS; ++i) {
        const Rect& r = L.gear[i];
        const int item = worn ? worn->items[i] : 0;
        const bool hot = item != 0 && r.contains(mx, my);
        drawRect(r, hot ? plateHot : plate);
        if (hot) noteHover(r);
        if (item == 0) {
            const std::string slot = fitText(gearSlotName((int)i), r.w - 4.0f, false);
            drawListText(slot, r.x + (r.w - listTextWidth(slot)) * 0.5f, r.y + (r.h - th) * 0.5f, RGBA{ 0.80f, 0.72f, 0.64f, 0.4f });
            continue;
        }
        drawItemCell(r, item, 1);
        if (hot) { hoverItem = item; hoverCount = 1; }
    }
    // Inventory
    drawListText(lang("UILABEL_INVENTORY"), L.invHeading.x, L.invHeading.y, heading);
    drawRect(L.invGrid, well);
    for (size_t i = 0; i < L.slots.size() && i < (size_t)INVENTORY_SLOTS; ++i) {
        const Rect& r = L.slots[i];
        const ItemStack st = inv ? inv->slots[i] : ItemStack{};
        const bool hot = st.item != 0 && r.contains(mx, my);
        drawRect(r, hot ? plateHot : plate);
        if (hot) noteHover(r);
        if (st.item == 0 || st.count <= 0) continue;
        drawItemCell(r, st.item, st.count);
        if (hot) { hoverItem = st.item; hoverCount = st.count; }
    }
    float ty = L.hint.y;
    for (const std::string& ln : wrapListText(lang("UILABEL_LOOT_HINT"), L.hint.w)) {
        if (ty + th > L.hint.y + L.hint.h + 0.5f) break;
        drawListText(ln, L.hint.x, ty, gray);
        ty += th;
    }
    drawButton(L.takeAllBtn, lang("UILABEL_TAKE_ALL"), true, L.takeAllBtn.contains(mx, my));
    drawButton(L.closeBtn, lang("UILABEL_CLOSE"), true, L.closeBtn.contains(mx, my));
    if (hoverItem != 0) drawItemTooltip(hoverItem, hoverCount, mx, my);
}

void Renderer::drawOptimizeAll(const OptimizeAllLayout& L, int unitCount, int attackShare, const std::array<bool, 3>& toggles,
                               float mx, float my) {
    const float s = uiH_ / 720.0f;
    const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f };
    const RGBA gray{ 0.80f, 0.72f, 0.64f, 1.0f };
    const RGBA accent{ 0.961f, 0.788f, 0.459f, 1.0f };
    const float th = listTextHeight("Ag");
    drawRect(Rect{ 0.0f, 0.0f, (float)uiW_, (float)uiH_ }, RGBA{ 0.0f, 0.0f, 0.0f, 0.45f });   // dims the game behind it
    drawThemedPanel(L.box);
    drawListText(fitText(lang("UILABEL_OPTIMIZE_ALL"), L.title.w, false), L.title.x, L.title.y + (L.title.h - th) * 0.5f, white);
    float ty = L.text.y;
    for (const std::string& ln : wrapListText(langf("MISC_OPTIMIZE_ALL_ASK", { std::to_string(unitCount) }), L.text.w)) {
        if (ty + th > L.text.y + L.text.h + 0.5f) break;
        drawListText(ln, L.text.x, ty, gray);
        ty += th;
    }
    // The slider: all defense at the left, all attack at the right
    const int share = std::clamp(attackShare, 0, 100);
    const std::string sl = langf("UILABEL_OPTIMIZE_BALANCE", { std::to_string(share), std::to_string(100 - share) });
    drawListText(fitText(sl, L.sliderLabel.w, false), L.sliderLabel.x, L.sliderLabel.y + (L.sliderLabel.h - th) * 0.5f, white);
    drawRect(L.track, RGBA{ 0.24f, 0.10f, 0.12f, 1.0f });
    drawRect(Rect{ L.track.x, L.track.y, L.track.w * share / 100.0f, L.track.h }, accent);
    const float kx = L.track.x + L.track.w * share / 100.0f, kw = 8.0f * s, kh = L.track.h + 8.0f * s;
    drawRect(Rect{ kx - kw * 0.5f, L.track.y + (L.track.h - kh) * 0.5f, kw, kh }, white);
    // The toggles: a box, filled when on, and its label
    const char* keys[3] = { "UILABEL_OPTIMIZE_ACCESSORIES", "UILABEL_OPTIMIZE_TWO_HANDED", "UILABEL_OPTIMIZE_VALUE_TIES" };
    for (int i = 0; i < 3; ++i) {
        const Rect& r = L.toggles[(size_t)i];
        const float box = r.h - 6.0f * s;
        const Rect b{ r.x, r.y + 3.0f * s, box, box };
        const bool hot = r.contains(mx, my);
        if (hot) noteHover(r);
        drawRect(b, hot ? RGBA{ 0.42f, 0.28f, 0.13f, 1.0f } : RGBA{ 0.48f, 0.24f, 0.27f, 1.0f });
        drawRect(Rect{ b.x + 2.0f * s, b.y + 2.0f * s, b.w - 4.0f * s, b.h - 4.0f * s },
                 toggles[(size_t)i] ? accent : RGBA{ 0.11f, 0.05f, 0.06f, 1.0f });
        const std::string label = lang(keys[i]);
        const float lx = b.x + box + 8.0f * s;
        const std::string shown = fitText(label, r.x + r.w - lx, false);
        if (shown != label) noteTip(label, r);
        drawListText(shown, lx, r.y + (r.h - th) * 0.5f, white);
    }
    drawButton(L.confirm, lang("UILABEL_CONFIRM"), unitCount > 0, unitCount > 0 && L.confirm.contains(mx, my));
    drawButton(L.cancel, lang("UILABEL_CANCEL"), true, L.cancel.contains(mx, my));
}

void Renderer::drawDisband(World& w, const DisbandLayout& L, Entity unit, float held, float mx, float my) {
    const float s = uiH_ / 720.0f;
    const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f };
    const RGBA gray{ 0.80f, 0.72f, 0.64f, 1.0f };
    const float th = listTextHeight("Ag");
    drawRect(Rect{ 0.0f, 0.0f, (float)uiW_, (float)uiH_ }, RGBA{ 0.0f, 0.0f, 0.0f, 0.45f });   // dims the game behind it
    drawThemedPanel(L.box);
    drawListText(fitText(lang("UILABEL_DISBAND"), L.title.w, false), L.title.x, L.title.y + (L.title.h - th) * 0.5f, white);

    // The portrait, as on the Profile tab
    drawRect(L.portrait, RGBA{ 0.16f, 0.07f, 0.085f, 1.0f });
    const TextTex& g = unitPortrait(w, unit);
    drawPortrait(g, Rect{ L.portrait.x, L.portrait.y, L.portrait.w, L.portrait.w }, unitSaturation(w, unit));

    // Beside it: the name, then its kind or body, its health, and its hunger (a person) or temperament (a mob)
    const Name* nm = w.get<Name>(unit);
    const std::string name = nm && !nm->text.empty() ? nm->text : std::string(lang("UILABEL_UNNAMED_UNIT"));
    const float rowH = L.info.h / (float)(1 + DISBAND_INFO_LINES);
    drawUiText(fitText(name, L.info.w, true), L.info.x, L.info.y + (rowH - uiTextHeight("Ag")) * 0.5f, white);
    std::vector<std::string> lines;
    const Mob* mb = w.get<Mob>(unit);
    if (mb) lines.push_back(lang(mobInfo(mb->kind).nameKey));
    else if (const Body* bd = w.get<Body>(unit)) lines.push_back(lang(bodyTypeKey(bd->type)));
    if (const Needs* n = w.get<Needs>(unit)) {
        lines.push_back(lang("UILABEL_HEALTH") + ": " + std::to_string(n->hp) + " / " + std::to_string(n->maxHp));
        if (!mb) lines.push_back(lang("UILABEL_HUNGER") + ": " + std::to_string(n->hunger) + " / " + std::to_string(n->maxHunger));
    }
    if (mb) lines.push_back(langf("UILABEL_TEMPERAMENT", { lang(temperamentKey(mobTemperament(mb->kind))) }));
    for (size_t i = 0; i < lines.size() && i < (size_t)DISBAND_INFO_LINES; ++i) {
        const float ry = L.info.y + rowH * (float)(i + 1);
        drawListText(fitText(lines[i], L.info.w, false), L.info.x, ry + (rowH - th) * 0.5f, gray);
    }

    // What it does, and how to confirm
    const std::string what = mb ? langf("MISC_DISBAND_ASK_MOB", { name }) : langf("MISC_DISBAND_ASK", { name });
    float ty = L.text.y;
    for (const std::string& ln : wrapListText(what, L.text.w)) {
        if (ty + th > L.text.y + L.text.h + 0.5f) break;
        drawListText(ln, L.text.x, ty, gray);
        ty += th;
    }
    const std::string hint = langf("UILABEL_DISBAND_HOLD", { std::to_string(DISBAND_HOLD_MS / 1000) });
    drawListText(fitText(hint, L.hint.w, false), L.hint.x + (L.hint.w - std::min(L.hint.w, listTextWidth(hint))) * 0.5f,
                 L.hint.y + (L.hint.h - th) * 0.5f, white);

    // Disband fills from the left with red while it is held
    drawButton(L.disband, lang("UILABEL_DISBAND"), true, L.disband.contains(mx, my));
    const float f = std::clamp(held, 0.0f, 1.0f);
    if (f > 0.0f) {
        drawRect(Rect{ L.disband.x, L.disband.y, L.disband.w * f, L.disband.h }, RGBA{ 0.85f, 0.12f, 0.12f, 0.55f });
        const std::string label = lang("UILABEL_DISBAND");
        drawListText(label, L.disband.x + (L.disband.w - listTextWidth(label)) * 0.5f, L.disband.y + (L.disband.h - th) * 0.5f, white);
    }
    drawButton(L.cancel, lang("UILABEL_CANCEL"), true, L.cancel.contains(mx, my));
    (void)s;
}

void Renderer::drawQuantityPrompt(const QuantityLayout& L, const std::string& itemName, int chosen, int available,
                                  float mx, float my) {
    const float s = uiH_ / 720.0f;
    const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f };
    const RGBA well{ 0.055f, 0.02f, 0.03f, 1.0f };
    const float th = listTextHeight("Ag");
    drawThemedPanel(L.box);
    const std::string title = langf("UILABEL_CHOOSE_HOW_MANY", { itemName, std::to_string(available) });
    drawListText(fitText(title, L.title.w, false), L.title.x, L.title.y + (L.title.h - th) * 0.5f, white);
    drawRect(L.countBox, well);
    const std::string num = std::to_string(chosen);
    drawListText(num, L.countBox.x + (L.countBox.w - listTextWidth(num)) * 0.5f,
                 L.countBox.y + (L.countBox.h - th) * 0.5f, white);
    auto btn = [&](const Rect& r, const std::string& label, bool enabled) {
        drawButton(r, label, enabled, enabled && r.contains(mx, my));
    };
    btn(L.minus100, "-100", chosen > 1);
    btn(L.minus10, "-10", chosen > 1);
    btn(L.minus1, "-", chosen > 1);
    btn(L.plus1, "+", chosen < available);
    btn(L.plus10, "+10", chosen < available);
    btn(L.plus100, "+100", chosen < available);
    btn(L.single, lang("UILABEL_SINGLE"), chosen != 1);
    btn(L.max, lang("UILABEL_MAX"), chosen != available);
    btn(L.confirm, lang("UILABEL_CONFIRM"), true);
    btn(L.cancel, lang("UILABEL_CANCEL"), true);
    (void)s;
}

StatsPanelLayout Renderer::statsPanelLayout(int winW, int winH) {
    const float s = winH / 720.0f;
    return layoutStatsPanel(winW, winH, fontTextHeight(statsRow_, "Ag") + 1.0f * s, fontTextHeight(statsTitle_, "Ag"));
}

void Renderer::drawStatsPanel(const StatsPanelLayout& L, const std::string& name, const Stats& stats,
                              float mx, float my) {
    const float s = uiH_ / 720.0f;
    const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f };
    const RGBA gray{ 0.74f, 0.66f, 0.59f, 1.0f };
    const RGBA heading{ 0.961f, 0.788f, 0.459f, 1.0f };
    const float bgAlpha = 0.5f;
    const float rowH = fontTextHeight(statsRow_, "Ag") + 1.0f * s;
    const float pad = L.left.x - L.box.x;
    const float iconH = fontTextHeight(statsRow_, "Ag");
    const float nameIndent = iconH + 4.0f * s;   // the stat names sit after their icons

    drawRect(L.box, RGBA{ 0.075f, 0.03f, 0.04f, bgAlpha });
    const std::string caption = langf("UILABEL_STATS_CAPTION", { name });
    drawFontText(statsTitle_, caption, L.box.x + (L.box.w - fontTextWidth(statsTitle_, caption)) * 0.5f,
                 L.box.y + pad, white);

    // Columns: the name at the left, then the level, the total XP and the XP still to go, right aligned
    auto row = [&](const Rect& col, float rowY, float indent, const std::string& a, const std::string& b,
                   const std::string& c, const std::string& d, RGBA tint) {
        drawFontText(statsRow_, a, col.x + indent, rowY, tint);
        drawFontText(statsRow_, b, col.x + 200.0f - fontTextWidth(statsRow_, b), rowY, tint);
        drawFontText(statsRow_, c, col.x + 312.0f - fontTextWidth(statsRow_, c), rowY, tint);
        drawFontText(statsRow_, d, col.x + col.w - fontTextWidth(statsRow_, d), rowY, tint);
    };
    auto column = [&](const Rect& col, const std::vector<StatGroup>& groups) {
        float rowY = col.y;
        row(col, rowY, nameIndent, lang("UILABEL_STAT"), lang("UILABEL_LEVEL"), lang("UILABEL_TOTAL_XP"), lang("UILABEL_TO_NEXT"), gray);
        rowY += rowH;
        for (StatGroup group : groups) {
            const std::string title = group == StatGroup::Attributes ? lang("UILABEL_ATTRIBUTES")
                              : group == StatGroup::Combat ? lang("UILABEL_COMBAT")
                              : group == StatGroup::Gathering ? lang("UILABEL_RESOURCE_GATHERING")
                              : group == StatGroup::Crafting ? lang("UILABEL_CRAFTING") : lang("UILABEL_UTILITY");
            drawFontText(statsRow_, title, col.x + nameIndent, rowY, heading);
            rowY += rowH;
            for (int i = 0; i < STAT_COUNT; ++i) {
                if (statInfo((StatId)i).group != group) continue;
                const int lv = levelOf(stats, (StatId)i);
                const std::string next = lv >= MAX_LEVEL ? std::string(lang("UILABEL_MAX"))
                                       : withCommas(xpForLevel(lv + 1) - stats.xp[i]);
                drawUiIcon(statIcon((StatId)i), Rect{ col.x, rowY, iconH, iconH });
                row(col, rowY, nameIndent, statName((StatId)i), std::to_string(lv), withCommas(stats.xp[i]), next, white);
                rowY += rowH;
            }
        }
    };
    column(L.left, { StatGroup::Attributes, StatGroup::Combat });
    column(L.right, { StatGroup::Gathering, StatGroup::Crafting, StatGroup::Utility });

    const bool hot = L.close.contains(mx, my);
    drawRect(L.close, hot ? RGBA{ 0.353f, 0.235f, 0.110f, bgAlpha } : RGBA{ 0.11f, 0.05f, 0.06f, bgAlpha });
    const float tw = fontTextWidth(statsRow_, lang("UILABEL_CLOSE")), th = fontTextHeight(statsRow_, lang("UILABEL_CLOSE"));
    drawFontText(statsRow_, lang("UILABEL_CLOSE"), L.close.x + (L.close.w - tw) * 0.5f, L.close.y + (L.close.h - th) * 0.5f, white);
}

ItemMenuLayout Renderer::itemMenuLayout(float x, float y, int buttonCount, int winW, int winH) {
    return layoutItemMenu(x, y, buttonCount, listTextHeight("Ag"), winW, winH);
}

void Renderer::drawItemMenu(const ItemMenuLayout& L, const std::vector<std::string>& labels, float mx, float my) {
    const float s = uiH_ / 720.0f;
    drawRect(L.box, theme::alpha(theme::PINK, 0.8f));
    drawRect(Rect{ L.box.x + 1.0f, L.box.y + 1.0f, L.box.w - 2.0f, L.box.h - 2.0f }, theme::alpha(theme::NIGHT, 0.92f));
    for (size_t i = 0; i < L.buttons.size() && i < labels.size(); ++i) {
        const Rect& r = L.buttons[i];
        const bool hot = r.contains(mx, my);
        if (hot) noteHover(r);
        if (hot) drawRectGradientH(r, rgb255(52, 30, 16, 0.85f), rgb255(96, 58, 30, 0.95f));
        drawRect(Rect{ r.x, r.y, 2.0f * s, r.h }, hot ? theme::GREEN : theme::alpha(theme::ASH, 0.35f));
        const float tw = listTextWidth(labels[i]), th = listTextHeight(labels[i]);
        drawListText(labels[i], r.x + (r.w - tw) * 0.5f, r.y + (r.h - th) * 0.5f, hot ? theme::BONE : rgb255(232, 214, 190, 1.0f));
    }
}
