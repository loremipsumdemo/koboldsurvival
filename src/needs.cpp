#include "systems_internal.h"

using namespace systems_detail;

void needsSystem(World& w, int hours, double gameSeconds) {
    for (ObjectId e : w.view<Needs>()) {
        if (w.has<Corpse>(e)) continue;
        Needs* n = w.get<Needs>(e);
        const bool mob = w.has<Mob>(e);
        const double gameHours = gameSeconds / 3600.0;
        if (mob) {
            n->hunger = n->maxHunger = 0;
            n->rested = n->maxRested = 0;
            n->comfort = n->maxComfort = 0;
            n->hurried = false;
        } else {
            if (const int maxR = maxRestedFor(statsOf(w, e)); maxR != n->maxRested) {
                const bool full = n->rested >= n->maxRested;
                n->maxRested = maxR;
                n->rested = full ? maxR : std::min(n->rested, maxR);
            }
            int ticks = hours;
            if (hours > 0 && n->hurried) {
                ticks += 1;
                n->hurried = false;
            }
            tickNeeds(*n, ticks);
            if (const Faction* fa = w.get<Faction>(e); fa && fa->id == FACTION_EXILE) n->hunger = n->maxHunger;

            const ObjectId seat = occupiedProp(w, e);
            const Prop* sp = seat != NO_OBJECT ? w.get<Prop>(seat) : nullptr;
            const double restGain = sp ? restedGainPerHour(sp->kind) : 0.0;
            const double comfortGain = sp ? comfortGainPerHour(sp->kind) : 0.0;
            const double restRate = restGain > 0.0 ? restGain : outCold(w, e) ? 0.0 : -RESTED_LOSS_PER_HOUR;
            driftNeed(n->rested, n->maxRested, n->restedPart, restRate, gameHours);
            driftNeed(n->comfort, n->maxComfort, n->comfortPart, comfortGain > 0.0 ? comfortGain : -COMFORT_LOSS_PER_HOUR,
                      gameHours);
        }

        if (outCold(w, e)) continue;
        const Combat* c = w.get<Combat>(e);
        regenHealth(*n, c && c->target != NO_OBJECT, gameSeconds, mob);
    }
}
