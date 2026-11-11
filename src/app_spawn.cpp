#include "app.h"
#include "build.h"
#include "level_file.h"
#include "spawn_cluster.h"
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

DeveloperLayout layoutDeveloper(int W, int H) {
    const float s = uiScaleFor((float)H);
    const float m = 10.0f * s, pad = 10.0f * s, line = 28.0f * s;
    DeveloperLayout L;
    const float w = 300.0f * s, h = std::min((float)H - 2.0f * m - 60.0f * s, 620.0f * s);
    L.panel = Rect{ m, std::floor((H - h) * 0.5f), w, h };
    L.title = Rect{ L.panel.x + pad, L.panel.y + pad, w - 2.0f * pad - line, line };
    L.close = Rect{ L.panel.x + w - pad - line, L.panel.y + pad, line, line };
    const float helpH = 44.0f * s;
    L.help = Rect{ L.panel.x + pad, L.panel.y + h - pad - helpH, w - 2.0f * pad, helpH };
    const float top = L.title.y + line + 8.0f * s;
    L.picker = layoutSpawnPicker(Rect{ L.panel.x + pad, top, w - 2.0f * pad, L.help.y - 6.0f * s - top }, s);
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
        enum Group { Weapons, Ammo, Armor, Accessories, Food, Medicine, Materials, Bones, GroupCount };
        static const char* const GROUP_KEYS[GroupCount] = { "UILABEL_ITEMS_WEAPONS", "UILABEL_ITEMS_AMMO", "UILABEL_ITEMS_ARMOR",
                                                            "UILABEL_ITEMS_ACCESSORIES", "UILABEL_ITEMS_FOOD", "UILABEL_ITEMS_MEDICINE",
                                                            "UILABEL_ITEMS_MATERIALS", "UILABEL_ITEMS_BONES" };
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

int App::spawnAt(SpawnPicker& p, float x, float z, bool levelIds) {
    spawnPickerCommit(p, true);
    if (p.tool != EditorTool::Unit && p.tool != EditorTool::Mob && p.tool != EditorTool::Prop && p.tool != EditorTool::Item) return 0;
    // Props kept a footprint apart, units and mobs a body apart, items a little
    float spacing = 0.6f;
    if (p.tool == EditorTool::Prop) spacing = std::max(1.0f, footprintReach(propInfo((PropKind)p.picked).foot) * 2.0f);
    else if (p.tool != EditorTool::Item) spacing = 1.0f;
    const std::vector<Vec2> spots = clusterSpots(world, terrain, rng, x, z, p.cluster, spacing, p.tool == EditorTool::Prop);
    int placed = 0;
    for (const Vec2& spot : spots) {
        LevelObject o;
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
    playSfx(Sfx::UiBack);
}

bool App::pressOnDeveloper() {
    if (screen != Screen::Playing || editing || !devSpawn.open) return false;
    const float x = e.button.x, y = e.button.y;
    const DeveloperLayout L = layoutDeveloper(winW, winH);
    if (L.panel.contains(x, y)) {
        if (e.button.button != SDL_BUTTON_LEFT) return true;
        if (L.close.contains(x, y)) {
            closeDeveloper();
            return true;
        }
        spawnPickerClick(devSpawn, L.picker, x, y);
        return true;
    }
    if (devSpawn.tool == EditorTool::None) return false;
    // With something picked, a right-click puts it down again and a left-click places it
    if (e.button.button == SDL_BUTTON_RIGHT) {
        devSpawn.tool = EditorTool::None;
        playSfx(Sfx::UiBack);
        return true;
    }
    if (e.button.button != SDL_BUTTON_LEFT) return false;
    if (const auto hit = pickGround(cam, terrain, x, y, winW, winH)) {
        const int placed = spawnAt(devSpawn, hit->first, hit->second, false);
        if (placed > 0) showNotice(langf("MISC_SPAWNED", { std::to_string(placed) }), 2.0f);
    }
    return true;
}

void App::drawDeveloper() {
    if (screen != Screen::Playing || editing || !devSpawn.open) return;
    const DeveloperLayout L = layoutDeveloper(winW, winH);
    const float textH = renderer.listTextHeight("Ag");
    renderer.drawThemedPanel(L.panel);
    renderer.drawListText(renderer.fitListText(lang("UILABEL_BUILD_DEVELOPER"), L.title.w), L.title.x,
                          L.title.y + (L.title.h - textH) * 0.5f, RGBA{ 1.0f, 0.78f, 0.30f, 1.0f });
    renderer.drawCloseButton(L.close);
    drawSpawnPicker(devSpawn, L.picker);
    float y = L.help.y;
    for (const std::string& line : renderer.wrapListText(lang("UILABEL_DEVELOPER_HELP"), L.help.w)) {
        renderer.drawListText(line, L.help.x, y, RGBA{ 0.80f, 0.72f, 0.64f, 1.0f });
        y += textH + 2.0f;
    }
}
