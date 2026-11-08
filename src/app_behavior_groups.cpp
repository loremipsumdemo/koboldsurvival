#include "app.h"
#include "behavior_groups.h"
#include "claims.h"
#include "squads.h"

using namespace play;

namespace {
constexpr RGBA PLAIN{ 1.0f, 1.0f, 1.0f, 1.0f };
}

// Your Altar's Behavior Groups window. On the left, the two Default Behaviors (Military and Citizen) and Groups 1 to 64;
// on the right, the task list of the one picked: switch tasks on and off, move them up and down, remove them, or add one,
// including a Group task that takes in the whole list of another group or Default Behavior.

namespace {
struct GroupsRow {
    Rect row, toggle, up, down, remove;
    Rect addAll;   // on a heading of the tasks that may be added
};

// The tasks that may be added, under a heading for each kind: option is -1 on a heading
struct AddRow {
    TaskCategory category = TaskCategory::Needs;
    int option = -1;
};

std::vector<AddRow> addRows(const std::vector<BehaviorOption>& options) {
    std::vector<AddRow> out;
    for (int c = 0; c < (int)TaskCategory::Count; ++c) {
        bool any = false;
        for (int i = 0; i < (int)options.size(); ++i) {
            if ((int)taskCategory(options[(size_t)i].rule.kind) != c) continue;
            if (!any) out.push_back(AddRow{ (TaskCategory)c, -1 });
            any = true;
            out.push_back(AddRow{ (TaskCategory)c, i });
        }
    }
    return out;
}

struct GroupsLayout {
    Rect box, title, close;
    Rect list, addGroup, removeGroup;
    std::vector<Rect> entries;   // one per ref shown, from the first
    int firstEntry = 0, entryRows = 0;
    Rect head, rules, addTask;
    std::vector<GroupsRow> rows;
    int firstRow = 0, ruleRows = 0;
};

// The refs down the left list: the two Default Behaviors, then every group
std::vector<int> groupRefs(const BehaviorGroups& g) {
    std::vector<int> refs = { GROUP_REF_MILITARY, GROUP_REF_CITIZEN };
    for (int k = 0; k < (int)g.groups.size(); ++k) refs.push_back(k);
    return refs;
}

GroupsLayout layoutGroups(int W, int H, int refCount, int refScroll, int rowCount, int rowScroll) {
    const float s = uiScaleFor((float)H);
    const float margin = 16.0f * s, pad = 14.0f * s, gap = 8.0f * s, line = 28.0f * s, btnH = 32.0f * s;
    const float rowH = 30.0f * s, rowGap = 3.0f * s;
    GroupsLayout L;
    const float boxW = std::min((float)W - 2.0f * margin, 860.0f * s);
    const float boxH = std::min((float)H - 2.0f * margin, 600.0f * s);
    L.box = Rect{ std::floor((W - boxW) * 0.5f), std::floor((H - boxH) * 0.5f), boxW, boxH };
    const float x = L.box.x + pad, w = boxW - 2.0f * pad;
    float y = L.box.y + pad;
    L.title = Rect{ x, y, w - line, line };
    L.close = Rect{ x + w - line, y, line, line };
    y += line + gap;
    const float leftW = std::min(250.0f * s, w * 0.36f);
    const float bottom = L.box.y + boxH - pad;

    // Left: the refs, then Add Group and Remove Group
    const float half = (leftW - gap) * 0.5f;
    L.addGroup = Rect{ x, bottom - btnH, half, btnH };
    L.removeGroup = Rect{ x + half + gap, bottom - btnH, half, btnH };
    L.list = Rect{ x, y, leftW, std::max(0.0f, bottom - btnH - gap - y) };
    L.entryRows = std::max(0, (int)std::floor((L.list.h + rowGap) / (rowH + rowGap) + 0.001f));
    L.firstEntry = std::clamp(refScroll, 0, std::max(0, refCount - L.entryRows));
    for (int i = L.firstEntry; i < refCount && i < L.firstEntry + L.entryRows; ++i) {
        L.entries.push_back(Rect{ L.list.x, L.list.y + (i - L.firstEntry) * (rowH + rowGap), L.list.w, rowH });
    }

    // Right: the picked list's name, its tasks (or the tasks that may be added), and Add Task
    const float rx = x + leftW + gap * 2.0f, rw = x + w - rx;
    L.head = Rect{ rx, y, rw, line };
    L.addTask = Rect{ rx, bottom - btnH, std::min(rw, 220.0f * s), btnH };
    L.rules = Rect{ rx, y + line + gap, rw, std::max(0.0f, bottom - btnH - gap - (y + line + gap)) };
    L.ruleRows = std::max(0, (int)std::floor((L.rules.h + rowGap) / (rowH + rowGap) + 0.001f));
    L.firstRow = std::clamp(rowScroll, 0, std::max(0, rowCount - L.ruleRows));
    const float b = rowH - 6.0f * s;
    for (int i = L.firstRow; i < rowCount && i < L.firstRow + L.ruleRows; ++i) {
        GroupsRow r;
        r.row = Rect{ L.rules.x, L.rules.y + (i - L.firstRow) * (rowH + rowGap), L.rules.w, rowH };
        r.toggle = Rect{ r.row.x + 3.0f * s, r.row.y + 3.0f * s, b, b };
        r.remove = Rect{ r.row.x + r.row.w - b - 3.0f * s, r.row.y + 3.0f * s, b, b };
        r.down = Rect{ r.remove.x - b - 3.0f * s, r.remove.y, b, b };
        r.up = Rect{ r.down.x - b - 3.0f * s, r.remove.y, b, b };
        const float aw = 96.0f * s;
        r.addAll = Rect{ r.row.x + r.row.w - aw - 3.0f * s, r.remove.y, aw, b };
        L.rows.push_back(r);
    }
    return L;
}

std::string fitTo(Renderer& renderer, const std::string& text, float maxW) {
    if (renderer.listTextWidth(text) <= maxW) return text;
    std::string t = text;
    while (!t.empty() && renderer.listTextWidth(t + "...") > maxW) t.pop_back();
    return t + "...";
}
}

void App::openBehaviorGroups(ObjectId altarProp) {
    groupsUi = BehaviorGroupsUi{};
    groupsUi.open = true;
    groupsUi.altar = altarProp;
    groupsUi.ref = 0;
    altar = AltarUi{};
    ghostsUi = GhostsUi{};
    selectMenuOpen = false;
    session.contextMenu.open = false;
    itemMenu.open = false;
    playSfx(Sfx::UiMenuOpen);
}

// What may be added to the list picked: the everyday tasks, Auto-Construct, Auto-Harvest of what grows in your claim,
// Auto-Haul to your stores, and (for a group) a Group task for each list it may take in
std::vector<BehaviorOption> App::groupTaskOptions() {
    std::vector<BehaviorOption> out;
    BehaviorGroups& g = behaviorGroups(world);
    const std::vector<BehaviorRule>* list = groupList(g, groupsUi.ref);
    if (!list) return out;
    auto offer = [&](const BehaviorRule& r, std::string label) {
        BehaviorOption o;
        o.rule = r;
        o.label = std::move(label);
        o.have = holdsBehavior(*list, r);
        out.push_back(std::move(o));
    };
    if (groupsUi.ref >= 0) {
        for (int ref : groupRefs(g)) {
            if (!canIncludeGroup(g, groupsUi.ref, ref)) continue;
            BehaviorRule r;
            r.kind = BehaviorKind::Group;
            r.propKind = ref;
            offer(r, behaviorName(r));
        }
    }
    for (BehaviorKind k : addableKinds(false)) {
        BehaviorRule r;
        r.kind = k;
        offer(r, behaviorName(r));
    }
    BehaviorRule build;
    build.kind = BehaviorKind::AutoConstruct;
    offer(build, behaviorName(build));
    const std::vector<Claim>& claims = claimsOf(world);
    const Transform* at = world.get<Transform>(groupsUi.altar);
    std::vector<char> harvestable((size_t)propKindCount(), 0);
    std::vector<std::pair<float, ObjectId>> stores;
    for (ObjectId p : world.view<Prop, Transform>()) {
        const Transform* pt = world.get<Transform>(p);
        if (!claimAt(claims, pt->x, pt->z, FACTION_PLAYER)) continue;
        const PropKind kind = world.get<Prop>(p)->kind;
        if (idx(kind) >= 0 && idx(kind) < propKindCount() && yieldsItems(kind)) harvestable[(size_t)idx(kind)] = true;
        if (world.has<Storage>(p) && !world.has<Blueprint>(p)) {
            stores.push_back({ at ? std::hypot(pt->x - at->x, pt->z - at->z) : 0.0f, p });
        }
    }
    for (int k = 0; k < propKindCount(); ++k) {
        if (!harvestable[(size_t)k]) continue;
        BehaviorRule r;
        r.kind = BehaviorKind::AutoHarvest;
        r.propKind = k;
        offer(r, behaviorName(r));
    }
    std::sort(stores.begin(), stores.end());
    for (const auto& [d, p] : stores) {
        BehaviorRule r;
        r.prop = p;
        r.propKind = idx(world.get<Prop>(p)->kind);
        r.kind = propInfo(world.get<Prop>(p)->kind).claimsLand ? BehaviorKind::AutoHaulBones : BehaviorKind::AutoHaul;
        offer(r, behaviorName(r) + "  (" + std::to_string((int)std::lround(d)) + "m)");
    }
    return out;
}

bool App::pressOnBehaviorGroups() {
    if (screen != Screen::Playing || !groupsUi.open) return false;
    if (e.button.button != SDL_BUTTON_LEFT) return true;
    const float x = e.button.x, y = e.button.y;
    BehaviorGroups& g = behaviorGroups(world);
    if (!validGroupRef(g, groupsUi.ref)) groupsUi.ref = 0;
    const std::vector<int> refs = groupRefs(g);
    std::vector<BehaviorRule>& list = *groupList(g, groupsUi.ref);
    const std::vector<BehaviorOption> options = groupsUi.adding ? groupTaskOptions() : std::vector<BehaviorOption>();
    const std::vector<AddRow> adds = addRows(options);
    const int rowCount = groupsUi.adding ? (int)adds.size() : (int)list.size();
    const GroupsLayout L = layoutGroups(winW, winH, (int)refs.size(), groupsUi.refScroll, rowCount, groupsUi.rowScroll);
    if (L.close.contains(x, y) || !L.box.contains(x, y)) {
        groupsUi = BehaviorGroupsUi{};
        playSfx(Sfx::UiBack);
        return true;
    }
    for (size_t i = 0; i < L.entries.size(); ++i) {
        if (!L.entries[i].contains(x, y)) continue;
        groupsUi.ref = refs[(size_t)L.firstEntry + i];
        groupsUi.adding = false;
        groupsUi.rowScroll = 0;
        playSfx(Sfx::UiClick);
        return true;
    }
    if (L.addGroup.contains(x, y)) {
        const int made = addBehaviorGroup(world);
        if (made < 0) {
            refuse(langf("MISC_GROUPS_FULL", { std::to_string(BEHAVIOR_GROUP_MAX) }));
            return true;
        }
        // A new group starts out doing what Group 1 does, so it can be built on
        BehaviorRule base;
        base.kind = BehaviorKind::Group;
        base.propKind = 0;
        g.groups[(size_t)made].push_back(base);
        behaviorGroupsChanged(world);
        groupsUi.ref = made;
        groupsUi.adding = false;
        groupsUi.rowScroll = 0;
        groupsUi.refScroll = (int)refs.size();
        playSfx(Sfx::UiClick);
        return true;
    }
    if (L.removeGroup.contains(x, y)) {
        if (groupsUi.ref <= 0) {
            refuse(lang("MISC_GROUP_CANT_REMOVE"));
            return true;
        }
        const int moved = citizensInGroup(world, groupsUi.ref);
        removeBehaviorGroup(world, groupsUi.ref);
        showNotice(langf("MISC_GROUP_REMOVED", { std::to_string(moved) }));
        groupsUi.ref = 0;
        groupsUi.adding = false;
        playSfx(Sfx::UiClick);
        return true;
    }
    if (L.addTask.contains(x, y)) {
        groupsUi.adding = !groupsUi.adding;
        groupsUi.rowScroll = 0;
        playSfx(Sfx::UiClick);
        return true;
    }
    for (size_t i = 0; i < L.rows.size(); ++i) {
        const GroupsRow& r = L.rows[i];
        if (!r.row.contains(x, y)) continue;
        const size_t row = (size_t)L.firstRow + i;
        if (groupsUi.adding) {
            if (row >= adds.size()) return true;
            const AddRow& add = adds[row];
            // A heading's Add All: every task under it the list doesn't have yet
            if (add.option < 0) {
                if (!r.addAll.contains(x, y)) return true;
                int added = 0;
                bool full = false;
                for (const AddRow& o : adds) {
                    if (o.option < 0 || o.category != add.category || options[(size_t)o.option].have) continue;
                    if (!addBehavior(list, options[(size_t)o.option].rule, BEHAVIOR_GROUP_TASKS)) {
                        full = true;
                        break;
                    }
                    ++added;
                }
                if (added > 0) behaviorGroupsChanged(world);
                if (full) refuse(langf("MISC_BEHAVIOR_LIST_FULL", { std::to_string(BEHAVIOR_GROUP_TASKS) }));
                else if (added == 0) refuse(lang("MISC_GROUP_HAS_ALL"));
                if (added > 0) {
                    showNotice(langf("MISC_GROUP_ADDED_ALL", { std::to_string(added), lang(TASK_CATEGORY_KEYS[(int)add.category]) }));
                    playSfx(Sfx::UiClick);
                }
                return true;
            }
            const BehaviorOption& o = options[(size_t)add.option];
            if (!addBehavior(list, o.rule, BEHAVIOR_GROUP_TASKS)) {
                refuse(langf("MISC_BEHAVIOR_LIST_FULL", { std::to_string(BEHAVIOR_GROUP_TASKS) }));
                return true;
            }
            behaviorGroupsChanged(world);
            showNotice(langf(o.have ? "MISC_BEHAVIOR_SWITCHED_ON" : "MISC_BEHAVIOR_ADDED", { behaviorName(o.rule) }));
            playSfx(Sfx::UiClick);
            return true;
        }
        if (row >= list.size()) return true;
        bool changed = false;
        if (r.toggle.contains(x, y)) {
            list[row].on = !list[row].on;
            changed = true;
        } else if (r.up.contains(x, y)) {
            changed = moveBehavior(list, row, -1);
        } else if (r.down.contains(x, y)) {
            changed = moveBehavior(list, row, 1);
        } else if (r.remove.contains(x, y)) {
            changed = removeBehavior(list, row);
        }
        if (changed) {
            behaviorGroupsChanged(world);
            playSfx(Sfx::UiClick);
        }
        return true;
    }
    return true;
}

void App::scrollBehaviorGroups(float mx, float my, float wheel) {
    BehaviorGroups& g = behaviorGroups(world);
    if (!validGroupRef(g, groupsUi.ref)) groupsUi.ref = 0;
    const std::vector<int> refs = groupRefs(g);
    const int rowCount = groupsUi.adding ? (int)addRows(groupTaskOptions()).size() : (int)groupList(g, groupsUi.ref)->size();
    const GroupsLayout L = layoutGroups(winW, winH, (int)refs.size(), groupsUi.refScroll, rowCount, groupsUi.rowScroll);
    const int step = wheel > 0.0f ? -3 : wheel < 0.0f ? 3 : 0;
    if (L.list.contains(mx, my)) groupsUi.refScroll = std::clamp(L.firstEntry + step, 0, std::max(0, (int)refs.size() - L.entryRows));
    if (L.rules.contains(mx, my)) groupsUi.rowScroll = std::clamp(L.firstRow + step, 0, std::max(0, rowCount - L.ruleRows));
}

void App::drawBehaviorGroups() {
    if (!groupsUi.open) return;
    BehaviorGroups& g = behaviorGroups(world);
    if (!validGroupRef(g, groupsUi.ref)) groupsUi.ref = 0;
    const std::vector<int> refs = groupRefs(g);
    const std::vector<BehaviorRule>& list = *groupList(g, groupsUi.ref);
    const std::vector<BehaviorOption> options = groupsUi.adding ? groupTaskOptions() : std::vector<BehaviorOption>();
    const std::vector<AddRow> adds = addRows(options);
    const int rowCount = groupsUi.adding ? (int)adds.size() : (int)list.size();
    const GroupsLayout L = layoutGroups(winW, winH, (int)refs.size(), groupsUi.refScroll, rowCount, groupsUi.rowScroll);
    groupsUi.refScroll = L.firstEntry;
    groupsUi.rowScroll = L.firstRow;
    const float s = uiScaleFor((float)winH);
    const float th = renderer.listTextHeight("Ag");
    auto mid = [&](const Rect& r) { return r.y + (r.h - th) * 0.5f; };
    const RGBA rowBg{ 0.20f, 0.08f, 0.10f, 0.9f }, rowHot{ 0.32f, 0.13f, 0.16f, 0.95f }, picked = theme::alpha(theme::GOLD_DEEP, 0.9f);

    renderer.drawScrim();
    renderer.drawThemedPanel(L.box);
    renderer.drawListText(lang("UILABEL_GROUPS_TITLE"), L.title.x, mid(L.title), theme::GOLD);
    renderer.drawButton(L.close, "X", true, L.close.contains(mouseX, mouseY));

    // The lists to pick from
    renderer.drawRect(L.list, theme::alpha(theme::WELL, 0.6f));
    for (size_t i = 0; i < L.entries.size(); ++i) {
        const Rect& r = L.entries[i];
        const int ref = refs[(size_t)L.firstEntry + i];
        const bool hot = r.contains(mouseX, mouseY);
        renderer.drawRect(r, ref == groupsUi.ref ? picked : hot ? rowHot : rowBg);
        std::string label = groupRefName(ref);
        if (ref >= 0) label += "  " + langf("UILABEL_GROUP_MEMBERS", { std::to_string(citizensInGroup(world, ref)) });
        const bool isDefault = ref == GROUP_REF_MILITARY || ref == GROUP_REF_CITIZEN;
        renderer.drawListText(fitTo(renderer, label, r.w - 12.0f * s), r.x + 6.0f * s, mid(r), isDefault ? theme::TEXT_DIM : PLAIN);
        if (isDefault) renderer.noteTip(lang(ref == GROUP_REF_MILITARY ? "UILABEL_GROUP_MILITARY_TIP" : "UILABEL_GROUP_CITIZEN_TIP"), r);
    }
    renderer.drawButton(L.addGroup, lang("UILABEL_GROUP_ADD"), (int)g.groups.size() < BEHAVIOR_GROUP_MAX, L.addGroup.contains(mouseX, mouseY));
    renderer.drawButton(L.removeGroup, lang("UILABEL_GROUP_REMOVE"), groupsUi.ref > 0, L.removeGroup.contains(mouseX, mouseY));

    // The picked list
    std::string head = groupRefName(groupsUi.ref);
    if (groupsUi.adding) head = langf("UILABEL_GROUP_ADDING", { head });
    renderer.drawListText(fitTo(renderer, head, L.head.w), L.head.x, mid(L.head), theme::GOLD);
    renderer.drawRect(L.rules, theme::alpha(theme::WELL, 0.6f));
    for (size_t i = 0; i < L.rows.size(); ++i) {
        const GroupsRow& r = L.rows[i];
        const size_t row = (size_t)L.firstRow + i;
        const bool hot = r.row.contains(mouseX, mouseY);
        if (groupsUi.adding && adds[row].option < 0) {
            // A heading, with Add All for every task under it
            const TaskCategory c = adds[row].category;
            renderer.drawRect(r.row, theme::alpha(theme::GOLD_DEEP, 0.75f));
            renderer.drawListText(lang(TASK_CATEGORY_KEYS[(int)c]), r.row.x + 6.0f * s, mid(r.row), theme::GOLD);
            bool missing = false;
            for (const AddRow& o : adds) missing = missing || (o.option >= 0 && o.category == c && !options[(size_t)o.option].have);
            renderer.drawButton(r.addAll, lang("UILABEL_GROUP_ADD_ALL"), missing, r.addAll.contains(mouseX, mouseY));
            renderer.noteTip(langf("UILABEL_GROUP_ADD_ALL_TIP", { lang(TASK_CATEGORY_KEYS[(int)c]) }), r.addAll);
            continue;
        }
        renderer.drawRect(r.row, hot ? rowHot : rowBg);
        if (groupsUi.adding) {
            const BehaviorOption& o = options[(size_t)adds[row].option];
            const std::string label = o.have ? langf("UILABEL_GROUP_HAVE", { o.label }) : o.label;
            renderer.drawListText(fitTo(renderer, label, r.row.w - 24.0f * s), r.row.x + 18.0f * s, mid(r.row), o.have ? theme::TEXT_DIM : PLAIN);
            renderer.noteTip(describeBehavior(o.rule), r.row);
            continue;
        }
        const BehaviorRule& rule = list[row];
        renderer.drawButton(r.toggle, rule.on ? "+" : "", true, r.toggle.contains(mouseX, mouseY));
        const float tx = r.toggle.x + r.toggle.w + 6.0f * s;
        const RGBA tint = !rule.on ? theme::TEXT_DIM : rule.kind == BehaviorKind::Group ? theme::GOLD : PLAIN;
        renderer.drawListText(fitTo(renderer, behaviorName(rule), r.up.x - 6.0f * s - tx), tx, mid(r.row), tint);
        renderer.drawButton(r.up, "^", row > 0, r.up.contains(mouseX, mouseY));
        renderer.drawButton(r.down, "v", row + 1 < list.size(), r.down.contains(mouseX, mouseY));
        renderer.drawButton(r.remove, "x", true, r.remove.contains(mouseX, mouseY));
        renderer.noteTip(describeBehavior(rule), Rect{ tx, r.row.y, r.up.x - tx, r.row.h });
    }
    if (rowCount == 0) {
        const std::string none = lang("UILABEL_GROUP_EMPTY");
        renderer.drawListText(none, L.rules.x + 8.0f * s, L.rules.y + 8.0f * s, theme::TEXT_DIM);
    }
    renderer.drawButton(L.addTask, lang(groupsUi.adding ? "UILABEL_GROUP_ADD_DONE" : "UILABEL_GROUP_ADD_TASK"), true,
                        L.addTask.contains(mouseX, mouseY));
}
