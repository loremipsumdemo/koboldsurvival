#include "renderer_internal.h"

using namespace render_detail;

namespace {
RGBA valueBarColor(float t) {
    const RGBA red{ 0.85f, 0.20f, 0.20f, 1.0f }, yellow{ 0.95f, 0.85f, 0.20f, 1.0f }, green{ 0.25f, 0.85f, 0.30f, 1.0f };
    const RGBA a = t < 0.5f ? red : yellow;
    const RGBA b = t < 0.5f ? yellow : green;
    const float u = t < 0.5f ? t * 2.0f : (t - 0.5f) * 2.0f;
    return RGBA{ a.r + (b.r - a.r) * u, a.g + (b.g - a.g) * u, a.b + (b.b - a.b) * u, 1.0f };
}
}

void Renderer::drawWrappedText(const std::string& text, const Rect& box, RGBA tint) {
    const float th = listTextHeight("Ag");
    float ty = box.y;
    for (const std::string& ln : wrapListText(text, box.w)) {
        if (ty + th > box.y + box.h + 0.5f) break;
        drawListText(ln, box.x, ty, tint);
        ty += th;
    }
}

int Renderer::drawStackGrid(const Rect& area, const std::vector<Rect>& cells, const ItemStack* stacks, size_t stackCount,
                            int skip, float mx, float my, CellLight light) {
    drawRect(area, theme::WELL);
    int hover = -1;
    for (size_t i = 0; i < cells.size(); ++i) {
        const Rect& r = cells[i];
        const ItemStack* st = stacks && i < stackCount ? &stacks[i] : nullptr;
        const bool filled = st && st->item != 0 && st->count > 0;
        const bool hot = r.contains(mx, my) && (light == CellLight::UnderMouse || filled);
        drawRect(r, hot ? theme::SLOT_HOT : theme::SLOT);
        if (hot && light == CellLight::WhenFilled) noteHover(r);
        if (!filled || (int)i == skip) continue;
        drawItemCell(r, st->item, st->count);
        if (hot) hover = (int)i;
    }
    return hover;
}

void Renderer::drawInteractionHeader(const Rect& bar, const Rect& nameBox, const std::string& name) {
    const float s = uiScale();
    const float th = listTextHeight("Ag");
    drawRect(bar, theme::PINK_DEEP);
    drawListText(fitText(lang("UILABEL_INTERACTION_PANEL"), bar.w - 12.0f * s, false), bar.x + 6.0f * s,
                 bar.y + (bar.h - th) * 0.5f, WHITE);
    drawListText(fitTextTip(name, nameBox.w, nameBox), nameBox.x, nameBox.y, WHITE);
}

void Renderer::drawDialogue(const DialogueLayout& L, const std::vector<SpokenLine>& convo,
                            const std::vector<DialogueOption>& options, float mx, float my) {
    const float s = uiScale();
    const RGBA playerTint{ 0.72f, 0.88f, 1.0f, 1.0f };
    const RGBA rowBg = theme::alpha(theme::SLOT, 0.92f);
    const Rect& b = L.box;
    drawRect(Rect{ b.x - 1.5f * s, b.y - 1.5f * s, b.w + 3.0f * s, b.h + 3.0f * s }, theme::PINK);
    drawRect(b, theme::alpha(theme::MENU_FILL, 0.96f));

    const float lineH = listTextHeight("Ag");
    const size_t shownFrom = convo.size() > L.lines.size() ? convo.size() - L.lines.size() : 0;
    for (size_t i = 0; i < L.lines.size() && shownFrom + i < convo.size(); ++i) {
        const SpokenLine& ln = convo[shownFrom + i];
        const Rect& r = L.lines[i];
        drawRect(r, rowBg);
        const float pic = r.h - 10.0f * s;
        const Rect picRect = ln.player ? Rect{ r.x + r.w - pic - 5.0f * s, r.y + 5.0f * s, pic, pic }
                                       : Rect{ r.x + 5.0f * s, r.y + 5.0f * s, pic, pic };
        drawRect(Rect{ picRect.x - 1.0f * s, picRect.y - 1.0f * s, picRect.w + 2.0f * s, picRect.h + 2.0f * s }, theme::PINK_DEEP);
        drawUnitPicture(ln.sprite, picRect);
        const float textX = ln.player ? r.x + 10.0f * s : picRect.x + picRect.w + 10.0f * s;
        const float textW = r.w - pic - 24.0f * s;
        const std::vector<std::string> wrapped = wrapListText(ln.text, textW);
        for (size_t j = 0; j < wrapped.size() && (float)j * lineH < r.h - 8.0f * s; ++j) {
            drawListText(fitText(wrapped[j], textW, false), textX, r.y + 6.0f * s + (float)j * lineH,
                         ln.player ? playerTint : WHITE);
        }
    }

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
    const float s = uiScale();
    const RGBA panel = theme::alpha(theme::PANEL_FILL, 0.95f);
    const float th = listTextHeight("Ag");
    const float pad = 10.0f * s, line = 20.0f * s;

    drawRect(L.interaction, panel);
    drawRect(L.interBar, theme::PINK_DEEP);
    drawListText(lang("UILABEL_INTERACTION_PANEL"), L.interBar.x + 6.0f * s, L.interBar.y + (L.interBar.h - th) * 0.5f, WHITE);
    const float nameY = L.interBar.y + L.interBar.h + pad;
    drawListText(fitText(npcName, L.interaction.w - 2.0f * pad, false), L.interaction.x + pad, nameY, WHITE);
    drawListText(lang("UILABEL_INVENTORY"), L.interaction.x + pad, nameY + line, theme::GOLD);
    const int hoverNpc = drawStackGrid(L.npcGrid, L.npcSlots, npcInv ? npcInv->slots.data() : nullptr,
                                       (size_t)INVENTORY_SLOTS, dragFrom, mx, my);

    drawRect(L.tradePanel, panel);
    drawRect(L.tradeBar, theme::PINK_DEEP);
    drawListText(lang("UILABEL_TRADE_PANEL"), L.tradeBar.x + 6.0f * s, L.tradeBar.y + (L.tradeBar.h - th) * 0.5f, WHITE);
    auto side = [&](const Rect& box, const std::vector<Rect>& cells, const std::vector<TradeGhost>& ghosts,
                    const std::string& hint, const Rect& total) {
        drawRect(box, theme::WELL);
        for (size_t i = 0; i < cells.size(); ++i) {
            drawRect(cells[i], theme::SLOT);
            if (i < ghosts.size()) drawItemCell(cells[i], ghosts[i].stack.item, ghosts[i].stack.count);
        }
        if (ghosts.empty()) {
            const std::vector<std::string> lines = wrapListText(hint, box.w - 2.0f * pad);
            float ty = box.y + (box.h - (float)lines.size() * th) * 0.5f;
            for (const std::string& ln : lines) {
                const std::string shown = fitText(ln, box.w - 2.0f * pad, false);
                drawListText(shown, box.x + (box.w - listTextWidth(shown)) * 0.5f, ty, theme::TEXT_QUIET);
                ty += th;
            }
        }
        drawListText(fitText(langf("UILABEL_TOTAL_VALUE", { std::to_string(tradeValueOf(ghosts)) }), total.w, false), total.x, total.y, WHITE);
    };
    auto nameOver = [&](const Rect& r, const std::string& name) {
        const std::string shown = fitTextTip(name, r.w, r);
        drawListText(shown, r.x, r.y + (r.h - th) * 0.5f, theme::GOLD);
    };
    nameOver(L.theirName, npcName);
    nameOver(L.yourName, yourName);
    side(L.theirBox, L.theirCells, theirs, lang("UILABEL_TRADE_DROP_THEIRS"), L.theirTotal);
    side(L.yourBox, L.yourCells, yours, lang("UILABEL_TRADE_DROP_YOURS"), L.yourTotal);

    if (showValue) drawRect(L.valueBar, theme::WELL);
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
                 status.ok ? theme::OK_TEXT : theme::WARN_TEXT);

    drawButton(L.cancelBtn, lang("UILABEL_CANCEL"), true, L.cancelBtn.contains(mx, my));
    drawButton(L.tradeBtn, lang("UILABEL_TRADE"), status.ok, status.ok && L.tradeBtn.contains(mx, my));

    if (hoverNpc >= 0 && npcInv) {
        const ItemStack& st = npcInv->slots[hoverNpc];
        drawItemTooltip(st.item, st.count, mx, my);
    }
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
    const float th = listTextHeight("Ag");

    drawThemedPanel(L.panel);
    drawInteractionHeader(L.bar, L.name, propName);
    drawListText(fitText(lang(L.output ? "UILABEL_INPUT" : "UILABEL_INVENTORY"), L.heading.w, false), L.heading.x,
                 L.heading.y, theme::GOLD);
    ItemStack hoverStack{};
    const ItemStack* in = storage ? storage->slots.data() : nullptr;
    const int hotIn = drawStackGrid(L.grid, L.slots, in, (size_t)STORAGE_SLOTS, dragFrom, mx, my);
    if (hotIn >= 0) hoverStack = in[hotIn];
    if (L.output) {
        drawListText(fitText(lang("UILABEL_OUTPUT"), L.outHeading.w, false), L.outHeading.x, L.outHeading.y, theme::GOLD);
        const ItemStack* out = output ? output->slots.data() : nullptr;
        const int hotOut = drawStackGrid(L.outGrid, L.outSlots, out, (size_t)STORAGE_SLOTS, dragFrom - STORAGE_SLOTS, mx, my);
        if (hotOut >= 0) hoverStack = out[hotOut];
        const std::string line = fitTextTip(status, L.status.w, L.status);
        drawListText(line, L.status.x, L.status.y + (L.status.h - th) * 0.5f, theme::AMBER);
    }
    drawWrappedText(hint, L.hint, theme::TEXT_QUIET);
    if (L.lockToggle && !toggleLabel.empty()) drawButton(L.lockBtn, toggleLabel, true, L.lockBtn.contains(mx, my));
    drawButton(L.closeBtn, lang("UILABEL_CLOSE"), true, L.closeBtn.contains(mx, my));
    if (hoverStack.item != 0 && dragFrom < 0) drawItemTooltip(hoverStack.item, hoverStack.count, mx, my);
}

void Renderer::drawLoot(const LootLayout& L, const std::string& name, const Equipment* worn, const Inventory* inv,
                        float mx, float my) {
    drawThemedPanel(L.panel);
    drawInteractionHeader(L.bar, L.name, name);
    int hoverItem = 0, hoverCount = 0;
    drawListText(lang("UILABEL_EQUIPMENT"), L.gearHeading.x, L.gearHeading.y, theme::GOLD);
    drawRect(L.gearGrid, theme::WELL);
    for (size_t i = 0; i < L.gear.size() && i < (size_t)GEAR_SLOTS; ++i) {
        const Rect& r = L.gear[i];
        const int item = worn ? worn->items[i] : 0;
        const bool hot = item != 0 && r.contains(mx, my);
        drawRect(r, hot ? theme::SLOT_HOT : theme::SLOT);
        if (hot) noteHover(r);
        if (item == 0) {
            drawUiIcon(gearSlotIcon((int)i), Rect{ r.x + 2.0f, r.y + 2.0f, r.w - 4.0f, r.h - 4.0f }, theme::EMPTY_SLOT);
            continue;
        }
        drawItemCell(r, item, 1);
        if (hot) { hoverItem = item; hoverCount = 1; }
    }
    drawListText(lang("UILABEL_INVENTORY"), L.invHeading.x, L.invHeading.y, theme::GOLD);
    const int hotInv = drawStackGrid(L.invGrid, L.slots, inv ? inv->slots.data() : nullptr, (size_t)INVENTORY_SLOTS, -1,
                                     mx, my, CellLight::WhenFilled);
    if (hotInv >= 0) { hoverItem = inv->slots[hotInv].item; hoverCount = inv->slots[hotInv].count; }
    drawWrappedText(lang("UILABEL_LOOT_HINT"), L.hint, theme::TEXT_QUIET);
    drawButton(L.takeAllBtn, lang("UILABEL_TAKE_ALL"), true, L.takeAllBtn.contains(mx, my));
    drawButton(L.closeBtn, lang("UILABEL_CLOSE"), true, L.closeBtn.contains(mx, my));
    if (hoverItem != 0) drawItemTooltip(hoverItem, hoverCount, mx, my);
}

void Renderer::drawOptimizeAll(const OptimizeAllLayout& L, int unitCount, int attackShare, const std::array<bool, 3>& toggles,
                               float mx, float my) {
    const float s = uiScale();
    const float th = listTextHeight("Ag");
    drawScrim();
    drawThemedPanel(L.box);
    drawListText(fitText(lang("UILABEL_OPTIMIZE_ALL"), L.title.w, false), L.title.x, L.title.y + (L.title.h - th) * 0.5f, WHITE);
    drawWrappedText(langf("MISC_OPTIMIZE_ALL_ASK", { std::to_string(unitCount) }), L.text, theme::TEXT_DIM);
    const int share = std::clamp(attackShare, 0, 100);
    const std::string sl = langf("UILABEL_OPTIMIZE_BALANCE", { std::to_string(share), std::to_string(100 - share) });
    drawListText(fitText(sl, L.sliderLabel.w, false), L.sliderLabel.x, L.sliderLabel.y + (L.sliderLabel.h - th) * 0.5f, WHITE);
    drawRect(L.track, RGBA{ 0.24f, 0.10f, 0.12f, 1.0f });
    drawRect(Rect{ L.track.x, L.track.y, L.track.w * share / 100.0f, L.track.h }, theme::GOLD);
    const float kx = L.track.x + L.track.w * share / 100.0f, kw = 8.0f * s, kh = L.track.h + 8.0f * s;
    drawRect(Rect{ kx - kw * 0.5f, L.track.y + (L.track.h - kh) * 0.5f, kw, kh }, WHITE);
    const char* keys[3] = { "UILABEL_OPTIMIZE_ACCESSORIES", "UILABEL_OPTIMIZE_TWO_HANDED", "UILABEL_OPTIMIZE_VALUE_TIES" };
    for (int i = 0; i < 3; ++i) {
        const Rect& r = L.toggles[(size_t)i];
        const float box = r.h - 6.0f * s;
        const Rect b{ r.x, r.y + 3.0f * s, box, box };
        const bool hot = r.contains(mx, my);
        if (hot) noteHover(r);
        drawRect(b, hot ? RGBA{ 0.42f, 0.28f, 0.13f, 1.0f } : RGBA{ 0.48f, 0.24f, 0.27f, 1.0f });
        drawRect(Rect{ b.x + 2.0f * s, b.y + 2.0f * s, b.w - 4.0f * s, b.h - 4.0f * s },
                 toggles[(size_t)i] ? theme::GOLD : theme::BOX_FILL);
        const std::string label = lang(keys[i]);
        const float lx = b.x + box + 8.0f * s;
        const std::string shown = fitTextTip(label, r.x + r.w - lx, r);
        drawListText(shown, lx, r.y + (r.h - th) * 0.5f, WHITE);
    }
    drawButton(L.confirm, lang("UILABEL_CONFIRM"), unitCount > 0, unitCount > 0 && L.confirm.contains(mx, my));
    drawButton(L.cancel, lang("UILABEL_CANCEL"), true, L.cancel.contains(mx, my));
}

void Renderer::drawDisband(World& w, const DisbandLayout& L, ObjectId unit, float held, float mx, float my) {
    const float s = uiScale();
    const float th = listTextHeight("Ag");
    drawScrim();
    drawThemedPanel(L.box);
    drawListText(fitText(lang("UILABEL_DISBAND"), L.title.w, false), L.title.x, L.title.y + (L.title.h - th) * 0.5f, WHITE);

    drawRect(L.portrait, theme::TAB_OFF);
    const TextTex& g = unitPortrait(w, unit);
    drawPortrait(g, Rect{ L.portrait.x, L.portrait.y, L.portrait.w, L.portrait.w }, unitSaturation(w, unit));

    const Name* nm = w.get<Name>(unit);
    const std::string name = nm && !nm->text.empty() ? nm->text : std::string(lang("UILABEL_UNNAMED_UNIT"));
    const float rowH = L.info.h / (float)(1 + DISBAND_INFO_LINES);
    drawUiText(fitText(name, L.info.w, true), L.info.x, L.info.y + (rowH - uiTextHeight("Ag")) * 0.5f, WHITE);
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
        drawListText(fitText(lines[i], L.info.w, false), L.info.x, ry + (rowH - th) * 0.5f, theme::TEXT_DIM);
    }

    const std::string what = mb ? langf("MISC_DISBAND_ASK_MOB", { name }) : langf("MISC_DISBAND_ASK", { name });
    drawWrappedText(what, L.text, theme::TEXT_DIM);
    const std::string hint = langf("UILABEL_DISBAND_HOLD", { std::to_string(DISBAND_HOLD_MS / 1000) });
    drawListText(fitText(hint, L.hint.w, false), L.hint.x + (L.hint.w - std::min(L.hint.w, listTextWidth(hint))) * 0.5f,
                 L.hint.y + (L.hint.h - th) * 0.5f, WHITE);

    drawButton(L.disband, lang("UILABEL_DISBAND"), true, L.disband.contains(mx, my));
    const float f = std::clamp(held, 0.0f, 1.0f);
    if (f > 0.0f) {
        drawRect(Rect{ L.disband.x, L.disband.y, L.disband.w * f, L.disband.h }, RGBA{ 0.85f, 0.12f, 0.12f, 0.55f });
        const std::string label = lang("UILABEL_DISBAND");
        drawListText(label, L.disband.x + (L.disband.w - listTextWidth(label)) * 0.5f, L.disband.y + (L.disband.h - th) * 0.5f, WHITE);
    }
    drawButton(L.cancel, lang("UILABEL_CANCEL"), true, L.cancel.contains(mx, my));
    (void)s;
}

void Renderer::drawQuantityPrompt(const QuantityLayout& L, const std::string& itemName, int chosen, int available,
                                  float mx, float my) {
    const float s = uiScale();
    const float th = listTextHeight("Ag");
    drawThemedPanel(L.box);
    const std::string title = langf("UILABEL_CHOOSE_HOW_MANY", { itemName, std::to_string(available) });
    drawListText(fitText(title, L.title.w, false), L.title.x, L.title.y + (L.title.h - th) * 0.5f, WHITE);
    drawRect(L.countBox, theme::WELL);
    const std::string num = std::to_string(chosen);
    drawListText(num, L.countBox.x + (L.countBox.w - listTextWidth(num)) * 0.5f,
                 L.countBox.y + (L.countBox.h - th) * 0.5f, WHITE);
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

ItemMenuLayout Renderer::itemMenuLayout(float x, float y, int buttonCount, int winW, int winH) {
    return layoutItemMenu(x, y, buttonCount, listTextHeight("Ag"), winW, winH);
}

void Renderer::drawItemMenu(const ItemMenuLayout& L, const std::vector<std::string>& labels, float mx, float my) {
    const float s = uiScale();
    drawRect(L.box, theme::alpha(theme::PINK, 0.8f));
    drawRect(Rect{ L.box.x + 1.0f, L.box.y + 1.0f, L.box.w - 2.0f, L.box.h - 2.0f }, theme::alpha(theme::NIGHT, 0.92f));
    for (size_t i = 0; i < L.buttons.size() && i < labels.size(); ++i) {
        const Rect& r = L.buttons[i];
        const bool hot = r.contains(mx, my);
        if (hot) noteHover(r);
        if (hot) drawRectGradientH(r, rgb255(52, 30, 16, 0.85f), rgb255(96, 58, 30, 0.95f));
        drawRect(Rect{ r.x, r.y, 2.0f * s, r.h }, hot ? theme::GOLD : theme::alpha(theme::ASH, 0.35f));
        const float tw = listTextWidth(labels[i]), th = listTextHeight(labels[i]);
        drawListText(labels[i], r.x + (r.w - tw) * 0.5f, r.y + (r.h - th) * 0.5f, hot ? theme::BONE : rgb255(232, 214, 190, 1.0f));
    }
}
