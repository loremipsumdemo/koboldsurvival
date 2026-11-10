#include "commerce.h"
#include "behavior_groups.h"
#include "squads.h"
#include "ghosts.h"
#include "airship.h"
#include "wheelbarrow.h"
#include "building.h"
#include "simulation.h"
#include "calendar.h"
#include "claims.h"
#include "npc_camps.h"
#include "taming.h"
#include "combat.h"
#include "factions.h"
#include "hostility.h"
#include "need_behaviors.h"
#include "profile.h"

namespace {
SimEvents& events(World& w) { return w.resource<SimEvents>(); }

template <typename T>
void append(std::vector<T>& to, std::vector<T>&& more) {
    if (to.empty()) to = std::move(more);
    else to.insert(to.end(), std::make_move_iterator(more.begin()), std::make_move_iterator(more.end()));
}
}

SimEvents takeEvents(World& w) {
    SimEvents out;
    std::swap(out, events(w));
    return out;
}

void queueSound(World& w, Sfx s) {
    std::vector<QueuedSound>& sounds = events(w).sounds;
    for (const QueuedSound& q : sounds) {
        if (q.sfx == s && !q.placed) return;
    }
    sounds.push_back(QueuedSound{ s, false });
}

void queueSoundAt(World& w, Sfx s, float x, float y, float z) {
    events(w).sounds.push_back(QueuedSound{ s, true, x, y, z });
}

void queueSoundFrom(World& w, Sfx s, ObjectId e) {
    if (const Transform* t = w.get<Transform>(e)) queueSoundAt(w, s, t->x, t->y + 0.5f, t->z);
    else queueSound(w, s);
}

void queueLog(World& w, const std::string& text, LogKind kind, bool npc, Tint tint) {
    events(w).log.push_back(LogLine{ text, tint, kind, npc });
}

void queueUnitLabel(World& w, ObjectId unit, const std::string& text, bool red) {
    if (unit == NO_OBJECT || text.empty()) return;
    events(w).labels.push_back(TradeLabel{ unit, text, 0.0f, red });
}

void queuePuff(World& w, float x, float y, float z) {
    events(w).puffs.push_back(Puff{ x, y, z, 0.0f });
}

void queueItemMove(World& w, ObjectId from, ObjectId to, int item, int count) {
    if (item <= 0 || count <= 0) return;
    queueUnitLabel(w, from, itemLossLabelFor(count, item), true);
    queueUnitLabel(w, to, itemGainLabelFor(count, item));
}

void queueFloatingNumber(World& w, const FloatingNumber& number) {
    events(w).floats.push_back(number);
}

void simStep(World& w, Terrain& t, Rng& rng, GameState& gs, float dt, bool running) {
    w.resource<WorldClock>().seconds = gs.clockSeconds;
    // Fighting first, so a unit that dies this frame is gone before the others move. One sound covers the
    // swings of the frame: a hit if any landed, otherwise a miss if any were missed or dodged.
    PROFILED("factionSystem", factionSystem(w, dt));
    PROFILED("defendSystem", defendSystem(w));
    PROFILED("combatSystem", combatSystem(w, t, rng, dt));
    PROFILED("patrolSpawnSystem", patrolSpawnSystem(w, t, dt));
    fleeSystem(w, t, dt);
    // Behaviors after the fight, so a unit hit this frame can drink in the same frame
    PROFILED("behaviorSystem", behaviorSystem(w, dt));
    downedSystem(w);
    PROFILED("squadSystem", squadSystem(w, dt, running));
    PROFILED("steeringSystem", steeringSystem(w, t, dt, running));
    airshipSystem(w, t, dt);
    wheelbarrowSystem(w);
    PROFILED("pickupSystem", pickupSystem(w, t));
    append(events(w).arrivals, PROFILED("propOrderSystem", propOrderSystem(w, t)));
    PROFILED("wildMobSystem", wildMobSystem(w, t, dt));
    PROFILED("behaviorGroupSystem", behaviorGroupSystem(w));
    PROFILED("automationSystem", automationSystem(w, t, dt, rng));
    PROFILED("buildingSystem", buildingSystem(w, t, dt));
    PROFILED("tameJobSystem", tameJobSystem(w, t, dt, dt * GAME_SECONDS_PER_REAL_SECOND));
    PROFILED("commerceSystem", commerceSystem(w, t, rng, dt));
    PROFILED("npcCampSystem", npcCampSystem(w, t, dt, dt * GAME_SECONDS_PER_REAL_SECOND));
    CarryEvents carried = PROFILED("carrySystem", carrySystem(w));
    append(events(w).lootOpened, std::move(carried.loot));
    PROFILED("leaveOpenLocksSystem", leaveOpenLocksSystem(w, t));
    occupyBanSystem(w, dt);
    cleanupActiveSquads(w);
    settleSquads(w);
    const double gameSecs = dt * GAME_SECONDS_PER_REAL_SECOND;
    const int hoursPassed = advanceClock(gs.clockSeconds, dt);
    PROFILED("needsSystem", needsSystem(w, hoursPassed, gameSecs));
    ghostSystem(w, gameSecs);
    downedSystem(w);
    WakeChanges wakes = PROFILED("unconsciousSystem", unconsciousSystem(w, gameSecs));
    append(events(w).wakes.woke, std::move(wakes.woke));
    append(events(w).wakes.died, std::move(wakes.died));
    PROFILED("playDeadSystem", playDeadSystem(w));
    corpseDecaySystem(w, gameSecs);
    bonesDecaySystem(w, gameSecs);
    groundFoodSystem(w, gameSecs);
    append(events(w).gains, PROFILED("workSystem", workSystem(w, gameSecs, rng)));
    PROFILED("passiveCraftSystem", passiveCraftSystem(w, gameSecs));
    gs.claimDecayDue += gameSecs;
    if (gs.claimDecayDue >= GAME_SECONDS_PER_REAL_SECOND) {
        PROFILED("claimDecaySystem", claimDecaySystem(w, gs.claimDecayDue));
        PROFILED("outdoorDecaySystem", outdoorDecaySystem(w, gs.claimDecayDue));
        gs.claimDecayDue = 0.0;
    }
}
