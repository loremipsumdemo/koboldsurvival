// Weight carried and worn, how encumbered that makes a unit, and how fast it walks (encumbrance.h)
#include "encumbrance.h"
#include "combat.h"
#include "inventory.h"
#include "items.h"
#include "mobs.h"
#include <algorithm>

namespace {
constexpr float ATHLETICS_PER_LEVEL = 0.005f;   // walk speed gained per Athletics level, as a share
}

float wornWeight(const Equipment& worn) {
    float total = 0.0f;
    for (int item : worn.items) {
        if (const ItemDef* d = findItem(item)) total += d->weight;
    }
    return total;
}

float carriedWeightOf(World& w, Entity e) {
    float total = 0.0f;
    if (const Inventory* inv = w.get<Inventory>(e)) total += carriedWeight(*inv);
    if (const Equipment* worn = w.get<Equipment>(e)) total += wornWeight(*worn);
    return total;
}

float encumbranceFor(float carried, const Stats& s) {
    const float limit = carryLimit(s);
    if (limit <= 0.0f) return 1.0f;
    return std::clamp(carried / limit, 0.0f, 1.0f);
}

float encumbranceOf(World& w, Entity e) {
    return encumbranceFor(carriedWeightOf(w, e), statsOf(w, e));
}

float walkSpeedOf(World& w, Entity e, bool running) {
    const Unit* u = w.get<Unit>(e);
    if (!u || isDowned(w, e)) return 0.0f;
    // Running is the player's pace for its squads, so a red unit never runs
    if (!w.has<Commandable>(e)) running = false;

    const Stats s = statsOf(w, e);
    const Combat* c = w.get<Combat>(e);
    const float stamina = c ? c->stamina : 1.0f;
    const float load = encumbranceOf(w, e);
    float speed = u->speed * (1.0f + ATHLETICS_PER_LEVEL * (levelOf(s, StatId::Athletics) - 1)) * (1.0f - load);
    if (isSneaking(w, e)) {
        running = false;          // a sneaking unit never runs
        speed *= SNEAK_SPEED;
    }
    if (running) speed *= 1.0f + RUN_SPEED_BONUS + RUN_ATHLETICS_PER_LEVEL * (levelOf(s, StatId::Athletics) - 1);
    if (stamina <= 0.0f) speed *= 0.5f;
    // A boss walking home after giving up a chase (and taking no damage) hurries
    if (const SpawnHome* home = w.get<SpawnHome>(e); home && home->returning) {
        if (const Mob* m = w.get<Mob>(e); m && mobInfo(m->kind).category == MobCategory::Boss) {
            speed *= BOSS_RETURN_SPEED_FACTOR;
        }
    }
    return std::max(0.0f, speed);
}

float strengthXpWhileWalking(float encumbrance, float seconds) {
    if (encumbrance < ENCUMBRANCE_XP_FROM || seconds <= 0.0f) return 0.0f;
    const float share = (encumbrance - ENCUMBRANCE_XP_FROM) / (1.0f - ENCUMBRANCE_XP_FROM);
    return 0.5f * share * seconds;
}
