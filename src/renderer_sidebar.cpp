// Renderer: the Unit Panel's sidebar tabs: stats, inventory and gear, build, and behaviors.
#include "renderer_internal.h"

using namespace render_detail;

// The scroll bar down the sidebar's right side, when its tab is taller than the panel: the thumb is the part in view
void Renderer::drawSidebarScrollBar(const SidebarLayout& L) {
    if (L.maxScroll <= 0.0f) return;
    const float s = uiScale();
    const Rect track{ L.panel.x + L.panel.w - 9.0f * s, L.body.y + 4.0f * s, 5.0f * s, L.body.h - 8.0f * s };
    const float totalH = L.maxScroll + L.body.h;
    drawScrollBar(track, scrollThumb(track, L.body.h / totalH, 16.0f * s, L.scroll / L.maxScroll));
}

// The Stats tab: every stat's level under the heading of its group, with its progress bar. Returns the stat under
// the mouse, or -1. The hover tooltip is drawn later, outside the panel's clip (drawStatTip).
int Renderer::drawStatsTab(const SidebarLayout& L, const Stats& stats, float mx, float my, bool mob) {
    const float s = uiScale();
    const Rect& area = L.statsArea;
    const float rowH = L.statRowH;
    const float th = listTextHeight("Ag");
    // The level sits on the left, right aligned in its column; then the icon and the name. The name and its
    // progress bar run to the right edge.
    const float levelR = area.x + 26.0f * s;
    const float iconX = area.x + 32.0f * s;
    const float nameX = iconX + rowH;
    const float barEnd = area.x + area.w;
    int hovered = -1;

    auto row = [&](float y, int icon, const std::string& name, const std::string& lv, RGBA tint) {
        // The text sits above a 3px progress bar strip along the bottom edge of the row
        const float ty = y + (rowH - 4.0f * s - th) * 0.5f;
        drawListText(lv, levelR - listTextWidth(lv), ty, tint);
        if (icon >= 0) drawUiIcon(icon, Rect{ iconX, y, rowH - 2.0f * s, rowH - 2.0f * s });
        drawListText(fitText(name, barEnd - 8.0f * s - nameX, false), nameX, ty, tint);
    };
    float y = area.y;
    row(y, -1, lang("UILABEL_STAT"), lang("UILABEL_LEVEL"), theme::TEXT_QUIET);
    y += rowH;
    const StatGroup groups[] = { StatGroup::Attributes, StatGroup::Combat, StatGroup::Gathering,
                                 StatGroup::Crafting, StatGroup::Utility };
    for (StatGroup group : groups) {
        if (mob && group != StatGroup::Attributes && group != StatGroup::Combat) continue;
        const std::string title = group == StatGroup::Attributes ? lang("UILABEL_ATTRIBUTES")
                          : group == StatGroup::Combat ? lang("UILABEL_COMBAT")
                          : group == StatGroup::Gathering ? lang("UILABEL_RESOURCE_GATHERING")
                          : group == StatGroup::Crafting ? lang("UILABEL_CRAFTING") : lang("UILABEL_UTILITY");
        drawListText(title, area.x, y + (rowH - th) * 0.5f, theme::GOLD);
        y += rowH;
        for (int i = 0; i < STAT_COUNT; ++i) {
            if (statInfo((StatId)i).group != group) continue;
            const int lv = levelOf(stats, (StatId)i);
            row(y, statIcon((StatId)i), statName((StatId)i), std::to_string(lv), WHITE);
            // Progress from this level toward the next one
            const float barY = y + rowH - 3.0f * s, barH = 3.0f * s;
            const float barW = barEnd - nameX;
            drawRect(Rect{ nameX, barY, barW, barH }, RGBA{ 0.24f, 0.11f, 0.13f, 1.0f });
            drawRect(Rect{ nameX, barY, barW * levelProgress(stats, (StatId)i), barH }, theme::LEVEL_BAR);
            if (mx >= area.x && mx < area.x + area.w && my >= y && my < y + rowH) hovered = i;
            y += rowH;
        }
    }
    return hovered;
}

// The tooltip for a stat: its name, level, progress to the next level, its current XP and the XP the next level needs
void Renderer::drawStatTip(const Stats& stats, StatId stat, float mx, float my) {
    const float s = uiScale();
    const int lv = levelOf(stats, stat);
    const int pct = (int)std::lround(levelProgress(stats, stat) * 100.0f);
    std::vector<TipRow> rows(5);
    rows[0].text = statName(stat);
    rows[0].tint = theme::GOLD;
    rows[1].text = langf("UILABEL_TIP_LEVEL", { std::to_string(lv) });
    rows[2].text = langf("UILABEL_TIP_PROGRESS", { std::to_string(pct) });
    rows[3].text = langf("UILABEL_TIP_CURRENT_XP", { withCommas(stats.xp[stat]) });
    rows[4].text = langf("UILABEL_NEXT_LEVEL_XP_LINE", { lv >= MAX_LEVEL ? std::string(lang("UILABEL_MAX")) : withCommas(xpForLevel(lv + 1)) });
    const float width = tipWidth(rows), height = tipHeight(rows.size());
    // Beside the mouse, moved to the other side or up when the window has no room for it
    float x = mx + 14.0f * s, y = my + 14.0f * s;
    if (x + width > uiW_) x = mx - 14.0f * s - width;
    if (y + height > uiH_) y = uiH_ - height;
    drawTip(rows, std::max(0.0f, x), std::max(0.0f, y), width);
}

void Renderer::drawInventorySidebar(const SidebarLayout& L, World& w, Entity unit, float mx, float my,
                                    int dimSlot, int dimGear, int dragItem, bool editable) {
    const float s = uiScale();
    const float bgAlpha = 0.5f;   // every background in the sidebar; text, item colors and bars stay solid
    // The bar with its [-] or [+] button, and the tabs under it. They stay where they are when the content scrolls.
    // They take the real pointer, copied here before it is hidden from the contents below.
    auto frame = [&, mx, my]() {
        drawTitleBar(L.title, ICON_UNIT_PANEL, lang("UILABEL_UNIT_PANEL"));
        if (L.minus.w > 0.0f) drawCollapseButton(L.minus, false);
        if (L.plus.w > 0.0f) drawCollapseButton(L.plus, true);
        const std::string names[UNIT_TAB_COUNT] = { lang("UILABEL_INVENTORY"), lang("UILABEL_PROFILE"), lang("UILABEL_STATS"), lang("UILABEL_BEHAVIOR"), lang("UILABEL_BUILD") };
        for (int i = 0; i < UNIT_TAB_COUNT; ++i) {
            const Rect& t = L.tabs[i];
            if (t.w <= 0.0f) continue;
            // A unit not in your squad shows only its Profile: the other tabs are dimmed and do nothing
            const bool on = unitTabEnabled(w, unit, (UnitTab)i);
            drawRect(t, i == idx(L.tab) ? theme::alpha(theme::GOLD_DEEP, 0.95f) : RGBA{ 0.16f, 0.07f, 0.085f, on ? 0.9f : 0.45f });
            if (i == idx(L.tab)) drawRect(Rect{ t.x, t.y + t.h - 2.0f, t.w, 2.0f }, theme::GOLD);
            if (on && t.contains(mx, my)) noteHover(t);
            const float tw = listTextWidth(names[i]), th = listTextHeight("Ag");
            drawListText(names[i], t.x + (t.w - tw) * 0.5f, t.y + (t.h - th) * 0.5f,
                         on ? WHITE : RGBA{ 0.60f, 0.51f, 0.47f, 0.6f });
        }
    };
    drawRect(L.panel, RGBA{ 0.07f, 0.025f, 0.035f, bgAlpha });
    // The contents scroll under the bar and the tabs, and only the part in the body shows: a pointer outside the body
    // is over none of them, so nothing hidden lights up or shows its tooltip
    if (!L.body.contains(mx, my)) mx = my = -1.0e9f;
    if (!L.open) {
        frame();
        return;
    }
    if (L.tab == UnitTab::Build) {
        // The Build tab needs no unit: it lists the props to pick and place. Its rows scroll like the other tabs.
        beginClip(L.body);
        drawBuildSection(L, mx, my);
        drawSidebarScrollBar(L);
        endClip();
        frame();
        return;
    }
    if (unit == NULL_ENTITY) {
        // Nothing is selected, and the body says so
        frame();
        drawUiTextCentered(lang("UILABEL_NO_UNIT_SELECTED"), L.panel.x + L.panel.w * 0.5f,
                           L.body.y + (L.body.h - uiTextHeight("Ag")) * 0.5f, theme::TEXT_DIM);
        return;
    }
    const Inventory* inv = w.get<Inventory>(unit);
    const Needs needs = w.get<Needs>(unit) ? *w.get<Needs>(unit) : Needs{};
    const Stats stats = w.get<Stats>(unit) ? *w.get<Stats>(unit) : defaultStats();
    const Name* nm = w.get<Name>(unit);
    const Equipment* worn = w.get<Equipment>(unit);
    // The picked tab's contents scroll, so they are clipped to the body, under the tabs
    beginClip(L.body);

    const float listH = listTextHeight("Ag");
    int hoverGear = -1;
    int hoverSlot = -1;
    int hoverStat = -1;   // the stat under the mouse in the Stats tab
    if (L.tab == UnitTab::Profile) {
    // ---- Profile card: the unit's name, health, hunger and carry, then its picture under them ----
    drawRect(L.profile, RGBA{ 0.11f, 0.05f, 0.06f, bgAlpha });
    drawRect(L.portrait, RGBA{ 0.16f, 0.07f, 0.085f, bgAlpha });
    const TextTex& g = unitPortrait(w, unit);
    drawPortrait(g, Rect{ L.portrait.x, L.portrait.y, L.portrait.w, L.portrait.w }, unitSaturation(w, unit));
    // Low health: a red gradient rises from the bottom of the picture by the share of health lost
    if (needs.maxHp > 0 && needs.hp < needs.maxHp) {
        const float lost = std::clamp(1.0f - (float)needs.hp / (float)needs.maxHp, 0.0f, 1.0f);
        const float gh = L.portrait.h * lost;
        if (gh > 0.0f) {
            drawRectGradient(Rect{ L.portrait.x, L.portrait.y + L.portrait.h - gh, L.portrait.w, gh },
                             theme::alpha(theme::HURT, 0.0f), theme::alpha(theme::HURT, 0.65f));
        }
    }

    // The text across the whole card, over the picture
    const float tx = L.profile.x + 10.0f * s;
    const float rightEdge = L.profile.x + L.profile.w - 10.0f * s;
    const float colW = rightEdge - tx;

    const std::string name = nm ? nm->text : std::string(lang("UILABEL_UNNAMED_UNIT"));
    float y = L.profile.y + 10.0f * s;
    drawUiText(fitText(name, L.profile.x + L.profile.w - 10.0f * s - tx, true), tx, y, WHITE);

    y += uiTextHeight("Ag") + 6.0f * s;
    // Under the name: the faction the unit belongs to, then the body type the name was drawn for, a line each
    auto infoLine = [&](const std::string& line) {
        const std::string shown = fitTextTip(line, colW, Rect{ tx, y, colW, listTextHeight("Ag") });
        drawListText(shown, tx, y, theme::TEXT_DIM);
        y += listTextHeight("Ag") + 6.0f * s;
    };
    if (const Faction* fa = w.get<Faction>(unit)) infoLine(factionName(fa->id));
    if (const Body* bd = w.get<Body>(unit)) infoLine(lang(bodyTypeKey(bd->type)));
    // A wild mob has no reputation with anyone: its temperament says how it takes to outsiders (mobs.h)
    if (const Mob* mb = w.get<Mob>(unit); mb && w.has<Faction>(unit) && isMobFaction(w.get<Faction>(unit)->id)) {
        infoLine(langf("UILABEL_TEMPERAMENT", { lang(temperamentKey(mobTemperament(mb->kind))) }));
    }

    // A stat: its label and value on one line, and a bar under them. Returns the y for the next stat.
    auto statRow = [&](float rowY, const std::string& label, const std::string& value, float frac, RGBA fill) {
        drawListText(label, tx, rowY, theme::TEXT_DIM);
        drawListText(value, rightEdge - listTextWidth(value), rowY, WHITE);
        const float barY = rowY + listTextHeight("Ag") + 3.0f * s;
        const float barH = 8.0f * s;
        drawRect(Rect{ tx, barY, colW, barH }, RGBA{ 0.24f, 0.11f, 0.13f, bgAlpha });
        drawRect(Rect{ tx, barY, colW * std::clamp(frac, 0.0f, 1.0f), barH }, fill);
        return barY + barH + 9.0f * s;
    };
    y = statRow(y, lang("UILABEL_HEALTH"), std::to_string(needs.hp) + " / " + std::to_string(needs.maxHp),
                needs.maxHp > 0 ? (float)needs.hp / (float)needs.maxHp : 0.0f, theme::HEALTH_BAR);
    // Doom, under the health, while the unit is Unconscious (or dead, at 100): it dies when the bar is full. One
    // knocked out well rested has no Doom: it just lies out until it wakes.
    if (const Unconscious* un = w.get<Unconscious>(unit); un && !un->rested) {
        const int doom = (int)std::floor(std::clamp(un->doom, 0.0f, 100.0f));
        y = statRow(y, lang("UILABEL_DOOM"), std::to_string(doom) + " / 100", un->doom / 100.0f,
                    RGBA{ 0.56f, 0.20f, 0.78f, 1.0f });
    }
    // A mob has no hunger, Comfort or carry: it can't feed itself and carries nothing. It does have Rested.
    const bool mob = w.has<Mob>(unit);
    if (!mob) {
        y = statRow(y, lang("UILABEL_HUNGER"), std::to_string(needs.hunger) + " / " + std::to_string(needs.maxHunger),
                    needs.maxHunger > 0 ? (float)needs.hunger / (float)needs.maxHunger : 0.0f, RGBA{ 0.86f, 0.62f, 0.22f, 1.0f });
    }
    y = statRow(y, lang("UILABEL_RESTED"), std::to_string(needs.rested) + " / " + std::to_string(needs.maxRested),
                needs.maxRested > 0 ? (float)needs.rested / (float)needs.maxRested : 0.0f, RGBA{ 0.38f, 0.55f, 0.95f, 1.0f });
    if (!mob) {
    y = statRow(y, lang("UILABEL_COMFORT"), std::to_string(needs.comfort) + " / " + std::to_string(needs.maxComfort),
                needs.maxComfort > 0 ? (float)needs.comfort / (float)needs.maxComfort : 0.0f, RGBA{ 0.35f, 0.78f, 0.62f, 1.0f });

    // Carried weight against what Strength lets the unit carry
    const float carried = carriedWeightOf(w, unit);
    const float limit = carryLimit(stats);
    char carry[48];
    std::snprintf(carry, sizeof(carry), "%.1f / %.0f", carried, limit);
    statRow(y, lang("UILABEL_CARRY"), carry, limit > 0.0f ? carried / limit : 0.0f, theme::LEVEL_BAR);
    }
    }

    if (L.tab == UnitTab::Inventory) {
    // ---- Gear: what the unit wears, four slots to a row, then the totals ----
    const bool offLocked = worn && offHandBlocked(*worn);
    const ItemDef* dragDef = dragItem != 0 ? findItem(dragItem) : nullptr;
    // A two-handed weapon, held or dragged, dims the off hand
    const bool dimOff = offLocked || (dragDef && dragDef->equipSlot == "two_hand");
    drawRect(L.gearPanel, RGBA{ 0.10f, 0.04f, 0.05f, bgAlpha });
    drawIconText(L.gearPanel.x + 10.0f * s, L.gearPanel.y + 6.0f * s, listTextHeight("Ag"), ICON_EQUIPMENT, lang("UILABEL_EQUIPMENT"), WHITE);
    // Optimize and Remove All, for a unit of yours. A label too long for its button is shortened, with the whole of it
    // in a tooltip.
    auto gearButton = [&](const Rect& r, const std::string& label) {
        const std::string shown = fitTextTip(label, r.w - 6.0f * s, r);
        drawSmallButton(r, shown, editable, editable && r.contains(mx, my));
    };
    // A mob has no gear slots: the panel says so in their place
    if (w.has<Mob>(unit)) {
        drawListText(lang("UILABEL_MOB_NO_GEAR"), L.gearPanel.x + 10.0f * s, L.gearPanel.y + L.gearPanel.h * 0.5f - listH * 0.5f,
                     theme::TEXT_DIM);
    } else {
        gearButton(L.optimizeButton, lang("UILABEL_OPTIMIZE"));
        gearButton(L.removeAllButton, lang("UILABEL_REMOVE_ALL"));
    }

    // While an item is dragged, each gear slot shows whether it may go there: green if so, red if not.
    // A gear item leaving its own slot is not in the way of itself.
    Equipment eval = worn ? *worn : Equipment{};
    if (dimGear >= 0) eval.items[dimGear] = 0;

    const float inset = 5.0f * s;
    for (size_t gi = 0; gi < L.gear.size() && gi < GEAR_SLOTS; ++gi) {
        const Rect& r = L.gear[gi];
        RGBA edge{ 0.48f, 0.24f, 0.27f, bgAlpha };
        if (dragItem != 0) {
            std::string why;
            const bool fits = canEquip(eval, dragItem, (int)gi, why);
            edge = fits ? RGBA{ 0.35f, 0.90f, 0.45f, 1.0f } : RGBA{ 0.92f, 0.30f, 0.30f, 1.0f };
        }
        drawRect(Rect{ r.x - 1.0f, r.y - 1.0f, r.w + 2.0f, r.h + 2.0f }, edge);
        drawRect(r, RGBA{ 0.16f, 0.07f, 0.085f, bgAlpha });
        if (r.contains(mx, my)) hoverGear = (int)gi;

        const int item = worn ? worn->items[gi] : 0;
        const bool locked = (int)gi == idx(GearSlot::OffHand) && offLocked;
        if (item == 0) {
            // An empty slot shows its name, or lang("UILABEL_TWO_HANDED") for a locked off hand, centred, dimmed and
            // wrapped onto two lines if it needs them. The full name is in the hover tooltip.
            drawSlotLabel(r, locked ? lang("UILABEL_TWO_HANDED") : gearSlotName((int)gi), inset, 2,
                          locked ? theme::TEXT_DIM : theme::alpha(theme::TEXT_DIM, 0.45f));
            continue;
        }

        // The item's 64 x 64 picture fills the cell, as in the inventory. The slot's and the item's names are in the
        // hover tooltip.
        float alpha = 1.0f;
        if ((int)gi == dimGear) alpha = 0.30f;
        else if ((int)gi == idx(GearSlot::OffHand) && dimOff) alpha = 0.35f;
        drawItemPictureAt(r.x + r.w * 0.5f, r.y + r.h * 0.5f, item, alpha);
    }

    int attack = 0, defense = 0;
    if (worn) gearTotals(*worn, attack, defense);
    const std::string totals = langf("UILABEL_ATTACK_DEFENSE", { std::to_string(attack), std::to_string(defense) });
    drawListText(totals, L.gearPanel.x + 10.0f * s, L.gearPanel.y + L.gearPanel.h - listH - 6.0f * s, WHITE);

    }
    // ---- Inventory grid on its own background ----
    if (L.tab == UnitTab::Inventory) {
    drawRect(L.gridPanel, RGBA{ 0.10f, 0.04f, 0.05f, bgAlpha });
    drawIconText(L.gridPanel.x + 10.0f * s, L.gridPanel.y + 6.0f * s, listTextHeight("Ag"), ICON_INVENTORY, lang("UILABEL_INVENTORY"), WHITE);
    // A mob's inventory is not shown until it is a corpse (mobs.h): the panel says Not available in place of its slots,
    // its slot count and its sort button
    const bool hidden = w.has<Mob>(unit) && !w.has<Corpse>(unit);
    if (hidden) {
        const std::string na = lang("UILABEL_MOB_INVENTORY_HIDDEN");
        drawListText(na, L.gridPanel.x + (L.gridPanel.w - listTextWidth(na)) * 0.5f, L.gridPanel.y + (L.gridPanel.h - listH) * 0.5f, theme::TEXT_DIM);
    } else {
        // The slot count sits left of the sort button, which is at the right of the heading
        const std::string used = std::to_string(inv ? occupiedSlots(*inv) : 0) + " / " + std::to_string(INVENTORY_SLOTS) + " slots";
        drawListText(used, L.sortButton.x - 8.0f * s - listTextWidth(used), L.gridPanel.y + 6.0f * s, theme::TEXT_DIM);
        drawSmallButton(L.sortButton, lang("UILABEL_SORT"), editable, editable && L.sortButton.contains(mx, my));
    }

    for (size_t i = 0; !hidden && inv && i < L.slots.size() && i < INVENTORY_SLOTS; ++i) {
        const Rect& r = L.slots[i];
        drawRect(Rect{ r.x - 1.0f, r.y - 1.0f, r.w + 2.0f, r.h + 2.0f }, RGBA{ 0.48f, 0.24f, 0.27f, bgAlpha });
        drawRect(r, RGBA{ 0.16f, 0.07f, 0.085f, bgAlpha });

        const ItemStack& st = inv->slots[i];
        if (st.item == 0 || st.count <= 0) continue;

        // An item shows its picture, dimmed while it is being dragged. The count sits in the corner.
        drawItemCell(r, st.item, st.count, (int)i == dimSlot ? 0.30f : 1.0f);
        if (r.contains(mx, my)) hoverSlot = (int)i;
    }

    }
    // The behavior list
    if (L.tab == UnitTab::Behavior) {
        const Behaviors* beh = w.get<Behaviors>(unit);
        const Equipment* worn = w.get<Equipment>(unit);
        const std::array<bool, 4> modes{ beh && beh->passive, beh && beh->block, beh && beh->sneak,
                                         worn && isRangedWeapon(worn->items[GearSlot::MainHand]) };
        drawBehaviorSection(L, beh ? beh->rules : std::vector<BehaviorRule>(), taskNameOf(w, unit), mx, my, editable, modes);
    }
    if (L.tab == UnitTab::Stats) hoverStat = drawStatsTab(L, stats, mx, my, w.has<Mob>(unit));
    drawSidebarScrollBar(L);
    endClip();
    frame();

    // Tooltips and highlights are for hovering, not while something is being dragged
    if (dragItem == 0 && hoverStat >= 0) drawStatTip(stats, (StatId)hoverStat, mx, my);
    if (dragItem == 0 && hoverSlot >= 0 && (int)hoverSlot != dimSlot) {
        const Rect& r = L.slots[hoverSlot];
        drawRect(r, RGBA{ 1.0f, 1.0f, 1.0f, 0.18f });
        drawItemTooltip(inv->slots[hoverSlot].item, inv->slots[hoverSlot].count, mx, my, worn);
    }
    if (dragItem == 0 && hoverGear >= 0 && hoverGear != dimGear && worn && worn->items[hoverGear] != 0) {
        const Rect& r = L.gear[hoverGear];
        drawRect(r, RGBA{ 1.0f, 1.0f, 1.0f, 0.18f });
        drawItemTooltip(worn->items[hoverGear], 1, mx, my);
    }
}

// The Build tab's body. The heading says what is picked; each row has a select button (which picks the kind, or puts it
// down if it is picked already) and the prop's name. Row 0 is Destroy, the tool that takes buildings down.
void Renderer::drawBuildSection(const SidebarLayout& L, float mx, float my) {
    const float s = uiScale();
    auto pickName = [](const BuildPick& pick) {
        return pick.placing() ? lang(propInfo(pick.kind).nameKey) : lang("UILABEL_BUILD_DESTROY");
    };
    const std::string head = buildSelected_.any()
        ? langf("UILABEL_BUILD_SELECTED", { pickName(buildSelected_) })
        : lang("UILABEL_BUILD_NONE");
    const float headTextH = listTextHeight("Ag");
    drawListText(head, L.buildHeading.x + 4.0f * s, L.buildHeading.y + (L.buildHeading.h - headTextH) * 0.5f,
                 buildSelected_.any() ? theme::GOLD : theme::TEXT_DIM);
    const float btnW = 64.0f * s, gap = 8.0f * s;
    for (size_t i = 0; i < L.buildRows.size(); ++i) {
        const Rect& row = L.buildRows[i];
        const BuildPick pick = buildRowPick((int)i);
        if (!pick.any()) continue;
        const bool usable = buildInClaim_ || buildAnywhere(pick);
        const bool picked = pick == buildSelected_;
        const bool hovered = usable && row.contains(mx, my);
        if (hovered) drawRect(row, RGBA{ 1.0f, 1.0f, 1.0f, 0.06f });
        const Rect btn{ row.x + 2.0f * s, row.y + 2.0f * s, btnW, row.h - 4.0f * s };
        drawSmallButton(btn, lang(picked ? "UILABEL_BUILD_PICKED" : "UILABEL_BUILD_SELECT"), usable, hovered);
        const float th = listTextHeight("Ag");
        const RGBA tone = !usable ? theme::alpha(theme::ASH, 0.5f) : picked ? theme::GOLD : WHITE;
        drawListText(pickName(pick), btn.x + btnW + gap, row.y + (row.h - th) * 0.5f, tone);
    }
}

void Renderer::drawBehaviorSection(const SidebarLayout& L, const std::vector<BehaviorRule>& rules, const std::string& task,
                                   float mx, float my, bool editable, const std::array<bool, 4>& modes) {
    const float s = uiScale();
    const float bgAlpha = 0.5f;
    const float listH = listTextHeight("Ag");
    const Rect& P = L.behaviorPanel;

    drawRect(P, RGBA{ 0.10f, 0.04f, 0.05f, bgAlpha });
    drawIconText(P.x + 10.0f * s, P.y + 6.0f * s, listTextHeight("Ag"), ICON_BEHAVIOR, lang("UILABEL_BEHAVIOR"), WHITE);
    const std::string count = std::to_string(rules.size()) + " / " + std::to_string(BEHAVIOR_LIMIT);
    const float countX = P.x + P.w - 10.0f * s - listTextWidth(count);
    drawListText(count, countX, P.y + 6.0f * s, theme::TEXT_DIM);
    // The unit's task, such as Mining, centred between the heading and the count
    {
        const float left = P.x + 10.0f * s + listTextHeight("Ag") + 6.0f * s + listTextWidth(lang("UILABEL_BEHAVIOR")) + 12.0f * s;
        const float room = countX - 12.0f * s - left;
        const std::string full = langf("UILABEL_TASK", { task });
        const std::string shown = room > 0.0f ? fitText(full, room, false) : std::string();
        if (!shown.empty()) {
            const Rect area{ left, P.y + 6.0f * s, room, listTextHeight("Ag") };
            if (shown != full) noteTip(full, area);
            drawListText(shown, left + (room - listTextWidth(shown)) * 0.5f, P.y + 6.0f * s, RGBA{ 1.0f, 0.86f, 0.55f, 1.0f });
        }
    }

    // The modes: a lit button for each one that is on. Ranged follows the weapon, so it is never pressed. Each tooltip
    // says what its mode does.
    {
        static const char* const names[4] = { "UILABEL_MODE_PASSIVE", "UILABEL_MODE_BLOCK", "UILABEL_MODE_SNEAK", "UILABEL_MODE_RANGED" };
        static const char* const tips[4] = { "UILABEL_MODE_PASSIVE_TIP", "UILABEL_MODE_BLOCK_TIP", "UILABEL_MODE_SNEAK_TIP", "UILABEL_MODE_RANGED_TIP" };
        for (int i = 0; i < 4; ++i) {
            const Rect& r = L.modes[(size_t)i];
            if (r.w <= 0.0f) continue;
            const bool pressable = editable && i < 3;
            const bool hot = pressable && r.contains(mx, my);
            if (hot) noteHover(r);
            const RGBA fill = modes[(size_t)i] ? theme::alpha(theme::GOLD_DEEP, 0.95f)
                                               : RGBA{ 0.16f, 0.07f, 0.085f, hot ? 1.0f : 0.85f };
            drawRect(r, fill);
            if (modes[(size_t)i]) drawRect(Rect{ r.x, r.y + r.h - 2.0f, r.w, 2.0f }, theme::GOLD);
            if (hot) drawRect(r, RGBA{ 1.0f, 1.0f, 1.0f, 0.08f });
            const std::string label = fitText(lang(names[i]), r.w - 6.0f * s, false);
            const bool dim = i == 3;
            const RGBA tint = modes[(size_t)i] ? WHITE : (dim ? RGBA{ 0.60f, 0.51f, 0.47f, 1.0f } : theme::TEXT_DIM);
            drawListText(label, r.x + (r.w - listTextWidth(label)) * 0.5f, r.y + (r.h - listH) * 0.5f, tint);
            noteTip(lang(tips[i]), r);
        }
    }

    const std::string heads[2] = { lang("UILABEL_BEHAVIOR_ON"), lang("UILABEL_BEHAVIOR") };
    for (size_t i = 0; i < 2; ++i) {
        const Rect& h = L.behaviorHeads[i];
        drawListText(fitText(heads[i], h.w, false), h.x, h.y + (h.h - listH) * 0.5f, theme::TEXT_DIM);
    }

    for (size_t i = 0; i < L.behaviorRows.size() && i < rules.size(); ++i) {
        const BehaviorRowLayout& R = L.behaviorRows[i];
        const BehaviorRule& r = rules[i];
        if (editable && R.row.contains(mx, my)) drawRect(R.row, RGBA{ 1.0f, 1.0f, 1.0f, 0.06f });
        // A behavior that is off reads gray, so the list shows at a glance which ones work
        const RGBA text = r.on ? WHITE : theme::TEXT_DIM;

        // The on box: a dark square, with a blue fill while the rule is on
        drawRect(Rect{ R.toggle.x - 1.0f, R.toggle.y - 1.0f, R.toggle.w + 2.0f, R.toggle.h + 2.0f },
                 RGBA{ 0.48f, 0.24f, 0.27f, bgAlpha });
        drawRect(R.toggle, RGBA{ 0.16f, 0.07f, 0.085f, bgAlpha });
        if (r.on) {
            const float inset = 3.0f * s;
            drawRect(Rect{ R.toggle.x + inset, R.toggle.y + inset,
                           R.toggle.w - 2.0f * inset, R.toggle.h - 2.0f * inset },
                     RGBA{ 0.35f, 0.62f, 0.95f, editable ? 1.0f : 0.5f });
        }

        // The behavior's name, fitted to its cell. A behavior waiting after a try that could not be done says so.
        drawRect(R.label, theme::alpha(theme::TAB_OFF, 0.90f));
        std::string label = behaviorName(r);
        if (r.on && r.cooldown > 0.0f) label += "  " + lang("UILABEL_BEHAVIOR_WAITING");
        drawListText(fitText(label, R.label.w - 8.0f * s, false), R.label.x + 4.0f * s, R.label.y + (R.label.h - listH) * 0.5f,
                     text);

        // Eat, Heal and Rest have a # button, which asks for the percent they act below
        if (hasPercent(r.kind)) {
            drawSmallButton(R.number, "#", editable, editable && R.number.contains(mx, my));
            if (editable) {
                const PercentRange range = percentRange(r.kind);
                noteTip(langf("UILABEL_BEHAVIOR_NUMBER_TIP", { std::to_string(range.lo), std::to_string(range.hi) }), R.number);
            }
        }
        // Any behavior can be deleted: Add Behavior puts it back
        const bool upOn = editable && i > 0;
        const bool downOn = editable && i + 1 < rules.size();
        const bool removeOn = editable;
        drawSmallButton(R.up, "^", upOn, upOn && R.up.contains(mx, my));
        drawSmallButton(R.down, "v", downOn, downOn && R.down.contains(mx, my));
        drawSmallButton(R.remove, "x", removeOn, removeOn && R.remove.contains(mx, my));

    }

    // + Add Behavior, lit while its list is open, then the list: a behavior the unit holds already reads gray and
    // says so, and picking it only switches it on
    if (L.behaviorAdd.w > 0.0f) {
        const bool full = rules.size() >= (size_t)BEHAVIOR_LIMIT;
        const bool open = !L.behaviorOptions.empty();
        const std::string label = (open ? "- " : "+ ") + lang("UILABEL_BEHAVIOR_ADD");
        drawSmallButton(L.behaviorAdd, label, editable, editable && L.behaviorAdd.contains(mx, my));
        if (open) drawRect(Rect{ L.behaviorAdd.x, L.behaviorAdd.y + L.behaviorAdd.h - 2.0f * s, L.behaviorAdd.w, 2.0f * s }, theme::GOLD);
        for (size_t i = 0; i < L.behaviorOptions.size() && i < L.behaviorOptionInfo.size(); ++i) {
            const Rect& o = L.behaviorOptions[i];
            const BehaviorOption& opt = L.behaviorOptionInfo[i];
            const bool usable = editable && (opt.have || !full);
            const bool hot = usable && o.contains(mx, my);
            if (hot) noteHover(o);
            drawRect(o, hot ? theme::alpha(theme::GOLD_DEEP, 0.95f) : theme::alpha(theme::TAB_OFF, 0.90f));
            drawRect(Rect{ o.x, o.y, 2.0f * s, o.h }, hot ? theme::GOLD : theme::alpha(theme::ASH, 0.45f));
            const std::string tag = opt.have ? lang("UILABEL_BEHAVIOR_HAVE") : (full ? lang("UILABEL_BEHAVIOR_FULL") : std::string());
            const float tagW = tag.empty() ? 0.0f : listTextWidth(tag) + 8.0f * s;
            drawListText(fitText(opt.label, o.w - 10.0f * s - tagW, false), o.x + 6.0f * s, o.y + (o.h - listH) * 0.5f,
                         opt.have || !usable ? theme::TEXT_DIM : WHITE);
            if (!tag.empty()) drawListText(tag, o.x + o.w - tagW, o.y + (o.h - listH) * 0.5f, theme::alpha(theme::ASH, 0.8f));
            noteTip(describeBehavior(opt.rule), o);
        }
    }
}

void Renderer::drawPercentPrompt(const PercentPromptLayout& P, const std::string& title, const std::string& typed,
                                 const std::string& range, const std::string& error, float mx, float my) {
    const float s = uiScale();
    const float listH = listTextHeight("Ag");
    // The rest of the window dims behind it, as it takes every click until it is answered
    drawScrim();
    drawRect(Rect{ P.box.x - 1.0f, P.box.y - 1.0f, P.box.w + 2.0f, P.box.h + 2.0f }, theme::alpha(theme::ASH, 0.6f));
    drawRect(P.box, theme::alpha(theme::BOX_FILL, 0.97f));
    drawRect(Rect{ P.box.x, P.box.y, P.box.w, 2.0f * s }, theme::GOLD);
    const std::string t = fitText(title, P.box.w - 20.0f * s, false);
    drawListText(t, P.box.x + (P.box.w - listTextWidth(t)) * 0.5f, P.titleY, WHITE);
    // The box: what is typed, a blinking caret, and the % sign
    const Rect& in = P.input;
    drawRect(Rect{ in.x - 1.0f, in.y - 1.0f, in.w + 2.0f, in.h + 2.0f }, theme::GOLD);
    drawRect(in, theme::WELL);
    std::string shown = typed;
    shown += (SDL_GetTicks() / 500) % 2 == 0 ? "|" : " ";
    shown += "%";
    drawListText(shown, in.x + (in.w - listTextWidth(shown)) * 0.5f, in.y + (in.h - listH) * 0.5f, WHITE);
    drawListText(range, P.box.x + (P.box.w - listTextWidth(range)) * 0.5f, P.rangeY, theme::TEXT_DIM);
    if (!error.empty()) {
        const std::string e = fitText(error, P.box.w - 20.0f * s, false);
        drawListText(e, P.box.x + (P.box.w - listTextWidth(e)) * 0.5f, P.errorY, RGBA{ 1.0f, 0.45f, 0.40f, 1.0f });
    }
    drawSmallButton(P.ok, lang("UILABEL_BEHAVIOR_PROMPT_SET"), true, P.ok.contains(mx, my));
    drawSmallButton(P.cancel, lang("UILABEL_CANCEL"), true, P.cancel.contains(mx, my));
}

void Renderer::drawBehaviorTip(const SidebarLayout& L, World& w, Entity unit, float mx, float my) {
    const BehaviorHit hit = behaviorHitAt(L, mx, my);
    const Behaviors* b = w.get<Behaviors>(unit);
    if (hit.row < 0 || !b || hit.row >= (int)b->rules.size()) return;
    const float s = uiScale();
    // What it does, then why it is waiting, when it is
    const BehaviorRule& r = b->rules[(size_t)hit.row];
    std::vector<TipRow> rows(1);
    rows[0].text = describeBehavior(r);
    const std::string why = r.on ? waitReason(r, b->seek != NULL_ENTITY ? nameOf(w, b->seek) : std::string()) : std::string();
    if (!why.empty()) {
        rows.push_back(TipRow{});
        rows.back().text = langf("UILABEL_BEHAVIOR_WAIT_TIP", { why });
    }
    const float width = tipWidth(rows), height = tipHeight(rows.size());
    // Beside the mouse, moved to the other side or up when the window has no room for it
    float x = mx + 14.0f * s, y = my + 14.0f * s;
    if (x + width > uiW_) x = mx - 14.0f * s - width;
    if (y + height > uiH_) y = uiH_ - height;
    drawTip(rows, std::max(0.0f, x), std::max(0.0f, y), width);
}
