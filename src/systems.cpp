#include "systems.h"
#include "language.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <cstdlib>
#include "combat.h"
#include "inventory.h"
#include "items.h"
#include "needs.h"
#include "party.h"
#include "props.h"

namespace {

constexpr float UNIT_CLEARANCE = 0.2f;   // extra gap kept between units
constexpr float PICKUP_REACH = 2.0f;     // a unit this close to a ground item (in tiles) can take it

constexpr float FOLLOW_DISTANCE = 1.6f;   // a unit following or defending another keeps within this many tiles of it

// Ends a unit's standing orders: the walk to a prop, a bed it lies on, the work it does, and following or defending a
// unit. Every order the player gives calls this, so a unit given another order leaves its bed or stops what it was at.
void endPropTasks(World& w, Entity u) {
    w.remove<PropOrder>(u);
    w.remove<Occupying>(u);
    w.remove<Working>(u);
    w.remove<FollowOrder>(u);
    w.remove<DefendOrder>(u);
}

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
    w.add(u, Faction{ FACTION_PLAYER });
}

// Makes there be exactly `count` player units: the missing ones are added at the spawn, and any beyond count go. The
// party is put into them in the order they were made (applyParty), so the last made are the ones that go.
void ensurePlayerUnits(World& w, Terrain& t, Rng& rng, int count) {
    std::vector<Entity> players = w.view<Unit, Commandable>();
    std::sort(players.begin(), players.end());
    for (size_t i = (size_t)std::max(0, count); i < players.size(); ++i) w.destroy(players[i]);
    const int have = std::min((int)players.size(), std::max(0, count));
    if (have >= count) return;
    const Vec2 spawn = findSpawn(t);
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
            w.add(e, Faction{ neutral ? FACTION_GRAY : FACTION_RED });
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
    // own tile. They do not block movement. They go down a round of every kind at a time (wagon, campfire, ... forge,
    // then wagon again), so if the ground near the spawn ever runs out, every kind has been placed before any has its
    // last copies.
    std::vector<GridPos> propTiles;
    const int propTotal = PROP_KIND_COUNT * PROP_COPIES;
    for (int attempt = 0; attempt < 400 * propTotal && (int)propTiles.size() < propTotal; ++attempt) {
        GridPos g{ rng.range(tileAt(spawn.x - PROP_RANGE), tileAt(spawn.x + PROP_RANGE)),
                   rng.range(tileAt(spawn.z - PROP_RANGE), tileAt(spawn.z + PROP_RANGE)) };
        if (!t.walkable(g.x, g.y)) continue;
        if (std::find(propTiles.begin(), propTiles.end(), g) != propTiles.end()) continue;
        const int serial = (int)propTiles.size();
        const int kind = serial % PROP_KIND_COUNT;
        propTiles.push_back(g);
        const float x = g.x + 0.5f, z = g.y + 0.5f;
        Entity prop = w.create();
        w.add(prop, Transform{ x, t.heightAt(x, z), z });
        w.add(prop, Prop{ kind, serial });
        if (hasStorage(propInfo(kind))) w.add(prop, Storage{});   // a barrel or a campfire starts empty
        if (propInfo(kind).use == PropUse::PassiveCrafting) {     // a forge starts cold, with an empty output
            w.add(prop, Output{});
            w.add(prop, Fuel{});
            w.add(prop, PropWork{});
        }
        if (propInfo(kind).lockLevel > 0) w.add(prop, Lock{ true, propInfo(kind).lockLevel });   // Lockable: starts locked
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
        // No unit starts out carrying so much that it is encumbered
        if (Inventory* inv = w.get<Inventory>(u)) {
            Equipment* worn = w.get<Equipment>(u);
            std::array<int, GEAR_SLOTS> gear = worn ? worn->items : std::array<int, GEAR_SLOTS>{};
            keepUnderEncumbrance(inv->slots, gear, *w.get<Stats>(u));
            if (worn) worn->items = gear;
        }
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

void issueMoveOrder(World& w, Terrain& t, const std::vector<Entity>& given, Vec2 clickPoint) {
    // A unit locked in a cage stays where it is
    std::vector<Entity> units;
    for (Entity u : given) {
        if (!lockedIn(w, u)) units.push_back(u);
    }
    if (units.empty()) return;

    // A single unit walks its own route, with no formation. A move order replaces any pickup.
    if (units.size() == 1) {
        Entity u = units[0];
        w.remove<SquadMember>(u);
        w.remove<PickupOrder>(u);
        w.remove<AttackOrder>(u);
        w.remove<TalkOrder>(u);
        endPropTasks(w, u);
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
        endPropTasks(w, units[i]);
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
        if (lockedIn(w, u)) continue;
        w.remove<SquadMember>(u);
        w.remove<AttackOrder>(u);
        w.remove<TalkOrder>(u);
        endPropTasks(w, u);
        w.add(u, SoloMove{});
        planSolo(w, t, u, target);
        w.add(u, PickupOrder{ item });
    }
}

void steerUnits(World& w, const std::vector<Entity>& units, Vec2 target) {
    for (Entity u : units) {
        if (!w.has<Commandable>(u) || !w.get<Transform>(u) || lockedIn(w, u)) continue;
        if (w.has<SquadMember>(u)) w.remove<SquadMember>(u);
        if (w.has<AttackOrder>(u)) w.remove<AttackOrder>(u);
        w.remove<TalkOrder>(u);
        endPropTasks(w, u);
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
        if (lockedIn(w, u)) continue;
        // A fight replaces any move, pickup or formation the unit was on
        w.remove<SquadMember>(u);
        w.remove<SoloMove>(u);
        w.remove<PickupOrder>(u);
        w.remove<TalkOrder>(u);
        endPropTasks(w, u);
        w.add(u, AttackOrder{ target });
        ensureCombat(w, u);
        w.get<Combat>(u)->target = target;
    }
}

// Right-click on an NPC: each selected unit walks up to it. The first in talk range starts the talk.
void issueTalkOrder(World& w, const std::vector<Entity>& units, Entity npc, bool trade) {
    if (!w.has<Unit>(npc) || !w.get<Transform>(npc)) return;
    for (Entity u : units) {
        if (!w.has<Commandable>(u) || !w.get<Transform>(u) || lockedIn(w, u)) continue;
        w.remove<SquadMember>(u);
        w.remove<SoloMove>(u);
        w.remove<PickupOrder>(u);
        w.remove<AttackOrder>(u);
        w.remove<TalkOrder>(u);
        endPropTasks(w, u);
        w.add(u, TalkOrder{ npc, trade });
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

namespace {

// The unit a unit is following or defending (and not off fighting an attacker), or NULL_ENTITY
Entity leadOf(World& w, Entity e) {
    if (const FollowOrder* f = w.get<FollowOrder>(e)) return f->target;
    if (const DefendOrder* d = w.get<DefendOrder>(e); d && !w.has<AttackOrder>(e)) return d->ward;
    return NULL_ENTITY;
}

// Whether a unit following or defending lead is close enough to stop. It is when it is within FOLLOW_DISTANCE of the
// lead, or when it touches another unit with the same lead that is closer to the lead than it is and lies toward the
// lead from it (within 60 degrees). That other unit must always be the closer of the two, so two followers can never
// hold each other in place: when the lead walks off, the nearest one goes after it, and the rest follow in turn.
// A crowd around one unit settles into a cluster instead of pressing in on it forever.
// Stopping behind another counts only within a radius that grows with the crowd: about what that many units need
// packed around the lead. Further out, a unit keeps on, and the idle units in its way step aside (steeringSystem),
// so a crowd that came in single file gathers round the lead instead of trailing behind it in a long line.
bool closeEnoughToLead(World& w, const std::vector<Entity>& units, Entity e, Entity lead) {
    const Transform* tr = w.get<Transform>(e);
    const Transform* lt = w.get<Transform>(lead);
    if (!tr || !lt) return true;
    const float gx = lt->x - tr->x, gz = lt->z - tr->z;
    const float myGap = std::sqrt(gx * gx + gz * gz);
    if (myGap <= FOLLOW_DISTANCE) return true;
    const Unit* mu = w.get<Unit>(e);
    int crowd = 0;
    for (Entity o : units) crowd += leadOf(w, o) == lead ? 1 : 0;
    const float spacing = 2.0f * mu->radius + UNIT_CLEARANCE;
    if (myGap > FOLLOW_DISTANCE + 0.75f * spacing * std::sqrt((float)crowd)) return false;
    for (Entity o : units) {
        if (o == e || o == lead || leadOf(w, o) != lead) continue;
        const Transform* ot = w.get<Transform>(o);
        const Unit* ou = w.get<Unit>(o);
        const float ox = ot->x - tr->x, oz = ot->z - tr->z;
        const float touching = mu->radius + ou->radius + UNIT_CLEARANCE + 0.3f;
        const float gap = std::sqrt(ox * ox + oz * oz);
        if (gap > touching || gap < 1e-4f) continue;
        const float theirGap = std::hypot(lt->x - ot->x, lt->z - ot->z);
        if (theirGap >= myGap) continue;                       // it must be closer to the lead
        if (ox * gx + oz * gz < 0.5f * gap * myGap) continue;   // and toward the lead from here, within 60 degrees
        return true;
    }
    return false;
}

} // namespace

void steeringSystem(World& w, Terrain& t, float dt, bool running) {
    // Every unit that moves: player units by their orders, hostiles by their chase
    std::vector<Entity> units = w.view<Unit, Transform>();
    std::vector<std::pair<float, float>> vel(units.size(), { 0.0f, 0.0f });
    std::vector<float> speeds(units.size(), 0.0f);
    // Each unit's goal this frame, whether it is going somewhere of its own (else it is idle, holding its spot), and
    // whether it is pinned in place (lying on a bed)
    std::vector<std::pair<float, float>> goal(units.size(), { 0.0f, 0.0f });
    std::vector<char> going(units.size(), 0), pinned(units.size(), 0);
    // For a unit settled while following or defending, and not in a fight, the unit it follows; for a worker settled
    // at its prop, the prop (else NULL_ENTITY). Units that share one hold still among themselves: see pass 1b.
    std::vector<Entity> calmLead(units.size(), NULL_ENTITY);

    // The units something is attacking this frame
    std::vector<Entity> attacked;
    for (Entity e : units) {
        if (const Combat* c = w.get<Combat>(e); c && c->target != NULL_ENTITY) attacked.push_back(c->target);
    }
    std::sort(attacked.begin(), attacked.end());
    auto underAttack = [&](Entity e) { return std::binary_search(attacked.begin(), attacked.end(), e); };
    const float unitWidth = 2.0f * 0.4f + UNIT_CLEARANCE;   // one unit's room, at the usual radius

    // Pass 1a: each unit's goal: where its orders take it, or the spot it is idling at.
    // Positions are read here but not changed, so the order of units doesn't matter.
    for (size_t i = 0; i < units.size(); ++i) {
        Entity e = units[i];
        Transform* tr = w.get<Transform>(e);

        float tx = tr->x, tz = tr->z;   // default: stay put
        float leadGap = -1.0f;          // how far a following or defending unit is from the unit it follows
        // A unit on a bed or bedroll lies still on it: it does not move, and nothing pushes it off
        if (w.has<Occupying>(e)) {
            speeds[i] = 0.0f;
            pinned[i] = 1;
            goal[i] = { tr->x, tr->z };
            continue;
        }
        if (const Working* g = w.get<Working>(e); g && !w.has<SoloMove>(e)) {
            // A worker stays where it is while it is in reach. One pushed out of reach walks back toward the prop.
            if (const Transform* pt = w.get<Transform>(g->prop)) {
                const float gx = pt->x - tr->x, gz = pt->z - tr->z;
                if (gx * gx + gz * gz > PROP_REACH * PROP_REACH) {
                    tx = pt->x;
                    tz = pt->z;
                } else if (!underAttack(e)) {
                    calmLead[i] = g->prop;   // settled at its work, among the others working the same prop
                }
            }
        } else if (const TalkOrder* talk = w.get<TalkOrder>(e)) {
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
        } else if (w.has<FollowOrder>(e) || w.has<DefendOrder>(e)) {
            // Following or defending (with no attacker to fight): walk after the unit, and stop once close enough
            const Entity lead = leadOf(w, e);
            if (const Transform* lt = w.get<Transform>(lead)) {
                leadGap = std::hypot(lt->x - tr->x, lt->z - tr->z);
                // Once settled, a unit that is not in a fight stays put, without checking again, until the unit it
                // follows has moved off by at least a whole unit's room. Small moves of the lead, or of the others
                // around it, don't set it shuffling.
                const IdleSpot* rest = w.get<IdleSpot>(e);
                const bool calm = !underAttack(e) && !underAttack(lead);
                const bool stay = calm && rest && rest->leadGap >= 0.0f && leadGap <= rest->leadGap + unitWidth;
                if (stay) {
                    calmLead[i] = lead;
                } else if (!closeEnoughToLead(w, units, e, lead)) {
                    tx = lt->x;
                    tz = lt->z;
                } else if (calm) {
                    calmLead[i] = lead;
                }
            } else {
                w.remove<FollowOrder>(e);   // the unit it followed is gone
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

        // With nowhere to go, a unit holds the spot where it came to rest. With somewhere to go, the spot is dropped,
        // and a new one is taken where it next stops.
        const bool idle = tx == tr->x && tz == tr->z;
        if (idle) {
            IdleSpot* spot = w.get<IdleSpot>(e);
            if (!spot) spot = &w.add(e, IdleSpot{ tr->x, tr->z, leadGap });
            else if (spot->leadGap < 0.0f) spot->leadGap = leadGap;
            tx = spot->x;
            tz = spot->z;
        } else if (w.has<IdleSpot>(e)) {
            w.remove<IdleSpot>(e);
        }
        goal[i] = { tx, tz };
        going[i] = idle ? 0 : 1;
        // A downed unit has speed 0. A player unit's speed depends on its load, Athletics, stamina and pace.
        speeds[i] = walkSpeedOf(w, e, running);
    }

    // Pass 1b: each unit's velocity: toward its goal, plus separation from the units too close to it.
    // An idle unit that a unit going somewhere is pushing gives way: it stops pulling back to its spot, and steps
    // aside, off the other unit's way. Once nothing pushes it, it walks back to its spot.
    for (size_t i = 0; i < units.size(); ++i) {
        if (pinned[i]) continue;
        const Entity e = units[i];
        const Transform* tr = w.get<Transform>(e);
        const Unit* u = w.get<Unit>(e);

        float vx = 0.0f, vz = 0.0f, sideX = 0.0f, sideZ = 0.0f;
        bool shoved = false;
        for (size_t j = 0; j < units.size(); ++j) {
            if (j == i) continue;
            const Transform* o = w.get<Transform>(units[j]);
            const Unit* ou = w.get<Unit>(units[j]);
            float sx = tr->x - o->x, sz = tr->z - o->z;
            const float minD = u->radius + ou->radius + UNIT_CLEARANCE;
            const float d2 = sx * sx + sz * sz;
            if (d2 >= minD * minD) continue;
            // Two settled units following the same unit leave each other be: no pushing back and forth between them
            if (calmLead[i] != NULL_ENTITY && calmLead[i] == calmLead[j]) continue;
            float d = std::sqrt(d2);
            if (d < 1e-4f) { sx = 1.0f; sz = 0.0f; d = 1.0f; }
            const float push = (minD - d) * 8.0f;
            vx += sx / d * push;
            vz += sz / d * push;
            if (going[i] && pinned[j]) {
                // Against a unit that can't give way (one lying on a bed): slide around it, on the side already nearer
                float hx = goal[i].first - tr->x, hz = goal[i].second - tr->z;
                const float hl = std::sqrt(hx * hx + hz * hz);
                if (hl > 1e-4f) {
                    hx /= hl;
                    hz /= hl;
                    float side = -hz * sx + hx * sz;                          // which side of this unit's heading it is on
                    if (std::fabs(side) < 1e-3f) side = (e & 1u) ? 1.0f : -1.0f;   // dead ahead: pick a side
                    const float sgn = side > 0.0f ? 1.0f : -1.0f;
                    vx += -hz * sgn * push * 1.5f;
                    vz += hx * sgn * push * 1.5f;
                }
            }
            if (!going[i] && going[j]) {
                // Pushed by a unit on its way: step to the side of its path, the side this unit is already on
                shoved = true;
                float hx = goal[j].first - o->x, hz = goal[j].second - o->z;
                const float hl = std::sqrt(hx * hx + hz * hz);
                if (hl > 1e-4f) {
                    hx /= hl;
                    hz /= hl;
                    float side = hx * sz - hz * sx;                      // which side of its heading this unit is on
                    if (std::fabs(side) < 1e-3f) side = (e & 1u) ? 1.0f : -1.0f;   // dead ahead: pick a side
                    const float s = side > 0.0f ? 1.0f : -1.0f;
                    sideX += -hz * s * push;
                    sideZ += hx * s * push;
                }
            }
        }
        if (shoved) {
            vx += sideX;
            vz += sideZ;
        } else {
            const float dx = goal[i].first - tr->x, dz = goal[i].second - tr->z;
            const float dist = std::sqrt(dx * dx + dz * dz);
            // A settled follower only walks back to its spot when it is at least a whole unit's room away from it
            const float deadZone = calmLead[i] != NULL_ENTITY ? unitWidth : 0.05f;
            if (dist > deadZone && speeds[i] > 0.0f) {
                const float sp = std::min(speeds[i], dist * 2.5f);   // slow down near the goal
                vx += dx / dist * sp;
                vz += dz / dist * sp;
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

// ---- Props: using storage, beds, resource props and crafting props ----

namespace {

// The flat distance in tiles between two entities, or a large number if either has no place
float flatGap(World& w, Entity a, Entity b) {
    const Transform* ta = w.get<Transform>(a);
    const Transform* tb = w.get<Transform>(b);
    if (!ta || !tb) return 1e9f;
    return std::hypot(ta->x - tb->x, ta->z - tb->z);
}

PropUse useOf(World& w, Entity prop) {
    const Prop* p = w.get<Prop>(prop);
    return p ? propInfo(p->kind).use : PropUse::None;
}

int workersAt(World& w, Entity prop) {
    int n = 0;
    for (Entity o : w.view<Working>()) n += w.get<Working>(o)->prop == prop ? 1 : 0;
    return n;
}

std::string itemNameFor(int item) {
    const ItemDef* def = findItem(item);
    return def ? itemDisplayName(*def) : std::string(lang("MISC_AN_ITEM"));
}

// Uses up one of an item from a row of slots. False if there is none.
bool takeOne(ItemStack* slots, int count, int item) {
    for (int i = 0; i < count; ++i) {
        if (slots[i].item != item || slots[i].count <= 0) continue;
        if (--slots[i].count <= 0) slots[i] = ItemStack{};
        return true;
    }
    return false;
}

} // namespace

namespace {

// Sends every unit working a prop off it, with a line in the log for each, and clears the prop's progress, so work
// that was cut short counts for nothing
void stopWorkersAt(World& w, Entity prop) {
    for (Entity u : w.view<Working>()) {
        if (w.get<Working>(u)->prop != prop) continue;
        queueLog(langf("MISC_CRAFT_STOPS", { nameOf(w, u) }), LogKind::Other);
        w.remove<Working>(u);
    }
    if (PropWork* pw = w.get<PropWork>(prop)) {
        pw->progress = 0.0;
        pw->workers = 0;
    }
}

} // namespace

CraftState craftStateOf(World& w, Entity prop) {
    const Prop* p = w.get<Prop>(prop);
    const Storage* st = w.get<Storage>(prop);
    if (!p || propInfo(p->kind).use != PropUse::Crafting) return CraftState::NoInput;
    const PropInfo& info = propInfo(p->kind);
    if (info.input == 0) return CraftState::Ready;   // the water pump and the training dummy need nothing
    if (!st) return CraftState::NoInput;
    for (const ItemStack& stack : st->slots) {
        if (stack.count > 0 && recipeOutput(p->kind, stack.item) > 0) return CraftState::Ready;
    }
    return CraftState::NoInput;
}

bool propInTheWay(World& w, Entity unit, Entity prop) {
    const Transform* a = w.get<Transform>(unit);
    const Transform* b = w.get<Transform>(prop);
    if (!a || !b) return false;
    const float abx = b->x - a->x, abz = b->z - a->z;
    const float len2 = abx * abx + abz * abz;
    for (Entity p : w.view<Prop, Transform>()) {
        if (p == prop) continue;
        const Transform* c = w.get<Transform>(p);
        // The point of the line nearest the other prop's centre
        float t = len2 > 1e-8f ? ((c->x - a->x) * abx + (c->z - a->z) * abz) / len2 : 0.0f;
        t = std::clamp(t, 0.0f, 1.0f);
        const float nx = a->x + abx * t - c->x, nz = a->z + abz * t - c->z;
        if (nx * nx + nz * nz < PROP_BLOCK_RADIUS * PROP_BLOCK_RADIUS) return true;
    }
    return false;
}

Entity propOccupant(World& w, Entity prop) {
    for (Entity u : w.view<Occupying>()) {
        if (w.get<Occupying>(u)->prop == prop) return u;
    }
    return NULL_ENTITY;
}

int propWorkerCount(World& w, Entity prop) {
    int n = workersAt(w, prop);
    for (Entity u : w.view<PropOrder>()) n += w.get<PropOrder>(u)->prop == prop ? 1 : 0;
    return n;
}

void issuePropOrder(World& w, Terrain& t, const std::vector<Entity>& units, Entity prop, PropAction action) {
    const Transform* pt = w.get<Transform>(prop);
    if (!pt || !w.has<Prop>(prop)) return;
    for (Entity u : units) {
        if (!w.has<Commandable>(u) || !w.get<Transform>(u)) continue;
        if (lockedIn(w, u)) {
            // Locked in: the only order it takes is to unlock the prop it is in, which it does from where it is
            if (action == PropAction::Unlock && occupiedProp(w, u) == prop) w.add(u, PropOrder{ prop, action });
            continue;
        }
        w.remove<SquadMember>(u);
        w.remove<PickupOrder>(u);
        w.remove<AttackOrder>(u);
        w.remove<TalkOrder>(u);
        endPropTasks(w, u);
        w.add(u, SoloMove{});
        planSolo(w, t, u, Vec2{ pt->x, pt->z });
        w.add(u, PropOrder{ prop, action });
    }
}

void issueFollowOrder(World& w, const std::vector<Entity>& units, Entity target) {
    if (!w.has<Unit>(target) || !w.get<Transform>(target)) return;
    for (Entity u : units) {
        if (u == target || !w.has<Commandable>(u) || !w.get<Transform>(u) || lockedIn(w, u)) continue;
        w.remove<SquadMember>(u);
        w.remove<SoloMove>(u);
        w.remove<PickupOrder>(u);
        w.remove<AttackOrder>(u);
        w.remove<TalkOrder>(u);
        endPropTasks(w, u);
        w.add(u, FollowOrder{ target });
    }
}

void issueDefendOrder(World& w, const std::vector<Entity>& units, Entity ward) {
    if (!w.has<Unit>(ward) || !w.get<Transform>(ward)) return;
    for (Entity u : units) {
        if (u == ward || !w.has<Commandable>(u) || !w.get<Transform>(u) || lockedIn(w, u)) continue;
        w.remove<SquadMember>(u);
        w.remove<SoloMove>(u);
        w.remove<PickupOrder>(u);
        w.remove<AttackOrder>(u);
        w.remove<TalkOrder>(u);
        endPropTasks(w, u);
        w.add(u, DefendOrder{ ward, NULL_ENTITY });
    }
}

void defendSystem(World& w) {
    for (Entity d : w.view<DefendOrder>()) {
        DefendOrder* o = w.get<DefendOrder>(d);
        if (!w.has<Unit>(o->ward) || !w.get<Transform>(o->ward)) {   // the unit defended is gone
            w.remove<AttackOrder>(d);
            w.remove<DefendOrder>(d);
            continue;
        }
        // Still fighting an attacker that is standing and still after the ward: keep at it
        auto attackingWard = [&](Entity a) {
            const Combat* c = w.get<Combat>(a);
            const Needs* n = w.get<Needs>(a);
            // Your own units never fight each other, even one that attacks the unit defended
            return a != d && !w.has<Commandable>(a) && w.has<Unit>(a) && w.get<Transform>(a) && c &&
                   c->target == o->ward && (!n || n->hp > 0);
        };
        if (o->engaged != NULL_ENTITY && attackingWard(o->engaged) && w.has<AttackOrder>(d)) continue;
        if (o->engaged != NULL_ENTITY) {
            w.remove<AttackOrder>(d);
            if (Combat* c = w.get<Combat>(d)) c->target = NULL_ENTITY;
            o->engaged = NULL_ENTITY;
        }
        // The nearest unit now attacking the ward, if any, becomes the one to fight
        const Transform* dt = w.get<Transform>(d);
        Entity best = NULL_ENTITY;
        float bestD2 = 0.0f;
        for (Entity a : w.view<Unit, Transform, Combat>()) {
            if (!attackingWard(a)) continue;
            const Transform* at = w.get<Transform>(a);
            const float d2 = (at->x - dt->x) * (at->x - dt->x) + (at->z - dt->z) * (at->z - dt->z);
            if (best == NULL_ENTITY || d2 < bestD2) {
                best = a;
                bestD2 = d2;
            }
        }
        if (best != NULL_ENTITY) {
            o->engaged = best;
            w.add(d, AttackOrder{ best });
        }
    }
}

void makeHostile(World& w, Entity unit) {
    if (!w.has<Unit>(unit) || w.has<Commandable>(unit) || w.has<Hostile>(unit)) return;
    w.remove<Neutral>(unit);
    w.add(unit, Hostile{});
    queueLog(langf("MISC_TURNS_HOSTILE", { nameOf(w, unit) }), LogKind::Seen);
}

std::vector<PropArrival> propOrderSystem(World& w) {
    std::vector<PropArrival> out;
    // view() returns a copy, so ending an order inside the loop is safe
    for (Entity u : w.view<PropOrder, Transform>()) {
        const Entity prop = w.get<PropOrder>(u)->prop;
        const Transform* pt = w.get<Transform>(prop);
        if (!pt || !w.has<Prop>(prop)) {             // the prop is gone
            w.remove<PropOrder>(u);
            w.remove<SoloMove>(u);
            continue;
        }
        if (flatGap(w, u, prop) <= PROP_REACH) {
            const PropAction action = w.get<PropOrder>(u)->action;
            w.remove<PropOrder>(u);
            w.remove<SoloMove>(u);
            PropArrival a;
            a.unit = u;
            a.prop = prop;
            a.action = action;
            const PropInfo& info = propInfo(w.get<Prop>(prop)->kind);
            const Lock* lock = w.get<Lock>(prop);
            if (action == PropAction::Unlock) {
                // Unlock: start picking the lock, if the unit has the skill and nobody else is at it
                a.craft = tryStartUnlock(w, u, prop);
                a.started = a.craft == WorkStart::Started;
                out.push_back(a);
                continue;
            }
            if (lock && lock->locked) {
                // A locked prop can't be opened or used
                a.locked = true;
                out.push_back(a);
                continue;
            }
            if (action == PropAction::Inventory) {
                // Inventory: the game opens the storage or crafting prop's panel
                if (hasStorage(info)) out.push_back(a);
                continue;
            }
            switch (info.use) {
            case PropUse::Storage:
                break;
            case PropUse::Occupy: {
                // One try, once in reach. Another prop on the straight line to the bed makes it unreachable; another
                // unit already on it makes it taken. Either way the order ends and the unit stays where it is.
                // Otherwise the unit is put on the bed, right on its spot, wherever in reach it stopped.
                const Entity occupant = propOccupant(w, prop);
                if (propInTheWay(w, u, prop)) {
                    a.unreachable = true;
                } else if (occupant != NULL_ENTITY && occupant != u) {
                    a.taken = true;
                } else {
                    Transform* ut = w.get<Transform>(u);
                    ut->x = pt->x;
                    ut->y = pt->y;
                    ut->z = pt->z;
                    w.remove<IdleSpot>(u);
                    w.add(u, Occupying{ prop });
                    a.started = true;
                    // A lockable one (the Prisoner Cage) locks behind the unit
                    if (Lock* lk = w.get<Lock>(prop)) {
                        lk->locked = true;
                        queueLog(langf("MISC_LOCKED_IN", { nameOf(w, u), lang(info.nameKey) }), LogKind::Other);
                    }
                }
                break;
            }
            case PropUse::Resource:
                if (workersAt(w, prop) >= info.workers) {
                    a.taken = true;
                } else {
                    w.add(u, Working{ prop });
                    a.started = true;
                }
                break;
            case PropUse::Crafting:
                // Use: start crafting if the input and output allow it, checked now, at the moment of the try
                a.craft = tryStartWork(w, u, prop);
                a.started = a.craft == WorkStart::Started;
                a.taken = a.craft == WorkStart::Full;
                break;
            case PropUse::PassiveCrafting:   // works by itself: only its Inventory does anything
            case PropUse::None:
                break;
            }
            if (info.use != PropUse::None && info.use != PropUse::Storage && info.use != PropUse::PassiveCrafting) out.push_back(a);
            continue;
        }
        // Stopped short with no route left (the prop can't be reached), so give up
        const SoloMove* so = w.get<SoloMove>(u);
        if (!so || so->next >= so->waypoints.size()) {
            w.remove<PropOrder>(u);
            w.remove<SoloMove>(u);
        }
    }

    // A unit on a bed stays exactly on it: nothing but an order of yours moves it off. If the bed is gone, it gets
    // up. One unit to a bed: should two ever be on one, the first to have been made keeps it and the other gets up.
    std::vector<Entity> lying = w.view<Occupying, Transform>();
    std::sort(lying.begin(), lying.end());
    std::vector<Entity> bedsTaken;
    for (Entity u : lying) {
        const Entity bed = w.get<Occupying>(u)->prop;
        const Transform* pt = w.get<Transform>(bed);
        if (!pt || std::find(bedsTaken.begin(), bedsTaken.end(), bed) != bedsTaken.end()) {
            w.remove<Occupying>(u);
            continue;
        }
        bedsTaken.push_back(bed);
        Transform* ut = w.get<Transform>(u);
        ut->x = pt->x;
        ut->y = pt->y;
        ut->z = pt->z;
    }
    return out;
}

std::vector<WorkGain> workSystem(World& w, double gameSeconds, Rng& rng) {
    std::vector<WorkGain> gains;
    // The units working each prop this frame. A downed unit stops working. One pushed out of reach is walking back,
    // so it does not add to the progress until it is in reach again.
    std::map<Entity, std::vector<Entity>> workersOf;
    for (Entity u : w.view<Working, Transform>()) {
        const Working* wk = w.get<Working>(u);
        const Entity prop = wk->prop;
        const Lock* lock = w.get<Lock>(prop);
        // Picking a lock that is still locked, or working a prop that is not
        const bool valid = wk->unlocking ? (lock && lock->locked) : (isWorkProp(useOf(w, prop)) && !(lock && lock->locked));
        if (!valid) {
            w.remove<Working>(u);
            continue;
        }
        if (const Needs* n = w.get<Needs>(u); n && n->hp <= 0) {
            w.remove<Working>(u);
            continue;
        }
        if (flatGap(w, u, prop) > PROP_REACH + 0.5f) continue;
        workersOf[prop].push_back(u);
    }
    for (Entity e : w.view<PropWork>()) w.get<PropWork>(e)->workers = 0;

    for (auto& [prop, workers] : workersOf) {
        std::sort(workers.begin(), workers.end());
        const PropInfo& info = propInfo(w.get<Prop>(prop)->kind);
        if (w.get<Working>(workers[0])->unlocking) {
            // Picking the lock: each attempt fills the bar, at the picker's Lockpicking pace, then succeeds with
            // lockpickChance. A failed attempt starts the next one; a success opens the lock and the work is done.
            PropWork& pw = w.has<PropWork>(prop) ? *w.get<PropWork>(prop) : w.add(prop, PropWork{});
            pw.workers = (int)workers.size();
            double perHour = 0.0;
            for (Entity u : workers) {
                perHour += 1.0 / (LOCKPICK_SHARE * workHoursFor(levelOf(statsOf(w, u), STAT_LOCKPICKING)));
            }
            pw.progress += gameSeconds / 3600.0 * perHour;
            Lock* lock = w.get<Lock>(prop);
            while (pw.progress >= 1.0 && lock->locked) {
                pw.progress -= 1.0;
                ++pw.made;   // the ring hops at the end of each attempt
                const Entity picker = workers[pw.turn % workers.size()];
                ++pw.turn;
                const int level = levelOf(statsOf(w, picker), STAT_LOCKPICKING);
                if ((double)rng.unit() < lockpickChance(level)) {
                    lock->locked = false;
                    gains.push_back(WorkGain{ picker, 0, WorkGain::Kind::Unlocked });
                    queueLog(langf("MISC_UNLOCKS", { nameOf(w, picker), lang(info.nameKey) }), LogKind::Other);
                    grantXp(w, picker, STAT_LOCKPICKING, LOCKPICK_XP_SUCCESS, lang("MISC_REASON_LOCKPICKING"));
                } else {
                    gains.push_back(WorkGain{ picker, 0, WorkGain::Kind::LockpickFailed });
                    grantXp(w, picker, STAT_LOCKPICKING, LOCKPICK_XP_FAIL, lang("MISC_REASON_LOCKPICKING"));
                }
            }
            if (!lock->locked) {
                for (Entity u : workers) w.remove<Working>(u);
                pw.progress = 0.0;
                pw.workers = 0;
            }
            continue;
        }
        const bool crafting = info.use == PropUse::Crafting;
        // A crafting prop with none of its input left sends its workers off
        auto stopAll = [&]() {
            stopWorkersAt(w, prop);
            workers.clear();
        };
        if (crafting && craftStateOf(w, prop) != CraftState::Ready) {
            stopAll();
            continue;
        }
        PropWork& pw = w.has<PropWork>(prop) ? *w.get<PropWork>(prop) : w.add(prop, PropWork{});
        pw.workers = (int)workers.size();

        // Each worker adds one item per workHoursFor(its level) in-game hours
        double perHour = 0.0;
        for (Entity u : workers) perHour += 1.0 / workHoursFor(levelOf(statsOf(w, u), info.stat));
        pw.progress += gameSeconds / 3600.0 * perHour;

        const std::string itemName = itemNameFor(info.item);
        while (pw.progress >= 1.0 && !workers.empty()) {
            if (crafting) {
                // What this item is made from and into: the first input in the prop's slots that it has a recipe for
                // (a campfire cooks each kind of raw meat into its own cooked meat), or nothing for a prop that needs
                // no input
                int inItem = 0, outItem = info.item;
                if (info.input != 0) {
                    outItem = 0;
                    if (const Storage* st = w.get<Storage>(prop)) {
                        for (const ItemStack& stack : st->slots) {
                            if (stack.count <= 0) continue;
                            if (const int made = recipeOutput(w.get<Prop>(prop)->kind, stack.item); made > 0) {
                                inItem = stack.item;
                                outItem = made;
                                break;
                            }
                        }
                    }
                }
                const std::string outName = itemNameFor(outItem);
                // A finished item. The input is checked at this moment: if it is gone, nothing is made and the work
                // stops. The item goes to the next worker in turn that has room for it. If none has, nothing is used
                // up or made: the bar stays full, the workers stop, and the next unit with room to work here finishes it.
                if (craftStateOf(w, prop) != CraftState::Ready) {
                    stopAll();
                    break;
                }
                // A prop that makes nothing (the training dummy) gives the next worker in turn its XP
                Entity got = outItem <= 0 ? workers[pw.turn % workers.size()] : NULL_ENTITY;
                for (size_t k = 0; k < workers.size() && got == NULL_ENTITY; ++k) {
                    const Entity cand = workers[(pw.turn + k) % workers.size()];
                    const Inventory* inv = w.get<Inventory>(cand);
                    if (inv && slotsCanTake(inv->slots.data(), INVENTORY_SLOTS, outItem, 1)) got = cand;
                }
                if (got == NULL_ENTITY) {
                    for (Entity u : workers) {
                        queueLog(langf("MISC_GATHER_NO_ROOM", { nameOf(w, u), outName }), LogKind::Other);
                        w.remove<Working>(u);
                    }
                    workers.clear();
                    pw.progress = 1.0;   // held full until someone with room works it
                    break;
                }
                if (info.input != 0) {   // the water pump and the training dummy use nothing up
                    Storage* st = w.get<Storage>(prop);
                    if (!st || inItem <= 0 || !takeOne(st->slots.data(), STORAGE_SLOTS, inItem)) {
                        stopAll();
                        break;
                    }
                }
                pw.progress -= 1.0;
                ++pw.made;   // the ring hops for it
                ++pw.turn;
                if (outItem > 0) {
                    addToInventory(*w.get<Inventory>(got), outItem, 1);
                    gains.push_back(WorkGain{ got, outItem });
                    queueLog(langf("MISC_CRAFTS", { nameOf(w, got), outName }), LogKind::Pickup);
                    grantXp(w, got, info.stat, WORK_XP_PER_ITEM, lang("MISC_REASON_CRAFTING"));
                } else {
                    // Training: no item, only the skill's XP
                    gains.push_back(WorkGain{ got, info.stat, WorkGain::Kind::Trained, WORK_XP_PER_ITEM });
                    grantXp(w, got, info.stat, WORK_XP_PER_ITEM, lang("MISC_REASON_TRAINING"));
                }
                if (craftStateOf(w, prop) != CraftState::Ready) {
                    stopAll();   // that was the last of the input
                    break;
                }
                continue;
            }
            pw.progress -= 1.0;
            ++pw.made;   // finished, whatever becomes of the item: the ring hops for it
            const size_t pick = pw.turn % workers.size();
            ++pw.turn;
            const Entity got = workers[pick];
            Inventory* inv = w.get<Inventory>(got);
            if (inv && addToInventory(*inv, info.item, 1) == 0) {
                gains.push_back(WorkGain{ got, info.item });
                queueLog(langf("MISC_GATHERS", { nameOf(w, got), itemName }), LogKind::Pickup);
                grantXp(w, got, info.stat, WORK_XP_PER_ITEM, lang("MISC_REASON_GATHERING"));
            } else {
                // No room: the item falls at the unit's feet, and the unit stops working
                if (const Transform* ut = w.get<Transform>(got)) {
                    const Entity drop = w.create();
                    w.add(drop, Transform{ ut->x, ut->y, ut->z });
                    w.add(drop, GroundItem{ info.item, 1 });
                }
                queueLog(langf("MISC_GATHER_NO_ROOM", { nameOf(w, got), itemName }), LogKind::Drop);
                w.remove<Working>(got);
                workers.erase(workers.begin() + (long)pick);
            }
        }
        pw.workers = (int)workers.size();
        // A resource prop left with no workers keeps what it had, short of a whole item. A crafting prop may be left
        // full, waiting for a unit with room.
        if (workers.empty() && !crafting) pw.progress = std::min(pw.progress, 0.999);
    }
    return gains;
}

WorkStart tryStartWork(World& w, Entity unit, Entity prop) {
    const Prop* p = w.get<Prop>(prop);
    if (!p || !isWorkProp(propInfo(p->kind).use) || !w.get<Transform>(unit)) return WorkStart::TooFar;
    if (const Working* wk = w.get<Working>(unit); wk && wk->prop == prop) return WorkStart::Started;   // already at it
    if (flatGap(w, unit, prop) > STORAGE_CLOSE_RANGE) return WorkStart::TooFar;
    if (const Needs* n = w.get<Needs>(unit); n && n->hp <= 0) return WorkStart::TooFar;
    if (const Lock* lock = w.get<Lock>(prop); lock && lock->locked) return WorkStart::Locked;
    if (lockedIn(w, unit)) return WorkStart::Locked;   // shut in a cage, it can't get to work
    if (workersAt(w, prop) >= propInfo(p->kind).workers) return WorkStart::Full;
    if (propInfo(p->kind).use == PropUse::Crafting) {
        const CraftState cs = craftStateOf(w, prop);
        if (cs == CraftState::NoInput) return WorkStart::NoInput;
    }
    w.remove<SquadMember>(unit);
    w.remove<SoloMove>(unit);
    w.remove<PickupOrder>(unit);
    w.remove<AttackOrder>(unit);
    w.remove<TalkOrder>(unit);
    endPropTasks(w, unit);
    w.add(unit, Working{ prop });
    return WorkStart::Started;
}

WorkStart tryStartUnlock(World& w, Entity unit, Entity prop) {
    const Lock* lock = w.get<Lock>(prop);
    if (!lock || !w.has<Prop>(prop) || !w.get<Transform>(unit)) return WorkStart::NotLocked;
    if (!lock->locked) return WorkStart::NotLocked;
    if (const Working* wk = w.get<Working>(unit); wk && wk->prop == prop && wk->unlocking) return WorkStart::Started;
    if (flatGap(w, unit, prop) > STORAGE_CLOSE_RANGE) return WorkStart::TooFar;
    if (const Needs* n = w.get<Needs>(unit); n && n->hp <= 0) return WorkStart::TooFar;
    if (levelOf(statsOf(w, unit), STAT_LOCKPICKING) < lock->level) return WorkStart::SkillTooLow;
    if (workersAt(w, prop) >= 1) return WorkStart::Full;   // one unit at a lock at a time
    const bool inside = occupiedProp(w, unit) == prop;     // picking the cage it is shut in, from the inside
    if (!inside && lockedIn(w, unit)) return WorkStart::Locked;
    w.remove<SquadMember>(unit);
    w.remove<SoloMove>(unit);
    w.remove<PickupOrder>(unit);
    w.remove<AttackOrder>(unit);
    w.remove<TalkOrder>(unit);
    if (inside) {
        // It stays in the cage while it works at the lock, and after the lock opens, until it is ordered out
        w.remove<PropOrder>(unit);
        w.remove<Working>(unit);
        w.remove<FollowOrder>(unit);
        w.remove<DefendOrder>(unit);
    } else {
        endPropTasks(w, unit);
    }
    w.add(unit, Working{ prop, true });
    return WorkStart::Started;
}

void propStorageChanged(World& w, Entity prop) {
    const Prop* p = w.get<Prop>(prop);
    if (!p || propInfo(p->kind).use != PropUse::Crafting) return;
    if (craftStateOf(w, prop) != CraftState::Ready) stopWorkersAt(w, prop);
}

std::string taskNameOf(World& w, Entity u) {
    Entity prop = NULL_ENTITY;
    if (const Working* g = w.get<Working>(u); g && g->unlocking) return statName(STAT_LOCKPICKING);
    if (const Working* g = w.get<Working>(u)) prop = g->prop;
    else if (const PropOrder* po = w.get<PropOrder>(u); po && useOf(w, po->prop) == PropUse::Resource) prop = po->prop;
    if (const Prop* p = prop != NULL_ENTITY ? w.get<Prop>(prop) : nullptr) return statName(propInfo(p->kind).stat);
    if (const Occupying* oc = w.get<Occupying>(u)) {
        if (lockedIn(w, u)) return lang("UILABEL_TASK_LOCKED_IN");
        const Prop* p = w.get<Prop>(oc->prop);
        return lang(p && isSeat(p->kind) ? "UILABEL_TASK_SITTING" : "UILABEL_TASK_RESTING");
    }
    if (w.has<DefendOrder>(u)) return lang("UILABEL_TASK_DEFENDING");
    if (w.has<FollowOrder>(u)) return lang("UILABEL_TASK_FOLLOWING");
    const SoloMove* so = w.get<SoloMove>(u);
    if (so && so->next < so->waypoints.size()) return lang("UILABEL_TASK_MOVING");
    if (const SquadMember* sm = w.get<SquadMember>(u)) {
        if (const Squad* sq = w.get<Squad>(sm->squad); sq && !sq->arrived) return lang("UILABEL_TASK_MOVING");
    }
    return lang("UILABEL_TASK_IDLE");
}

std::vector<int> emotesOf(World& w, Entity u) {
    std::vector<int> out;
    auto propEmoteOf = [&](Entity prop) {
        const Prop* p = w.get<Prop>(prop);
        return p ? propEmote(p->kind) : (int)EMOTE_NONE;
    };
    if (const Working* wk = w.get<Working>(u)) {
        if (wk->unlocking) out.push_back(EMOTE_LOCKPICKING);
        else if (propEmoteOf(wk->prop) != EMOTE_NONE) out.push_back(propEmoteOf(wk->prop));
    }
    if (const Occupying* oc = w.get<Occupying>(u); oc && propEmoteOf(oc->prop) != EMOTE_NONE) out.push_back(propEmoteOf(oc->prop));
    if (lockedIn(w, u)) out.push_back(EMOTE_LOCKED_IN);
    if (w.has<Inventory>(u) && w.has<Stats>(u)) {
        const float limit = carryLimit(statsOf(w, u));
        if (limit > 0.0f && carriedWeightOf(w, u) >= ENCUMBERED_SHARE * limit) out.push_back(EMOTE_ENCUMBERED);
    }
    if (const Needs* n = w.get<Needs>(u); n && n->hunger < 1) out.push_back(EMOTE_HUNGRY);
    return out;
}

bool propAcceptsItem(int kind, int item) {
    const PropInfo& info = propInfo(kind);
    if (item <= 0) return false;
    if (info.use == PropUse::Crafting || info.use == PropUse::PassiveCrafting) return recipeOutput(kind, item) > 0;
    const ItemDef* def = findItem(item);
    switch (info.accepts) {
    case ItemFilter::Any: return true;
    case ItemFilter::RawFood: return def && hasTag(*def, "raw_food") && def->typeId != "consumable";
    case ItemFilter::Food: return def && def->typeId == "consumable";
    case ItemFilter::Weapon: return def && def->typeId == "weapon";
    case ItemFilter::Armor: return def && def->typeId == "armor";
    case ItemFilter::Water: return def && hasTag(*def, "water");
    }
    return false;
}

int recipeOutput(int kind, int input) {
    const PropInfo& info = propInfo(kind);
    if (input <= 0 || (info.use != PropUse::Crafting && info.use != PropUse::PassiveCrafting)) return 0;
    if (info.input == INPUT_COOKABLE) {
        const ItemDef* def = findItem(input);
        return def && def->cooksInto > 0 && findItem(def->cooksInto) ? def->cooksInto : 0;
    }
    return input == info.input ? info.item : 0;
}

Entity occupiedProp(World& w, Entity u) {
    const Occupying* oc = w.get<Occupying>(u);
    return oc ? oc->prop : NULL_ENTITY;
}

bool lockedIn(World& w, Entity u) {
    const Occupying* oc = w.get<Occupying>(u);
    if (!oc) return false;
    const Lock* lock = w.get<Lock>(oc->prop);
    return lock && lock->locked;
}

PassiveState passiveStateOf(World& w, Entity prop) {
    const Prop* p = w.get<Prop>(prop);
    const Output* out = w.get<Output>(prop);
    const Fuel* fuel = w.get<Fuel>(prop);
    if (!p || !out || !fuel) return PassiveState::NoFuel;
    const PropInfo& info = propInfo(p->kind);
    if (!slotsCanTake(out->slots.data(), STORAGE_SLOTS, info.item, 1)) return PassiveState::OutputFull;
    if (fuel->hours > 1e-9) return PassiveState::Burning;
    const Storage* st = w.get<Storage>(prop);
    if (st) {
        for (const ItemStack& s : st->slots) {
            if (s.item == info.input && s.count > 0) return PassiveState::Burning;   // it lights the next one now
        }
    }
    return PassiveState::NoFuel;
}

void passiveCraftSystem(World& w, double gameSeconds) {
    for (Entity e : w.view<Prop, Output, Fuel>()) {
        const PropInfo& info = propInfo(w.get<Prop>(e)->kind);
        if (info.use != PropUse::PassiveCrafting) continue;
        Output& out = *w.get<Output>(e);
        Fuel& fuel = *w.get<Fuel>(e);
        Storage* st = w.get<Storage>(e);
        PropWork& pw = w.has<PropWork>(e) ? *w.get<PropWork>(e) : w.add(e, PropWork{});
        const double craftHours = PASSIVE_CRAFT_HOURS > 0.0 ? PASSIVE_CRAFT_HOURS : 1.0;
        const double perInput = info.fuelHours > 0.0 ? info.fuelHours : 1.0;
        double hours = std::max(0.0, gameSeconds / 3600.0);
        bool burning = false;
        // A long jump of the clock can finish several items, so this goes round until the time is used up or it stops
        for (int guard = 0; guard < 100000; ++guard) {
            // A finished item goes into the output, if it has room. If not, it waits there, finished.
            if (pw.progress >= 1.0 - 1e-9) {
                if (!slotsCanTake(out.slots.data(), STORAGE_SLOTS, info.item, 1)) break;
                addToSlots(out.slots.data(), STORAGE_SLOTS, info.item, 1);
                pw.progress = 0.0;
                ++pw.made;   // the ring hops for it
            }
            // No room for the next item: pause, burning nothing
            if (!slotsCanTake(out.slots.data(), STORAGE_SLOTS, info.item, 1)) break;
            if (hours <= 1e-12) {
                burning = fuel.hours > 1e-9 || (st && std::any_of(st->slots.begin(), st->slots.end(), [&](const ItemStack& s) {
                    return s.item == info.input && s.count > 0;
                }));
                break;
            }
            // Out of fuel: the next input item is used up, and burns for its hours
            if (fuel.hours <= 1e-9) {
                fuel.hours = 0.0;
                if (!st || !takeOne(st->slots.data(), STORAGE_SLOTS, info.input)) break;
                fuel.hours = perInput;
            }
            const double step = std::min({ hours, fuel.hours, (1.0 - pw.progress) * craftHours });
            if (step <= 0.0) break;
            burning = true;
            hours -= step;
            fuel.hours -= step;
            pw.progress = std::min(1.0, pw.progress + step / craftHours);
        }
        pw.workers = burning ? 1 : 0;   // the ring shows while it burns
    }
}
