#include "systems.h"
#include "language.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <cstdlib>
#include "combat.h"
#include "inventory.h"
#include "items.h"
#include "needs.h"
#include "party.h"
#include "props.h"

namespace {

constexpr float UNIT_CLEARANCE = 0.2f;   // extra gap kept between units
constexpr float PICKUP_REACH = 0.9f;     // a unit this close to a ground item can take it

// Stops a unit's pickup: the order and its walk both end
void endPickup(World& w, Entity u) {
    w.remove<PickupOrder>(u);
    w.remove<SoloMove>(u);
}

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
// Returns the center of the square, in world units.
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

namespace {


// Starting stats: attributes at levels 5..15, skills at levels 1..5. Each stat starts with exactly
// the XP for its level.
Stats startingStats(Rng& rng) {
    Stats s;
    for (int i = 0; i < STAT_COUNT; ++i) {
        const bool attribute = statInfo(i).group == StatGroup::Attributes;
        s.xp[i] = xpForLevel(attribute ? rng.range(5, 15) : rng.range(1, 5));
    }
    return s;
}

} // namespace

Stats randomStartingStats(Rng& rng) {
    return startingStats(rng);
}

int randomBodyType(Rng& rng) {
    const int r = rng.range(1, 100);
    return r <= 45 ? BODY_MASCULINE : r <= 90 ? BODY_FEMININE : BODY_ANDROGYNOUS;
}

int bodyForSavedName(const std::string& name) {
    const size_t h = std::hash<std::string>{}(name) % 100;
    return h < 45 ? BODY_MASCULINE : h < 90 ? BODY_FEMININE : BODY_ANDROGYNOUS;
}

// Player units never share a name. The names come from the set for the body type.
std::string randomUnitName(Rng& rng, int body, const std::vector<std::string>& taken) {
    const NameSet set = body == BODY_FEMININE ? NameSet::Feminine
                      : body == BODY_ANDROGYNOUS ? NameSet::Androgynous : NameSet::Masculine;
    const std::vector<std::string> names = languageNames(set);
    if (names.empty()) return lang("UILABEL_UNNAMED_UNIT");
    for (int attempt = 0; attempt < 200; ++attempt) {
        const std::string& name = names[rng.range(0, (int)names.size() - 1)];
        if (std::find(taken.begin(), taken.end(), name) == taken.end()) return name;
    }
    return names[0];
}

// One player unit in the block centred on the spawn: slot i of the block, moved onto free ground
void spawnPlayerUnit(World& w, Terrain& t, Rng& rng, Vec2 spawn, int i) {
    const Vec2 p = t.snap({ spawn.x - 3.0f + (i % SQUAD_COLUMNS) * 1.5f,
                            spawn.z - 3.0f + (i / SQUAD_COLUMNS) * 1.5f });
    Entity u = w.create();
    w.add(u, Transform{ p.x, t.heightAt(p.x, p.z), p.z });
    w.add(u, Unit{});
    w.add(u, Commandable{});
    w.add(u, Inventory{});
    w.add(u, Equipment{});
    w.add(u, Needs{});
    w.add(u, Behaviors{});
    w.add(u, Combat{});
    w.add(u, Name{});
    w.add(u, UnitSprite{ rng.range(1, UNIT_SPRITE_COUNT) });
    w.add(u, Body{ randomBodyType(rng) });
}

// Makes sure there are `count` player units, adding the missing ones at the spawn
void ensurePlayerUnits(World& w, Terrain& t, Rng& rng, int count) {
    const Vec2 spawn = findSpawn(t);
    const int have = (int)w.view<Unit, Commandable>().size();
    for (int i = have; i < count; ++i) spawnPlayerUnit(w, t, rng, spawn, i);
}

// Stacks of loot an NPC carries when it spawns: a few random items, each stack a random count up to its stack limit
constexpr int NPC_LOOT_MIN = 2;
constexpr int NPC_LOOT_MAX = 5;

Inventory randomNpcLoot(Rng& rng) {
    Inventory inv;
    const std::vector<ItemDef>& items = itemDatabase();
    if (items.empty()) return inv;
    const int stacks = rng.range(NPC_LOOT_MIN, NPC_LOOT_MAX);
    for (int i = 0; i < stacks; ++i) {
        const ItemDef& d = items[(size_t)rng.range(0, (int)items.size() - 1)];
        addToInventory(inv, d.id, rng.range(1, std::max(1, stackLimit(d.id))));
    }
    return inv;
}

void setupWorld(World& w, Terrain& t, Rng& rng) {
    const Vec2 spawn = findSpawn(t);
    // Read in the ground around the spawn, so the units and red units can stand on it
    if (t.streamed()) t.stream({ spawn }, SPAWN_LOAD_RADIUS, 0);
    const int size = t.size();
    auto tileAt = [&](float v) { return std::clamp((int)std::floor(v), 0, size - 1); };

    // Units: a block centred on the spawn
    for (int i = 0; i < START_UNIT_COUNT; ++i) spawnPlayerUnit(w, t, rng, spawn, i);

    // Red units and neutral units: the same kind of unit, but they stand still and can't be commanded. A red unit
    // fights a player unit that comes close; a neutral one can be talked to. Both are placed on walkable tiles within
    // range of the spawn, but not close to it. The attempt limit stops a small map from looping forever when it has
    // fewer free tiles than units.
    std::vector<GridPos> enemyTiles;
    auto placeNpcs = [&](int count, bool neutral) {
        const size_t target = enemyTiles.size() + count;
        for (int attempt = 0; attempt < 20000 && enemyTiles.size() < target; ++attempt) {
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
            w.add(e, Unit{ HOSTILE_WALK_SPEED });
            if (neutral) w.add(e, Neutral{});
            else w.add(e, Hostile{});
            w.add(e, randomNpcLoot(rng));
            w.add(e, Equipment{});
            w.add(e, Needs{});
            w.add(e, Behaviors{});
            w.add(e, Combat{});
            w.add(e, Name{});
            w.add(e, Dialogue{});
            w.add(e, UnitSprite{ rng.range(1, UNIT_SPRITE_COUNT) });
        }
    };
    placeNpcs(ENEMY_COUNT, false);
    placeNpcs(NEUTRAL_COUNT, true);

    // Props on free ground near the spawn: PROP_COPIES of each kind (props.h), wagons included, each on its
    // own tile. They are scenery and do not block movement.
    std::vector<GridPos> propTiles;
    const int propTotal = PROP_KIND_COUNT * PROP_COPIES;
    for (int attempt = 0; attempt < 20000 && (int)propTiles.size() < propTotal; ++attempt) {
        GridPos g{ rng.range(tileAt(spawn.x - PROP_RANGE), tileAt(spawn.x + PROP_RANGE)),
                   rng.range(tileAt(spawn.z - PROP_RANGE), tileAt(spawn.z + PROP_RANGE)) };
        if (!t.walkable(g.x, g.y)) continue;
        if (std::find(propTiles.begin(), propTiles.end(), g) != propTiles.end()) continue;
        const int kind = (int)propTiles.size() / PROP_COPIES;
        propTiles.push_back(g);
        const float x = g.x + 0.5f, z = g.y + 0.5f;
        Entity prop = w.create();
        w.add(prop, Transform{ x, t.heightAt(x, z), z });
        w.add(prop, Prop{ kind });
    }

    // Names, stats, health and gear for every unit. These draw after the placement above, so where units
    // stand doesn't depend on them.
    std::vector<std::string> takenNames;
    for (Entity u : w.view<Unit>()) {
        if (!w.has<Stats>(u)) w.add(u, startingStats(rng));
        if (Needs* nd = w.get<Needs>(u)) {
            nd->maxHp = maxHealthFor(*w.get<Stats>(u));
            nd->hp = nd->maxHp;
        }
        if (w.has<Hostile>(u) || w.has<Neutral>(u)) w.add(u, randomHostileGear(rng));
        // The body comes first: the name is drawn from the set for it
        if (!w.has<Body>(u)) w.add(u, Body{ randomBodyType(rng) });
        const int body = w.get<Body>(u)->type;
        Name* nm = w.get<Name>(u);
        if (!nm) continue;
        if (w.has<Commandable>(u)) {
            nm->text = randomUnitName(rng, body, takenNames);
            takenNames.push_back(nm->text);
        } else {
            nm->text = randomUnitName(rng, body, {});
        }
    }
}

void issueMoveOrder(World& w, Terrain& t, const std::vector<Entity>& units, Vec2 clickPoint) {
    if (units.empty()) return;

    // A single unit walks its own route, with no formation. A move order replaces any pickup.
    if (units.size() == 1) {
        Entity u = units[0];
        w.remove<SquadMember>(u);
        w.remove<PickupOrder>(u);
        w.remove<AttackOrder>(u);
        w.remove<TalkOrder>(u);
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
        w.remove<PickupOrder>(units[i]);
        w.remove<AttackOrder>(units[i]);
        w.remove<TalkOrder>(units[i]);
        w.add(units[i], SquadMember{ sq, (int)i });
    }

    planSquad(w, t, sq, clickPoint);
}

void spawnGroundItems(World& w, Terrain& t, Rng& rng, int perItem) {
    const int size = t.size();
    // Random tiles are tried until one is walkable. Most of a map is usually land, so a few tries
    // are enough; the cap only stops a map with almost no walkable ground from stalling the load.
    constexpr int TRIES_PER_COPY = 500;
    for (const ItemDef& def : itemDatabase()) {
        for (int n = 0; n < perItem; ++n) {
            for (int attempt = 0; attempt < TRIES_PER_COPY; ++attempt) {
                const int x = rng.range(0, size - 1), z = rng.range(0, size - 1);
                bool walk = false;
                float h = 0.0f;
                if (!t.tileGround(x, z, walk, h) || !walk) continue;

                Entity e = w.create();
                w.add(e, Transform{ x + 0.5f, h, z + 0.5f });
                w.add(e, GroundItem{ def.id, 1 });
                break;
            }
        }
    }
}

void issuePickupOrder(World& w, Terrain& t, const std::vector<Entity>& units, Entity item) {
    const Transform* it = w.get<Transform>(item);
    if (!it || !w.has<GroundItem>(item)) return;
    const Vec2 target{ it->x, it->z };

    for (Entity u : units) {
        if (!w.has<Inventory>(u) || !w.get<Transform>(u)) continue;   // only units that carry items
        w.remove<SquadMember>(u);
        w.remove<AttackOrder>(u);
        w.remove<TalkOrder>(u);
        w.add(u, SoloMove{});
        planSolo(w, t, u, target);
        w.add(u, PickupOrder{ item });
    }
}

void steerUnits(World& w, const std::vector<Entity>& units, Vec2 target) {
    for (Entity u : units) {
        if (!w.has<Commandable>(u) || !w.get<Transform>(u)) continue;
        if (w.has<SquadMember>(u)) w.remove<SquadMember>(u);
        if (w.has<AttackOrder>(u)) w.remove<AttackOrder>(u);
        w.remove<TalkOrder>(u);
        if (w.has<PickupOrder>(u)) w.remove<PickupOrder>(u);
        if (!w.has<SoloMove>(u)) w.add(u, SoloMove{});
        SoloMove* so = w.get<SoloMove>(u);
        so->waypoints.assign(1, target);   // one straight waypoint: no path is planned
        so->next = 0;
    }
}

void issueAttackOrder(World& w, const std::vector<Entity>& units, Entity target) {
    if (!w.has<Unit>(target) || !w.get<Transform>(target)) return;
    for (Entity u : units) {
        if (!w.has<Commandable>(u) || !w.get<Transform>(u)) continue;   // only your own units fight on order
        // A fight replaces any move, pickup or formation the unit was on
        w.remove<SquadMember>(u);
        w.remove<SoloMove>(u);
        w.remove<PickupOrder>(u);
        w.remove<TalkOrder>(u);
        w.add(u, AttackOrder{ target });
        ensureCombat(w, u);
        w.get<Combat>(u)->target = target;
    }
}

// Right-click on an NPC: each selected unit walks up to it. The first in talk range starts the talk.
void issueTalkOrder(World& w, const std::vector<Entity>& units, Entity npc) {
    if (!w.has<Unit>(npc) || !w.get<Transform>(npc)) return;
    for (Entity u : units) {
        if (!w.has<Commandable>(u) || !w.get<Transform>(u)) continue;
        w.remove<SquadMember>(u);
        w.remove<SoloMove>(u);
        w.remove<PickupOrder>(u);
        w.remove<AttackOrder>(u);
        w.remove<TalkOrder>(u);
        w.add(u, TalkOrder{ npc });
    }
}

void clearTalkOrders(World& w) {
    for (Entity u : w.view<TalkOrder>()) w.remove<TalkOrder>(u);
}

bool dropStack(World& w, Entity unit, int slot) {
    Inventory* inv = w.get<Inventory>(unit);
    const Transform* tr = w.get<Transform>(unit);
    if (!inv || !tr || slot < 0 || slot >= INVENTORY_SLOTS) return false;

    ItemStack& s = inv->slots[slot];
    if (s.item == 0 || s.count <= 0) return false;

    const ItemDef* def = findItem(s.item);
    queueLog(langf("MISC_DROPS", { nameOf(w, unit), std::to_string(s.count), def ? itemDisplayName(*def) : lang("MISC_AN_ITEM") }),
             LogKind::Drop);
    Entity e = w.create();
    w.add(e, Transform{ tr->x, tr->y, tr->z });
    w.add(e, GroundItem{ s.item, s.count });
    s = ItemStack{};
    return true;
}

void needsSystem(World& w, int hours, double gameSeconds) {
    for (Entity e : w.view<Needs>()) {
        Needs* n = w.get<Needs>(e);
        // A unit that moved at run pace in the hour that just ended pays double for its next tick. The mark is
        // used up here, so later hours cost the usual amount.
        int ticks = hours;
        if (hours > 0 && n->hurried) {
            ticks += 1;
            n->hurried = false;
        }
        tickNeeds(*n, ticks);

        const Combat* c = w.get<Combat>(e);
        regenHealth(*n, c && c->target != NULL_ENTITY, gameSeconds);
    }
}

int pickupSystem(World& w) {
    int taken = 0;
    // view() returns a copy, so ending a pickup inside the loop is safe
    for (Entity u : w.view<PickupOrder, Transform, Inventory>()) {
        const Entity item = w.get<PickupOrder>(u)->item;
        const Transform* ut = w.get<Transform>(u);
        const Transform* it = w.get<Transform>(item);
        GroundItem* gi = w.get<GroundItem>(item);
        if (!it || !gi) {                       // someone else took it, or it was removed
            endPickup(w, u);
            continue;
        }

        const float dx = it->x - ut->x, dz = it->z - ut->z;
        if (dx * dx + dz * dz <= PICKUP_REACH * PICKUP_REACH) {
            // Take what fits. Whatever is left stays on the ground. If nothing fits the order ends,
            // and the unit stays where it is.
            const int count = gi->count;
            const ItemDef* def = findItem(gi->item);
            const std::string itemName = def ? itemDisplayName(*def) : std::string(lang("MISC_AN_ITEM"));
            const int left = addToInventory(*w.get<Inventory>(u), gi->item, gi->count);
            if (left < count) {
                queueLog(langf("MISC_PICKS_UP", { nameOf(w, u), std::to_string(count - left), itemName }), LogKind::Pickup);
            }
            if (left == 0) w.destroy(item);
            else gi->count = left;
            if (w.has<Commandable>(u) && left < count) ++taken;
            endPickup(w, u);
            continue;
        }

        // Stopped short with no route left (the item can't be reached), so give up
        const SoloMove* so = w.get<SoloMove>(u);
        if (!so || so->next >= so->waypoints.size()) endPickup(w, u);
    }
    return taken;
}

void squadSystem(World& w, float dt, bool running) {
    for (Entity e : w.view<Squad>()) {
        Squad* sq = w.get<Squad>(e);
        if (sq->arrived) continue;

        // The squad keeps to the pace of its slowest member that can still move. Members that can't
        // move at all (downed, or carrying too much) are left behind, and don't hold the squad back.
        // Walking, the squad is also held to its own pace. Running, each member runs at its own speed,
        // so the center goes at the slowest member's run pace.
        float slowest = -1.0f;
        for (Entity m : w.view<SquadMember, Unit>()) {
            if (w.get<SquadMember>(m)->squad != e) continue;
            const float s = walkSpeedOf(w, m, running);
            if (s > 0.0f && (slowest < 0.0f || s < slowest)) slowest = s;
        }
        const float pace = slowest < 0.0f ? 0.0f : running ? slowest : std::min(sq->speed, slowest);

        Vec2 wp = sq->waypoints[sq->next];
        float tx = wp.x, tz = wp.z;
        float dx = tx - sq->cx, dz = tz - sq->cz;
        float d = std::sqrt(dx * dx + dz * dz);
        float step = pace * dt;

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

void steeringSystem(World& w, Terrain& t, float dt, bool running) {
    // Every unit that moves: player units by their orders, hostiles by their chase
    std::vector<Entity> units = w.view<Unit, Transform>();
    std::vector<std::pair<float, float>> vel(units.size(), { 0.0f, 0.0f });
    std::vector<float> speeds(units.size(), 0.0f);

    // Pass 1: each unit's desired velocity, from its slot plus separation.
    // Positions are read here but not changed, so the order of units doesn't matter.
    for (size_t i = 0; i < units.size(); ++i) {
        Entity e = units[i];
        Transform* tr = w.get<Transform>(e);
        Unit* u = w.get<Unit>(e);

        float tx = tr->x, tz = tr->z;   // default: stay put
        if (const TalkOrder* talk = w.get<TalkOrder>(e)) {
            // A talk: walk at the NPC until a unit is in talk range, which starts the talk
            if (const Transform* nt = w.get<Transform>(talk->npc)) {
                tx = nt->x;
                tz = nt->z;
            }
        } else if (SquadMember* sm = w.get<SquadMember>(e)) {
            if (Squad* sq = w.get<Squad>(sm->squad)) {
                // Slots are centerd on the squad center, so the group's middle sits on
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
        } else if (const AttackOrder* ao = w.get<AttackOrder>(e)) {
            // An ordered fight: walk at the red unit, and stop once it is close enough to swing
            if (const Transform* tt = w.get<Transform>(ao->target)) {
                const float gx = tt->x - tr->x, gz = tt->z - tr->z;
                if (std::sqrt(gx * gx + gz * gz) > ATTACK_REACH * CHASE_STOP_SHARE) {
                    tx = tt->x;
                    tz = tt->z;
                }
            }
        } else if (w.has<Hostile>(e)) {
            // A hostile walks at its target while it has one, and stops once it is close enough to swing
            if (const Combat* c = w.get<Combat>(e); c && c->target != NULL_ENTITY) {
                if (const Transform* tt = w.get<Transform>(c->target)) {
                    const float gx = tt->x - tr->x, gz = tt->z - tr->z;
                    if (std::sqrt(gx * gx + gz * gz) > ATTACK_REACH * CHASE_STOP_SHARE) {
                        tx = tt->x;
                        tz = tt->z;
                    }
                }
            }
        }

        // A downed unit has speed 0. A player unit's speed depends on its load, Athletics, stamina and pace.
        speeds[i] = walkSpeedOf(w, e, running);
        float dx = tx - tr->x, dz = tz - tr->z;
        float dist = std::sqrt(dx * dx + dz * dz);
        float vx = 0.0f, vz = 0.0f;
        if (dist > 0.05f && speeds[i] > 0.0f) {
            float sp = std::min(speeds[i], dist * 2.5f);   // slow down near the target
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

    // Pass 2: apply movement and slide along walls. Walking gives XP, and records the distance walked and
    // the load for the combat step, which runs first next frame.
    for (size_t i = 0; i < units.size(); ++i) {
        const Entity e = units[i];
        Transform* tr = w.get<Transform>(e);
        const bool player = w.has<Commandable>(e);
        const float load = encumbranceOf(w, e);
        const float x0 = tr->x, z0 = tr->z, y0 = tr->y;

        float nx = tr->x + vel[i].first * dt;
        float nz = tr->z + vel[i].second * dt;
        // Try each axis separately so units slide along walls instead of stopping
        if (t.isFree(nx, tr->z)) tr->x = nx;
        if (t.isFree(tr->x, nz)) tr->z = nz;
        tr->y = t.heightAt(tr->x, tr->z);

        const float moved = std::hypot(tr->x - x0, tr->z - z0);
        if (Combat* c = w.get<Combat>(e)) {
            c->moved = moved;
            c->encumbrance = load;
        }
        if (moved > 0.0f) {
            grantXp(w, e, STAT_ENDURANCE, ENDURANCE_XP_PER_TILE * moved, lang("MISC_REASON_WALKING"));
            grantXp(w, e, STAT_STRENGTH, strengthXpWhileWalking(load, dt), lang("MISC_REASON_CARRYING"));
            if (tr->y > y0) grantXp(w, e, STAT_ATHLETICS, tr->y - y0, lang("MISC_REASON_CLIMBING"));
            // Moving at run pace marks the unit for a double hunger tick at the next hour. A nudge from a neighbor,
            // a hundredth of a tile or less in a frame, is not walking.
            if (player && running && moved >= 0.01f) {
                if (Needs* nd = w.get<Needs>(e)) nd->hurried = true;
            }
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
