#include "npc_roles.h"
#include <algorithm>
#include <cmath>
#include "behavior.h"
#include "claims.h"
#include "components.h"
#include "factions.h"
#include "gates.h"
#include "mobs.h"
#include "squads.h"
#include "systems_internal.h"

using namespace systems_detail;

namespace {
constexpr float PERIMETER_MIN_RADIUS = 10.0f;   // a claim this small or smaller has no Perimeter (npc_builders.cpp)
constexpr int MOST_CHANGES_PER_LOOK = 3;        // units of a faction that change role in one look

struct RoleClock {
    double hours = 0.0;
};

// What a unit was doing as one role is dropped
void leaveDuties(World& w, ObjectId u) {
    w.remove<GateKeeper>(u);
    w.remove<GuardPostDuty>(u);
    w.remove<Patrolling>(u);
    w.remove<AskingEntry>(u);
    w.remove<GateWork>(u);
}

void takeRules(World& w, ObjectId u, std::vector<BehaviorRule> rules) {
    Behaviors& b = w.has<Behaviors>(u) ? *w.get<Behaviors>(u) : w.add(u, Behaviors{});
    b.rules = std::move(rules);
    b.busy = false;
    b.seeking = false;
    b.seek = NO_OBJECT;
    b.stepProp = NO_OBJECT;
    b.wait = 0.0f;
    b.idleFor = 0.0f;
    b.pending = true;
    w.remove<SoloMove>(u);
}
}

void makeNpcCitizen(World& w, ObjectId u, Vec2 home) {
    if (!w.has<Entity>(u)) return;
    w.remove<Fortified>(u);
    leaveDuties(w, u);
    w.remove<SquadMember>(u);
    if (!w.has<Citizen>(u)) w.add(u, Citizen{});
    w.add(u, SpawnHome{ home.x, home.z, false });
    takeRules(w, u, npcFactionBehaviors());
}

void makeNpcFortified(World& w, ObjectId u, Vec2 home) {
    if (!w.has<Entity>(u)) return;
    makeNpcCitizen(w, u, home);
    makeFortified(w, u);   // its own tasks: needs, sleep, and building the Perimeter's pieces
}

void makeNpcMilitary(World& w, ObjectId u) {
    if (!w.has<Entity>(u)) return;
    w.remove<Fortified>(u);
    leaveDuties(w, u);
    w.remove<Citizen>(u);
    w.remove<SpawnHome>(u);
    w.add(u, SquadMember{ 0 });
    takeRules(w, u, npcMilitaryBehaviors());
}

void npcRolesHour(World& w, double gameHours) {
    RoleClock& clock = w.resource<RoleClock>();
    clock.hours += gameHours;
    if (clock.hours < 1.0) return;
    clock.hours = 0.0;
    const std::vector<Claim> claims = claimsOf(w);
    for (const Claim& c : claims) {
        if (c.camp || c.owner == FACTION_PLAYER || !factionCanClaim(c.owner) || isMobFaction(c.owner) || factionPlayerControlled(c.owner) ||
            !w.has<Prop>(c.altar)) {
            continue;
        }
        // The faction's units by role (those away on a raid, a shopping trip or the like are not counted)
        std::vector<ObjectId> citizens, fortified, military;
        for (ObjectId u : w.view<Entity, Faction, Behaviors>()) {
            if (factionOf(w, u) != c.owner || w.has<Mob>(u) || w.has<Corpse>(u) || w.has<Commandable>(u)) continue;
            if (w.has<Raider>(u) || w.has<Shopper>(u)) continue;
            (w.has<Fortified>(u) ? fortified : w.has<Citizen>(u) ? citizens : military).push_back(u);
        }
        const int total = (int)(citizens.size() + fortified.size() + military.size());
        if (total < 2) continue;
        std::sort(citizens.begin(), citizens.end());
        std::sort(fortified.begin(), fortified.end());
        std::sort(military.begin(), military.end());

        // What each should be: the Fortified only with a Perimeter to guard, at least one citizen to keep the home
        const FactionRules& rules = factionRules(w, c.owner);
        const int fortifiedWant = c.radius > PERIMETER_MIN_RADIUS ? (int)std::lround(rules.fortified * 0.01 * total) : 0;
        const int militaryWant = std::min((int)std::lround(rules.military * 0.01 * total), std::max(0, total - fortifiedWant - 1));
        const int citizensWant = total - fortifiedWant - militaryWant;

        enum Role { Citizen_, Fortified_, Military_ };
        std::vector<ObjectId>* lists[3] = { &citizens, &fortified, &military };
        const int want[3] = { citizensWant, fortifiedWant, militaryWant };
        for (int changes = 0; changes < MOST_CHANGES_PER_LOOK; ++changes) {
            // The role most short is given a unit from the role most over
            int shortRole = -1, overRole = -1, shortBy = 0, overBy = 0;
            for (int r = 0; r < 3; ++r) {
                const int d = want[r] - (int)lists[r]->size();
                if (d > shortBy) {
                    shortBy = d;
                    shortRole = r;
                }
                if (-d > overBy) {
                    overBy = -d;
                    overRole = r;
                }
            }
            if (shortRole < 0 || overRole < 0) break;
            // An idle unit: not in a task, hurt, carried, in bed or on an errand
            ObjectId pick = NO_OBJECT;
            for (ObjectId u : *lists[overRole]) {
                const Behaviors* b = w.get<Behaviors>(u);
                if (!b || b->busy || outCold(w, u) || w.has<Carried>(u) || w.has<Occupying>(u) || w.has<PropOrder>(u) || w.has<GateWork>(u)) continue;
                pick = u;
                break;
            }
            if (pick == NO_OBJECT) break;
            lists[overRole]->erase(std::find(lists[overRole]->begin(), lists[overRole]->end(), pick));
            lists[shortRole]->push_back(pick);
            const Vec2 home{ c.x, c.z };
            if (shortRole == Citizen_) makeNpcCitizen(w, pick, home);
            else if (shortRole == Fortified_) makeNpcFortified(w, pick, home);
            else makeNpcMilitary(w, pick);
        }
    }
}
