#include "systems.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <vector>

namespace {

bool sameTile(GridPos a, GridPos b) { return a.x == b.x && a.y == b.y; }

Appearance makeAppearance(TextTex glyph, GLuint sprite) {
    Appearance ap;
    ap.glyph = glyph;
    ap.spriteTex = sprite;
    return ap;
}

// Grid-based A* with 8-way movement. Same algorithm as the original Lua version.
std::vector<GridPos> findPath(GridPos start, GridPos goal) {
    if (sameTile(start, goal)) return {};
    if (goal.x < 0 || goal.x >= GRID || goal.y < 0 || goal.y >= GRID) return {};

    const int N = GRID * GRID;
    const float INF = std::numeric_limits<float>::infinity();
    auto idx = [](int x, int y) { return y * GRID + x; };
    auto heuristic = [&](int x, int y) {
        return (float)std::max(std::abs(x - goal.x), std::abs(y - goal.y));
    };

    std::vector<float> g(N, INF);
    std::vector<int> cameFrom(N, -1);
    std::vector<bool> closed(N, false);
    std::vector<bool> inOpen(N, false);
    std::vector<int> open;

    int s = idx(start.x, start.y);
    int t = idx(goal.x, goal.y);
    g[s] = 0.0f;
    open.push_back(s);
    inOpen[s] = true;

    static const int NB[8][2] = {
        { 0, -1 }, { 0, 1 }, { -1, 0 }, { 1, 0 },
        { -1, -1 }, { 1, -1 }, { -1, 1 }, { 1, 1 }
    };

    while (!open.empty()) {
        size_t best = 0;
        float bestF = INF;
        for (size_t i = 0; i < open.size(); ++i) {
            int k = open[i];
            float f = g[k] + heuristic(k % GRID, k / GRID);
            if (f < bestF) { bestF = f; best = i; }
        }

        int cur = open[best];
        open.erase(open.begin() + best);
        inOpen[cur] = false;

        if (cur == t) {
            std::vector<GridPos> result;
            for (int k = t; k != s; k = cameFrom[k]) {
                result.push_back({ k % GRID, k / GRID });
            }
            std::reverse(result.begin(), result.end());
            return result;
        }

        closed[cur] = true;
        int cx = cur % GRID, cy = cur / GRID;
        for (const auto& n : NB) {
            int nx = cx + n[0], ny = cy + n[1];
            if (nx < 0 || nx >= GRID || ny < 0 || ny >= GRID) continue;
            int nk = idx(nx, ny);
            if (closed[nk]) continue;

            float tentative = g[cur] + 1.0f;
            if (tentative < g[nk]) {
                cameFrom[nk] = cur;
                g[nk] = tentative;
                if (!inOpen[nk]) {
                    open.push_back(nk);
                    inOpen[nk] = true;
                }
            }
        }
    }
    return {};
}

// Spawns a tile-positioned entity that is destroyed on level regeneration
Entity spawnLevelTile(World& w, GridPos g, Appearance ap) {
    Entity e = w.create();
    w.add(e, g);
    w.add(e, Transform{ tileCenter(g.x), tileCenter(g.y) });
    w.add(e, ap);
    w.add(e, LevelEntity{});
    return e;
}

} // namespace

Entity findPlayer(World& w) {
    auto players = w.view<Player>();
    return players.empty() ? NULL_ENTITY : players.front();
}

void spawnFloor(World& w, const Assets& a) {
    Appearance ap = makeAppearance(a.glyphFloor, a.floorTex);
    for (int y = 0; y < GRID; y++) {
        for (int x = 0; x < GRID; x++) {
            Entity e = w.create();
            w.add(e, GridPos{ x, y });
            w.add(e, Transform{ tileCenter(x), tileCenter(y) });
            w.add(e, ap);
            w.add(e, Floor{});
        }
    }
}

void generateLevel(World& w, const Assets& a, Rng& rng) {
    for (Entity e : w.view<LevelEntity>()) w.destroy(e);

    // Four distinct tiles for player, key, door, wagon
    std::vector<GridPos> taken;
    auto pickTile = [&]() {
        for (;;) {
            GridPos p{ rng.range(0, GRID - 1), rng.range(0, GRID - 1) };
            bool used = std::any_of(taken.begin(), taken.end(),
                                    [&](const GridPos& t) { return sameTile(t, p); });
            if (!used) {
                taken.push_back(p);
                return p;
            }
        }
    };
    GridPos playerPos = pickTile();
    GridPos keyPos = pickTile();
    GridPos doorPos = pickTile();
    GridPos wagonPos = pickTile();

    // Player
    Entity player = spawnLevelTile(w, playerPos,
                                   makeAppearance(a.glyphPlayer, a.playerTex));
    w.add(player, Player{});
    w.add(player, Path{});
    w.add(player, Segment{ tileCenter(playerPos.x), tileCenter(playerPos.y),
                           tileCenter(playerPos.x), tileCenter(playerPos.y), 1.0f });

    // Key
    Entity key = spawnLevelTile(w, keyPos, makeAppearance(a.glyphKey, a.keyTex));
    w.add(key, Key{});

    // Door
    Entity door = spawnLevelTile(w, doorPos, makeAppearance(a.glyphDoor, a.doorTex));
    w.add(door, Door{});

    // Wagon (3D model in sprite mode, glyph in ASCII mode)
    Appearance wagonAp = makeAppearance(a.glyphWagon, 0);
    wagonAp.model = &a.wagonModel;
    Entity wagon = spawnLevelTile(w, wagonPos, wagonAp);
    w.add(wagon, Wagon{});
}

void moveRequestSystem(World& w, Commands& cmds) {
    if (!cmds.moveTo) return;
    GridPos target = *cmds.moveTo;
    cmds.moveTo.reset();

    Entity p = findPlayer(w);
    if (!p) return;

    GridPos from = *w.get<GridPos>(p);
    Path* path = w.get<Path>(p);
    path->steps = findPath(from, target);
    path->accum = 0.0f;
}

void movementSystem(World& w, float dt) {
    // 1. Interpolate the visual position along the current segment
    for (Entity e : w.view<Segment, Transform>()) {
        Segment* seg = w.get<Segment>(e);
        Transform* tr = w.get<Transform>(e);
        if (seg->t < 1.0f) {
            seg->t = std::min(1.0f, seg->t + dt / MOVE_TIME);
            tr->x = seg->startX + (seg->endX - seg->startX) * seg->t;
            tr->z = seg->startZ + (seg->endZ - seg->startZ) * seg->t;
        }
    }

    // 2. Advance the logical grid position one step per MOVE_TIME
    for (Entity e : w.view<Path, GridPos, Segment, Transform>()) {
        Path* path = w.get<Path>(e);
        if (path->steps.empty()) {
            path->accum = 0.0f;
            continue;
        }

        path->accum += dt;
        if (path->accum < MOVE_TIME) continue;
        path->accum = 0.0f;

        GridPos next = path->steps.front();
        path->steps.erase(path->steps.begin());

        GridPos* pos = w.get<GridPos>(e);
        int adx = std::abs(next.x - pos->x);
        int ady = std::abs(next.y - pos->y);
        if (adx > 1 || ady > 1 || (adx == 0 && ady == 0)) {
            path->steps.clear();   // path became invalid
            continue;
        }

        pos->x = next.x;
        pos->y = next.y;

        Segment* seg = w.get<Segment>(e);
        Transform* tr = w.get<Transform>(e);
        seg->startX = tr->x;
        seg->startZ = tr->z;
        seg->endX = tileCenter(next.x);
        seg->endZ = tileCenter(next.y);
        seg->t = 0.0f;
    }
}

void pickupSystem(World& w) {
    Entity p = findPlayer(w);
    if (!p) return;
    GridPos pos = *w.get<GridPos>(p);

    for (Entity k : w.view<Key, GridPos>()) {
        if (sameTile(*w.get<GridPos>(k), pos)) {
            w.destroy(k);
            w.get<Player>(p)->hasKey = true;
        }
    }
}

void levelSystem(World& w, const Assets& a, Rng& rng) {
    Entity p = findPlayer(w);
    if (!p) return;

    const Player* player = w.get<Player>(p);
    const GridPos* pos = w.get<GridPos>(p);
    const Segment* seg = w.get<Segment>(p);
    if (!player->hasKey || seg->t < 1.0f) return;

    for (Entity d : w.view<Door, GridPos>()) {
        if (sameTile(*w.get<GridPos>(d), *pos)) {
            generateLevel(w, a, rng);   // destroys the player, so return immediately
            return;
        }
    }
}
