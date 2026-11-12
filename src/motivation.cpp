#include "motivation.h"
#include <algorithm>
#include "behavior.h"
#include "building.h"
#include "components.h"

double motivationOf(World& w, int faction) {
    const Motivation& m = w.resource<Motivation>();
    const auto it = m.value.find(faction);
    return it == m.value.end() ? 0.0 : it->second;
}

double motivationMax(World& w, int faction) {
    const Motivation& m = w.resource<Motivation>();
    const auto it = m.buildings.find(faction);
    return MOTIVATION_BASE_MAX + MOTIVATION_PER_BUILDING * (it == m.buildings.end() ? 0 : it->second);
}

void setMotivation(World& w, int faction, double value) {
    w.resource<Motivation>().value[faction] = std::max(0.0, value);
}

bool isMotivated(World& w, int faction) {
    return motivationOf(w, faction) > MOTIVATION_FAST;
}

float motivationSpeedFactor(World& w, ObjectId unit) {
    Motivation& m = w.resource<Motivation>();
    if (m.value.empty()) return 1.0f;
    const Faction* f = w.get<Faction>(unit);
    const int faction = f ? f->id : FACTION_PLAYER;
    const auto it = m.value.find(faction);
    if (it == m.value.end() || it->second <= MOTIVATION_FAST) return 1.0f;
    const Transform* t = w.get<Transform>(unit);
    if (!t || !claimAt(m.claims, t->x, t->z, faction)) return 1.0f;
    return 1.0f + MOTIVATION_SPEED_BONUS;
}

void motivationSystem(World& w, float dt, double gameSeconds) {
    Motivation& m = w.resource<Motivation>();
    bool dancing = false;
    for (ObjectId u : w.view<Behaviors>()) {
        const Behaviors* b = w.get<Behaviors>(u);
        dancing = dancing || (b->busy && b->began && b->step.kind == BehaviorKind::Dance);
    }
    if (!dancing && m.value.empty()) return;   // nothing to count, nothing to tick down
    m.lookIn -= dt;
    if (m.lookIn <= 0.0f) {
        m.lookIn = MOTIVATION_LOOK_SECONDS;
        m.claims = claimsOf(w);
        m.buildings.clear();
        for (const BuildingCluster& b : buildingClusters(w, buildingMap(w))) {
            if (b.complete) ++m.buildings[b.owner];
        }
    }
    const double hours = gameSeconds / 3600.0;
    for (ObjectId u : w.view<Behaviors>()) {
        const Behaviors* b = w.get<Behaviors>(u);
        if (!b->busy || !b->began || b->step.kind != BehaviorKind::Dance) continue;
        const Faction* f = w.get<Faction>(u);
        m.value[f ? f->id : FACTION_PLAYER] += hours * MOTIVATION_POINTS_PER_DANCER_HOUR;
    }
    for (auto& [faction, value] : m.value) {
        if (value > 0.0) value -= hours * MOTIVATION_DECAY_PER_HOUR;
        value = std::clamp(value, 0.0, motivationMax(w, faction));
    }
    // Rising up to the threshold: a chime, for the player's faction (the first look only notes where it is, so loading a game is silent)
    for (const auto& [faction, value] : m.value) {
        const bool now = value > MOTIVATION_FAST;
        const auto was = m.over.find(faction);
        if (faction == FACTION_PLAYER && now && was != m.over.end() && !was->second) queueSound(w, Sfx::MotivationChime);
        m.over[faction] = now;
    }
}
