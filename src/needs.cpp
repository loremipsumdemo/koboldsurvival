#include "systems_internal.h"
#include "npc_camps.h"
#include "schedule.h"

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
            if (n->maxRested != RESTED_MAX) {   // a save from when the bar followed Vitality
                const bool full = n->rested >= n->maxRested;
                n->maxRested = RESTED_MAX;
                n->rested = full ? RESTED_MAX : std::min(n->rested, RESTED_MAX);
            }
            int ticks = hours;
            if (hours > 0 && n->hurried) {
                ticks += 1;
                n->hurried = false;
            }
            // Lying in a bed or bedroll: asleep, so hunger comes at half the rate
            const ObjectId bedNow = occupiedProp(w, e);
            const bool asleep = bedNow != NO_OBJECT && w.has<Prop>(bedNow) && propInfo(w.get<Prop>(bedNow)->kind).sleeping;
            tickNeeds(*n, ticks, asleep);
            // Other factions' units never get hungrier than this, unless caged
            if (n->maxHunger > 0 && factionOf(w, e) != FACTION_PLAYER && !w.has<Commandable>(e) && !inPrisonerCage(w, e)) {
                n->hunger = std::max(n->hunger, (n->maxHunger * HUNGER_FLOOR_PERCENT + 99) / 100);
            }
            if (const Faction* fa = w.get<Faction>(e); fa && fa->id == FACTION_FACTIONLESS) n->hunger = n->maxHunger;

            const ObjectId seat = occupiedProp(w, e);
            const Prop* sp = seat != NO_OBJECT ? w.get<Prop>(seat) : nullptr;
            const double restGain = sp ? restedGainPerHour(sp->kind) : 0.0;
            double comfortGain = sp ? comfortGainPerHour(sp->kind) : 0.0;
            // Chatting with someone gives comfort too
            if (const Behaviors* bh = w.get<Behaviors>(e); bh && bh->busy && bh->began && bh->step.kind == BehaviorKind::Socialize) {
                comfortGain = std::max(comfortGain, SOCIAL_COMFORT_PER_HOUR);
            }
            if (const Behaviors* bh = w.get<Behaviors>(e); bh && bh->busy && bh->began) {
                if (bh->step.kind == BehaviorKind::Play) comfortGain = std::max(comfortGain, PLAY_COMFORT_PER_HOUR);
                if (bh->step.kind == BehaviorKind::Dance) comfortGain = std::max(comfortGain, DANCE_COMFORT_PER_HOUR);
            }
            // Sitting down (the task) keeps a unit from tiring
            const Behaviors* sit = w.get<Behaviors>(e);
            const bool sitting = sit && sit->busy && sit->step.kind == BehaviorKind::SitDown && seat != NO_OBJECT;
            const double restRate = restGain > 0.0 ? restGain : (outCold(w, e) || sitting) ? 0.0 : -restedLossPerHour(n->maxRested);
            driftNeed(n->rested, n->maxRested, n->restedPart, restRate, gameHours);
            driftNeed(n->comfort, n->maxComfort, n->comfortPart, comfortGain > 0.0 ? comfortGain : -COMFORT_LOSS_PER_HOUR,
                      gameHours);
        }

        if (outCold(w, e)) continue;
        const Combat* c = w.get<Combat>(e);
        regenHealth(*n, c && c->target != NO_OBJECT, gameSeconds, mob);
    }
}
