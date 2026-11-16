#include "light.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>
#include "building.h"
#include "calendar.h"
#include "chunk_source.h"
#include "claims.h"
#include "components.h"
#include "items.h"
#include "map_theme.h"
#include "mobs.h"
#include "prop_index.h"

namespace {
// The sky's share of daylight, 0 to 1, as the sky draws it (renderer_world.cpp puts the sun by this hour: the theme's fixed hour if it has one)
float daylightAt(double clockSeconds, const MapTheme* theme) {
    const float minutes = theme && !theme->dayCycle ? (float)theme->fixedHour * 60.0f : timeOfDayMinutes(clockSeconds);
    const float ang = (minutes / 1440.0f - 0.25f) * 6.2831853f;
    const float elev = std::sin(ang), side = std::cos(ang);
    const float len = std::sqrt(side * side * 0.16f + elev * elev + side * side * 0.81f);
    const float y = len > 0.0f ? elev / len : elev;
    const float t = std::clamp((y + 0.10f) / 0.30f, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

void light(LightGrid& g, float x, float z, float radius) {
    const int r = (int)std::ceil(radius);
    const int cx = tileOf(x), cz = tileOf(z);
    const float r2 = radius * radius;
    for (int dz = -r; dz <= r; ++dz) {
        for (int dx = -r; dx <= r; ++dx) {
            if ((float)(dx * dx + dz * dz) <= r2) g.lit.insert(tileKey(cx + dx, cz + dz));
        }
    }
}
}

float fireLightRadius(float fireSize) { return std::clamp(3.0f + 6.0f * fireSize, 3.0f, 7.0f); }

bool isTorchItem(int item) {
    const ItemDef* d = item > 0 ? findItem(item) : nullptr;
    return d && hasTag(*d, "torch");
}

void lightSystem(World& w, const Terrain& t, double clockSeconds, float dt) {
    LightGrid& g = w.resource<LightGrid>();
    g.since += dt;
    if (g.since < LIGHT_REBUILD_SECONDS) return;
    g.since = 0.0f;

    const std::shared_ptr<const ChunkSource> source = t.source();
    const MapTheme* theme = source ? source->theme() : nullptr;
    const float weatherSun = theme ? weatherLookOf(*theme).sun : 1.0f;
    g.darkness = std::clamp(1.0f - daylightAt(clockSeconds, theme) * std::min(weatherSun, 1.0f), 0.0f, 1.0f);

    g.lit.clear();
    std::vector<ObjectId> orphans;
    for (int k = 0; k < propKindCount(); ++k) {
        const PropInfo& info = propInfo((PropKind)k);
        if (!info.fire && !info.wallDecor) continue;
        for (ObjectId e : propsOfKind(w, (PropKind)k)) {
            if (w.has<Blueprint>(e)) continue;
            const Transform* tr = w.get<Transform>(e);
            if (!tr) continue;
            if (info.wallDecor) {
                // A wall-hung prop whose wall is gone falls with it
                const ObjectId wall = wallTileAt(w, tileOf(tr->x), tileOf(tr->z), storeyOf(w, e));
                if (wall == NO_OBJECT || w.has<Blueprint>(wall)) {
                    orphans.push_back(e);
                    continue;
                }
            }
            if (info.fire) light(g, tr->x, tr->z, fireLightRadius(info.fireSize));
        }
    }
    for (ObjectId u : w.view<Equipment, Transform>()) {
        const Equipment* q = w.get<Equipment>(u);
        if (!isTorchItem(q->items[GearSlot::MainHand]) && !isTorchItem(q->items[GearSlot::OffHand])) continue;
        const Needs* n = w.get<Needs>(u);
        if (n && n->hp <= 0) continue;
        const Transform* tr = w.get<Transform>(u);
        light(g, tr->x, tr->z, HELD_TORCH_RADIUS);
    }
    for (ObjectId e : orphans) breakBuilding(w, e, false);
}

bool tileLit(World& w, int tx, int tz) {
    return w.resource<LightGrid>().lit.count(tileKey(tx, tz)) != 0;
}

float worldDarkness(World& w) { return w.resource<LightGrid>().darkness; }

float darkAccuracyPenalty(World& w, ObjectId attacker, ObjectId defender) {
    const float dark = worldDarkness(w);
    if (dark <= 0.01f) return 0.0f;
    if (const Mob* m = w.get<Mob>(attacker); m && m->kind == MobKind::Bat) return 0.0f;
    const Transform* a = w.get<Transform>(attacker);
    const Transform* d = w.get<Transform>(defender);
    if (!a || !d) return 0.0f;
    if (tileLit(w, tileOf(a->x), tileOf(a->z)) || tileLit(w, tileOf(d->x), tileOf(d->z))) return 0.0f;
    return DARK_ACCURACY_PENALTY * dark;
}
