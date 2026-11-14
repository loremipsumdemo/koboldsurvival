#include "app.h"
#include "behavior_groups.h"
#include "claims.h"
#include "squads.h"
#include "calendar.h"
#include "schedule.h"

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
    Rect box, title, close, tabBehavior, tabSchedule;
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
    const float tabW = 120.0f * s;
    L.tabSchedule = Rect{ L.close.x - gap - tabW, y, tabW, line };
    L.tabBehavior = Rect{ L.tabSchedule.x - gap - tabW, y, tabW, line };
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

// The Schedule tab: the brushes along the top, then a row of 24 hours for the global schedule and for each group
struct SchedLayout {
    Rect paint[ACTIVITY_COUNT];
    Rect labelHead, hoursHead;
    float labelW = 0.0f, cellW = 0.0f, rowH = 0.0f, rowGap = 0.0f, top = 0.0f, x = 0.0f, gridX = 0.0f;
    Rect area;
    int first = 0, rows = 0;
};

SchedLayout layoutSchedule(const GroupsLayout& L, float s, int rowCount, int scroll) {
    SchedLayout S;
    const float btnH = 34.0f * s, gap = 8.0f * s;
    S.x = L.list.x;
    const float w = L.box.x + L.box.w - 14.0f * s - S.x;
    const float y0 = L.list.y;
    for (int a = 0; a < ACTIVITY_COUNT; ++a) S.paint[a] = Rect{ S.x + a * (130.0f * s + gap), y0, 130.0f * s, btnH };
    S.labelW = std::min(250.0f * s, w * 0.3f);
    S.gridX = S.x + S.labelW + gap;
    S.cellW = std::floor((S.x + w - S.gridX) / (float)SCHEDULE_HOURS);
    S.rowH = 44.0f * s;
    S.rowGap = 4.0f * s;
    const float headY = y0 + btnH + gap * 1.5f;
    S.hoursHead = Rect{ S.gridX, headY, S.cellW * SCHEDULE_HOURS, 22.0f * s };
    S.labelHead = Rect{ S.x, headY, S.labelW, S.hoursHead.h };
    S.top = headY + S.hoursHead.h + gap;
    const float bottom = L.box.y + L.box.h - 14.0f * s;
    S.area = Rect{ S.x, S.top, w, std::max(0.0f, bottom - S.top) };
    S.rows = std::max(1, (int)std::floor((S.area.h + S.rowGap) / (S.rowH + S.rowGap)));
    S.first = std::clamp(scroll, 0, std::max(0, rowCount - S.rows));
    return S;
}
Rect schedRow(const SchedLayout& S, int visible) {
    return Rect{ S.x, S.top + visible * (S.rowH + S.rowGap), S.gridX + S.cellW * SCHEDULE_HOURS - S.x, S.rowH };
}
Rect schedCell(const SchedLayout& S, int visible, int hour) {
    return Rect{ S.gridX + hour * S.cellW, S.top + visible * (S.rowH + S.rowGap), S.cellW - 1.0f, S.rowH };
}
Rect schedToggle(const SchedLayout& S, int visible, float s) {
    const float tw = 52.0f * s, th = 26.0f * s;
    return Rect{ S.x + S.labelW - tw - 8.0f * s, S.top + visible * (S.rowH + S.rowGap) + (S.rowH - th) * 0.5f, tw, th };
}
RGBA activityColor(int a, bool live) {
    static const RGBA C[ACTIVITY_COUNT] = { { 0.24f, 0.24f, 0.66f, 1.0f }, { 0.55f, 0.55f, 0.22f, 1.0f }, { 0.49f, 0.29f, 0.49f, 1.0f } };
    RGBA c = C[std::clamp(a, 0, ACTIVITY_COUNT - 1)];
    if (!live) { c.r *= 0.42f; c.g *= 0.42f; c.b *= 0.42f; }
    return c;
}

std::string fitTo(Renderer& renderer, const std::string& text, float maxW) {
    return renderer.fitListText(text, maxW);
}
}

void App::openBehaviorGroups(ObjectId altarProp, int faction) {
    groupsUi = BehaviorGroupsUi{};
    groupsUi.open = true;
    groupsUi.faction = faction;
    groupsUi.readOnly = faction != FACTION_PLAYER;   // another faction's lists are for looking at
    groupsUi.altar = altarProp;
    groupsUi.ref = 0;
    altar = AltarUi{};
    ghostsUi = GhostsUi{};
    selectMenuOpen = false;
    session.contextMenu.open = false;
    itemMenu.open = false;
    playSfx(Sfx::UiMenuOpen);
}

void App::openSchedule(ObjectId altarProp, int faction) {
    openBehaviorGroups(altarProp, faction);
    groupsUi.tab = 1;
    groupsUi.scheduleOnly = true;
}

BehaviorGroups& App::groupsShown() {
    if (!groupsUi.readOnly) return behaviorGroups(world);
    return behaviorGroups(world, groupsUi.faction);
}

// What may be added to the list picked: the everyday tasks, Auto-Construct, Auto-Harvest of what grows in your claim,
// Auto-Haul to your stores, and (for a group) a Group task for each list it may take in
std::vector<BehaviorOption> App::groupTaskOptions() {
    std::vector<BehaviorOption> out;
    BehaviorGroups& g = groupsShown();
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
    BehaviorGroups& g = groupsShown();
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
    if (groupsUi.tab == 1) {
        if (!groupsUi.readOnly) scheduleClick(x, y, true);
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
    if (groupsUi.readOnly) return true;   // another faction's lists are for looking at: only picking one is allowed
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
    BehaviorGroups& g = groupsShown();
    if (!validGroupRef(g, groupsUi.ref)) groupsUi.ref = 0;
    const std::vector<int> refs = groupRefs(g);
    const int rowCount = groupsUi.adding ? (int)addRows(groupTaskOptions()).size() : (int)groupList(g, groupsUi.ref)->size();
    const GroupsLayout L = layoutGroups(winW, winH, (int)refs.size(), groupsUi.refScroll, rowCount, groupsUi.rowScroll);
    const int step = wheel > 0.0f ? -3 : wheel < 0.0f ? 3 : 0;
    if (groupsUi.tab == 1) {
        const SchedLayout S = layoutSchedule(L, uiScaleFor((float)winH), 2 + (int)g.groups.size(), groupsUi.schedScroll);
        if (S.area.contains(mx, my)) groupsUi.schedScroll = std::clamp(S.first + step, 0, std::max(0, 2 + (int)g.groups.size() - S.rows));
        return;
    }
    if (L.list.contains(mx, my)) groupsUi.refScroll = std::clamp(L.firstEntry + step, 0, std::max(0, (int)refs.size() - L.entryRows));
    if (L.rules.contains(mx, my)) groupsUi.rowScroll = std::clamp(L.firstRow + step, 0, std::max(0, rowCount - L.ruleRows));
}

void App::drawBehaviorGroups() {
    if (!groupsUi.open) return;
    BehaviorGroups& g = groupsShown();
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
    std::string windowTitle = lang(groupsUi.scheduleOnly ? "UILABEL_GROUPS_TAB_SCHEDULE" : "UILABEL_GROUPS_TITLE");
    if (groupsUi.readOnly) windowTitle += ": " + factionName(groupsUi.faction) + " (" + lang("UILABEL_VIEW_ONLY") + ")";
    renderer.drawListText(fitTo(renderer, windowTitle, L.title.w), L.title.x, mid(L.title), theme::GOLD);
    auto refName = [&](int ref) {
        if (!groupsUi.readOnly || ref < 0) return groupRefName(ref);
        return ref == 0 ? std::string(lang("UILABEL_NPC_GROUP_DEFAULT")) : langf("UILABEL_NPC_GROUP_IRREGULAR", { std::to_string(ref) });
    };
    renderer.drawButton(L.close, "X", true, L.close.contains(mouseX, mouseY));
    if (groupsUi.tab == 1) {
        const int rowTotal = 2 + (int)g.groups.size();   // the global schedule, each group, then the Perimeter Closed hours
        const SchedLayout S = layoutSchedule(L, s, rowTotal, groupsUi.schedScroll);
        groupsUi.schedScroll = S.first;
        for (int a = 0; a < ACTIVITY_COUNT; ++a) {
            const bool picked = groupsUi.paint == a;
            renderer.drawRect(Rect{ S.paint[a].x - 3.0f * s, S.paint[a].y - 3.0f * s, S.paint[a].w + 6.0f * s, S.paint[a].h + 6.0f * s },
                              picked ? RGBA{ 1.0f, 1.0f, 1.0f, 0.9f } : RGBA{ 0.0f, 0.0f, 0.0f, 0.0f });
            renderer.drawRect(S.paint[a], activityColor(a, true));
            const std::string label = lang(ACTIVITY_KEYS[a]);
            renderer.drawListText(label, S.paint[a].x + (S.paint[a].w - renderer.listTextWidth(label)) * 0.5f, mid(S.paint[a]), PLAIN);
            renderer.noteTip(lang("UILABEL_SCHEDULE_HELP"), S.paint[a]);
        }
        const int nowHour = clockOf(world.resource<WorldClock>().seconds).hour;
        for (int h = 0; h < SCHEDULE_HOURS; ++h) {
            const std::string n = std::to_string(h);
            const Rect c = schedCell(S, 0, h);
            renderer.drawListText(n, c.x + (c.w - renderer.listTextWidth(n)) * 0.5f, S.hoursHead.y, h == nowHour ? theme::GOLD : theme::TEXT_DIM);
        }
        for (int i = 0; i < S.rows && S.first + i < rowTotal; ++i) {
            const int idx = S.first + i;   // 0 the global schedule, then each group
            if (idx == rowTotal - 1) {
                // The Perimeter Closed hours: red shut, green open. Its own switch, or the Sleep hours of the global schedule.
                const bool own = g.perimeter.override;
                const Rect row = schedRow(S, i);
                renderer.drawRect(Rect{ row.x, row.y, S.labelW, row.h }, theme::alpha(theme::WELL, own ? 0.8f : 0.55f));
                renderer.drawListText(fitTo(renderer, lang("UILABEL_SCHEDULE_PERIMETER"), S.labelW - 66.0f * s), row.x + 8.0f * s, row.y + 3.0f * s,
                                      own ? PLAIN : theme::TEXT_DIM);
                if (!own) renderer.drawListText(lang("UILABEL_SCHEDULE_PERIMETER_USING"), row.x + 8.0f * s, row.y + 3.0f * s + th, theme::TEXT_QUIET);
                const Rect tg = schedToggle(S, i, s);
                renderer.drawRect(tg, own ? RGBA{ 0.30f, 0.70f, 0.32f, 1.0f } : RGBA{ 0.30f, 0.30f, 0.32f, 1.0f });
                const float kn = tg.h - 6.0f * s;
                renderer.drawRect(Rect{ own ? tg.x + tg.w - kn - 3.0f * s : tg.x + 3.0f * s, tg.y + 3.0f * s, kn, kn }, RGBA{ 1.0f, 1.0f, 1.0f, 1.0f });
                renderer.noteTip(lang("UILABEL_SCHEDULE_PERIMETER_TIP"), Rect{ row.x, row.y, S.labelW, row.h });
                for (int h = 0; h < SCHEDULE_HOURS; ++h) {
                    const Rect c = schedCell(S, i, h);
                    const bool shut = perimeterClosedAtHour(world, h, groupsUi.faction);
                    RGBA col = shut ? RGBA{ 0.62f, 0.18f, 0.18f, 1.0f } : RGBA{ 0.25f, 0.50f, 0.28f, 1.0f };
                    if (!own) { col.r *= 0.42f; col.g *= 0.42f; col.b *= 0.42f; }
                    renderer.drawRect(c, col);
                    if (h == nowHour) renderer.drawRect(Rect{ c.x, c.y, c.w, 3.0f * s }, theme::GOLD);
                }
                continue;
            }
            const int ref = idx - 1;
            const Schedule& sch = scheduleOfRef(g, ref);
            const bool live = idx == 0 || sch.override;
            const Rect row = schedRow(S, i);
            renderer.drawRect(Rect{ row.x, row.y, S.labelW, row.h }, theme::alpha(theme::WELL, live ? 0.8f : 0.55f));
            const std::string name = idx == 0 ? lang("UILABEL_SCHEDULE_GLOBAL") : refName(ref);
            renderer.drawListText(fitTo(renderer, name, S.labelW - 66.0f * s), row.x + 8.0f * s, idx == 0 ? mid(row) : row.y + 3.0f * s,
                                  live ? PLAIN : theme::TEXT_DIM);
            if (idx > 0 && !groupsUi.readOnly) {
                if (!sch.override) renderer.drawListText(lang("UILABEL_SCHEDULE_USING_GLOBAL"), row.x + 8.0f * s, row.y + 3.0f * s + th, theme::TEXT_QUIET);
                const Rect tg = schedToggle(S, i, s);
                renderer.drawRect(tg, sch.override ? RGBA{ 0.30f, 0.70f, 0.32f, 1.0f } : RGBA{ 0.30f, 0.30f, 0.32f, 1.0f });
                const float kn = tg.h - 6.0f * s;
                renderer.drawRect(Rect{ sch.override ? tg.x + tg.w - kn - 3.0f * s : tg.x + 3.0f * s, tg.y + 3.0f * s, kn, kn }, RGBA{ 1.0f, 1.0f, 1.0f, 1.0f });
                renderer.noteTip(lang("UILABEL_SCHEDULE_OVERRIDE"), tg);
            }
            const Schedule& shown = live ? sch : g.schedule;
            for (int h = 0; h < SCHEDULE_HOURS; ++h) {
                const Rect c = schedCell(S, i, h);
                renderer.drawRect(c, activityColor(shown.hours[(size_t)h], live));
                if (h == nowHour) renderer.drawRect(Rect{ c.x, c.y, c.w, 3.0f * s }, theme::GOLD);
            }
        }
        return;
    }

    // The lists to pick from
    renderer.drawRect(L.list, theme::alpha(theme::WELL, 0.6f));
    for (size_t i = 0; i < L.entries.size(); ++i) {
        const Rect& r = L.entries[i];
        const int ref = refs[(size_t)L.firstEntry + i];
        const bool hot = r.contains(mouseX, mouseY);
        renderer.drawRect(r, ref == groupsUi.ref ? picked : hot ? rowHot : rowBg);
        std::string label = refName(ref);
        if (ref >= 0 && !groupsUi.readOnly) label += "  " + langf("UILABEL_GROUP_MEMBERS", { std::to_string(citizensInGroup(world, ref)) });
        const bool isDefault = ref == GROUP_REF_MILITARY || ref == GROUP_REF_CITIZEN;
        renderer.drawListText(fitTo(renderer, label, r.w - 12.0f * s), r.x + 6.0f * s, mid(r), isDefault ? theme::TEXT_DIM : PLAIN);
        if (isDefault) renderer.noteTip(lang(ref == GROUP_REF_MILITARY ? "UILABEL_GROUP_MILITARY_TIP" : "UILABEL_GROUP_CITIZEN_TIP"), r);
    }
    if (!groupsUi.readOnly) {
        renderer.drawButton(L.addGroup, lang("UILABEL_GROUP_ADD"), (int)g.groups.size() < BEHAVIOR_GROUP_MAX, L.addGroup.contains(mouseX, mouseY));
        renderer.drawButton(L.removeGroup, lang("UILABEL_GROUP_REMOVE"), groupsUi.ref > 0, L.removeGroup.contains(mouseX, mouseY));
    }

    // The picked list
    std::string head = refName(groupsUi.ref);
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
        if (!groupsUi.readOnly) {
            renderer.drawButton(r.up, "^", row > 0, r.up.contains(mouseX, mouseY));
            renderer.drawButton(r.down, "v", row + 1 < list.size(), r.down.contains(mouseX, mouseY));
            renderer.drawButton(r.remove, "x", true, r.remove.contains(mouseX, mouseY));
        }
        renderer.noteTip(describeBehavior(rule), Rect{ tx, r.row.y, r.up.x - tx, r.row.h });
    }
    if (rowCount == 0) {
        const std::string none = lang("UILABEL_GROUP_EMPTY");
        renderer.drawListText(none, L.rules.x + 8.0f * s, L.rules.y + 8.0f * s, theme::TEXT_DIM);
    }
    if (!groupsUi.readOnly) {
        renderer.drawButton(L.addTask, lang(groupsUi.adding ? "UILABEL_GROUP_ADD_DONE" : "UILABEL_GROUP_ADD_TASK"), true,
                            L.addTask.contains(mouseX, mouseY));
    }
}

bool App::scheduleClick(float x, float y, bool press) {
    if (!groupsUi.open || groupsUi.tab != 1 || groupsUi.readOnly) return false;
    BehaviorGroups& g = groupsShown();
    const std::vector<int> refs = groupRefs(g);
    const GroupsLayout L = layoutGroups(winW, winH, (int)refs.size(), groupsUi.refScroll, 0, 0);
    const float s = uiScaleFor((float)winH);
    const int rowTotal = 2 + (int)g.groups.size();
    const SchedLayout S = layoutSchedule(L, s, rowTotal, groupsUi.schedScroll);
    if (press) {
        for (int a = 0; a < ACTIVITY_COUNT; ++a) {
            if (!S.paint[a].contains(x, y)) continue;
            groupsUi.paint = a;
            playSfx(Sfx::UiClick);
            return true;
        }
    }
    for (int i = 0; i < S.rows && S.first + i < rowTotal; ++i) {
        if (S.first + i == rowTotal - 1) {
            // The Perimeter Closed hours: its switch starts them as the Sleep hours; a press on an hour flips it and a drag sets the rest alike
            if (press && schedToggle(S, i, s).contains(x, y)) {
                if (!g.perimeter.override) {
                    for (int h = 0; h < SCHEDULE_HOURS; ++h) g.perimeter.hours[(size_t)h] = perimeterClosedAtHour(world, h) ? 1 : 0;
                }
                g.perimeter.override = !g.perimeter.override;
                playSfx(Sfx::UiClick);
                return true;
            }
            if (!g.perimeter.override) continue;
            for (int h = 0; h < SCHEDULE_HOURS; ++h) {
                if (!schedCell(S, i, h).contains(x, y)) continue;
                if (press) {
                    groupsUi.perimPaint = g.perimeter.hours[(size_t)h] ? 0 : 1;
                    playSfx(Sfx::UiClick);
                }
                g.perimeter.hours[(size_t)h] = (unsigned char)groupsUi.perimPaint;
                return true;
            }
            continue;
        }
        const int ref = S.first + i - 1;
        Schedule& sch = scheduleOfRef(g, ref);
        if (press && ref >= 0 && schedToggle(S, i, s).contains(x, y)) {
            sch.override = !sch.override;
            // A group's own schedule starts out as the global one
            if (sch.override) {
                const bool keep = sch.override;
                sch = g.schedule;
                sch.override = keep;
            }
            playSfx(Sfx::UiClick);
            return true;
        }
        if (ref >= 0 && !sch.override) continue;
        for (int h = 0; h < SCHEDULE_HOURS; ++h) {
            if (!schedCell(S, i, h).contains(x, y)) continue;
            if (sch.hours[(size_t)h] != (unsigned char)groupsUi.paint) {
                sch.hours[(size_t)h] = (unsigned char)groupsUi.paint;
                if (press) playSfx(Sfx::UiClick);
            }
            return true;
        }
    }
    return false;
}
