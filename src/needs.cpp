// How hunger, rest and comfort change with the in-game time.
#include "systems_internal.h"

using namespace systems_detail;

void needsSystem(World& w, int hours, double gameSeconds) {
    for (Entity e : w.view<Needs>()) {
        if (w.has<Corpse>(e)) continue;   // a corpse needs nothing
        Needs* n = w.get<Needs>(e);
        const bool mob = w.has<Mob>(e);
        const double gameHours = gameSeconds / 3600.0;
        // The most Rested follows Vitality. A unit that was fully rested stays fully rested as it grows.
        if (const int maxR = maxRestedFor(statsOf(w, e)); maxR != n->maxRested) {
            const bool full = n->rested >= n->maxRested;
            n->maxRested = maxR;
            n->rested = full ? maxR : std::min(n->rested, maxR);
        }
        if (mob) {
            // A mob can't feed itself, so it has no hunger to lose (and none to starve from), and no Comfort. Its
            // Rested never falls by itself and rises a little every hour, more in a bed or bedroll.
            n->hunger = n->maxHunger;
            n->hurried = false;
            const Entity bed = occupiedProp(w, e);
            const Prop* bp = bed != NULL_ENTITY ? w.get<Prop>(bed) : nullptr;
            const double restGain = std::max(MOB_RESTED_GAIN_PER_HOUR, bp ? restedGainPerHour(bp->kind) : 0.0);
            driftNeed(n->rested, n->maxRested, n->restedPart, restGain, gameHours);
        } else {
            // A unit that moved at run pace in the hour that just ended pays double for its next tick. The mark is
            // used up here, so later hours cost the usual amount.
            int ticks = hours;
            if (hours > 0 && n->hurried) {
                ticks += 1;
                n->hurried = false;
            }
            tickNeeds(*n, ticks);
            // An Exile never goes hungry
            if (const Faction* fa = w.get<Faction>(e); fa && fa->id == FACTION_EXILE) n->hunger = n->maxHunger;

            // Rested rises lying in a bed or bedroll and falls while awake anywhere else; Comfort rises in a seat or
            // bed without a lock and falls anywhere else
            const Entity seat = occupiedProp(w, e);
            const Prop* sp = seat != NULL_ENTITY ? w.get<Prop>(seat) : nullptr;
            const double restGain = sp ? restedGainPerHour(sp->kind) : 0.0;
            const double comfortGain = sp ? comfortGainPerHour(sp->kind) : 0.0;
            const double restRate = restGain > 0.0 ? restGain : w.has<Unconscious>(e) ? 0.0 : -RESTED_LOSS_PER_HOUR;
            driftNeed(n->rested, n->maxRested, n->restedPart, restRate, gameHours);
            driftNeed(n->comfort, n->maxComfort, n->comfortPart, comfortGain > 0.0 ? comfortGain : -COMFORT_LOSS_PER_HOUR,
                      gameHours);
        }

        // An Unconscious unit does not heal: only Doom falling to 0 in a bed, or a rested one's wait running out, wakes it
        if (w.has<Unconscious>(e)) continue;
        const Combat* c = w.get<Combat>(e);
        regenHealth(*n, c && c->target != NULL_ENTITY, gameSeconds, mob);
    }
}
