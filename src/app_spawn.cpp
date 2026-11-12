#include "app.h"
#include "build.h"
#include "level_file.h"
#include "recipes.h"
#include "spawn_cluster.h"
#include "systems_internal.h"
#include "useitem.h"

using namespace play;

// The spawn list: units, mobs, props and items, picked and placed with a left click. The level editor and the Build
// tab's Developer panel both draw and run it through these, so a change here shows in both.

SpawnPickerLayout layoutSpawnPicker(const Rect& area, float s) {
    const float gap = 6.0f * s, btnH = 32.0f * s, rowH = 26.0f * s, rowGap = 2.0f * s;
    SpawnPickerLayout L;
    float y = area.y;
    const float third = (area.w - 2.0f * gap) / 3.0f;
    for (int i = 0; i < 3; ++i) L.tabs[i] = Rect{ area.x + i * (third + gap), y, third, btnH };
    y += btnH + gap;
    const float small = 30.0f * s, box = 46.0f * s;
    // Faction: the label, then < name >
    const float labelW = std::min(area.w * 0.3f, 80.0f * s);
    L.factionLabel = Rect{ area.x, y, labelW, btnH };
    L.factionDown = Rect{ area.x + labelW + gap, y, small, btnH };
    L.factionUp = Rect{ area.x + area.w - small, y, small, btnH };
    L.factionBox = Rect{ L.factionDown.x + small + gap, y, L.factionUp.x - gap - (L.factionDown.x + small + gap), btnH };
    {
        // Stack: the label, then - N +, 1 and Max
        const float word = 44.0f * s;
        L.stackMax = Rect{ area.x + area.w - word, y, word, btnH };
        L.stackOne = Rect{ L.stackMax.x - gap - small, y, small, btnH };
        L.stackUp = Rect{ L.stackOne.x - gap - small, y, small, btnH };
        L.stackBox = Rect{ L.stackUp.x - gap - box, y, box, btnH };
        L.stackDown = Rect{ L.stackBox.x - gap - small, y, small, btnH };
        L.stackLabel = Rect{ area.x, y, std::max(0.0f, L.stackDown.x - gap - area.x), btnH };
    }
    y += btnH + gap;
    // Cluster of N: the label, then - N +
    L.clusterUp = Rect{ area.x + area.w - small, y, small, btnH };
    L.clusterBox = Rect{ L.clusterUp.x - gap - box, y, box, btnH };
    L.clusterDown = Rect{ L.clusterBox.x - gap - small, y, small, btnH };
    L.clusterLabel = Rect{ area.x, y, L.clusterDown.x - gap - area.x, btnH };
    y += btnH + gap;
    L.listArea = Rect{ area.x, y, area.w, std::max(0.0f, area.y + area.h - y) };
    const int shown = std::max(1, (int)(L.listArea.h / (rowH + rowGap)));
    for (int i = 0; i < shown; ++i) L.rows.push_back(Rect{ area.x, y + i * (rowH + rowGap), area.w, rowH });
    return L;
}

DeveloperLayout layoutDeveloper(int W, int H, EditorTab tab) {
    const bool themeTab = tab == EditorTab::Theme, killTab = tab == EditorTab::Kill, destroyTab = tab == EditorTab::Destroy,
               sweepTab = killTab || destroyTab || tab == EditorTab::Clean;
    const float s = uiScaleFor((float)H);
    const float m = 10.0f * s, pad = 10.0f * s, line = 28.0f * s, gap = 6.0f * s;
    DeveloperLayout L;
    const float w = 300.0f * s, h = std::min((float)H - 2.0f * m - 60.0f * s, 620.0f * s);
    L.panel = Rect{ m, std::floor((H - h) * 0.5f), w, h };
    L.title = Rect{ L.panel.x + pad, L.panel.y + pad, w - 2.0f * pad - line, line };
    L.close = Rect{ L.panel.x + w - pad - line, L.panel.y + pad, line, line };
    // Two rows of tabs: Spawn and Theme, then Kill, Clean and Destroy
    const float halfW = (w - 2.0f * pad - gap) * 0.5f, thirdW = (w - 2.0f * pad - 2.0f * gap) / 3.0f;
    L.spawnTab = Rect{ L.panel.x + pad, L.title.y + line + gap, halfW, line };
    L.themeTab = Rect{ L.spawnTab.x + halfW + gap, L.spawnTab.y, halfW, line };
    const float row2 = L.spawnTab.y + line + gap;
    L.killTab = Rect{ L.panel.x + pad, row2, thirdW, line };
    L.cleanTab = Rect{ L.killTab.x + thirdW + gap, row2, thirdW, line };
    L.destroyTab = Rect{ L.cleanTab.x + thirdW + gap, row2, thirdW, line };
    const float top = row2 + line + gap;
    // The bottom of the list: above the help text, or above the theme's status and buttons
    float listBottom = 0.0f;
    if (themeTab) {
        L.makePermanent = Rect{ L.panel.x + pad, L.panel.y + h - pad - line, w - 2.0f * pad, line };
        L.revert = Rect{ L.panel.x + pad, L.makePermanent.y - gap - line, w - 2.0f * pad, line };
        L.status = Rect{ L.panel.x + pad, L.revert.y - gap - line, w - 2.0f * pad, line };
        listBottom = L.status.y - gap;
    } else {
        const float helpH = 44.0f * s;
        L.help = Rect{ L.panel.x + pad, L.panel.y + h - pad - helpH, w - 2.0f * pad, helpH };
        listBottom = L.help.y - 6.0f * s;
        if (!sweepTab) {
            L.gather = Rect{ L.panel.x + pad, L.help.y - gap - line, w - 2.0f * pad, line };
            listBottom = L.gather.y - gap;
        }
    }
    L.themeArea = Rect{ L.panel.x + pad, top, w - 2.0f * pad, listBottom - top };
    L.picker = layoutSpawnPicker(L.themeArea, s);
    if (sweepTab) {
        // The mode: its label and box, the list dropping under the box. Kill and Destroy have their three toggles next. Then the radius
        // row, or the question for every one on the map.
        const Rect& a = L.themeArea;
        const float btnH = 32.0f * s, small = 30.0f * s, box = 46.0f * s;
        const float labelW = std::min(a.w * 0.3f, 80.0f * s);
        L.modeLabel = Rect{ a.x, a.y, labelW, btnH };
        L.modeBox = Rect{ a.x + labelW + gap, a.y, a.w - labelW - gap, btnH };
        for (int i = 0; i < SWEEP_MODE_COUNT; ++i) L.modeRows[i] = Rect{ L.modeBox.x, L.modeBox.y + (i + 1) * btnH, L.modeBox.w, btnH };
        float y = a.y + btnH + gap;
        if (killTab || destroyTab) {
            for (int i = 0; i < 3; ++i) {
                L.toggles[i] = Rect{ a.x, y, a.w, btnH };
                y += btnH + gap;
            }
        }
        L.radiusUp = Rect{ a.x + a.w - small, y, small, btnH };
        L.radiusBox = Rect{ L.radiusUp.x - gap - box, y, box, btnH };
        L.radiusDown = Rect{ L.radiusBox.x - gap - small, y, small, btnH };
        L.radiusLabel = Rect{ a.x, y, L.radiusDown.x - gap - a.x, btnH };
        L.sweepText = Rect{ a.x, y, a.w, 4.0f * line };
        L.sweepHold = Rect{ a.x, L.sweepText.y + L.sweepText.h + gap, a.w, btnH };
        L.sweepCancel = Rect{ a.x, L.sweepHold.y + btnH + gap, a.w, btnH };
    }
    return L;
}

namespace {
// The factions the spawn list's Faction row steps through: every faction but the mobs' own. Factionless is the wild:
// a Factionless mob is one of its own kind's.
std::vector<int> spawnFactions() {
    std::vector<int> out;
    for (const FactionDef& f : factions()) {
        if (!isMobFaction(f.id)) out.push_back(f.id);
    }
    return out;
}

std::string spawnFactionName(int faction) { return factionName(faction); }
}

std::vector<App::EditorEntry> App::spawnEntries(EditorTab tab) {
    std::vector<EditorEntry> out;
    switch (tab) {
    case EditorTab::Entities:
        // The kind of unit; what it belongs to is the Faction row's
        out.push_back(EditorEntry{ lang("UILABEL_EDITOR_UNITS") });
        out.push_back(EditorEntry{ lang("UILABEL_EDITOR_KOBOLD"), EditorTool::Unit, 0 });
        out.push_back(EditorEntry{ lang("UILABEL_EDITOR_MOBS") });
        for (int i = 1; i <= mobKindCount(); ++i) {
            if (isRegularMob((MobKind)i)) out.push_back(EditorEntry{ lang(mobInfo((MobKind)i).nameKey), EditorTool::Mob, i });
        }
        out.push_back(EditorEntry{ lang("UILABEL_EDITOR_BOSS_MOBS") });
        for (int i = 1; i <= mobKindCount(); ++i) {
            if (isBossMob((MobKind)i)) out.push_back(EditorEntry{ lang(mobInfo((MobKind)i).nameKey), EditorTool::Mob, i });
        }
        break;
    case EditorTab::Props:
        // Under the Build tab's headings
        for (int g = 0; g < (int)BuildGroup::Developer; ++g) {
            bool headed = false;
            for (PropKind k : propKindsListed()) {
                if (propInfo(k).group != (BuildGroup)g) continue;
                if (!headed) out.push_back(EditorEntry{ lang(BUILD_GROUP_LABELS[g]) });
                headed = true;
                out.push_back(EditorEntry{ lang(propInfo(k).nameKey), EditorTool::Prop, idx(k) });
            }
        }
        break;
    case EditorTab::Items: {
        // Under headings, each in the item table's order
        enum Group { Materials, Weapons, Ammo, Armor, Accessories, Food, Medicine, Bones, GroupCount };
        static const char* const GROUP_KEYS[GroupCount] = { "UILABEL_ITEMS_MATERIALS", "UILABEL_ITEMS_WEAPONS", "UILABEL_ITEMS_AMMO",
                                                            "UILABEL_ITEMS_ARMOR", "UILABEL_ITEMS_ACCESSORIES", "UILABEL_ITEMS_FOOD",
                                                            "UILABEL_ITEMS_MEDICINE", "UILABEL_ITEMS_BONES" };
        auto groupOf = [](const ItemDef& d) {
            if (d.typeId == "weapon") return Weapons;
            if (hasTag(d, "arrow") || hasTag(d, "bolt")) return Ammo;
            if (d.typeId == "armor") return Armor;
            if (d.typeId == "accessory") return Accessories;
            if (hasTag(d, "raw_food") || restoreAmount(d, UseEffect::Stat::Hunger) > 0) return Food;
            if (d.typeId == "consumable") return Medicine;
            if (hasTag(d, "bones")) return Bones;
            return Materials;
        };
        for (int g = 0; g < GroupCount; ++g) {
            bool headed = false;
            for (const ItemDef& d : itemDatabase()) {
                if (groupOf(d) != g) continue;
                if (!headed) out.push_back(EditorEntry{ lang(GROUP_KEYS[g]) });
                headed = true;
                out.push_back(EditorEntry{ itemDisplayName(d), EditorTool::Item, d.id });
            }
        }
        break;
    }
    case EditorTab::Theme:
        break;
    }
    return out;
}

namespace {
// The most of the picked item one stack holds (1 when no item is picked)
int pickedStackLimit(const SpawnPicker& p) {
    return p.tool == EditorTool::Item && findItem(p.picked) ? std::max(1, stackLimit(p.picked)) : 1;
}
}

void App::spawnPickerCommit(SpawnPicker& p, bool keep) {
    if (p.typing == SpawnPicker::Typing::None) return;
    if (keep && !p.typed.empty()) {
        const int n = std::atoi(p.typed.c_str());
        if (p.typing == SpawnPicker::Typing::Cluster) {
            p.cluster = std::clamp(n, 1, SPAWN_CLUSTER_MAX);
        } else {
            p.stack = std::clamp(n, 1, pickedStackLimit(p));
            p.stackMax = false;
        }
    }
    p.typing = SpawnPicker::Typing::None;
    p.typed.clear();
}

bool App::spawnPickerKey(SpawnPicker& p) {
    if (p.typing == SpawnPicker::Typing::None) return false;
    const SDL_Keycode k = e.key.key;
    int digit = -1;
    if (k >= SDLK_0 && k <= SDLK_9) digit = (int)(k - SDLK_0);
    else if (k >= SDLK_KP_1 && k <= SDLK_KP_9) digit = (int)(k - SDLK_KP_1) + 1;
    else if (k == SDLK_KP_0) digit = 0;
    if (digit >= 0) {
        if (p.typed.size() < 3) p.typed += std::to_string(digit);
    } else if (k == SDLK_BACKSPACE) {
        if (!p.typed.empty()) p.typed.pop_back();
    } else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
        spawnPickerCommit(p, true);
        playSfx(Sfx::UiClick);
    } else if (k == SDLK_ESCAPE) {
        spawnPickerCommit(p, false);
        playSfx(Sfx::UiBack);
    }
    return true;   // every key goes to the box while it is typed into
}

void App::drawSpawnPicker(SpawnPicker& p, const SpawnPickerLayout& L) {
    const float s = uiScaleFor((float)winH);
    const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f }, gold{ 1.0f, 0.78f, 0.30f, 1.0f };
    const float textH = renderer.listTextHeight("Ag");
    static const char* const TAB_KEYS[3] = { "UILABEL_EDITOR_ENTITIES", "UILABEL_EDITOR_PROPS", "UILABEL_EDITOR_ITEMS" };
    for (int i = 0; i < 3; ++i) {
        const bool on = p.tab == (EditorTab)i;
        renderer.drawButton(L.tabs[i], (on ? "> " : "") + lang(TAB_KEYS[i]), true, L.tabs[i].contains(mouseX, mouseY));
    }
    if (p.tab != EditorTab::Items) {
        renderer.drawListText(renderer.fitListText(lang("UILABEL_SPAWN_FACTION"), L.factionLabel.w), L.factionLabel.x,
                              L.factionLabel.y + (L.factionLabel.h - textH) * 0.5f, white);
        renderer.drawButton(L.factionDown, "<", true, L.factionDown.contains(mouseX, mouseY));
        renderer.drawRect(L.factionBox, RGBA{ 0.14f, 0.09f, 0.08f, 1.0f });
        {
            const FactionColor c = factionColor(p.faction);
            const float d = 10.0f * s;
            renderer.drawRect(Rect{ L.factionBox.x + 6.0f * s, L.factionBox.y + (L.factionBox.h - d) * 0.5f, d, d }, RGBA{ c.r, c.g, c.b, 1.0f });
        }
        const std::string name = renderer.fitListText(spawnFactionName(p.faction), L.factionBox.w - 26.0f * s);
        renderer.drawListText(name, L.factionBox.x + 20.0f * s, L.factionBox.y + (L.factionBox.h - textH) * 0.5f, gold);
        renderer.drawButton(L.factionUp, ">", true, L.factionUp.contains(mouseX, mouseY));
        if (L.factionBox.contains(mouseX, mouseY)) renderer.noteTip(lang("UILABEL_SPAWN_FACTION_TIP"), L.factionBox);
    }
    renderer.drawListText(renderer.fitListText(lang("UILABEL_SPAWN_CLUSTER"), L.clusterLabel.w), L.clusterLabel.x,
                          L.clusterLabel.y + (L.clusterLabel.h - textH) * 0.5f, white);
    // A count's box: click it and type a number, Enter to keep it
    auto countBox = [&](const Rect& r, int value, bool typing) {
        renderer.drawRect(r, typing ? RGBA{ 0.30f, 0.20f, 0.12f, 1.0f } : RGBA{ 0.14f, 0.09f, 0.08f, 1.0f });
        const std::string text = typing ? p.typed + "|" : std::to_string(value);
        renderer.drawListText(text, r.x + (r.w - renderer.listTextWidth(text)) * 0.5f, r.y + (r.h - textH) * 0.5f, gold);
        if (!typing && r.contains(mouseX, mouseY)) renderer.noteTip(lang("UILABEL_SPAWN_TYPE_TIP"), r);
    };
    if (p.tab == EditorTab::Items) {
        const int limit = pickedStackLimit(p);
        if (p.stackMax) p.stack = limit;
        p.stack = std::clamp(p.stack, 1, limit);
        renderer.drawListText(renderer.fitListText(langf("UILABEL_SPAWN_STACK", { std::to_string(limit) }), L.stackLabel.w), L.stackLabel.x,
                              L.stackLabel.y + (L.stackLabel.h - textH) * 0.5f, white);
        renderer.drawButton(L.stackDown, "-", p.stack > 1, L.stackDown.contains(mouseX, mouseY));
        countBox(L.stackBox, p.stack, p.typing == SpawnPicker::Typing::Stack);
        renderer.drawButton(L.stackUp, "+", p.stack < limit, L.stackUp.contains(mouseX, mouseY));
        renderer.drawButton(L.stackOne, std::string(!p.stackMax && p.stack == 1 ? ">1" : "1"), true, L.stackOne.contains(mouseX, mouseY));
        renderer.drawButton(L.stackMax, (p.stackMax ? ">" : "") + lang("UILABEL_SPAWN_STACK_MAX"), true, L.stackMax.contains(mouseX, mouseY));
        if (L.stackOne.contains(mouseX, mouseY)) renderer.noteTip(lang("UILABEL_SPAWN_STACK_ONE_TIP"), L.stackOne);
        if (L.stackMax.contains(mouseX, mouseY)) renderer.noteTip(lang("UILABEL_SPAWN_STACK_MAX_TIP"), L.stackMax);
    }
    renderer.drawButton(L.clusterDown, "-", p.cluster > 1, L.clusterDown.contains(mouseX, mouseY));
    countBox(L.clusterBox, p.cluster, p.typing == SpawnPicker::Typing::Cluster);
    renderer.drawButton(L.clusterUp, "+", p.cluster < SPAWN_CLUSTER_MAX, L.clusterUp.contains(mouseX, mouseY));
    if (p.tab == EditorTab::Theme) return;   // the level editor's theme panel takes the list's place
    const std::vector<EditorEntry> entries = spawnEntries(p.tab);
    p.listScroll = std::clamp(p.listScroll, 0, std::max(0, (int)entries.size() - (int)L.rows.size()));
    for (size_t i = 0; i < L.rows.size() && (size_t)p.listScroll + i < entries.size(); ++i) {
        const EditorEntry& entry = entries[(size_t)p.listScroll + i];
        const Rect& r = L.rows[i];
        if (entry.tool == EditorTool::None) {
            renderer.drawListText(renderer.fitListText(entry.label, r.w), r.x, r.y + (r.h - textH) * 0.5f, gold);
            continue;
        }
        const bool on = p.tool == entry.tool && p.picked == entry.value;
        const bool hot = r.contains(mouseX, mouseY);
        if (on || hot) renderer.drawRect(r, on ? RGBA{ 0.45f, 0.30f, 0.14f, 1.0f } : RGBA{ 0.28f, 0.18f, 0.12f, 1.0f });
        renderer.drawListText(renderer.fitListText(entry.label, r.w - 12.0f * s), r.x + 6.0f * s, r.y + (r.h - textH) * 0.5f, on ? gold : white);
    }
}

bool App::spawnPickerClick(SpawnPicker& p, const SpawnPickerLayout& L, float x, float y) {
    // A click anywhere keeps what was typed; one on a count's box starts typing into it
    spawnPickerCommit(p, true);
    if (L.clusterBox.contains(x, y) || (p.tab == EditorTab::Items && L.stackBox.contains(x, y))) {
        p.typing = L.clusterBox.contains(x, y) ? SpawnPicker::Typing::Cluster : SpawnPicker::Typing::Stack;
        p.typed.clear();
        playSfx(Sfx::UiClick);
        return false;
    }
    if (p.tab == EditorTab::Items) {
        const int limit = pickedStackLimit(p);
        const int by = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0 ? 5 : 1;
        if (L.stackDown.contains(x, y) || L.stackUp.contains(x, y)) {
            const int was = p.stack;
            p.stack = std::clamp(p.stack + (L.stackUp.contains(x, y) ? by : -by), 1, limit);
            p.stackMax = false;
            playSfx(p.stack != was ? Sfx::UiClick : Sfx::UiError);
            return false;
        }
        if (L.stackOne.contains(x, y) || L.stackMax.contains(x, y)) {
            p.stackMax = L.stackMax.contains(x, y);
            p.stack = p.stackMax ? limit : 1;
            playSfx(Sfx::UiClick);
            return false;
        }
    }
    for (int i = 0; i < 3; ++i) {
        if (!L.tabs[i].contains(x, y)) continue;
        p.tab = (EditorTab)i;
        p.listScroll = 0;
        p.tool = EditorTool::None;
        p.picked = 0;
        playSfx(Sfx::UiTab);
        return true;
    }
    if (p.tab != EditorTab::Items && (L.factionDown.contains(x, y) || L.factionUp.contains(x, y))) {
        const std::vector<int> list = spawnFactions();
        const auto at = std::find(list.begin(), list.end(), p.faction);
        const int i = at == list.end() ? 0 : (int)(at - list.begin());
        const int n = (int)list.size();
        p.faction = list[(size_t)(((i + (L.factionUp.contains(x, y) ? 1 : -1)) % n + n) % n)];
        playSfx(Sfx::UiClick);
        return true;
    }
    // Shift steps the count by 5
    const int step = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0 ? 5 : 1;
    if (L.clusterDown.contains(x, y) || L.clusterUp.contains(x, y)) {
        const int was = p.cluster;
        p.cluster = std::clamp(p.cluster + (L.clusterUp.contains(x, y) ? step : -step), 1, SPAWN_CLUSTER_MAX);
        playSfx(p.cluster != was ? Sfx::UiClick : Sfx::UiError);
        return false;
    }
    if (p.tab == EditorTab::Theme) return false;
    const std::vector<EditorEntry> entries = spawnEntries(p.tab);
    for (size_t i = 0; i < L.rows.size() && (size_t)p.listScroll + i < entries.size(); ++i) {
        if (!L.rows[i].contains(x, y)) continue;
        const EditorEntry& entry = entries[(size_t)p.listScroll + i];
        if (entry.tool == EditorTool::None) return false;
        p.tool = entry.tool;
        p.picked = entry.value;
        // The stack follows the item picked: a full one with Max on, else no more than this item's limit
        p.stack = p.stackMax ? pickedStackLimit(p) : std::clamp(p.stack, 1, pickedStackLimit(p));
        playSfx(Sfx::UiSelect);
        return true;
    }
    return false;
}

void App::spawnPickerWheel(SpawnPicker& p, const SpawnPickerLayout& L, float x, float y, float notches) {
    if (L.clusterBox.contains(x, y) || L.clusterDown.contains(x, y) || L.clusterUp.contains(x, y)) {
        p.cluster = std::clamp(p.cluster + (notches > 0.0f ? 1 : notches < 0.0f ? -1 : 0), 1, SPAWN_CLUSTER_MAX);
        return;
    }
    if (p.tab == EditorTab::Items && (L.stackBox.contains(x, y) || L.stackDown.contains(x, y) || L.stackUp.contains(x, y))) {
        p.stack = std::clamp(p.stack + (notches > 0.0f ? 1 : notches < 0.0f ? -1 : 0), 1, pickedStackLimit(p));
        p.stackMax = false;
        return;
    }
    p.listScroll = std::max(0, p.listScroll - (int)notches);
}

int App::spawnAt(SpawnPicker& p, float x, float z, bool levelIds, int storey) {
    spawnPickerCommit(p, true);
    if (p.tool != EditorTool::Unit && p.tool != EditorTool::Mob && p.tool != EditorTool::Prop && p.tool != EditorTool::Item) return 0;
    // Props kept a footprint apart, units and mobs a body apart, items a little
    float spacing = 0.6f;
    if (p.tool == EditorTool::Prop) spacing = std::max(1.0f, footprintReach(propInfo((PropKind)p.picked).foot) * 2.0f);
    else if (p.tool != EditorTool::Item) spacing = 1.0f;
    std::vector<Vec2> spots = clusterSpots(world, terrain, rng, x, z, p.cluster, spacing, p.tool == EditorTool::Prop);
    if (storey > 0) {
        // On an upper floor: only where there is floor of that level, and the point clicked itself when the ground search finds it
        // taken (it looks at the ground below)
        if (spots.empty()) spots.push_back(Vec2{ x, z });
        const BuildingMap& m = cachedBuildingMap(world);
        spots.erase(std::remove_if(spots.begin(), spots.end(), [&](const Vec2& s) {
            return !floorSurfaceOn(world, m, (int)std::floor(s.x), (int)std::floor(s.z), storey);
        }), spots.end());
    }
    int placed = 0;
    for (const Vec2& spot : spots) {
        LevelObject o;
        o.storey = storey;
        o.yaw = p.yaw;
        o.kind = p.picked;
        o.x = spot.x;
        o.z = spot.z;
        if (p.tool == EditorTool::Prop) {
            snapBuildSpot((PropKind)p.picked, spot.x, spot.z, o.x, o.z);
            o.type = LevelObject::Type::Prop;
            // A built prop belongs to the faction picked (a Factionless one to yours, as it needs an owner that can claim);
            // another prop only if a faction is picked
            const bool none = p.faction == FACTION_FACTIONLESS;
            if (propInfo((PropKind)p.picked).built) o.owner = none ? FACTION_PLAYER : p.faction;
            else if (!none && p.faction != FACTION_PLAYER) o.owner = p.faction;
        } else if (p.tool == EditorTool::Unit) {
            // A Kobold of the faction picked
            o.type = LevelObject::Type::Unit;
            o.kind = p.faction;
        } else if (p.tool == EditorTool::Mob) {
            // A mob of the faction picked; a Factionless one is wild, of its own kind's
            o.type = LevelObject::Type::Mob;
            o.owner = p.faction == FACTION_FACTIONLESS ? -1 : p.faction;
        } else {
            o.type = LevelObject::Type::Item;
            o.count = std::clamp(p.stackMax ? pickedStackLimit(p) : p.stack, 1, pickedStackLimit(p));
        }
        // The level editor numbers what it places, for its level file; a game has no use for the numbers
        o.id = levelIds ? world.resource<LevelInfo>().nextId++ : 0;
        if (placeLevelObject(world, terrain, rng, o) != NO_OBJECT) ++placed;
    }
    if (placed > 0) playSfx(Sfx::Drop);
    return placed;
}

// ---- The Build tab's Developer panel ----

void App::openDeveloper() {
    devSpawn.open = true;
    playSfx(Sfx::UiMenuOpen);
}

void App::closeDeveloper() {
    if (!devSpawn.open) return;
    devSpawn.open = false;
    devSpawn.tool = EditorTool::None;
    devSpawn.picked = 0;
    devSpawn.tab = EditorTab::Entities;
    devThemeConfirm = false;
    devKill = KillUi{};
    devClean = SweepUi{};
    devDestroy = DestroyUi{};
    playSfx(Sfx::UiBack);
}

bool App::pressOnDeveloper() {
    if (screen != Screen::Playing || editing || !devSpawn.open) return false;
    const float x = e.button.x, y = e.button.y;
    const bool themeTab = devSpawn.tab == EditorTab::Theme, killTab = devSpawn.tab == EditorTab::Kill, cleanTab = devSpawn.tab == EditorTab::Clean,
               destroyTab = devSpawn.tab == EditorTab::Destroy, sweepTab = killTab || cleanTab || destroyTab;
    const DeveloperLayout L = layoutDeveloper(winW, winH, devSpawn.tab);
    if (L.panel.contains(x, y)) {
        if (e.button.button != SDL_BUTTON_LEFT) return true;
        if (L.close.contains(x, y)) {
            closeDeveloper();
            return true;
        }
        if (L.spawnTab.contains(x, y) || L.themeTab.contains(x, y) || L.killTab.contains(x, y) || L.cleanTab.contains(x, y) ||
            L.destroyTab.contains(x, y)) {
            const bool toTheme = L.themeTab.contains(x, y), toKill = L.killTab.contains(x, y), toClean = L.cleanTab.contains(x, y),
                       toDestroy = L.destroyTab.contains(x, y);
            devSpawn.tool = EditorTool::None;
            devSpawn.picked = 0;
            devSpawn.tab = toTheme ? EditorTab::Theme : toKill ? EditorTab::Kill : toClean ? EditorTab::Clean
                         : toDestroy ? EditorTab::Destroy : EditorTab::Entities;
            devThemeConfirm = false;
            for (SweepUi* sw : { (SweepUi*)&devKill, &devClean, (SweepUi*)&devDestroy }) sw->dropOpen = sw->holding = false;
            if (toTheme) openDevTheme();
            playSfx(Sfx::UiTab);
            return true;
        }
        if (sweepTab) {
            sweepClick(L, x, y);
            return true;
        }
        if (themeTab) {
            if (L.makePermanent.contains(x, y)) devThemeMakePermanent();
            else if (L.revert.contains(x, y)) devThemeRevert();
            else if (L.themeArea.contains(x, y)) themeClick(L.themeArea, x, y);
            else devThemeConfirm = false;
            return true;
        }
        if (L.gather.contains(x, y)) {
            showNotice(langf("MISC_GATHERED", { std::to_string(gatherUnits()) }), 2.0f);
            return true;
        }
        spawnPickerClick(devSpawn, L.picker, x, y);
        return true;
    }
    if (sweepTab) {
        // A left click on the world kills, cleans or destroys what is under it, or around it, unless the mode is every one
        SweepUi& sw = sweepUi();
        if (e.button.button != SDL_BUTTON_LEFT || sw.mode == SweepMode::All) return false;
        sw.dropOpen = false;
        const int n = killTab ? killAt(x, y) : cleanTab ? cleanAt(x, y) : destroyAt(x, y);
        showNotice(langf(killTab ? "MISC_KILLED" : cleanTab ? "MISC_CLEANED" : "MISC_DESTROYED", { std::to_string(n) }), 2.0f);
        return true;
    }
    if (themeTab || devSpawn.tool == EditorTool::None) return false;
    // With something picked, a right-click puts it down again and a left-click places it
    if (e.button.button == SDL_BUTTON_RIGHT) {
        devSpawn.tool = EditorTool::None;
        playSfx(Sfx::UiBack);
        return true;
    }
    if (e.button.button != SDL_BUTTON_LEFT) return false;
    // On the floor of a building the pointer is over (the same pick as a click to move), so a prop is put on that floor, not the ground below
    int storey = 0;
    if (const auto hit = pickFloor(x, y, storey)) {
        const int placed = spawnAt(devSpawn, hit->first, hit->second, false, storey);
        if (placed > 0) showNotice(langf("MISC_SPAWNED", { std::to_string(placed) }), 2.0f);
    }
    return true;
}

// Every unit of yours to the ground floor, around the point the camera is looking at: for a unit lost upstairs or stuck
int App::gatherUnits() {
    std::vector<ObjectId> units;
    for (ObjectId u : world.view<Entity, Transform>()) {
        if (isUnit(world, u) && factionOf(world, u) == FACTION_PLAYER && !world.has<Corpse>(u) && !world.has<Carried>(u)) units.push_back(u);
    }
    if (units.empty()) return 0;
    std::sort(units.begin(), units.end());
    const std::vector<Vec2> spots = clusterSpots(world, terrain, rng, cam.focusX, cam.focusZ, (int)units.size(), 1.0f, false);
    for (size_t i = 0; i < units.size(); ++i) {
        const ObjectId u = units[i];
        // Whatever it was doing there is over (its squad is kept)
        if (Behaviors* b = world.get<Behaviors>(u)) systems_detail::stopAutoStep(world, u, *b);
        systems_detail::endPropTasks(world, u);
        world.remove<BuildJob>(u);
        world.remove<StallJob>(u);
        world.remove<CraftJob>(u);
        world.remove<Climbing>(u);
        world.remove<Route>(u);
        world.remove<SoloMove>(u);
        world.remove<IdleSpot>(u);
        world.remove<Upstairs>(u);
        const Vec2 at = i < spots.size() ? spots[i] : Vec2{ cam.focusX, cam.focusZ };
        Transform* tr = world.get<Transform>(u);
        tr->x = at.x;
        tr->z = at.z;
        tr->y = terrain.standHeight(at.x, at.z);
    }
    playSfx(Sfx::Drop);
    return (int)units.size();
}

void App::drawDeveloper() {
    if (screen != Screen::Playing || editing || !devSpawn.open) return;
    const bool themeTab = devSpawn.tab == EditorTab::Theme, killTab = devSpawn.tab == EditorTab::Kill, cleanTab = devSpawn.tab == EditorTab::Clean,
               destroyTab = devSpawn.tab == EditorTab::Destroy;
    const DeveloperLayout L = layoutDeveloper(winW, winH, devSpawn.tab);
    const RGBA gray{ 0.80f, 0.72f, 0.64f, 1.0f }, gold{ 1.0f, 0.78f, 0.30f, 1.0f };
    const float textH = renderer.listTextHeight("Ag");
    renderer.drawThemedPanel(L.panel);
    renderer.drawListText(renderer.fitListText(lang("UILABEL_BUILD_DEVELOPER"), L.title.w), L.title.x,
                          L.title.y + (L.title.h - textH) * 0.5f, gold);
    renderer.drawCloseButton(L.close);
    renderer.drawButton(L.spawnTab, (themeTab || killTab || cleanTab || destroyTab ? "" : "> ") + lang("UILABEL_DEV_TAB_SPAWN"), true, L.spawnTab.contains(mouseX, mouseY));
    renderer.drawButton(L.themeTab, (themeTab ? "> " : "") + lang("UILABEL_GEN_TAB_THEME"), true, L.themeTab.contains(mouseX, mouseY));
    renderer.drawButton(L.killTab, (killTab ? "> " : "") + lang("UILABEL_DEV_TAB_KILL"), true, L.killTab.contains(mouseX, mouseY));
    renderer.drawButton(L.cleanTab, (cleanTab ? "> " : "") + lang("UILABEL_DEV_TAB_CLEAN"), true, L.cleanTab.contains(mouseX, mouseY));
    renderer.drawButton(L.destroyTab, (destroyTab ? "> " : "") + lang("UILABEL_DEV_TAB_DESTROY"), true, L.destroyTab.contains(mouseX, mouseY));
    if (killTab || cleanTab || destroyTab) {
        drawSweepTab(L);
        return;
    }
    if (themeTab) {
        drawThemePanel(L.themeArea);
        const bool confirm = devThemeConfirm;
        renderer.drawListText(renderer.fitListText(lang(confirm ? "UILABEL_DEV_THEME_CONFIRM" : "UILABEL_DEV_THEME_TEMP"), L.status.w),
                              L.status.x, L.status.y + (L.status.h - textH) * 0.5f, confirm ? gold : gray);
        renderer.drawButton(L.revert, lang("UILABEL_DEV_REVERT"), true, L.revert.contains(mouseX, mouseY));
        renderer.drawButton(L.makePermanent, lang("UILABEL_DEV_MAKE_PERMANENT"), true, L.makePermanent.contains(mouseX, mouseY));
        return;
    }
    drawSpawnPicker(devSpawn, L.picker);
    renderer.drawButton(L.gather, lang("UILABEL_DEV_GATHER"), true, L.gather.contains(mouseX, mouseY));
    if (L.gather.contains(mouseX, mouseY)) renderer.noteTip(lang("UILABEL_DEV_GATHER_TIP"), L.gather);
    float y = L.help.y;
    for (const std::string& line : renderer.wrapListText(lang("UILABEL_DEVELOPER_HELP"), L.help.w)) {
        renderer.drawListText(line, L.help.x, y, gray);
        y += textH + 2.0f;
    }
}

// The Kill Entity and Clean tabs. Kill brings an entity down and then dead as a fight does, so it leaves a corpse and the
// game's own records of a death; the toggles then say what becomes of the corpse and what it held. Each counts what it did.
bool App::killEntity(ObjectId u) {
    if (!world.has<Entity>(u) || world.has<Corpse>(u)) return false;
    knockOut(world, u);
    finishDying(world, u);
    if (!world.has<Corpse>(u)) return false;
    queueSoundFrom(world, Sfx::Dies, u);
    const KillUi& k = devKill;
    const Transform at = *world.get<Transform>(u);
    const int storey = storeyOf(world, u);
    if (k.dropItems || k.deleteItems) {
        // What it held leaves the corpse, to the ground or to nothing. (The two toggles are never both on.)
        const std::vector<ItemStack> belongings = unitBelongings(world, u);
        if (Inventory* inv = world.get<Inventory>(u)) *inv = Inventory{};
        if (Equipment* worn = world.get<Equipment>(u)) *worn = Equipment{};
        if (k.dropItems) dropStacks(world, at, belongings, storey);
    }
    if (k.deleteCorpse) {
        // A corpse carried by someone is put down first, and one deleted with its things still on it takes them along
        if (const Carried* carried = world.get<Carried>(u)) world.remove<Carrying>(carried->by);
        world.destroy(u);
    }
    return true;
}

bool App::cleanItem(ObjectId g) {
    if (!world.has<GroundItem>(g)) return false;
    world.destroy(g);
    return true;
}

// What the mode of a left click in the world kills or cleans: the one under it, or those within the radius of the ground
// under it
int App::killAt(float mx, float my) {
    if (devKill.mode == SweepMode::Single) {
        for (ObjectId u : renderer.entitiesAtPixel(world, cam, mx, my, winW, winH)) {
            if (killEntity(u)) return 1;
        }
        return 0;
    }
    const auto hit = pickGround(cam, terrain, mx, my, winW, winH);
    if (!hit) return 0;
    const float reach = (float)devKill.radius;
    int killed = 0;
    for (ObjectId u : world.view<Entity, Transform>()) {
        const Transform* t = world.get<Transform>(u);
        if (distSq(t->x, t->z, hit->first, hit->second) <= reach * reach && killEntity(u)) ++killed;
    }
    return killed;
}

int App::cleanAt(float mx, float my) {
    if (devClean.mode == SweepMode::Single) {
        const std::vector<ObjectId> items = renderer.groundItemsAtPixel(world, terrain, cam, mx, my, winW, winH);
        const ObjectId g = renderer.nearestGroundItem(items, world, cam, mx, my, winW, winH);
        return g != NO_OBJECT && cleanItem(g) ? 1 : 0;
    }
    const auto hit = pickGround(cam, terrain, mx, my, winW, winH);
    if (!hit) return 0;
    const float reach = (float)devClean.radius;
    int cleaned = 0;
    for (ObjectId g : world.view<GroundItem, Transform>()) {
        const Transform* t = world.get<Transform>(g);
        if (distSq(t->x, t->z, hit->first, hit->second) <= reach * reach && cleanItem(g)) ++cleaned;
    }
    return cleaned;
}

int App::killAll() {
    int killed = 0;
    for (ObjectId u : world.view<Entity>()) {
        if (killEntity(u)) ++killed;
    }
    return killed;
}

int App::cleanAll() {
    int cleaned = 0;
    for (ObjectId g : world.view<GroundItem>()) {
        if (cleanItem(g)) ++cleaned;
    }
    return cleaned;
}

// Destroy breaks a prop the way the game does (breakBuilding), so what it held is put on the ground and its Debris is
// left, unless the toggles say otherwise
bool App::destroyProp(ObjectId e) {
    if (!world.has<Prop>(e)) return false;
    if (devDestroy.deleteItems) {
        // What it holds, of every kind there is: its store, its output, a pile of bones, the logs brought to a blueprint
        if (Storage* st = world.get<Storage>(e)) {
            for (ItemStack& slot : st->slots) slot = ItemStack{};
        }
        if (Output* put = world.get<Output>(e)) {
            for (ItemStack& slot : put->slots) slot = ItemStack{};
        }
        if (BonesPile* pile = world.get<BonesPile>(e)) pile->items.clear();
        if (Blueprint* bp = world.get<Blueprint>(e)) bp->supplied = 0;
    }
    breakBuilding(world, e, !devDestroy.deleteDebris);
    return true;
}

int App::destroyAt(float mx, float my) {
    if (devDestroy.mode == SweepMode::Single) {
        const std::vector<ObjectId> props = renderer.propsAtPixel(world, cam, mx, my, winW, winH);
        return !props.empty() && destroyProp(props.front()) ? 1 : 0;
    }
    const auto hit = pickGround(cam, terrain, mx, my, winW, winH);
    if (!hit) return 0;
    const float reach = (float)devDestroy.radius;
    int destroyed = 0;
    for (ObjectId p : world.view<Prop, Transform>()) {
        if (!world.has<Prop>(p)) continue;   // gone with another, such as a foundation's pieces
        const Transform* t = world.get<Transform>(p);
        if (distSq(t->x, t->z, hit->first, hit->second) <= reach * reach && destroyProp(p)) ++destroyed;
    }
    return destroyed;
}

int App::destroyAll() {
    int destroyed = 0;
    for (ObjectId p : world.view<Prop>()) {
        if (destroyProp(p)) ++destroyed;
    }
    return destroyed;
}

// The tab the Developer panel is on, as an index into the three sweeps: 0 Kill, 1 Clean, 2 Destroy
static int sweepKind(EditorTab tab) { return tab == EditorTab::Kill ? 0 : tab == EditorTab::Clean ? 1 : 2; }

SweepUi& App::sweepUi() {
    switch (sweepKind(devSpawn.tab)) {
    case 0: return devKill;
    case 1: return devClean;
    default: return devDestroy;
    }
}

void App::sweepClick(const DeveloperLayout& L, float x, float y) {
    SweepUi& k = sweepUi();
    const int kind = sweepKind(devSpawn.tab);
    if (L.modeBox.contains(x, y)) {
        k.dropOpen = !k.dropOpen;
        playSfx(Sfx::UiClick);
        return;
    }
    if (k.dropOpen) {
        k.dropOpen = false;
        for (int i = 0; i < SWEEP_MODE_COUNT; ++i) {
            if (!L.modeRows[i].contains(x, y)) continue;
            k.mode = (SweepMode)i;
            k.holding = false;
            playSfx(Sfx::UiClick);
        }
        return;
    }
    if (kind != 1) {
        bool* flags[3] = { &devKill.deleteCorpse, &devKill.dropItems, &devKill.deleteItems };
        if (kind == 2) flags[0] = &devDestroy.deleteDebris, flags[1] = &devDestroy.dropItems, flags[2] = &devDestroy.deleteItems;
        for (int i = 0; i < 3; ++i) {
            if (!L.toggles[i].contains(x, y)) continue;
            if (kind == 2 && i > 0) {
                // What it held is dropped or deleted: pressing one picks it, and the other goes off
                *flags[1] = i == 1;
                *flags[2] = i == 2;
            } else {
                *flags[i] = !*flags[i];
                // Dropping and deleting what a corpse held are one or the other
                if (*flags[i] && i == 1) *flags[2] = false;
                if (*flags[i] && i == 2) *flags[1] = false;
            }
            playSfx(Sfx::UiClick);
            return;
        }
    }
    if (k.mode == SweepMode::Radius) {
        const int step = L.radiusDown.contains(x, y) ? -1 : L.radiusUp.contains(x, y) ? 1 : 0;
        if (step != 0) {
            k.radius = std::clamp(k.radius + step, SWEEP_RADIUS_MIN, SWEEP_RADIUS_MAX);
            playSfx(Sfx::UiClick);
        }
    } else if (k.mode == SweepMode::All) {
        if (L.sweepHold.contains(x, y)) {
            k.holding = true;
            k.holdStart = SDL_GetTicks();
        } else if (L.sweepCancel.contains(x, y)) {
            k.mode = SweepMode::Single;
            playSfx(Sfx::UiBack);
        }
    }
}

// Holding the All button down for its three seconds does it. Letting go, or moving off the button, cancels.
void App::updateSweepHold() {
    float hx = 0.0f, hy = 0.0f;
    const bool leftHeld = (SDL_GetMouseState(&hx, &hy) & SDL_BUTTON_LMASK) != 0;
    SweepUi* const uis[3] = { &devKill, &devClean, &devDestroy };
    const EditorTab tabs[3] = { EditorTab::Kill, EditorTab::Clean, EditorTab::Destroy };
    for (int i = 0; i < 3; ++i) {
        SweepUi& k = *uis[i];
        if (!k.holding) continue;
        const bool shown = screen == Screen::Playing && !editing && devSpawn.open && devSpawn.tab == tabs[i] && k.mode == SweepMode::All;
        if (!shown || !leftHeld || !layoutDeveloper(winW, winH, tabs[i]).sweepHold.contains(hx, hy)) {
            k.holding = false;
        } else if (SDL_GetTicks() - k.holdStart >= SWEEP_ALL_HOLD_MS) {
            k.holding = false;
            const int n = i == 0 ? killAll() : i == 1 ? cleanAll() : destroyAll();
            showNotice(langf(i == 0 ? "MISC_KILLED" : i == 1 ? "MISC_CLEANED" : "MISC_DESTROYED", { std::to_string(n) }), 2.0f);
        }
    }
}

void App::drawSweepTab(const DeveloperLayout& L) {
    const RGBA gray{ 0.80f, 0.72f, 0.64f, 1.0f }, gold{ 1.0f, 0.78f, 0.30f, 1.0f };
    const float textH = renderer.listTextHeight("Ag");
    const int kind = sweepKind(devSpawn.tab);
    const SweepUi& k = sweepUi();
    // By tab: Kill, Clean, Destroy
    static const char* const MODE_KEYS[3][SWEEP_MODE_COUNT] = {
        { "UILABEL_DEV_KILL_SINGLE", "UILABEL_DEV_KILL_RADIUS", "UILABEL_DEV_KILL_ALL" },
        { "UILABEL_DEV_CLEAN_SINGLE", "UILABEL_DEV_KILL_RADIUS", "UILABEL_DEV_CLEAN_ALL" },
        { "UILABEL_DEV_DESTROY_SINGLE", "UILABEL_DEV_KILL_RADIUS", "UILABEL_DEV_DESTROY_ALL" } };
    static const char* const HELP_SINGLE[3] = { "UILABEL_DEV_KILL_HELP_SINGLE", "UILABEL_DEV_CLEAN_HELP_SINGLE", "UILABEL_DEV_DESTROY_HELP_SINGLE" };
    static const char* const HELP_RADIUS[3] = { "UILABEL_DEV_KILL_HELP_RADIUS", "UILABEL_DEV_CLEAN_HELP_RADIUS", "UILABEL_DEV_DESTROY_HELP_RADIUS" };
    static const char* const WARN[3] = { "UILABEL_DEV_KILL_WARN", "UILABEL_DEV_CLEAN_WARN", "UILABEL_DEV_DESTROY_WARN" };
    static const char* const HOLD[3] = { "UILABEL_DEV_KILL_HOLD", "UILABEL_DEV_CLEAN_HOLD", "UILABEL_DEV_DESTROY_HOLD" };
    const char* const* modeKeys = MODE_KEYS[kind];
    renderer.drawListText(renderer.fitListText(lang("UILABEL_DEV_KILL_MODE"), L.modeLabel.w), L.modeLabel.x,
                          L.modeLabel.y + (L.modeLabel.h - textH) * 0.5f, gray);
    renderer.drawButton(L.modeBox, lang(modeKeys[(int)k.mode]) + (k.dropOpen ? "  ^" : "  v"), true, L.modeBox.contains(mouseX, mouseY));
    auto wrapped = [&](const std::string& text, const Rect& area, const RGBA& color) {
        float y = area.y;
        for (const std::string& line : renderer.wrapListText(text, area.w)) {
            renderer.drawListText(line, area.x, y, color);
            y += textH + 2.0f;
        }
    };
    if (kind != 1) {
        const bool on[3] = { kind == 0 ? devKill.deleteCorpse : devDestroy.deleteDebris, kind == 0 ? devKill.dropItems : devDestroy.dropItems,
                             kind == 0 ? devKill.deleteItems : devDestroy.deleteItems };
        static const char* const TOGGLE_KEYS[2][3] = {
            { "UILABEL_DEV_KILL_DELETE_CORPSE", "UILABEL_DEV_KILL_DROP_ITEMS", "UILABEL_DEV_KILL_DELETE_ITEMS" },
            { "UILABEL_DEV_DESTROY_DELETE_DEBRIS", "UILABEL_DEV_KILL_DROP_ITEMS", "UILABEL_DEV_KILL_DELETE_ITEMS" } };
        for (int i = 0; i < 3; ++i) {
            renderer.drawButton(L.toggles[i], std::string(on[i] ? "[x] " : "[ ] ") + lang(TOGGLE_KEYS[kind == 0 ? 0 : 1][i]), true,
                                L.toggles[i].contains(mouseX, mouseY));
        }
    }
    if (k.mode == SweepMode::Radius) {
        renderer.drawListText(renderer.fitListText(lang("UILABEL_DEV_KILL_RADIUS"), L.radiusLabel.w), L.radiusLabel.x,
                              L.radiusLabel.y + (L.radiusLabel.h - textH) * 0.5f, gray);
        renderer.drawButton(L.radiusDown, "-", k.radius > SWEEP_RADIUS_MIN, L.radiusDown.contains(mouseX, mouseY));
        renderer.drawButton(L.radiusBox, std::to_string(k.radius), true, false);
        renderer.drawButton(L.radiusUp, "+", k.radius < SWEEP_RADIUS_MAX, L.radiusUp.contains(mouseX, mouseY));
        wrapped(lang(HELP_RADIUS[kind]), L.help, gray);
    } else if (k.mode == SweepMode::All) {
        wrapped(langf(WARN[kind], { std::to_string(SWEEP_ALL_HOLD_MS / 1000) }), L.sweepText, gold);
        const float fill = k.holding ? std::min(1.0f, (float)(SDL_GetTicks() - k.holdStart) / (float)SWEEP_ALL_HOLD_MS) : 0.0f;
        renderer.drawHoldButton(L.sweepHold, lang(HOLD[kind]), fill);
        renderer.drawButton(L.sweepCancel, lang("UILABEL_CANCEL"), true, L.sweepCancel.contains(mouseX, mouseY));
    } else {
        wrapped(lang(HELP_SINGLE[kind]), L.help, gray);
    }
    if (k.dropOpen) {
        for (int i = 0; i < SWEEP_MODE_COUNT; ++i) {
            renderer.drawButton(L.modeRows[i], ((int)k.mode == i ? "> " : "") + lang(modeKeys[i]), true, L.modeRows[i].contains(mouseX, mouseY));
        }
    }
}
