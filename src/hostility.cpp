// Which units fight which: factions, provocations and the Hostile and Neutral markers (hostility.h)
#include "hostility.h"
#include "combat.h"
#include "factions.h"
#include "language.h"
#include "mobs.h"
#include <algorithm>

int factionOf(World& w, Entity e) {
    if (const Faction* f = w.get<Faction>(e)) return f->id;
    // A unit with no faction goes by its side: yours, a hostile (Red) or a neutral (Green)
    if (w.has<Commandable>(e)) return FACTION_PLAYER;
    return w.has<Hostile>(e) ? FACTION_RED : FACTION_GREEN;
}

namespace {
bool provokedBy(World& w, Entity e, int faction) {
    const Provoked* p = w.get<Provoked>(e);
    if (!p) return false;
    for (const Provoked::Entry& en : p->against) {
        if (en.faction == faction && en.secondsLeft > 0.0f) return true;
    }
    return false;
}
} // namespace

bool unitsHostile(World& w, Entity a, Entity b) {
    if (a == b) return false;
    const int fa = factionOf(w, a), fb = factionOf(w, b);
    if (fa == fb) return false;
    return factionsHostile(fa, fb) || provokedBy(w, a, fb) || provokedBy(w, b, fa);
}

bool hostileToPlayer(World& w, Entity e) {
    if (w.has<Commandable>(e)) return false;
    const int f = factionOf(w, e);
    return f != FACTION_PLAYER && (factionsHostile(f, FACTION_PLAYER) || provokedBy(w, e, FACTION_PLAYER));
}

void provoke(World& w, Entity unit, int faction) {
    if (!w.has<Unit>(unit) || factionOf(w, unit) == faction) return;
    Provoked& p = w.has<Provoked>(unit) ? *w.get<Provoked>(unit) : w.add(unit, Provoked{});
    for (Provoked::Entry& en : p.against) {
        if (en.faction == faction) {
            en.secondsLeft = PROVOKED_SECONDS;
            return;
        }
    }
    p.against.push_back(Provoked::Entry{ faction, PROVOKED_SECONDS });
}

void factionSystem(World& w, float dt) {
    const std::vector<Entity> units = w.view<Unit, Transform>();
    for (Entity u : w.view<Provoked>()) {
        Provoked* p = w.get<Provoked>(u);
        const Transform* tu = w.get<Transform>(u);
        // Out cold, it sees nothing; awake, any standing unit of the faction within sight keeps the grudge fresh
        const bool awake = tu && !w.has<Unconscious>(u);
        for (Provoked::Entry& en : p->against) {
            bool seen = false;
            for (Entity o : units) {
                if (!awake || o == u || factionOf(w, o) != en.faction || w.has<Unconscious>(o)) continue;
                const Transform* to = w.get<Transform>(o);
                const float dx = to->x - tu->x, dz = to->z - tu->z;
                if (dx * dx + dz * dz <= PROVOKED_SIGHT_RANGE * PROVOKED_SIGHT_RANGE) {
                    seen = true;
                    break;
                }
            }
            en.secondsLeft = seen ? PROVOKED_SECONDS : en.secondsLeft - dt;
        }
        p->against.erase(std::remove_if(p->against.begin(), p->against.end(),
                                        [](const Provoked::Entry& en) { return en.secondsLeft <= 0.0f; }),
                         p->against.end());
        if (p->against.empty()) w.remove<Provoked>(u);
    }
    for (Entity u : w.view<Unit>()) {
        if (w.has<Commandable>(u)) {
            w.remove<Hostile>(u);
            w.remove<Neutral>(u);
            continue;
        }
        const bool hostile = hostileToPlayer(w, u);
        if (hostile && !w.has<Hostile>(u)) {
            const bool wasNeutral = w.has<Neutral>(u);
            w.remove<Neutral>(u);
            w.add(u, Hostile{});
            if (wasNeutral && !w.has<Unconscious>(u)) {
                queueSound(Sfx::TurnHostile);
                queueLog(langf("MISC_TURNS_HOSTILE", { nameOf(w, u) }), LogKind::Seen);
            }
        } else if (!hostile && !w.has<Neutral>(u)) {
            const bool wasHostile = w.has<Hostile>(u);
            w.remove<Hostile>(u);
            w.add(u, Neutral{});
            // It lost sight of your units long enough to calm down: it drops its fight with them
            if (wasHostile && !w.has<Unconscious>(u)) {
                queueLog(langf("MISC_CALMS_DOWN", { nameOf(w, u) }), LogKind::Seen);
                if (Combat* c = w.get<Combat>(u); c && c->target != NULL_ENTITY && w.has<Commandable>(c->target)) {
                    c->target = NULL_ENTITY;
                }
            }
        }
    }
}
