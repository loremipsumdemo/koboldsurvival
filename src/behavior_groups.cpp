#include "behavior_groups.h"
#include <algorithm>
#include "components.h"
#include "factions.h"
#include "squads.h"

namespace {
BehaviorRule groupTask(int ref) {
    BehaviorRule r;
    r.kind = BehaviorKind::Group;
    r.propKind = ref;
    return r;
}

void expandInto(const BehaviorGroups& g, int ref, std::vector<BehaviorRule>& out, std::vector<int>& seen) {
    const std::vector<BehaviorRule>* list = groupList(g, ref);
    if (!list || std::find(seen.begin(), seen.end(), ref) != seen.end()) return;
    seen.push_back(ref);
    for (const BehaviorRule& r : *list) {
        if (r.kind == BehaviorKind::Group) {
            if (r.on) expandInto(g, r.propKind, out, seen);
            continue;
        }
        if (!holdsBehavior(out, r)) out.push_back(r);
    }
}

// Whether the tasks of ref lead, through Group tasks, to target
bool reaches(const BehaviorGroups& g, int ref, int target, std::vector<int>& seen) {
    if (ref == target) return true;
    const std::vector<BehaviorRule>* list = groupList(g, ref);
    if (!list || std::find(seen.begin(), seen.end(), ref) != seen.end()) return false;
    seen.push_back(ref);
    for (const BehaviorRule& r : *list) {
        if (r.kind == BehaviorKind::Group && reaches(g, r.propKind, target, seen)) return true;
    }
    return false;
}
}

void resetBehaviorGroups(BehaviorGroups& g) {
    g.military = defaultBehaviors();
    // Citizens: their needs (napping only when nearly spent, as they sleep the night through), then building, then
    // staying near your Altar. Hauling and harvesting tasks added to it sort in between building and movement.
    g.citizen = defaultBehaviors();
    for (BehaviorRule& r : g.citizen) {
        if (r.kind == BehaviorKind::Rest) r.percent = percentRange(BehaviorKind::Rest).lo;
    }
    BehaviorRule sleep, build, home;
    sleep.kind = BehaviorKind::Sleep;
    build.kind = BehaviorKind::AutoConstruct;
    home.kind = BehaviorKind::PatrolSpawn;
    g.citizen.insert(g.citizen.end(), { sleep, build, home });
    g.groups.assign(1, { groupTask(GROUP_REF_CITIZEN) });
    g.ready = true;
    ++g.version;
}

BehaviorGroups& behaviorGroups(World& w) {
    BehaviorGroups& g = w.resource<BehaviorGroups>();
    if (!g.ready) resetBehaviorGroups(g);
    if (g.groups.empty()) g.groups.push_back({ groupTask(GROUP_REF_CITIZEN) });
    return g;
}

std::vector<BehaviorRule>* groupList(BehaviorGroups& g, int ref) {
    if (ref == GROUP_REF_MILITARY) return &g.military;
    if (ref == GROUP_REF_CITIZEN) return &g.citizen;
    if (ref >= 0 && ref < (int)g.groups.size()) return &g.groups[(size_t)ref];
    return nullptr;
}

const std::vector<BehaviorRule>* groupList(const BehaviorGroups& g, int ref) {
    return groupList(const_cast<BehaviorGroups&>(g), ref);
}

bool validGroupRef(const BehaviorGroups& g, int ref) { return groupList(g, ref) != nullptr; }

std::vector<BehaviorRule> expandGroup(const BehaviorGroups& g, int ref) {
    std::vector<BehaviorRule> out;
    std::vector<int> seen;
    expandInto(g, ref, out, seen);
    return out;
}

bool canIncludeGroup(const BehaviorGroups& g, int from, int to) {
    if (from == to || !validGroupRef(g, from) || !validGroupRef(g, to)) return false;
    if (from == GROUP_REF_MILITARY || from == GROUP_REF_CITIZEN) return false;
    std::vector<int> seen;
    return !reaches(g, to, from, seen);
}

int addBehaviorGroup(World& w) {
    BehaviorGroups& g = behaviorGroups(w);
    if ((int)g.groups.size() >= BEHAVIOR_GROUP_MAX) return -1;
    g.groups.emplace_back();
    ++g.version;
    return (int)g.groups.size() - 1;
}

bool removeBehaviorGroup(World& w, int group) {
    BehaviorGroups& g = behaviorGroups(w);
    if (group <= 0 || group >= (int)g.groups.size()) return false;
    g.groups.erase(g.groups.begin() + group);
    for (std::vector<BehaviorRule>& list : g.groups) {
        list.erase(std::remove_if(list.begin(), list.end(), [&](const BehaviorRule& r) {
            return r.kind == BehaviorKind::Group && r.propKind == group;
        }), list.end());
        for (BehaviorRule& r : list) {
            if (r.kind == BehaviorKind::Group && r.propKind > group && r.propKind < BEHAVIOR_GROUP_MAX) --r.propKind;
        }
    }
    for (ObjectId u : w.view<Citizen>()) {
        Citizen& c = *w.get<Citizen>(u);
        if (c.group == group) c.group = 0;
        else if (c.group > group) --c.group;
        c.applied = -1;
    }
    ++g.version;
    return true;
}

void behaviorGroupsChanged(World& w) { ++behaviorGroups(w).version; }

bool setCitizenGroup(World& w, ObjectId u, int group) {
    Citizen* c = w.get<Citizen>(u);
    if (!c || group < 0 || group >= (int)behaviorGroups(w).groups.size()) return false;
    c->group = group;
    c->applied = -1;
    return true;
}

int citizensInGroup(World& w, int group) {
    int n = 0;
    for (ObjectId u : w.view<Citizen, Faction>()) {
        if (w.get<Faction>(u)->id == FACTION_PLAYER && w.get<Citizen>(u)->group == group) ++n;
    }
    return n;
}

void behaviorGroupSystem(World& w) {
    BehaviorGroups& g = behaviorGroups(w);
    for (ObjectId u : w.view<Citizen, Behaviors, Faction>()) {
        if (w.get<Faction>(u)->id != FACTION_PLAYER) continue;
        Citizen& c = *w.get<Citizen>(u);
        if (c.group < 0 || c.group >= (int)g.groups.size()) {
            c.group = 0;
            c.applied = -1;
        }
        if (c.applied == g.version) continue;
        c.applied = g.version;
        Behaviors& b = *w.get<Behaviors>(u);
        std::vector<BehaviorRule> next = expandGroup(g, c.group);
        for (BehaviorRule& r : next) {
            for (const BehaviorRule& old : b.rules) {
                if (!sameBehavior(old, r) || old.on != r.on) continue;
                r.cooldown = old.cooldown;
                r.why = old.why;
                break;
            }
        }
        // A task it was in the middle of that the group no longer has: it stops
        if (b.busy && !holdsBehavior(next, b.step)) b.busy = false;
        b.rules = std::move(next);
        b.pending = true;
    }
}
