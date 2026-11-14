#include "behavior_groups.h"
#include <algorithm>
#include "components.h"
#include "factions.h"
#include "squads.h"
#include "systems.h"
#include "claims.h"
#include "building.h"

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

std::vector<BehaviorRule> defaultCitizenBehaviors() {
    // Citizens: their needs (napping only when nearly spent, as they sleep the night through), then building, then
    // hauling (to each of your stores, kept up by storeHaulSync, then items off the ground), then patrolling your town.
    // Harvesting tasks added to it sort in between hauling and the patrol.
    std::vector<BehaviorRule> citizen = defaultBehaviors();
    for (BehaviorRule& r : citizen) {
        if (r.kind == BehaviorKind::Rest) r.percent = percentRange(BehaviorKind::Rest).lo;
    }
    // Each sorts into its place by its category (Auto-Haul to Stalls is in the defaults already)
    BehaviorRule sleep, build, ground, patrol, craft, social, sit, play, dance, junk;
    junk.kind = BehaviorKind::ClearJunk;   // on by default: clears the Debris and Boneheaps in the claim
    play.kind = BehaviorKind::Play;
    dance.kind = BehaviorKind::Dance;
    social.kind = BehaviorKind::Socialize;
    sit.kind = BehaviorKind::SitDown;
    craft.kind = BehaviorKind::AutoCraft;   // fills the Crafting Orders that keep a stock
    sleep.kind = BehaviorKind::Sleep;
    build.kind = BehaviorKind::AutoConstruct;
    ground.kind = BehaviorKind::AutoHaulGround;
    patrol.kind = BehaviorKind::Patrol;
    for (const BehaviorRule& r : { sleep, build, junk, ground, craft, patrol, social, sit, play, dance }) addBehavior(citizen, r, BEHAVIOR_TASKS_MAX);
    return citizen;
}

void resetBehaviorGroups(BehaviorGroups& g) {
    g.military = defaultBehaviors();
    g.citizen = defaultCitizenBehaviors();
    g.schedule = defaultSchedule();
    g.groupSchedules.clear();
    g.perimeter = Schedule{};
    g.listedStores.clear();
    g.storesListed = false;
    g.groups.assign(1, { groupTask(GROUP_REF_CITIZEN) });
    g.ready = true;
    ++g.version;
}

void resetNpcBehaviorGroups(BehaviorGroups& g) {
    g.military = npcMilitaryBehaviors();
    g.citizen = defaultCitizenBehaviors();
    g.schedule = defaultSchedule();
    g.groupSchedules.clear();
    // Group 1 is the default; Groups 2 and 3 keep the Irregular schedules (the default with Sleep swapped with Recreation, or Work)
    for (int k = 0; k < NPC_SCHEDULE_KINDS; ++k) {
        Schedule s = npcSchedule(k);
        s.override = k > 0;
        g.groupSchedules.push_back(s);
    }
    g.perimeter = Schedule{};   // its gates are shut in its Sleep hours until its Avatar says otherwise
    g.listedStores.clear();
    g.storesListed = false;
    g.groups.assign((size_t)NPC_SCHEDULE_KINDS, { groupTask(GROUP_REF_CITIZEN) });
    g.ready = true;
    ++g.version;
}

BehaviorGroups& behaviorGroups(World& w, int faction) {
    if (faction != FACTION_PLAYER) {
        BehaviorGroups& n = w.resource<NpcBehaviorGroups>().by[faction];
        if (!n.ready) resetNpcBehaviorGroups(n);
        return n;
    }
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
    scheduleOfRef(g, (int)g.groups.size() - 1);
    ++g.version;
    return (int)g.groups.size() - 1;
}

bool removeBehaviorGroup(World& w, int group) {
    BehaviorGroups& g = behaviorGroups(w);
    if (group <= 0 || group >= (int)g.groups.size()) return false;
    g.groups.erase(g.groups.begin() + group);
    if (group < (int)g.groupSchedules.size()) g.groupSchedules.erase(g.groupSchedules.begin() + group);
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

bool isGroupStore(World& w, ObjectId p, int faction) {
    const Prop* pp = w.get<Prop>(p);
    if (!pp || !w.has<Storage>(p) || w.has<Blueprint>(p)) return false;
    const PropInfo& info = propInfo(pp->kind);
    if ((info.use != PropUse::Storage && !info.claimsLand) || info.stall) return false;   // a Stall is stocked by its orders
    const Owner* own = w.get<Owner>(p);
    if (own && own->faction != faction) return false;
    const Transform* t = w.get<Transform>(p);
    return t && claimAt(claimsOf(w), t->x, t->z, faction) != nullptr;
}

std::vector<ObjectId> groupStores(World& w, int faction) {
    std::vector<ObjectId> out;
    for (ObjectId p : w.view<Prop, Storage>()) {
        if (isGroupStore(w, p, faction)) out.push_back(p);
    }
    return out;
}

void markStoresListed(World& w) {
    BehaviorGroups& g = behaviorGroups(w);
    g.listedStores = groupStores(w);
    std::sort(g.listedStores.begin(), g.listedStores.end());
    g.storesListed = true;
}

bool storeHaulSync(World& w, int faction) {
    BehaviorGroups& g = behaviorGroups(w, faction);
    bool changed = false;
    // Haul tasks to a store that is gone leave every list
    auto gone = [&](const BehaviorRule& r) { return ruleNamesProp(r.kind) && !w.has<Storage>(r.prop); };
    auto prune = [&](std::vector<BehaviorRule>& list) {
        const size_t before = list.size();
        list.erase(std::remove_if(list.begin(), list.end(), gone), list.end());
        changed = changed || list.size() != before;
    };
    prune(g.military);
    prune(g.citizen);
    for (std::vector<BehaviorRule>& list : g.groups) prune(list);
    g.listedStores.erase(std::remove_if(g.listedStores.begin(), g.listedStores.end(), [&](ObjectId p) { return !w.has<Storage>(p); }),
                         g.listedStores.end());
    // A store of yours new to the list gets a haul task in the Citizen Default Behavior (bones, for the Altar), once
    for (ObjectId p : groupStores(w, faction)) {
        if (std::binary_search(g.listedStores.begin(), g.listedStores.end(), p)) continue;
        g.listedStores.insert(std::upper_bound(g.listedStores.begin(), g.listedStores.end(), p), p);
        BehaviorRule r;
        r.kind = propInfo(w.get<Prop>(p)->kind).claimsLand ? BehaviorKind::AutoHaulBones : BehaviorKind::AutoHaul;
        r.prop = p;
        r.propKind = idx(w.get<Prop>(p)->kind);
        if (!holdsBehavior(g.citizen, r) && addBehavior(g.citizen, r, BEHAVIOR_GROUP_TASKS)) changed = true;
    }
    g.storesListed = true;
    if (changed) ++g.version;
    return changed;
}

void behaviorGroupSystem(World& w) {
    BehaviorGroups& pg = behaviorGroups(w);
    const bool syncNow = ++pg.syncTick >= STORE_SYNC_STEPS;
    if (syncNow) {
        pg.syncTick = 0;
        storeHaulSync(w, FACTION_PLAYER);
        // An NPC faction's citizens haul to its stores as yours do to yours
        for (const Claim& c : claimsOf(w)) {
            if (!c.camp && factionRunsItself(c.owner)) storeHaulSync(w, c.owner);
        }
    }
    // A Fortified unit made before they slept or built has no Sleep or Auto-Construct task: it is given them (it keeps no other from its group)
    for (ObjectId u : w.view<Fortified, Behaviors>()) {
        Behaviors& b = *w.get<Behaviors>(u);
        for (BehaviorKind kind : { BehaviorKind::Sleep, BehaviorKind::AutoConstruct }) {
            const bool has = std::any_of(b.rules.begin(), b.rules.end(), [&](const BehaviorRule& r) { return r.kind == kind; });
            if (has) continue;
            BehaviorRule rule;
            rule.kind = kind;
            if (addBehavior(b.rules, rule, BEHAVIOR_TASKS_MAX)) b.pending = true;
        }
    }
    // Each citizen, yours or an NPC faction's, follows its group's list
    int cachedFaction = -1;
    BehaviorGroups* gp = nullptr;
    for (ObjectId u : w.view<Citizen, Behaviors, Faction>()) {
        const int faction = w.get<Faction>(u)->id;
        if ((faction != FACTION_PLAYER && !factionRunsItself(faction)) || w.has<Fortified>(u)) continue;
        if (faction != cachedFaction) {
            gp = &behaviorGroups(w, faction);
            cachedFaction = faction;
        }
        BehaviorGroups& g = *gp;
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
    // An NPC faction's military follow its Military list (roaming), as yours follow theirs
    if (syncNow) {
        for (ObjectId u : w.view<Behaviors, Faction>()) {
            const int faction = w.get<Faction>(u)->id;
            if (faction == FACTION_PLAYER || !factionRunsItself(faction)) continue;
            if (w.has<Citizen>(u) || w.has<Commandable>(u) || w.has<Mob>(u) || w.has<Corpse>(u) || w.has<Raider>(u) || w.has<Shopper>(u)) continue;
            BehaviorGroups& g = behaviorGroups(w, faction);
            GroupApplied* applied = w.get<GroupApplied>(u);
            if (!applied) applied = &w.add(u, GroupApplied{});
            if (applied->version == g.version) continue;
            applied->version = g.version;
            Behaviors& b = *w.get<Behaviors>(u);
            std::vector<BehaviorRule> next = expandGroup(g, GROUP_REF_MILITARY);
            for (BehaviorRule& r : next) {
                for (const BehaviorRule& old : b.rules) {
                    if (!sameBehavior(old, r) || old.on != r.on) continue;
                    r.cooldown = old.cooldown;
                    r.why = old.why;
                    break;
                }
            }
            if (b.busy && !holdsBehavior(next, b.step)) b.busy = false;
            b.rules = std::move(next);
            b.pending = true;
        }
    }
}
