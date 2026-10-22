#include "systems.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace {

constexpr float UNIT_CLEARANCE = 0.2f;   // extra gap kept between units

// Plans a new route for a squad's center. Only the center paths; members follow slots.
void planSquad(World& w, Terrain& t, Entity squadE, Vec2 target) {
    Squad* sq = w.get<Squad>(squadE);
    sq->waypoints = t.findPath({ sq->cx, sq->cz }, target);
    sq->next = 0;
    sq->arrived = sq->waypoints.empty();
}

// Plans a single unit's own route to a ground point
void planSolo(World& w, Terrain& t, Entity e, Vec2 target) {
    SoloMove* so = w.get<SoloMove>(e);
    Transform* tr = w.get<Transform>(e);
    so->waypoints = t.findPath({ tr->x, tr->z }, target);
    so->next = 0;
}

// Formation spacing grows with the squad size, up to the full spacing at 10 units.
// Small squads stay compact so they don't spread out around the click point.
float slotSpacing(int size) {
    float scale = std::min(1.0f, std::sqrt(size / 10.0f));
    return std::max(MIN_SLOT_SPACING, SLOT_SPACING * scale);
}

} // namespace

namespace {

constexpr int SPAWN_SQUARE = 16;           // the spawn needs a walkable square this many tiles across
constexpr int SPAWN_SEARCH_STEP = 32;      // spacing of the spawn candidates, in tiles
constexpr int SPAWN_LOAD_RADIUS = 4;       // chunks read in around the spawn, so units can be placed on them
constexpr float ENEMY_RANGE = 128.0f;      // red units are placed within this many tiles of the spawn
constexpr float ENEMY_MIN_RANGE = 12.0f;   // and no closer than this, so the start is clear
constexpr float WAGON_RANGE = 40.0f;       // the wagon is placed within this many tiles of the spawn

bool walkableSquare(const Terrain& t, int x0, int z0, int n) {
    for (int z = z0; z < z0 + n; ++z) {
        for (int x = x0; x < x0 + n; ++x) {
            if (!t.walkable(x, z)) return false;
        }
    }
    return true;
}

// Finds where the player starts: the walkable square nearest the middle of the level. In an open
// world the chunks are read in as the search reaches them, so the whole level can be searched.
// Returns the centre of the square, in world units.
Vec2 findSpawn(Terrain& t) {
    const int size = t.size();
    const int mid = size / 2;
    struct Candidate { int x, z; long long d2; };
    std::vector<Candidate> cands;
    for (int z = 0; z + SPAWN_SQUARE <= size; z += SPAWN_SEARCH_STEP) {
        for (int x = 0; x + SPAWN_SQUARE <= size; x += SPAWN_SEARCH_STEP) {
            const long long dx = x - mid, dz = z - mid;
            cands.push_back({ x, z, dx * dx + dz * dz });
        }
    }
    std::stable_sort(cands.begin(), cands.end(),
                     [](const Candidate& a, const Candidate& b) { return a.d2 < b.d2; });

    const float half = SPAWN_SQUARE * 0.5f;
    for (const Candidate& c : cands) {
        if (t.streamed()) t.stream({ { c.x + half, c.z + half } }, 2, 0);
        if (walkableSquare(t, c.x, c.z, SPAWN_SQUARE)) return { c.x + half, c.z + half };
    }
    // No open square. A whole map in memory can still fall back to any walkable tile.
    if (!t.streamed()) {
        for (int z = 0; z < size; ++z) {
            for (int x = 0; x < size; ++x) {
                if (t.walkable(x, z)) return { x + 0.5f, z + 0.5f };
            }
        }
    }
    return { (float)mid, (float)mid };
}

} // namespace

void setupWorld(World& w, Terrain& t, Rng& rng) {
    const Vec2 spawn = findSpawn(t);
    // Read in the ground around the spawn, so the units and red units can stand on it
    if (t.streamed()) t.stream({ spawn }, SPAWN_LOAD_RADIUS, 0);
    const int size = t.size();
    auto tileAt = [&](float v) { return std::clamp((int)std::floor(v), 0, size - 1); };

    // Units: a block centred on the spawn, each moved onto free ground
    for (int i = 0; i < UNIT_COUNT; ++i) {
        const Vec2 p = t.snap({ spawn.x - 3.0f + (i % SQUAD_COLUMNS) * 1.5f,
                                spawn.z - 3.0f + (i / SQUAD_COLUMNS) * 1.5f });
        Entity u = w.create();
        w.add(u, Transform{ p.x, t.heightAt(p.x, p.z), p.z });
        w.add(u, Unit{});
        w.add(u, Commandable{});
        w.add(u, Inventory{});
        w.add(u, Name{});
        w.add(u, UnitSprite{ rng.range(1, 38) });
    }

    // Red units: same kind of unit, but they stand still and can't be commanded. They are placed on
    // walkable tiles within range of the spawn, but not close to it. The attempt limit stops a small
    // map from looping forever when it has fewer free tiles than red units.
    std::vector<GridPos> enemyTiles;
    for (int attempt = 0; attempt < 20000 && (int)enemyTiles.size() < ENEMY_COUNT; ++attempt) {
        GridPos g{ rng.range(tileAt(spawn.x - ENEMY_RANGE), tileAt(spawn.x + ENEMY_RANGE)),
                   rng.range(tileAt(spawn.z - ENEMY_RANGE), tileAt(spawn.z + ENEMY_RANGE)) };
        if (!t.walkable(g.x, g.y)) continue;
        const float x = g.x + 0.5f, z = g.y + 0.5f;
        const float dx = x - spawn.x, dz = z - spawn.z;
        if (dx * dx + dz * dz < ENEMY_MIN_RANGE * ENEMY_MIN_RANGE) continue;
        if (dx * dx + dz * dz > ENEMY_RANGE * ENEMY_RANGE) continue;
        bool taken = std::any_of(enemyTiles.begin(), enemyTiles.end(),
                                 [&](GridPos k) { return k == g; });
        if (taken) continue;

        enemyTiles.push_back(g);
        Entity e = w.create();
        w.add(e, Transform{ x, t.heightAt(x, z), z });
        w.add(e, Unit{});
        w.add(e, Hostile{});
        w.add(e, Name{});
        w.add(e, UnitSprite{ rng.range(1, 38) });
    }

    // One wagon on free ground near the spawn. It is decorative and does not block movement.
    for (int attempt = 0; attempt < 20000; ++attempt) {
        GridPos g{ rng.range(tileAt(spawn.x - WAGON_RANGE), tileAt(spawn.x + WAGON_RANGE)),
                   rng.range(tileAt(spawn.z - WAGON_RANGE), tileAt(spawn.z + WAGON_RANGE)) };
        if (!t.walkable(g.x, g.y)) continue;
        const float x = g.x + 0.5f, z = g.y + 0.5f;
        Entity wagon = w.create();
        w.add(wagon, Transform{ x, t.heightAt(x, z), z });
        w.add(wagon, Wagon{});
        break;
    }
}

void issueMoveOrder(World& w, Terrain& t, const std::vector<Entity>& units, Vec2 clickPoint) {
    if (units.empty()) return;

    // A single unit walks its own route, with no formation
    if (units.size() == 1) {
        Entity u = units[0];
        w.remove<SquadMember>(u);
        w.add(u, SoloMove{});
        planSolo(w, t, u, clickPoint);
        return;
    }

    // The formation center starts at the average position of the selected units
    float cx = 0.0f, cz = 0.0f;
    for (Entity u : units) {
        Transform* tr = w.get<Transform>(u);
        cx += tr->x;
        cz += tr->z;
    }
    cx /= units.size();
    cz /= units.size();

    Squad s;
    s.cx = cx;
    s.cz = cz;
    s.size = (int)units.size();
    Entity sq = w.create();
    w.add(sq, s);

    // Each selected unit joins the new squad. Overwriting SquadMember drops any old squad.
    for (size_t i = 0; i < units.size(); ++i) {
        w.remove<SoloMove>(units[i]);
        w.add(units[i], SquadMember{ sq, (int)i });
    }

    planSquad(w, t, sq, clickPoint);
}

void squadSystem(World& w, float dt) {
    for (Entity e : w.view<Squad>()) {
        Squad* sq = w.get<Squad>(e);
        if (sq->arrived) continue;

        Vec2 wp = sq->waypoints[sq->next];
        float tx = wp.x, tz = wp.z;
        float dx = tx - sq->cx, dz = tz - sq->cz;
        float d = std::sqrt(dx * dx + dz * dz);
        float step = sq->speed * dt;

        if (d > 1e-4f) {
            sq->headX = dx / d;
            sq->headZ = dz / d;
        }

        if (d <= step) {
            sq->cx = tx;
            sq->cz = tz;
            if (++sq->next >= sq->waypoints.size()) sq->arrived = true;
        } else {
            sq->cx += dx / d * step;
            sq->cz += dz / d * step;
        }
    }
}

void steeringSystem(World& w, Terrain& t, float dt) {
    std::vector<Entity> units = w.view<Unit, Transform, Commandable>();
    std::vector<std::pair<float, float>> vel(units.size(), { 0.0f, 0.0f });

    // Pass 1: each unit's desired velocity, from its slot plus separation.
    // Positions are read here but not changed, so the order of units doesn't matter.
    for (size_t i = 0; i < units.size(); ++i) {
        Entity e = units[i];
        Transform* tr = w.get<Transform>(e);
        Unit* u = w.get<Unit>(e);

        float tx = tr->x, tz = tr->z;   // default: stay put
        if (SquadMember* sm = w.get<SquadMember>(e)) {
            if (Squad* sq = w.get<Squad>(sm->squad)) {
                // Slots are centred on the squad centre, so the group's middle sits on
                // the click point: columns across the heading, rows behind it.
                int cols = std::min(SQUAD_COLUMNS, sq->size);
                int rows = (sq->size + cols - 1) / cols;
                float colOff = sm->slot % cols - (cols - 1) * 0.5f;
                float rowOff = sm->slot / cols - (rows - 1) * 0.5f;
                float spacing = slotSpacing(sq->size);

                float rightX = -sq->headZ, rightZ = sq->headX;
                float backX = -sq->headX, backZ = -sq->headZ;
                tx = sq->cx + (rightX * colOff + backX * rowOff) * spacing;
                tz = sq->cz + (rightZ * colOff + backZ * rowOff) * spacing;

                // Don't ask units to stand inside walls
                Vec2 st = t.snap({ tx, tz });
                tx = st.x;
                tz = st.z;
            }
        } else if (SoloMove* so = w.get<SoloMove>(e)) {
            // Move past any waypoints the unit has already reached
            while (so->next < so->waypoints.size()) {
                Vec2 wp = so->waypoints[so->next];
                float wx = wp.x - tr->x, wz = wp.z - tr->z;
                // Corners can be passed loosely; the final point is where the unit should stop
                bool last = so->next + 1 == so->waypoints.size();
                float reach = last ? 0.05f : 0.2f;
                if (wx * wx + wz * wz > reach * reach) break;
                ++so->next;
            }
            if (so->next < so->waypoints.size()) {
                tx = so->waypoints[so->next].x;
                tz = so->waypoints[so->next].z;
            }
        }

        float dx = tx - tr->x, dz = tz - tr->z;
        float dist = std::sqrt(dx * dx + dz * dz);
        float vx = 0.0f, vz = 0.0f;
        if (dist > 0.05f) {
            float sp = std::min(u->speed, dist * 2.5f);   // slow down near the target
            vx = dx / dist * sp;
            vz = dz / dist * sp;
        }

        // Separation: push away from any unit that is too close
        for (size_t j = 0; j < units.size(); ++j) {
            if (j == i) continue;
            Transform* o = w.get<Transform>(units[j]);
            Unit* ou = w.get<Unit>(units[j]);
            float sx = tr->x - o->x, sz = tr->z - o->z;
            float minD = u->radius + ou->radius + UNIT_CLEARANCE;
            float d2 = sx * sx + sz * sz;
            if (d2 < minD * minD) {
                float d = std::sqrt(d2);
                if (d < 1e-4f) { sx = 1.0f; sz = 0.0f; d = 1.0f; }
                float push = (minD - d) * 8.0f;
                vx += sx / d * push;
                vz += sz / d * push;
            }
        }
        vel[i] = { vx, vz };
    }

    // Pass 2: apply movement and slide along walls
    for (size_t i = 0; i < units.size(); ++i) {
        Transform* tr = w.get<Transform>(units[i]);

        float nx = tr->x + vel[i].first * dt;
        float nz = tr->z + vel[i].second * dt;
        // Try each axis separately so units slide along walls instead of stopping
        if (t.isFree(nx, tr->z)) tr->x = nx;
        if (t.isFree(tr->x, nz)) tr->z = nz;
        tr->y = t.heightAt(tr->x, tr->z);
    }
}

void cleanupSquads(World& w) {
    std::vector<Entity> used;
    for (Entity e : w.view<SquadMember>()) used.push_back(w.get<SquadMember>(e)->squad);

    for (Entity sq : w.view<Squad>()) {
        if (std::find(used.begin(), used.end(), sq) == used.end()) w.destroy(sq);
    }
}
