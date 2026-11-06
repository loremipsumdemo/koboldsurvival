#include "experience.h"
#include "combat.h"
#include "language.h"
#include "mobs.h"
#include <cmath>

Stats statsOf(World& w, Entity e) {
    const Stats* s = w.get<Stats>(e);
    return s ? *s : defaultStats();
}

void grantXp(World& w, Entity e, StatId stat, float amount, const char* whyKey) {
    if (amount <= 0.0f || idx(stat) < 0 || idx(stat) >= STAT_COUNT) return;
    Stats* st = w.get<Stats>(e);
    if (!st) return;
    if (w.has<Mob>(e) && !mobHasStat(stat)) return;

    XpBuffer& buf = w.has<XpBuffer>(e) ? *w.get<XpBuffer>(e) : w.add(e, XpBuffer{});
    const float total = buf.frac[idx(stat)] + amount;
    const float whole = std::floor(total);
    buf.frac[idx(stat)] = total - whole;
    if (whole < 1.0f) return;
    const int before = levelOf(*st, stat);
    addXp(*st, stat, (int)whole);
    if (levelOf(*st, stat) > before && w.has<Commandable>(e)) queueSound(Sfx::LevelUp);
    if (whyKey) {
        queueLog(langf("MISC_GAINS_XP", { nameOf(w, e), std::to_string((int)whole), statName(stat), lang(whyKey) }), LogKind::Xp, npcOnly(w, e));
    }
}
