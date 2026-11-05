// Renderer: the small pieces panels are built from: buttons, icons, title bars, item cells and item tooltips.
#include "renderer_internal.h"

using namespace render_detail;

namespace {

// The color of an item's name by tier: gray at tier 1, rising to gold at tier 6. Tier 0 stays white.
RGBA tierTint(int tier) {
    if (tier <= 0) return WHITE;
    const float t = std::clamp((tier - 1) / 5.0f, 0.0f, 1.0f);
    const RGBA gray{ 0.70f, 0.62f, 0.56f, 1.0f }, gold{ 1.0f, 0.84f, 0.32f, 1.0f };
    return RGBA{ gray.r + (gold.r - gray.r) * t, gray.g + (gold.g - gray.g) * t,
                 gray.b + (gold.b - gray.b) * t, 1.0f };
}

// The tooltip for an item: its name in its tier's color, then its stats that are not zero.
// Colors for the parts of a tooltip. A change is drawn theme::GOOD or theme::BAD, as the combat text is.

const RGBA TIP_TEXT{ 0.87f, 0.80f, 0.72f, 1.0f };

const RGBA TIP_HEADING = theme::GOLD;

const RGBA TIP_WARNING{ 1.0f, 0.72f, 0.35f, 1.0f };

// The numbers a tooltip compares, summed over the items an item would replace
struct GearSum {
    int attack = 0, defense = 0, durability = 0;
    float weight = 0.0f;
};

GearSum sumGear(const std::vector<int>& items) {
    GearSum s;
    for (int id : items) {
        const ItemDef* d = findItem(id);
        if (!d) continue;
        s.attack += d->attackValue;
        s.defense += d->defenseValue;
        s.weight += d->weight;
        if (d->maxDurability > 0) s.durability += d->currentDurability;
    }
    return s;
}

// The rows of an item's tooltip. With a baseline (the gear it would replace), each number that changes
// shows the change in brackets: green when better, red when worse. A lighter weight is better.
// Wraps a line of tooltip text to the tooltip's text width
using TipWrap = std::function<std::vector<std::string>(const std::string&)>;

// "main_hand" reads lang("UILABEL_SLOT_MAIN_HAND"); "armor" reads "Armor"
std::string tipWords(std::string s) {
    for (char& c : s) if (c == '_') c = ' ';
    if (!s.empty()) s[0] = (char)std::toupper((unsigned char)s[0]);
    return s;
}

// The language's word for an item's type, material or slot: the key prefix and the id in capitals (armor reads
// UILABEL_TYPE_ARMOR). A word with no key yet shows the fallback.
std::string itemWord(const std::string& prefix, const std::string& id, const std::string& fallback) {
    std::string key = prefix + id;
    for (char& c : key) c = (char)std::toupper((unsigned char)c);
    const std::string text = lang(key);
    return text == key ? fallback : text;
}

std::vector<TipRow> itemRows(const ItemDef& d, int count, const GearSum* base, const TipWrap& wrap) {
    std::vector<TipRow> rows;
    std::string title = itemDisplayName(d);
    if (count > 1) title += " x" + std::to_string(count);
    rows.push_back(TipRow{ title, tierTint(d.tier) });
    const std::string desc = itemDisplayDescription(d);
    if (!desc.empty()) {
        for (const std::string& line : wrap(desc)) rows.push_back(TipRow{ line, TIP_TEXT });
    }
    rows.push_back(TipRow{ langf("UILABEL_TIP_TYPE", { itemWord("UILABEL_TYPE_", d.typeId, tipWords(d.typeId)) }), TIP_TEXT });
    if (!d.material.empty()) rows.push_back(TipRow{ langf("UILABEL_TIP_MATERIAL", { itemWord("MISC_MATERIAL_", d.material, d.material) }), TIP_TEXT });
    if (!d.equipSlot.empty()) rows.push_back(TipRow{ langf("UILABEL_TIP_SLOT", { itemWord("UILABEL_SLOT_", d.equipSlot, tipWords(d.equipSlot)) }), TIP_TEXT });
    rows.push_back(TipRow{ langf("UILABEL_TIP_REQUIRED_LEVEL", { std::to_string(d.requiredLevel) }), TIP_TEXT });

    auto intChange = [&](TipRow& row, int now, int before, bool higherIsBetter) {
        if (!base || now == before) return;
        const int diff = now - before;
        row.change = "(" + std::string(diff > 0 ? "+" : "") + std::to_string(diff) + ")";
        row.changeTint = ((diff > 0) == higherIsBetter) ? theme::GOOD : theme::BAD;
    };

    if (d.attackValue > 0 || (base && base->attack > 0)) {
        TipRow row{ langf("UILABEL_TIP_ATTACK", { std::to_string(d.attackValue) }), TIP_TEXT };
        intChange(row, d.attackValue, base ? base->attack : 0, true);
        rows.push_back(row);
    }
    if (d.defenseValue > 0 || (base && base->defense > 0)) {
        TipRow row{ langf("UILABEL_TIP_DEFENSE", { std::to_string(d.defenseValue) }), TIP_TEXT };
        intChange(row, d.defenseValue, base ? base->defense : 0, true);
        rows.push_back(row);
    }

    char weight[32];
    std::snprintf(weight, sizeof(weight), "%.1f", d.weight);
    TipRow weightRow{ langf("UILABEL_TIP_WEIGHT", { weight }) + (count > 1 ? lang("UILABEL_TIP_EACH") : std::string()), TIP_TEXT };
    if (base && std::fabs(d.weight - base->weight) >= 0.05f) {
        std::snprintf(weight, sizeof(weight), "(%+.1f)", d.weight - base->weight);
        weightRow.change = weight;
        weightRow.changeTint = d.weight < base->weight ? theme::GOOD : theme::BAD;
    }
    rows.push_back(weightRow);

    if (d.maxDurability > 0) {
        TipRow row{ langf("UILABEL_TIP_DURABILITY", { std::to_string(d.currentDurability), std::to_string(d.maxDurability) }),
                    TIP_TEXT };
        intChange(row, d.currentDurability, base ? base->durability : 0, true);
        rows.push_back(row);
    }
    if (d.maxStackSize > 1) rows.push_back(TipRow{ langf("UILABEL_TIP_STACKS", { std::to_string(d.maxStackSize) }), TIP_TEXT });
    if (d.buyValue > 0) rows.push_back(TipRow{ langf("UILABEL_TIP_BUY", { std::to_string(d.buyValue) }), TIP_TEXT });
    if (d.sellValue > 0) rows.push_back(TipRow{ langf("UILABEL_TIP_SELL", { std::to_string(d.sellValue) }), TIP_TEXT });
    if (!d.onUse.empty()) rows.push_back(TipRow{ langf("UILABEL_TIP_USE", { d.onUse }), TIP_TEXT });
    return rows;
}

// A card beside a comparison: the gear slot, the item in it, and its stats. replaced marks the item the
// hovered item would take the place of. held is 0 for an empty slot.
std::vector<TipRow> gearCardRows(int slot, bool replaced, int held, const TipWrap& wrap) {
    std::vector<TipRow> rows;
    rows.push_back(TipRow{ gearSlotName(slot), TIP_HEADING });
    if (replaced) rows.push_back(TipRow{ lang("UILABEL_WILL_BE_REPLACED"), TIP_WARNING });
    const ItemDef* d = findItem(held);
    if (!d) {
        rows.push_back(TipRow{ lang("UILABEL_EMPTY"), TIP_TEXT });
        return rows;
    }
    for (const TipRow& row : itemRows(*d, 1, nullptr, wrap)) rows.push_back(row);
    return rows;
}

} // namespace

// Draws the tooltip set this frame, beside the mouse and kept on screen, with its text wrapped
void Renderer::drawTip() {
    if (tip_.empty()) return;
    const float s = uiScale();
    const float pad = 6.0f * s;
    const float maxW = std::min(360.0f * s, uiW_ * 0.5f);
    const std::vector<std::string> lines = wrapListText(tip_, maxW - 2.0f * pad);
    float w = 0.0f;
    for (const std::string& ln : lines) w = std::max(w, listTextWidth(ln));
    w += 2.0f * pad;
    const float lh = listTextHeight("Ag");
    const float h = (float)lines.size() * lh + 2.0f * pad;
    float x = mouseX_ + 14.0f * s, y = mouseY_ + 18.0f * s;
    if (x + w > uiW_) x = std::max(0.0f, uiW_ - w);
    if (y + h > uiH_) y = std::max(0.0f, mouseY_ - h - 4.0f * s);
    drawRect(Rect{ x, y, w, h }, theme::alpha(theme::PANEL_FILL, 0.96f));
    float ty = y + pad;
    for (const std::string& ln : lines) {
        drawListText(ln, x + pad, ty, WHITE);
        ty += lh;
    }
    tip_.clear();
}

void Renderer::drawHoldButton(const Rect& r, const std::string& label, float fill) {
    // Held down, it fills with hot pink from the left
    const float s = uiScale();
    drawRectGradientH(r, theme::alpha(theme::PINK_INK, 0.85f), theme::alpha(theme::PINK_DARK, 0.95f));
    if (fill > 0.0f) drawRect(Rect{ r.x, r.y, r.w * fill, r.h }, theme::alpha(theme::PINK, 0.9f));
    drawRect(Rect{ r.x, r.y, 3.0f * s, r.h }, theme::PINK);
    drawUiTextInBox(label, r, theme::BONE);
}

void Renderer::drawListButton(const Rect& r, const std::string& label, bool hovered) {
    // A pink button, for a row's own action (Delete)
    const float s = uiScale();
    if (hovered) noteHover(r);
    drawRectGradientH(r, hovered ? theme::alpha(theme::PINK_DEEP, 0.95f) : theme::alpha(theme::PINK_INK, 0.9f),
                      hovered ? theme::alpha(theme::PINK, 0.95f) : theme::alpha(theme::PINK_DARK, 0.95f));
    drawRect(Rect{ r.x, r.y, 2.0f * s, r.h }, hovered ? theme::BONE : theme::PINK);
    const float tw = listLine(label).w;
    const float th = listTextHeight(label);
    drawListText(label, r.x + (r.w - tw) * 0.5f, r.y + (r.h - th) * 0.5f, theme::BONE);
}

void Renderer::drawButton(const Rect& r, const std::string& label, bool enabled, bool hovered) {
    // A dark band with an edge down its left side; under the pointer the band turns green and the edge lights up
    const float s = uiScale();
    const bool lit = enabled && hovered;
    if (lit) noteHover(r);
    if (!enabled) {
        drawRect(r, theme::alpha(theme::NIGHT, 0.6f));
    } else if (lit) {
        drawRectGradientH(r, rgb255(52, 30, 16, 0.85f), rgb255(96, 58, 30, 0.95f));
    } else {
        drawRectGradientH(r, rgb255(32, 14, 18, 0.85f), rgb255(22, 10, 12, 0.95f));
    }
    drawRect(Rect{ r.x, r.y, 3.0f * s, r.h }, lit ? theme::GOLD : theme::alpha(theme::ASH, enabled ? 0.45f : 0.15f));
    const RGBA fg = !enabled ? theme::alpha(theme::ASH, 0.5f) : lit ? theme::BONE : rgb255(232, 214, 190, 1.0f);
    // A small inset keeps the text off the button's edge
    drawUiTextInBox(label, Rect{ r.x + 6.0f * s, r.y + 2.0f * s, r.w - 10.0f * s, r.h - 4.0f * s }, fg);
}

float Renderer::tipWidth(const std::vector<TipRow>& rows) {
    const float s = uiScale(), gap = 6.0f * s, pad = 7.0f * s;
    float w = 0.0f;
    for (const TipRow& r : rows) {
        float rowW = listTextWidth(r.text);
        if (!r.change.empty()) rowW += gap + listTextWidth(r.change);
        w = std::max(w, rowW);
    }
    return w + 2.0f * pad;
}

float Renderer::tipHeight(size_t rowCount) {
    if (rowCount == 0) return 0.0f;
    const float s = uiScale(), gap = 2.0f * s, pad = 7.0f * s;
    return rowCount * listTextHeight("Ag") + (rowCount - 1) * gap + 2.0f * pad;
}

void Renderer::drawTip(const std::vector<TipRow>& rows, float x, float y, float width) {
    const float s = uiScale(), gap = 2.0f * s, pad = 7.0f * s;
    const float lineH = listTextHeight("Ag");
    drawRect(Rect{ x, y, width, tipHeight(rows.size()) }, RGBA{ 0.0f, 0.0f, 0.0f, 0.5f });
    float ty = y + pad;
    for (const TipRow& r : rows) {
        drawListText(r.text, x + pad, ty, r.tint);
        if (!r.change.empty()) {
            drawListText(r.change, x + pad + listTextWidth(r.text) + 6.0f * s, ty, r.changeTint);
        }
        ty += lineH + gap;
    }
}

void Renderer::drawItemTooltip(int item, int count, float mx, float my, const Equipment* compareTo) {
    const ItemDef* d = findItem(item);
    if (!d) return;
    const float s = uiScale();
    const float gap = 8.0f * s;

    // Gear is compared with the gear it would replace, and each gear slot of its kind gets a card
    const bool compare = compareTo && !d->equipSlot.empty();
    std::vector<int> displaced;
    std::vector<int> replacedItems;
    if (compare) {
        displaced = displacedSlots(*compareTo, item);
        for (int slot : displaced) replacedItems.push_back(compareTo->items[slot]);
    }
    const GearSum base = sumGear(replacedItems);
    const float tipTextW = 260.0f * s;
    const TipWrap wrapTip = [this, tipTextW](const std::string& t) { return wrapListText(t, tipTextW); };
    const std::vector<TipRow> main = itemRows(*d, count, compare ? &base : nullptr, wrapTip);

    std::vector<std::vector<TipRow>> cards;
    if (compare) {
        for (int slot : comparisonSlots(item)) {
            const int held = compareTo->items[slot];
            const bool replaced = held != 0 && std::find(displaced.begin(), displaced.end(), slot) != displaced.end();
            cards.push_back(gearCardRows(slot, replaced, held, wrapTip));
        }
    }

    // The tooltip sits beside the mouse, moved to its left near the right edge
    const float mainW = tipWidth(main), mainH = tipHeight(main.size());
    float bx = mx + 14.0f * s;
    if (bx + mainW > uiW_) bx = mx - mainW - 14.0f * s;
    bx = std::max(0.0f, bx);

    std::vector<float> cardW;
    float cardsW = 0.0f, cardsH = 0.0f;
    for (const auto& c : cards) {
        cardW.push_back(tipWidth(c));
        cardsW += cardW.back() + (cardW.size() > 1 ? gap : 0.0f);
        cardsH = std::max(cardsH, tipHeight(c.size()));
    }

    float by = my + 12.0f * s;
    const float tallest = std::max(mainH, cardsH);
    if (by + tallest > uiH_) by = uiH_ - tallest;
    by = std::max(0.0f, by);

    // The cards go on the side of the tooltip that has room: to its right if they fit, else to its left
    const bool right = bx + mainW + gap + cardsW <= uiW_;
    float cx = std::max(0.0f, right ? bx + mainW + gap : bx - gap - cardsW);

    drawTip(main, bx, by, mainW);
    for (size_t i = 0; i < cards.size(); ++i) {
        drawTip(cards[i], cx, by, cardW[i]);
        cx += cardW[i] + gap;
    }
}

void Renderer::drawPortrait(const TextTex& g, const Rect& square, float saturation) {
    if (!g.tex) return;
    setSaturation(saturation);
    drawFitted(g, square, WHITE);
    setSaturation(1.0f);
}

// The X at the right end of a window's title bar, framed like the [-] and [+] buttons
void Renderer::drawCloseButton(const Rect& r) {
    const float s = uiScale();
    const bool hot = r.contains(mouseX_, mouseY_);
    if (hot) noteHover(r);
    drawRect(r, theme::alpha(theme::PINK, 0.9f));
    drawRect(Rect{ r.x + 1.0f, r.y + 1.0f, r.w - 2.0f, r.h - 2.0f },
             hot ? theme::alpha(theme::PINK_DEEP, 0.95f) : theme::alpha(theme::PINK_INK, 0.95f));
    const float m = r.w * 0.3f, lw = std::max(1.5f, 1.5f * s);
    const float x0 = r.x + m, y0 = r.y + m, x1 = r.x + r.w - m, y1 = r.y + r.h - m;
    auto line = [&](float ax, float ay, float bx, float by) {
        const float dx = bx - ax, dy = by - ay, len = std::sqrt(dx * dx + dy * dy);
        const float nx = -dy / len * lw * 0.5f, ny = dx / len * lw * 0.5f;
        drawUiTris({ { ax + nx, ay + ny, WHITE }, { bx + nx, by + ny, WHITE }, { bx - nx, by - ny, WHITE },
                     { ax + nx, ay + ny, WHITE }, { bx - nx, by - ny, WHITE }, { ax - nx, ay - ny, WHITE } });
    };
    line(x0, y0, x1, y1);
    line(x0, y1, x1, y0);
}

// The item's picture at exactly ITEM_PICTURE_PX square, its top left on a whole pixel, centered on (cx, cy)
void Renderer::drawItemPictureAt(float cx, float cy, int item, float alpha) {
    const float x = std::floor(cx - ITEM_PICTURE_PX * 0.5f + 0.5f), y = std::floor(cy - ITEM_PICTURE_PX * 0.5f + 0.5f);
    const float w = ITEM_PICTURE_PX, h = ITEM_PICTURE_PX;
    drawTexture(itemTexture(item)->tex, x, y, w, h, RGBA{ 1.0f, 1.0f, 1.0f, alpha });
}

void Renderer::drawItemPicture(const Rect& cell, int item, float alpha) {
    drawItemPictureAt(cell.x + cell.w * 0.5f, cell.y + cell.h * 0.5f, item, alpha);
}

void Renderer::drawItemCell(const Rect& r, int item, int count) {
    if (item == 0) return;
    const float s = uiScale();
    drawItemPicture(r, item, 1.0f);
    if (count > 1) {
        const std::string n = std::to_string(count);
        drawListText(n, r.x + r.w - listTextWidth(n) - 3.0f * s, r.y + r.h - listTextHeight("Ag") - 1.0f * s,
                     WHITE);
    }
}

// A black bar across the top at 30% opacity, with PAUSED in large white letters, centred. With a hint, a line under
// it says how to resume.
float Renderer::pauseBarHeight(bool hint) {
    const float s = uiScale();
    const float pad = 10.0f * s;
    const float bigH = uiTextHeight(lang("UILABEL_PAUSED")) * 2.6f;
    const float tipH = hint ? uiTextHeight(lang("UILABEL_PAUSE_HINT")) : 0.0f;
    return pad + bigH + (hint ? pad + tipH : 0.0f) + pad;
}

void Renderer::drawPauseBar(bool hint) {
    const float s = uiScale();
    const float pad = 10.0f * s;
    const float bigH = uiTextHeight(lang("UILABEL_PAUSED")) * 2.6f;
    const std::string tip = lang("UILABEL_PAUSE_HINT");
    const float h = pauseBarHeight(hint);
    drawRect(Rect{ 0.0f, 0.0f, (float)uiW_, h }, theme::alpha(theme::NIGHT, 0.55f));
    drawRectGradientH(Rect{ 0.0f, h - 2.0f * s, uiW_ * 0.5f, 2.0f * s }, theme::alpha(theme::PINK, 0.0f), theme::PINK);
    drawRectGradientH(Rect{ uiW_ * 0.5f, h - 2.0f * s, uiW_ * 0.5f, 2.0f * s }, theme::PINK, theme::alpha(theme::PINK, 0.0f));
    drawBigText(uiUpper(lang("UILABEL_PAUSED")), uiW_ * 0.5f, pad + bigH * 0.5f, bigH, theme::BONE);
    if (hint) drawUiText(tip, uiW_ * 0.5f - uiTextWidth(tip) * 0.5f, pad + bigH + pad, theme::ASH);
}

void Renderer::drawThemedPanel(const Rect& r) {
    const float s = uiScale();
    const float frame = std::max(1.0f, 2.0f * s), cut = 18.0f * s;
    std::vector<UiVert> v;
    cutCornerShape(v, r, cut, theme::PINK, theme::PINK_DEEP);
    cutCornerShape(v, Rect{ r.x + frame, r.y + frame, r.w - 2.0f * frame, r.h - 2.0f * frame }, cut - frame * 0.6f,
                   theme::alpha(rgb255(34, 14, 18, 1.0f), 0.97f), theme::alpha(rgb255(12, 5, 6, 1.0f), 0.98f));
    drawUiTris(v);
}

void Renderer::drawScrollBar(const Rect& track, const Rect& thumb, bool hot) {
    drawRect(track, theme::SCROLL_TRACK);
    drawRect(thumb, hot ? RGBA{ 1.0f, 0.565f, 0.565f, 1.0f } : theme::alpha(theme::PINK, 0.9f));
}

void Renderer::drawScrim() {
    drawRect(Rect{ 0.0f, 0.0f, (float)uiW_, (float)uiH_ }, theme::SCRIM);
}

// The lock at the left of a panel's [-] or [+]: closed while the panel's size and place are fixed, open (and green)
// while it can be moved and resized
void Renderer::drawLockButton(const Rect& r, bool locked) {
    const float s = uiScale();
    if (r.contains(mouseX_, mouseY_)) noteHover(r);
    drawRect(r, locked ? theme::alpha(theme::PINK, 0.9f) : theme::GOLD);
    drawRect(Rect{ r.x + 1.0f, r.y + 1.0f, r.w - 2.0f, r.h - 2.0f },
             locked ? theme::alpha(theme::PINK_INK, 0.95f) : theme::alpha(theme::SLOT_HOT, 0.95f));
    // The body: a small block in the lower half
    const float bw = r.w * 0.52f, bh = r.h * 0.34f;
    const float bx = r.x + (r.w - bw) * 0.5f, by = r.y + r.h * 0.50f;
    drawRect(Rect{ bx, by, bw, bh }, WHITE);
    // The shackle: a hoop over the body, its right leg lifted out when open
    const float lw = std::max(1.0f, 1.3f * s);
    const float sx0 = bx + bw * 0.18f, sx1 = bx + bw * 0.82f;
    const float top = r.y + r.h * 0.18f + (locked ? 0.0f : -r.h * 0.06f);
    const float legBottom = locked ? by : by - r.h * 0.12f;
    drawRect(Rect{ sx0, top, sx1 - sx0, lw }, WHITE);                 // across the top
    drawRect(Rect{ sx0, top, lw, by - top }, WHITE);                  // left leg, down into the body
    drawRect(Rect{ sx1 - lw, top, lw, legBottom - top }, WHITE);      // right leg
}

void Renderer::drawSmallButton(const Rect& r, const std::string& label, bool enabled, bool hovered) {
    const float s = uiScale();
    const bool lit = enabled && hovered;
    if (lit) noteHover(r);
    if (!enabled) drawRect(r, theme::alpha(theme::NIGHT, 0.6f));
    else if (lit) drawRectGradientH(r, rgb255(52, 30, 16, 0.85f), rgb255(96, 58, 30, 0.95f));
    else drawRectGradientH(r, rgb255(32, 14, 18, 0.85f), rgb255(22, 10, 12, 0.95f));
    drawRect(Rect{ r.x, r.y, 2.0f * s, r.h }, lit ? theme::GOLD : theme::alpha(theme::ASH, enabled ? 0.45f : 0.15f));
    const RGBA fg = !enabled ? theme::alpha(theme::ASH, 0.5f) : lit ? theme::BONE : rgb255(232, 214, 190, 1.0f);
    const float tw = listTextWidth(label), th = listTextHeight(label);
    drawListText(label, r.x + (r.w - tw) * 0.5f, r.y + (r.h - th) * 0.5f, fg);
}

void Renderer::drawDraggedStack(float mx, float my, int item, int count) {
    const float s = uiScale();
    const float sz = ITEM_PICTURE_PX;
    const Rect r{ std::floor(mx - sz * 0.5f + 0.5f), std::floor(my - sz * 0.5f + 0.5f), sz, sz };
    drawItemPicture(r, item, 0.85f);
    if (count > 1) {
        const std::string n = std::to_string(count);
        drawListText(n, r.x + r.w - listTextWidth(n) - 3.0f * s, r.y + r.h - listTextHeight(n) - 1.0f * s, WHITE);
    }
    const std::string name = itemNameOf(item);
    drawListText(name, r.x + r.w + 6.0f * s, my - listTextHeight(name) * 0.5f, WHITE);
}

void Renderer::drawUiIcon(int icon, const Rect& r) {
    const TextTex& t = uiIconTexture(icon);
    if (t.tex) {
        drawFitted(t, r, WHITE);
        return;
    }
    // The placeholder until the picture is packed: a pink square with its id in it
    drawRect(r, theme::PINK);
    const std::string num = std::to_string(icon);
    const float tw = listTextWidth(num), th = listTextHeight("Ag");
    drawListText(num, r.x + (r.w - tw) * 0.5f, r.y + (r.h - th) * 0.5f, RGBA{ 0.16f, 0.04f, 0.05f, 1.0f });
}

void Renderer::drawIconText(float x, float y, float h, int icon, const std::string& text, RGBA tint) {
    const float s = uiScale();
    drawUiIcon(icon, Rect{ x, y, h, h });
    const float th = listTextHeight("Ag");
    drawListText(text, x + h + 4.0f * s, y + (h - th) * 0.5f, tint);
}

void Renderer::drawIconLabel(const Rect& r, int icon, const std::string& text, RGBA tint, bool uiFont) {
    const float s = uiScale();
    const float gap = 6.0f * s;
    const float sz = std::max(0.0f, r.h - 8.0f * s);
    if (uiFont) {
        // The text gets the room beside the icon, and is shrunk to it when automatic text fit is on
        const float avail = std::max(0.0f, r.w - sz - gap - 8.0f * s);
        const std::string shown = fitShrinkText(text, avail);
        if (shown != text) noteTip(text, r);
        const float k = fitShrinkScale(shown, avail, r.h);
        const float x0 = r.x + (r.w - (sz + gap + uiTextWidth(shown) * k)) * 0.5f;
        drawUiIcon(icon, Rect{ x0, r.y + (r.h - sz) * 0.5f, sz, sz });
        drawUiTextScaled(shown, x0 + sz + gap, r.y + (r.h - uiTextHeight("Ag") * k) * 0.5f, k, tint);
        return;
    }
    const float textW = listTextWidth(text);
    const float textH = listTextHeight("Ag");
    const float x0 = r.x + (r.w - (sz + gap + textW)) * 0.5f;
    drawUiIcon(icon, Rect{ x0, r.y + (r.h - sz) * 0.5f, sz, sz });
    drawListText(text, x0 + sz + gap, r.y + (r.h - textH) * 0.5f, tint);
}

void Renderer::drawTitleBar(const Rect& bar, int icon, const std::string& title) {
    const float s = uiScale();
    // A panel's bar: deep pink fading to near black, with a hot pink line along its foot
    drawRectGradientH(bar, theme::alpha(theme::PINK_DEEP, 0.95f), theme::alpha(theme::PINK_INK, 0.92f));
    drawRect(Rect{ bar.x, bar.y + bar.h - std::max(1.0f, 1.0f * s), bar.w, std::max(1.0f, 1.0f * s) }, theme::alpha(theme::PINK, 0.85f));
    if (icon == ICON_NONE) {
        drawListText(title, bar.x + 6.0f * s, bar.y + (bar.h - listTextHeight("Ag")) * 0.5f, WHITE);
        return;
    }
    drawIconText(bar.x + 6.0f * s, bar.y + 3.0f * s, std::max(0.0f, bar.h - 6.0f * s), icon, title, WHITE);
}

void Renderer::drawCollapseButton(const Rect& r, bool collapsed) {
    if (r.contains(mouseX_, mouseY_)) noteHover(r);
    drawRect(r, theme::alpha(theme::PINK, 0.9f));
    drawRect(Rect{ r.x + 1.0f, r.y + 1.0f, r.w - 2.0f, r.h - 2.0f }, theme::alpha(theme::PINK_INK, 0.95f));
    const char* sign = collapsed ? "+" : "-";
    const float tw = listTextWidth(sign), th = listTextHeight("Ag");
    drawListText(sign, r.x + (r.w - tw) * 0.5f, r.y + (r.h - th) * 0.5f, WHITE);
}

void Renderer::drawNotice(const std::string& text, int winW, int winH) {
    if (text.empty()) return;
    const float s = winH / 720.0f;
    const float pad = 12.0f * s, top = 14.0f * s;
    const float w = uiTextWidth(text) + 2.0f * pad;
    const float h = uiTextHeight("Ag") + pad;
    const Rect box{ (winW - w) * 0.5f, top, w, h };
    // A dark band with a green edge on its left, like a lit menu item
    drawRectGradientH(box, rgb255(52, 30, 16, 0.85f), theme::alpha(theme::NIGHT, 0.85f));
    drawRect(Rect{ box.x, box.y, 3.0f * s, box.h }, theme::GOLD);
    drawUiText(text, box.x + pad, top + pad * 0.5f, theme::BONE);
}

void Renderer::drawUnitPicture(int sprite, const Rect& r) {
    drawFitted(unitPicture(sprite), r, WHITE);
}
