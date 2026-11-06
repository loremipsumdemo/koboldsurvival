#include "encumbrance.h"
#include "combat.h"
#include "inventory.h"
#include "items.h"
#include "mobs.h"
#include <algorithm>

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
    if (!w.has<Commandable>(e)) running = false;

    const float load = encumbranceOf(w, e);
    float speed = u->speed * (1.0f - load);
    if (isSneaking(w, e)) {
        running = false;
        speed *= SNEAK_SPEED;
    }
    if (running) speed *= 1.0f + RUN_SPEED_BONUS;
    if (const SpawnHome* home = w.get<SpawnHome>(e); home && home->returning) {
        if (const Mob* m = w.get<Mob>(e); m && mobInfo(m->kind).category == MobCategory::Boss) {
            speed *= BOSS_RETURN_SPEED_FACTOR;
        }
    }
    return std::max(0.0f, speed);
}
