#include "game_tables.h"
#include "party_ui.h"
#include "language.h"
#include <algorithm>
#include <cmath>
#include "inventory.h"
#include "items.h"
#include "mobs.h"
#include "stats.h"

std::string groupTitle(StatGroup g) {
    switch (g) {
    case StatGroup::Attributes: return lang("UILABEL_ATTRIBUTES");
    case StatGroup::Combat:     return lang("UILABEL_COMBAT");
    case StatGroup::Gathering:  return lang("UILABEL_RESOURCE_GATHERING");
    case StatGroup::Crafting:   return lang("UILABEL_CRAFTING");
    case StatGroup::Utility:    return lang("UILABEL_UTILITY");
    }
    return "";
}

namespace {
int pickedIndex(const Party& p, const PartyUi& ui) {
    return std::clamp(ui.selected, 0, std::max(0, (int)p.size() - 1));
}

}

const PartyMember& pickedMember(const Party& p, const PartyUi& ui) {
    static const PartyMember none{};
    return p.empty() ? none : p[pickedIndex(p, ui)];
}

PartyMember& pickedMember(Party& p, const PartyUi& ui) {
    static PartyMember none;
    if (p.empty()) {
        none = PartyMember{};
        return none;
    }
    return p[pickedIndex(p, ui)];
}

std::vector<DropEntry> dropdownEntries(const Party& p, const PartyUi& ui) {
    std::vector<DropEntry> out;
    switch (ui.dropKind) {
    case DropKind::None:
        break;
    case DropKind::Sprite:
        for (int i = 1; i <= UNIT_SPRITE_COUNT; ++i) out.push_back(DropEntry{ modelLabel(i), i });
        break;
    case DropKind::Body:
        for (int i = 0; i < 3; ++i) out.push_back(DropEntry{ lang(bodyTypeKey((BodyType)i)), i });
        break;
    case DropKind::Gear:
        out.push_back(DropEntry{ lang("UILABEL_EMPTY"), 0 });
        for (int id : gearChoices(pickedMember(p, ui), ui.dropSlot)) {
            out.push_back(DropEntry{ itemNameOf(id), id });
        }
        break;
    case DropKind::Item:
        out.push_back(DropEntry{ lang("UILABEL_EMPTY"), 0 });
        for (int id : allItemChoices()) out.push_back(DropEntry{ itemNameOf(id), id });
        break;
    case DropKind::MobKind:
        for (MobKind k : regularMobKinds()) out.push_back(DropEntry{ lang(mobInfo(k).nameKey), idx(k) });
        break;
    }
    return out;
}

int clampDropScroll(int scroll, int count) {
    if (count <= DROP_VISIBLE_ROWS) return 0;
    return std::clamp(scroll, 0, count - DROP_VISIBLE_ROWS);
}

int partyListVisible(int H) {
    const float s = uiScaleFor(H);
    const PartyPanels P = partyPanels(0, H);
    const float area = P.left.h - 12.0f * s - PARTY_HINT_H * s;
    return std::max(1, (int)std::floor((area + 10.0f * s) / (96.0f * s + 10.0f * s)));
}

int partyListMaxScroll(int count, int H) {
    return std::max(0, count - partyListVisible(H));
}

PartyListBar partyListBar(int W, int H, int count, int scroll) {
    PartyListBar b;
    const int maxScroll = partyListMaxScroll(count, H);
    if (maxScroll <= 0) return b;
    const float s = uiScaleFor(H);
    const PartyPanels P = partyPanels(W, H);
    const float top = P.left.y + 12.0f * s, bottom = P.left.y + P.left.h - (PARTY_HINT_H + 4.0f) * s;
    b.track = Rect{ P.left.x + P.left.w - 14.0f * s, top, 8.0f * s, std::max(0.0f, bottom - top) };
    b.thumb = scrollThumb(b.track, partyListVisible(H) / (float)count, 24.0f * s,
                          std::clamp(scroll, 0, maxScroll) / (float)maxScroll);
    return b;
}

int partyListScrollAt(int W, int H, int count, float thumbTop) {
    const int maxScroll = partyListMaxScroll(count, H);
    const PartyListBar b = partyListBar(W, H, count, 0);
    if (maxScroll <= 0) return 0;
    return std::clamp((int)std::lround(scrollFracAt(b.track, b.thumb, thumbTop) * maxScroll), 0, maxScroll);
}

PartyPanels partyPanels(int W, int H) {
    const float s = uiScaleFor(H);
    const float m = 16.0f * s, topH = 64.0f * s, bottomH = 72.0f * s;
    const float bodyY = topH, bodyH = H - topH - bottomH - m;
    const float leftW = 250.0f * s, rightW = 470.0f * s;

    PartyPanels P;
    P.topH = topH;
    P.left = Rect{ m, bodyY, leftW, bodyH };
    P.right = Rect{ W - m - rightW, bodyY, rightW, bodyH };
    const float cx = P.left.x + P.left.w + m;
    P.center = Rect{ cx, bodyY, P.right.x - m - cx, bodyH };
    P.bottom = Rect{ 0.0f, H - bottomH, (float)W, bottomH };
    return P;
}

Rect partyPreviewRect(int W, int H) {
    const float s = uiScaleFor(H);
    const Rect c = partyPanels(W, H).center;
    return Rect{ c.x + 20.0f * s, c.y + 20.0f * s, c.w - 40.0f * s, c.h - 130.0f * s };
}

std::string partyMessage(const Party& p) {
    if (p.empty()) return std::string(lang("UILABEL_NEEDS_UNITS"));
    return partyNamesValid(p) ? std::string(lang("UILABEL_CONFIRM_STARTS"))
                              : std::string(lang("UILABEL_NEEDS_NAMES"));
}

std::vector<Button> layoutPartySetup(int W, int H, const Party& p, const PartyUi& ui,
                                     const std::vector<DropEntry>& drop) {
    const float s = uiScaleFor(H);
    const PartyPanels P = partyPanels(W, H);
    const PartyMember& m = pickedMember(p, ui);
    std::vector<Button> out;
    std::vector<Button> dropGroup;
    Rect sprAnchor, gearAnchor, invAnchor;
    Rect bodyAnchor;
    Rect mobAnchor, addMobAnchor;
    const bool mob = !p.empty() && m.mob != MobKind::None;

    const float cardH = 96.0f * s, cardGap = 10.0f * s;
    const int count = (int)p.size();
    const int visible = partyListVisible(H);
    const int first = std::clamp(ui.listScroll, 0, partyListMaxScroll(count, H));
    const float listTop = P.left.y + 12.0f * s;
    const float cardW = P.left.w - 20.0f * s - (partyListMaxScroll(count, H) > 0 ? 14.0f * s : 0.0f);
    for (int i = first; i < count && i < first + visible; ++i) {
        const float cy = listTop + (i - first) * (cardH + cardGap);
        out.push_back(Button{ "", Rect{ P.left.x + 10.0f * s, cy, cardW, cardH },
                              Action::PartyPick, true, i });
    }
    const float hintH = PARTY_HINT_H - 8.0f;
    std::string hints = lang("UILABEL_CLICK_UNIT_EDIT");
    if (ui.editable) hints += "\n" + lang("UILABEL_SHIFT_CLICK");
    out.push_back(Button{ hints, Rect{ P.left.x + 10.0f * s, P.left.y + P.left.h - (hintH + 4.0f) * s, P.left.w - 20.0f * s, hintH * s },
                          Action::PartyNote, false, 0 });

    if (!p.empty()) {
    const float inX = P.right.x + 14.0f * s, inW = P.right.w - 28.0f * s;
    const float tabH = 34.0f * s, tabGap = 6.0f * s;
    const float tabW = (inW - (PARTY_TAB_COUNT - 1) * tabGap) / PARTY_TAB_COUNT;
    const std::string tabNames[PARTY_TAB_COUNT] = { lang("UILABEL_IDENTITY"), lang("UILABEL_STATS"), lang("UILABEL_GEAR"), lang("UILABEL_ITEMS") };
    const float tabY = P.right.y + 12.0f * s;
    for (int t = 0; t < PARTY_TAB_COUNT; ++t) {
        out.push_back(Button{ tabNames[t], Rect{ inX + t * (tabW + tabGap), tabY, tabW, tabH },
                              Action::PartyTabPick, true, t });
    }
    const float top = tabY + tabH + 22.0f * s;
    const int sel = pickedIndex(p, ui);

    switch (ui.tab) {
    case PartyTab::Identity: {
        const Rect nameBox{ inX, top + 24.0f * s, inW, 40.0f * s };
        out.push_back(Button{ m.name, nameBox, Action::PartyNameField, true, sel });

        if (mob) {
            const Rect kindBox{ inX, top + 108.0f * s, 220.0f * s, 40.0f * s };
            mobAnchor = kindBox;
            out.push_back(Button{ lang(mobInfo(m.mob).nameKey) + (ui.mobKindEditable ? "   v" : ""), kindBox,
                                  Action::PartyMobBox, ui.mobKindEditable, sel });
            out.push_back(Button{ lang("UILABEL_RANDOMIZE_UNIT"), Rect{ inX, kindBox.y + kindBox.h + 26.0f * s, 240.0f * s, 36.0f * s },
                                  Action::PartyRandomUnit, true, sel });
            break;
        }
        const Rect sprBox{ inX, top + 108.0f * s, 220.0f * s, 40.0f * s };
        sprAnchor = sprBox;
        out.push_back(Button{ modelLabel(m.sprite) + "   v", sprBox, Action::PartySpriteBox, true, sel });

        const Rect bodyBox{ inX, sprBox.y + sprBox.h + 26.0f * s, 220.0f * s, 40.0f * s };
        bodyAnchor = bodyBox;
        out.push_back(Button{ lang(bodyTypeKey(m.body)) + "   v", bodyBox, Action::PartyBodyBox, true, sel });

        out.push_back(Button{ lang("UILABEL_RANDOMIZE_UNIT"), Rect{ inX, bodyBox.y + bodyBox.h + 26.0f * s, 240.0f * s, 36.0f * s },
                              Action::PartyRandomUnit, true, sel });
        break;
    }

    case PartyTab::Stats: {
        const float colGap = 16.0f * s, colW = (inW - colGap) / 2.0f;
        const float rowH = 20.0f * s, pitch = 22.0f * s, groupGap = 6.0f * s, btn = 26.0f * s;
        auto column = [&](float x, const std::vector<StatGroup>& groups) {
            float ry = top;
            for (StatGroup g : groups) {
                out.push_back(Button{ groupTitle(g), Rect{ x, ry, colW, rowH }, Action::PartyHeading, false, 0 });
                ry += pitch;
                for (int i = 0; i < STAT_COUNT; ++i) {
                    if (statInfo((StatId)i).group != g) continue;
                    const float labelW = ui.editable ? colW - 2.0f * (btn + 4.0f * s) : colW;
                    out.push_back(Button{ statName((StatId)i), Rect{ x, ry, labelW, rowH }, Action::PartyStatLabel, false, i });
                    if (ui.editable) {
                        out.push_back(Button{ "-", Rect{ x + colW - 2.0f * btn - 4.0f * s, ry, btn, rowH },
                                              Action::PartyStep, true, i * 2 });
                        out.push_back(Button{ "+", Rect{ x + colW - btn, ry, btn, rowH },
                                              Action::PartyStep, true, i * 2 + 1 });
                    }
                    ry += pitch;
                }
                ry += groupGap;
            }
            return ry;
        };
        column(inX, { StatGroup::Attributes, StatGroup::Combat });
        if (!mob) column(inX + colW + colGap, { StatGroup::Gathering, StatGroup::Crafting, StatGroup::Utility });
        out.push_back(Button{ lang(ui.editable ? "UILABEL_LEVELS_HINT" : "UILABEL_CUSTOM_ONLY"),
                              Rect{ inX, P.right.y + P.right.h - 82.0f * s, inW, 64.0f * s }, Action::PartyNote, false, 0 });
        break;
    }

    case PartyTab::Gear: {
        if (mob) {
            out.push_back(Button{ lang("UILABEL_MOB_NO_GEAR"), Rect{ inX, top, inW, 64.0f * s }, Action::PartyNote, false, 0 });
            break;
        }
        const float gap = 8.0f * s, tileW = (inW - gap) / 2.0f, tileH = std::max(54.0f * s, ITEM_CELL_PX + 8.0f * s);
        const float pitch = tileH + gap;
        for (int g = 0; g < GEAR_SLOTS; ++g) {
            const int col = g % 2, row = g / 2;
            const Rect r{ inX + col * (tileW + gap), top + row * pitch, tileW, tileH };
            if (ui.dropKind == DropKind::Gear && ui.dropSlot == g) gearAnchor = r;
            out.push_back(Button{ m.gear[g] != 0 ? itemNameOf(m.gear[g]) : std::string(lang("UILABEL_EMPTY")), r,
                                  Action::PartyGearSlot, ui.editable, g });
        }
        const Equipment worn = wornBy(m);
        int attack = 0, defense = 0;
        gearTotals(worn, attack, defense);
        const float noteY = top + 4.0f * pitch + 2.0f * s;
        out.push_back(Button{ langf("UILABEL_ATTACK_DEFENSE", { std::to_string(attack), std::to_string(defense) }),
                              Rect{ inX, noteY, inW, 22.0f * s }, Action::PartyNote, false, 0 });
        out.push_back(Button{ lang(ui.editable ? "UILABEL_CLICK_SLOT" : "UILABEL_CUSTOM_ONLY"),
                              Rect{ inX, noteY + 26.0f * s, inW, 64.0f * s }, Action::PartyNote, false, 0 });
        break;
    }

    case PartyTab::Items: {
        if (mob) {
            out.push_back(Button{ lang("UILABEL_MOB_NO_ITEMS"), Rect{ inX, top, inW, 64.0f * s }, Action::PartyNote, false, 0 });
            break;
        }
        const float gap = 4.0f * s;
        const float cell = ITEM_CELL_PX;
        for (int i = 0; i < INVENTORY_SLOTS; ++i) {
            const int col = i % INVENTORY_COLUMNS, row = i / INVENTORY_COLUMNS;
            const Rect r{ inX + col * (cell + gap), top + row * (cell + gap), cell, cell };
            if (ui.dropKind == DropKind::Item && ui.dropSlot == i) invAnchor = r;
            out.push_back(Button{ m.items[i].item > 0 ? itemNameOf(m.items[i].item) : std::string(), r,
                                  Action::PartyInvSlot, true, i });
        }

        const int slot = std::clamp(ui.stackSlot, 0, INVENTORY_SLOTS - 1);
        const ItemStack& st = m.items[slot];
        const float infoY = top + INVENTORY_SLOTS / INVENTORY_COLUMNS * (cell + gap) + 6.0f * s;
        const std::string info = st.item > 0
            ? langf("UILABEL_SLOT_N_ITEM", { std::to_string(slot + 1), itemNameOf(st.item) })
            : langf("UILABEL_SLOT_N_EMPTY", { std::to_string(slot + 1) });
        out.push_back(Button{ info, Rect{ inX, infoY, inW, 22.0f * s }, Action::PartyNote, false, 0 });

        const float countY = infoY + 30.0f * s, btnW = 40.0f * s, labelW = 120.0f * s;
        const bool hasItem = st.item > 0;
        if (ui.editable) {
            out.push_back(Button{ "-", Rect{ inX, countY, btnW, 32.0f * s }, Action::PartyCount,
                                  hasItem && st.count > 1, -1 });
            out.push_back(Button{ langf("UILABEL_COUNT", { std::to_string(st.count) }),
                                  Rect{ inX + btnW + 8.0f * s, countY, labelW, 32.0f * s }, Action::PartyNote, false, 0 });
            out.push_back(Button{ "+", Rect{ inX + btnW + labelW + 16.0f * s, countY, btnW, 32.0f * s }, Action::PartyCount,
                                  hasItem && st.count < stackLimit(st.item), 1 });
        } else if (hasItem) {
            out.push_back(Button{ langf("UILABEL_COUNT", { std::to_string(st.count) }),
                                  Rect{ inX, countY, inW, 32.0f * s }, Action::PartyNote, false, 0 });
        }
        out.push_back(Button{ lang(ui.editable ? "UILABEL_CLICK_SLOT_STACK" : "UILABEL_CUSTOM_ONLY"),
                              Rect{ inX, countY + 44.0f * s, inW, 64.0f * s }, Action::PartyNote, false, 0 });
        break;
    }
    }
    }

    const float bY = P.bottom.y + 32.0f * s, bH = 36.0f * s, bW = 200.0f * s, bGap = 12.0f * s;
    out.push_back(Button{ lang("UILABEL_RANDOMIZE_ALL"), Rect{ P.bottom.x + 16.0f * s, bY, bW, bH }, Action::PartyRandomAll, true, 0 });
    out.push_back(Button{ lang("UILABEL_BACK"), Rect{ P.bottom.x + 16.0f * s + bW + bGap, bY, 160.0f * s, bH }, Action::Back, true, 0 });
    if (ui.resizable) {
        const float unitsX = P.bottom.x + 16.0f * s + bW + bGap + 160.0f * s + bGap, unitW = 150.0f * s;
        out.push_back(Button{ lang("UILABEL_ADD_UNIT"), Rect{ unitsX, bY, unitW, bH }, Action::PartyAddUnit,
                              (int)p.size() < PARTY_MAX_UNITS, 0 });
        out.push_back(Button{ lang("UILABEL_REMOVE_UNIT"), Rect{ unitsX + unitW + bGap, bY, unitW, bH }, Action::PartyRemoveUnit,
                              !p.empty(), 0 });
        addMobAnchor = Rect{ unitsX + 2.0f * (unitW + bGap), bY, unitW, bH };
        out.push_back(Button{ lang("UILABEL_ADD_MOB"), addMobAnchor, Action::PartyAddMob, (int)p.size() < PARTY_MAX_UNITS, 0 });
    }
    out.push_back(Button{ lang("UILABEL_CONFIRM"), Rect{ P.bottom.x + P.bottom.w - 16.0f * s - bW, bY, bW, bH },
                          Action::PartyConfirm, !p.empty() && partyNamesValid(p), 0 });
    out.push_back(Button{ partyMessage(p), Rect{ P.bottom.x + 16.0f * s, P.bottom.y + 6.0f * s, P.bottom.w - 32.0f * s, 20.0f * s },
                          Action::PartyMessage, false, 0 });

    if (ui.dropKind != DropKind::None) {
        const Rect anchor = ui.dropKind == DropKind::Sprite ? sprAnchor
                          : ui.dropKind == DropKind::Body ? bodyAnchor
                          : ui.dropKind == DropKind::MobKind ? (ui.dropSlot < 0 ? addMobAnchor : mobAnchor)
                          : ui.dropKind == DropKind::Gear ? gearAnchor : invAnchor;
        const int total = (int)drop.size();
        const int firstRow = clampDropScroll(ui.dropScroll, total);
        const int shown = std::min(DROP_VISIBLE_ROWS, total - firstRow);
        const bool scrollable = total > DROP_VISIBLE_ROWS;
        const float rowH = 26.0f * s, scrollH = 22.0f * s, pad = 4.0f * s;
        const float listW = std::min(std::max(anchor.w, 240.0f * s), W - 32.0f * s);
        float listX = anchor.x;
        if (listX + listW > W - 16.0f * s) listX = W - 16.0f * s - listW;
        if (listX < 16.0f * s) listX = 16.0f * s;
        const float listH = 2.0f * pad + (scrollable ? 2.0f * scrollH : 0.0f) + shown * rowH;
        float listY = anchor.y + anchor.h + 4.0f * s;
        if (listY + listH > P.bottom.y - 4.0f * s) listY = anchor.y - 4.0f * s - listH;
        if (listY < P.topH) listY = P.topH;

        dropGroup.push_back(Button{ "", Rect{ listX, listY, listW, listH }, Action::PartyDropBack, true, 0 });
        float ry = listY + pad;
        if (scrollable) {
            dropGroup.push_back(Button{ lang("UILABEL_SCROLL_UP"), Rect{ listX, ry, listW, scrollH }, Action::PartyDropScroll, firstRow > 0, -1 });
            ry += scrollH;
        }
        int current = 0;
        if (ui.dropKind == DropKind::Sprite) current = m.sprite;
        if (ui.dropKind == DropKind::Body) current = idx(m.body);
        if (ui.dropKind == DropKind::Gear) current = m.gear[std::clamp(ui.dropSlot, 0, GEAR_SLOTS - 1)];
        if (ui.dropKind == DropKind::Item) current = m.items[std::clamp(ui.dropSlot, 0, INVENTORY_SLOTS - 1)].item;
        if (ui.dropKind == DropKind::MobKind) current = ui.dropSlot < 0 ? -1 : idx(m.mob);
        for (int i = firstRow; i < firstRow + shown; ++i) {
            const bool isCurrent = drop[i].value == current;
            dropGroup.push_back(Button{ (isCurrent ? "> " : "  ") + drop[i].label, Rect{ listX, ry, listW, rowH },
                                        Action::PartyDropPick, true, i });
            ry += rowH;
        }
        if (scrollable) {
            dropGroup.push_back(Button{ lang("UILABEL_SCROLL_DOWN"), Rect{ listX, ry, listW, scrollH }, Action::PartyDropScroll,
                                        firstRow + shown < total, 1 });
        }
    }
    out.insert(out.end(), dropGroup.begin(), dropGroup.end());
    return out;
}
