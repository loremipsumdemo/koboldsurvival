#include "schedule.h"
#include <algorithm>
#include <array>
#include <map>
#include <vector>
#include "behavior_groups.h"
#include "calendar.h"
#include "components.h"
#include "squads.h"

Schedule defaultSchedule() {
    Schedule s;
    for (int h = 0; h < SCHEDULE_HOURS; ++h) {
        s.hours[(size_t)h] = (unsigned char)(h >= 22 || h < 6 ? Activity::Sleep : h < 14 ? Activity::Work : Activity::Recreation);
    }
    return s;
}

std::string scheduleText(const Schedule& s) {
    std::string out;
    for (unsigned char a : s.hours) out.push_back((char)('0' + std::min<int>(a, ACTIVITY_COUNT - 1)));
    return out;
}

bool scheduleFromText(const std::string& text, Schedule& out) {
    if ((int)text.size() != SCHEDULE_HOURS) return false;
    for (int h = 0; h < SCHEDULE_HOURS; ++h) {
        const char c = text[(size_t)h];
        if (c < '0' || c >= '0' + ACTIVITY_COUNT) return false;
        out.hours[(size_t)h] = (unsigned char)(c - '0');
    }
    return true;
}

Schedule npcSchedule(int kind) {
    Schedule s = defaultSchedule();
    if (kind != 1 && kind != 2) return s;
    const unsigned char swapWith = (unsigned char)(kind == 1 ? Activity::Recreation : Activity::Work);
    for (unsigned char& a : s.hours) {
        if (a == (unsigned char)Activity::Sleep) a = swapWith;
        else if (a == swapWith) a = (unsigned char)Activity::Sleep;
    }
    return s;
}

int npcIrregularMost(int units) {
    return units > 1 ? 1 + units / 10 : 0;
}

void npcScheduleSystem(World& w, float dt) {
    NpcScheduleClock& clock = w.resource<NpcScheduleClock>();
    clock.left -= dt;
    if (clock.left > 0.0f) return;
    clock.left = NPC_SCHEDULE_LOOK_SECONDS;
    std::map<int, std::vector<ObjectId>> byFaction;
    for (ObjectId u : w.view<Faction, Behaviors>()) {
        const int id = w.get<Faction>(u)->id;
        if (id == FACTION_PLAYER || id == FACTION_FACTIONLESS || w.has<Commandable>(u) || w.has<Mob>(u) || w.has<Corpse>(u)) continue;
        byFaction[id].push_back(u);
    }
    for (auto& [faction, units] : byFaction) {
        std::sort(units.begin(), units.end());
        const int most = npcIrregularMost((int)units.size());
        int irregular = 0, kinds[NPC_SCHEDULE_KINDS] = { 0, 0, 0 };
        for (ObjectId u : units) {
            const NpcSchedule* ns = w.get<NpcSchedule>(u);
            if (ns && ns->kind > 0) {
                ++irregular;
                ++kinds[std::clamp(ns->kind, 0, NPC_SCHEDULE_KINDS - 1)];
            }
        }
        // Too many: the latest on a schedule go back to the default. Too few: the latest units on the default take the one with fewer.
        for (auto it = units.rbegin(); it != units.rend() && irregular > most; ++it) {
            NpcSchedule* ns = w.get<NpcSchedule>(*it);
            if (!ns || ns->kind <= 0) continue;
            --kinds[std::clamp(ns->kind, 0, NPC_SCHEDULE_KINDS - 1)];
            ns->kind = 0;
            --irregular;
        }
        for (auto it = units.rbegin(); it != units.rend(); ++it) {
            NpcSchedule* ns = w.get<NpcSchedule>(*it);
            if (!ns) ns = &w.add(*it, NpcSchedule{});
            if (ns->kind > 0 || irregular >= most) continue;
            ns->kind = kinds[1] <= kinds[2] ? 1 : 2;
            ++kinds[ns->kind];
            ++irregular;
        }
    }
}

Schedule& scheduleOfRef(BehaviorGroups& g, int ref) {
    if (ref < 0 || ref >= BEHAVIOR_GROUP_MAX) return g.schedule;
    while ((int)g.groupSchedules.size() <= ref) {
        Schedule s = g.schedule;
        s.override = false;
        g.groupSchedules.push_back(s);
    }
    return g.groupSchedules[(size_t)ref];
}

Activity activityAtHour(World& w, ObjectId u, int hour) {
    const Faction* fac = w.get<Faction>(u);
    if (fac && fac->id != FACTION_PLAYER) {
        // Another faction's unit: the default schedule, or one of the Irregular ones
        const NpcSchedule* ns = w.get<NpcSchedule>(u);
        static const std::array<Schedule, NPC_SCHEDULE_KINDS> kinds = { npcSchedule(0), npcSchedule(1), npcSchedule(2) };
        return (Activity)std::min<int>(kinds[(size_t)std::clamp(ns ? ns->kind : 0, 0, NPC_SCHEDULE_KINDS - 1)].hours[(size_t)std::clamp(hour, 0, SCHEDULE_HOURS - 1)], ACTIVITY_COUNT - 1);
    }
    BehaviorGroups& g = behaviorGroups(w);
    const Schedule* s = &g.schedule;
    if (const Citizen* c = w.get<Citizen>(u); c && c->group >= 0 && c->group < (int)g.groupSchedules.size()) {
        const Faction* f = w.get<Faction>(u);
        if (f && f->id == FACTION_PLAYER && g.groupSchedules[(size_t)c->group].override) s = &g.groupSchedules[(size_t)c->group];
    }
    return (Activity)std::min<int>(s->hours[(size_t)std::clamp(hour, 0, SCHEDULE_HOURS - 1)], ACTIVITY_COUNT - 1);
}

Activity activityOf(World& w, ObjectId u) {
    return activityAtHour(w, u, clockOf(w.resource<WorldClock>().seconds).hour);
}

bool scheduleAllows(World& w, ObjectId u, BehaviorKind kind) {
    if (w.has<Mob>(u)) return true;
    switch (kind) {
    case BehaviorKind::Sleep: return activityOf(w, u) == Activity::Sleep;
    case BehaviorKind::Socialize: case BehaviorKind::SitDown: case BehaviorKind::Play: case BehaviorKind::Dance:
        return activityOf(w, u) == Activity::Recreation;
    default: break;
    }
    switch (taskCategory(kind)) {
    case TaskCategory::Building: case TaskCategory::Hauling: case TaskCategory::Harvesting: case TaskCategory::Movement:
        return activityOf(w, u) == Activity::Work;
    default: return true;
    }
}
