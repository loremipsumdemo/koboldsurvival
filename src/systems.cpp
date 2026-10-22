#include "systems.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace {

constexpr float UNIT_CLEARANCE = 0.2f;   // extra gap kept between units
constexpr float ENTER_RADIUS = 0.6f;     // how close a unit must be to step through the door
constexpr float KEY_RADIUS = 0.9f;       // how close a unit must be to grab a key

// Centre of a tile, for the building, which still sits on the tile grid
Vec2 tileCenter(GridPos g) {
    return { g.x + 0.5f, g.y + 0.5f };
}

// Plans a new route for a squad's center. Only the center paths; members follow slots.
void planSquad(World& w, Terrain& t, Entity squadE, Vec2 target, bool wantsDoor) {
    Squad* sq = w.get<Squad>(squadE);
    sq->waypoints = t.findPath({ sq->cx, sq->cz }, target);
    sq->next = 0;
    sq->arrived = sq->waypoints.empty();
    sq->wantsDoor = wantsDoor;
    sq->enterDoor = false;
}

// Plans a single unit's own route to a ground point
void planSolo(World& w, Terrain& t, Entity e, Vec2 target, bool wantsDoor) {
    SoloMove* so = w.get<SoloMove>(e);
    Transform* tr = w.get<Transform>(e);
    so->waypoints = t.findPath({ tr->x, tr->z }, target);
    so->next = 0;
    so->wantsDoor = wantsDoor;
    so->enterDoor = false;
}

// Formation spacing grows with the squad size, up to the full spacing at 10 units.
// Small squads stay compact so they don't spread out around the click point.
float slotSpacing(int size) {
    float scale = std::min(1.0f, std::sqrt(size / 10.0f));
    return std::max(MIN_SLOT_SPACING, SLOT_SPACING * scale);
}

} // namespace

Door* findDoor(World& w) {
    auto doors = w.view<Door>();
    return doors.empty() ? nullptr : w.get<Door>(doors.front());
}

void setupWorld(World& w, Terrain& t, Rng& rng) {
    // Building: 3x3 footprint, with the entrance on the south-centre tile
    Door door;
    door.origin = { rng.range(10, t.size() - 5), rng.range(10, t.size() - 5) };
    door.entrance = { door.origin.x + 1, door.origin.y + 2 };
    door.front = { door.origin.x + 1, door.origin.y + 3 };
    for (int dy = 0; dy < 3; ++dy) {
        for (int dx = 0; dx < 3; ++dx) {
            t.setWalkable(door.origin.x + dx, door.origin.y + dy, false);
        }
    }
    Entity d = w.create();
    w.add(d, door);

    // Units, in a block in the north-west corner
    for (int i = 0; i < UNIT_COUNT; ++i) {
        float x = 1.5f + (i % SQUAD_COLUMNS) * 1.5f;
        float z = 1.5f + (i / SQUAD_COLUMNS) * 1.5f;
        Entity u = w.create();
        w.add(u, Transform{ x, t.heightAt(x, z), z });
        w.add(u, Unit{});
        w.add(u, Commandable{});
        w.add(u, Inventory{});
        w.add(u, Name{});
        w.add(u, UnitSprite{ rng.range(1, 38) });
    }

    // Red units: same kind of unit, but they stand still and can't be commanded.
    // They're placed on walkable tiles outside the spawn corner.
    std::vector<GridPos> enemyTiles;
    while ((int)enemyTiles.size() < ENEMY_COUNT) {
        GridPos g{ rng.range(0, t.size() - 1), rng.range(0, t.size() - 1) };
        if (!t.walkable(g.x, g.y)) continue;
        if (g.x < 9 && g.y < 9) continue;
        bool taken = std::any_of(enemyTiles.begin(), enemyTiles.end(),
                                 [&](GridPos k) { return k == g; });
        if (taken) continue;

        enemyTiles.push_back(g);
        float x = g.x + 0.5f, z = g.y + 0.5f;
        Entity e = w.create();
        w.add(e, Transform{ x, t.heightAt(x, z), z });
        w.add(e, Unit{});
        w.add(e, Hostile{});
        w.add(e, Name{});
        w.add(e, UnitSprite{ rng.range(1, 38) });
    }

    // Keys, scattered across the map on walkable tiles, away from the spawn corner
    std::vector<GridPos> keyTiles;
    while ((int)keyTiles.size() < KEY_COUNT) {
        GridPos g{ rng.range(0, t.size() - 1), rng.range(0, t.size() - 1) };
        if (!t.walkable(g.x, g.y)) continue;
        if (g.x < 9 && g.y < 9) continue;
        bool duplicate = std::any_of(keyTiles.begin(), keyTiles.end(),
                                     [&](GridPos k) { return k == g; });
        if (duplicate) continue;

        keyTiles.push_back(g);
        float x = g.x + 0.5f, z = g.y + 0.5f;
        Entity k = w.create();
        w.add(k, Transform{ x, t.heightAt(x, z), z });
        w.add(k, Key{});
    }

    // One wagon on a free tile. It is decorative and does not block movement.
    GridPos wg;
    do {
        wg = { rng.range(0, t.size() - 1), rng.range(0, t.size() - 1) };
    } while (!t.walkable(wg.x, wg.y) || (wg.x < 9 && wg.y < 9));
    float wx = wg.x + 0.5f, wz = wg.y + 0.5f;
    Entity wagon = w.create();
    w.add(wagon, Transform{ wx, t.heightAt(wx, wz), wz });
    w.add(wagon, Wagon{});
}

void issueMoveOrder(World& w, Terrain& t, const std::vector<Entity>& units,
                    Vec2 clickPoint, bool clickedDoor) {
    if (units.empty()) return;

    Door* door = findDoor(w);
    bool wantsDoor = clickedDoor && door != nullptr;
    Vec2 target = clickPoint;
    if (wantsDoor) target = tileCenter(door->unlocked ? door->entrance : door->front);

    // A single unit walks its own route, with no formation
    if (units.size() == 1) {
        Entity u = units[0];
        w.remove<SquadMember>(u);
        w.add(u, SoloMove{});
        planSolo(w, t, u, target, wantsDoor);
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
        w.add(units[i], SquadMember{ sq, (int)i, {}, 0, false });
    }

    planSquad(w, t, sq, target, wantsDoor);
}

void squadSystem(World& w, float dt) {
    Door* door = findDoor(w);

    for (Entity e : w.view<Squad>()) {
        Squad* sq = w.get<Squad>(e);

        if (!sq->arrived) {
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

        sq->enterDoor = sq->wantsDoor && sq->arrived && door && door->unlocked;
    }
}

void steeringSystem(World& w, Terrain& t, GameState& gs, float dt) {
    Door* door = findDoor(w);
    std::vector<Entity> units = w.view<Unit, Transform, Commandable>();
    std::vector<std::pair<float, float>> vel(units.size(), { 0.0f, 0.0f });

    // Units going through the door don't push each other apart, so they can queue into the gap
    auto entering = [&](Entity e) {
        SquadMember* sm = w.get<SquadMember>(e);
        Squad* sq = sm ? w.get<Squad>(sm->squad) : nullptr;
        SoloMove* so = w.get<SoloMove>(e);
        return (sq && sq->enterDoor) || (so && so->enterDoor);
    };

    // Pass 1: each unit's desired velocity, from its slot plus separation.
    // Positions are read here but not changed, so the order of units doesn't matter.
    for (size_t i = 0; i < units.size(); ++i) {
        Entity e = units[i];
        Transform* tr = w.get<Transform>(e);
        Unit* u = w.get<Unit>(e);

        float tx = tr->x, tz = tr->z;   // default: stay put
        if (SquadMember* sm = w.get<SquadMember>(e)) {
            if (Squad* sq = w.get<Squad>(sm->squad)) {
                if (sq->enterDoor && door) {
                    // Walk the navmesh to the entrance, rather than straight at it through the wall
                    if (!sm->routed) {
                        sm->doorRoute = t.findPath({ tr->x, tr->z }, tileCenter(door->entrance));
                        sm->doorNext = 0;
                        sm->routed = true;
                    }
                    while (sm->doorNext < sm->doorRoute.size()) {
                        Vec2 wp = sm->doorRoute[sm->doorNext];
                        float wx = wp.x - tr->x, wz = wp.z - tr->z;
                        if (wx * wx + wz * wz > 0.2f * 0.2f) break;
                        ++sm->doorNext;
                    }
                    Vec2 goal = sm->doorNext < sm->doorRoute.size()
                              ? sm->doorRoute[sm->doorNext] : tileCenter(door->entrance);
                    tx = goal.x;
                    tz = goal.z;
                } else {
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

            so->enterDoor = so->wantsDoor && so->next >= so->waypoints.size()
                         && door && door->unlocked;
            if (so->enterDoor) {
                tx = door->entrance.x + 0.5f;
                tz = door->entrance.y + 0.5f;
            } else if (so->next < so->waypoints.size()) {
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
            if (entering(units[i]) || entering(units[j])) continue;
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

    // Pass 2: apply movement, slide along walls, and handle door entry
    std::vector<Entity> entered;
    for (size_t i = 0; i < units.size(); ++i) {
        Entity e = units[i];
        Transform* tr = w.get<Transform>(e);

        float nx = tr->x + vel[i].first * dt;
        float nz = tr->z + vel[i].second * dt;
        // Try each axis separately so units slide along walls instead of stopping
        if (t.isFree(nx, tr->z)) tr->x = nx;
        if (t.isFree(tr->x, nz)) tr->z = nz;
        tr->y = t.heightAt(tr->x, tr->z);

        if (door && door->unlocked) {
            SquadMember* sm = w.get<SquadMember>(e);
            Squad* sq = sm ? w.get<Squad>(sm->squad) : nullptr;
            SoloMove* so = w.get<SoloMove>(e);
            bool entering = (sq && sq->enterDoor) || (so && so->enterDoor);
            if (entering) {
                float ex = door->entrance.x + 0.5f, ez = door->entrance.y + 0.5f;
                float dx = tr->x - ex, dz = tr->z - ez;
                if (dx * dx + dz * dz < ENTER_RADIUS * ENTER_RADIUS) entered.push_back(e);
            }
        }
    }

    for (Entity e : entered) {
        w.destroy(e);
        gs.unitsEntered++;
    }
}

void pickupSystem(World& w, Terrain& t, GameState& gs) {
    std::vector<Entity> units = w.view<Unit, Transform, Commandable>();

    // Player units collect keys
    for (Entity k : w.view<Key, Transform>()) {
        Transform* kt = w.get<Transform>(k);
        for (Entity u : units) {
            Transform* ut = w.get<Transform>(u);
            float dx = ut->x - kt->x, dz = ut->z - kt->z;
            if (dx * dx + dz * dz < KEY_RADIUS * KEY_RADIUS) {
                w.destroy(k);
                gs.keysCollected++;
                break;
            }
        }
    }

    // Once every key is collected, the door opens and waiting squads replan to go in
    Door* door = findDoor(w);
    if (door && !door->unlocked && gs.keysCollected >= KEY_COUNT) {
        door->unlocked = true;
        t.setWalkable(door->entrance.x, door->entrance.y, true);
        for (Entity sq : w.view<Squad>()) {
            if (w.get<Squad>(sq)->wantsDoor) planSquad(w, t, sq, tileCenter(door->entrance), true);
        }
        for (Entity e : w.view<SoloMove>()) {
            if (w.get<SoloMove>(e)->wantsDoor) planSolo(w, t, e, tileCenter(door->entrance), true);
        }
    }
}

void cleanupSquads(World& w) {
    std::vector<Entity> used;
    for (Entity e : w.view<SquadMember>()) used.push_back(w.get<SquadMember>(e)->squad);

    for (Entity sq : w.view<Squad>()) {
        if (std::find(used.begin(), used.end(), sq) == used.end()) w.destroy(sq);
    }
}

void updateWinState(World& w, GameState& gs) {
    Door* door = findDoor(w);
    if (door && door->unlocked && gs.unitsEntered >= UNIT_COUNT) gs.won = true;
}
