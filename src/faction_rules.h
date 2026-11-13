#pragma once
#include <string>
#include <vector>
#include "behavior.h"
#include "components.h"
#include "ecs.h"
#include "terrain.h"

// A faction's rules for those outside it, at work (the rules themselves are in factions.h, from data/factions.txt):
//  - Deal with Downed: a unit with the task (or one of yours ordered to, from the right-click menu) goes to a hostile
//    unit lying knocked out near it, takes from it what its faction's take rule says, then leaves it, locks it in a
//    Prisoner Cage of its faction's, carries it out of its faction's claim, or finishes it off, as its downed rule says.
//  - Trespass: an outsider, neither ally nor enemy, standing in a faction's claim is let be, warned and then attacked
//    if it stays on, or attacked, as that faction's trespass rule says.
//  - Recruit: a Factionless unit that stays a while in a faction's claim, or an outsider rescued into one of its beds
//    who wakes there, joins it, if its recruit rule says so.
inline constexpr float DEAL_RANGE = 25.0f;             // how far a unit looks for a knocked-out enemy
inline constexpr float CARRY_OUT_MARGIN = 3.0f;        // how far past the claim's edge one is put down
inline constexpr float TRESPASS_WARN_SECONDS = 30.0f;  // a warned trespasser who stays this long is attacked
inline constexpr float RECRUIT_SECONDS = 60.0f;        // a Factionless unit this long in a claim that takes them in joins

// A Deal with Downed trip under way: to the knocked-out unit, then carrying it to a cage or out of the claim
struct DealJob {
    enum class Phase { Reach, Carry };
    Phase phase = Phase::Reach;
    ObjectId target = NO_OBJECT;
    ObjectId cage = NO_OBJECT;   // when carrying it to a cage
    float toX = 0.0f, toZ = 0.0f;  // when carrying it out of the claim
    float homeX = 0.0f, homeZ = 0.0f;   // that claim's centre, which the carrier goes back towards
    bool hasHome = false;
    float stuck = 0.0f;
    bool ordered = false;        // your order, not the task: no task to end
};

// On a knocked-out unit a faction has dealt with by leaving it or putting it out: that faction's units let it be
// until it wakes
struct Dealt {
    int faction = -1;
};

// On an outsider standing in a claim whose faction minds trespassers
struct Trespassing {
    int faction = -1;
    float seconds = 0.0f;
    bool warned = false;
    bool attacked = false;
};

// On a Factionless unit standing in the claim of a faction that takes the Factionless in
struct Recruiting {
    int faction = -1;
    float seconds = 0.0f;
};

// Starts a Deal with Downed trip, or says why it waits (r.why)
bool startDealStep(World& w, Terrain& t, ObjectId u, Behaviors& b, BehaviorRule& r);
// Your order to deal with this knocked-out unit, given to the first of units that can. False with why if none can, or
// your rules leave it be.
bool issueDealOrder(World& w, Terrain& t, const std::vector<ObjectId>& units, ObjectId target, std::string& why);
// Whether a unit of this faction would deal with target now: knocked out, hostile, not caged or dealt with already
bool dealableBy(World& w, int faction, ObjectId target);
void dealJobSystem(World& w, Terrain& t, float dt);

// Trespassers and the Factionless lingering in claims
void factionRulesSystem(World& w, float dt);
// A unit waking up in a bed: joins the bed's faction if that faction takes in those it rescues
void recruitOnWaking(World& w, ObjectId u);
// Moves a unit into a faction: yours as a citizen, another's as one of its units
void joinFaction(World& w, ObjectId u, int faction);
