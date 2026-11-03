#include "systems.h"
#include "claims.h"
#include "language.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <cstdlib>
#include "combat.h"
#include "equip.h"
#include "inventory.h"
#include "items.h"
#include "needs.h"
#include "party.h"
#include "props.h"
#include "emotes.h"

namespace {

constexpr float UNIT_CLEARANCE = 0.2f;   // extra gap kept between units
constexpr float PICKUP_REACH = 2.0f;     // a unit this close to a ground item (in tiles) can take it

constexpr float FOLLOW_DISTANCE = 1.6f;   // a unit following or defending another keeps within this many tiles of it

// Ends a unit's standing orders: the walk to a prop, a bed it lies on, the work it does, and following or defending a
// unit. Every order the player gives calls this, so a unit given another order leaves its bed or stops what it was at.
void endPropTasks(World& w, Entity u) {
    w.remove<LootOrder>(u);
    w.remove<CarryOrder>(u);
    w.remove<PutDownOrder>(u);
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
constexpr float CAMP_DISTANCE = 90.0f;      // a faction's camp is centred this far from the player's camp, at a compass point
constexpr float CAMP_SHIFT = 18.0f;         // how far a camp may move off its spot to find ground the kit fits on
constexpr float CAMP_GAP = 2.0f * CLAIM_RADIUS + 6.0f;   // the least distance between two camps' flags: no claims overlap
constexpr float PLAYER_CAMP_OFFSET = 8.0f;  // the player's camp is this far south of the spawn, clear of the squad
constexpr int CAMP_EXTRA_MIN = 3;           // random props each claim gets besides its kit
constexpr int CAMP_EXTRA_MAX = 5;
constexpr float CAMP_EXTRA_NEAR = 5.0f;     // placed this many tiles from the flag, or more,
constexpr float CAMP_EXTRA_FAR = 12.0f;     // up to this many
constexpr float CAMP_RADIUS_BASE = 4.0f;    // and its units stand within this many tiles of its centre,
constexpr float CAMP_RADIUS_PER_UNIT = 1.6f; // plus this times the square root of how many there are

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
constexpr int STORAGE_STACKS_MIN = 2;   // stacks a new storage prop starts with
constexpr int STORAGE_STACKS_MAX = 5;
constexpr int STORAGE_STACK_MOST = 10;  // most of one item in a starting stack
constexpr int STORAGE_VALUABLE = 30;    // buy value from which a non-gear item counts as a Locked Chest's treasure
constexpr int NPC_AMMO_MIN = 10;   // arrows or bolts an NPC with a ranged weapon starts with
constexpr int NPC_AMMO_MAX = 30;

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

// What a storage prop holds when a level places it: a few stacks of what belongs in it. A filtered one (the Weapon
// Rack, the Food Storage, ...) holds what it takes; the Barrel holds supplies (anything that is not gear); the Locked
// Chest holds the valuable things: gear, and anything worth STORAGE_VALUABLE or more.
Storage startingStorage(int kind, Rng& rng) {
    Storage st;
    std::vector<int> choices;
    for (const ItemDef& d : itemDatabase()) {
        if (!propAcceptsItem(kind, d.id)) continue;
        const bool gear = !d.equipSlot.empty();
        if (kind == PROP_BARREL && gear) continue;
        if (kind == PROP_LOCKED_CHEST && !gear && d.buyValue < STORAGE_VALUABLE) continue;
        choices.push_back(d.id);
    }
    if (choices.empty()) return st;
    const bool chest = kind == PROP_LOCKED_CHEST;
    const int stacks = rng.range(chest ? 3 : STORAGE_STACKS_MIN, chest ? 6 : STORAGE_STACKS_MAX);
    int slot = 0;
    for (int i = 0; i < stacks && slot < STORAGE_SLOTS; ++i) {
        const int item = choices[(size_t)rng.range(0, (int)choices.size() - 1)];
        const int most = std::max(1, std::min(stackLimit(item), STORAGE_STACK_MOST));
        st.slots[(size_t)slot++] = ItemStack{ item, rng.range(1, most) };
    }
    return st;
}

void setupWorld(World& w, Terrain& t, Rng& rng) {
    const Vec2 spawn = findSpawn(t);
    // Read in the ground around the spawn, so the units and red units can stand on it
    if (t.streamed()) t.stream({ spawn }, SPAWN_LOAD_RADIUS, 0);
    const int size = t.size();
    auto tileAt = [&](float v) { return std::clamp((int)std::floor(v), 0, size - 1); };

    // Units: a block centred on the spawn
    for (int i = 0; i < START_UNIT_COUNT; ++i) spawnPlayerUnit(w, t, rng, spawn, i);

    // The other factions' units (data/factioninfo.txt): the same kind of unit, but they can't be commanded. Each
    // faction's units stand together in a camp of their own, the camps spread around the spawn at even angles and far
    // enough apart that two camps don't see each other at the start. A camp's units are on walkable tiles, one each;
    // a camp that can't find room for all of them (water, a small map) spills over into the wider area. The attempt
    // limits stop a small map from looping forever.
    std::vector<GridPos> enemyTiles;
    auto tileFree = [&](GridPos g) {
        if (!t.walkable(g.x, g.y)) return false;
        const float x = g.x + 0.5f, z = g.y + 0.5f;
        const float dx = x - spawn.x, dz = z - spawn.z;
        if (dx * dx + dz * dz < ENEMY_MIN_RANGE * ENEMY_MIN_RANGE) return false;
        return std::none_of(enemyTiles.begin(), enemyTiles.end(), [&](GridPos k) { return k == g; });
    };
    auto placeUnit = [&](GridPos g, int faction) -> Entity {
        enemyTiles.push_back(g);
        const float x = g.x + 0.5f, z = g.y + 0.5f;
        Entity e = w.create();
        w.add(e, Transform{ x, t.heightAt(x, z), z });
        w.add(e, Unit{ HOSTILE_WALK_SPEED });
        if (factionsHostile(faction, FACTION_PLAYER)) w.add(e, Hostile{});
        else w.add(e, Neutral{});
        w.add(e, Faction{ faction });
        w.add(e, randomNpcLoot(rng));
        w.add(e, Equipment{});
        w.add(e, Needs{});
        w.add(e, Behaviors{});
        w.add(e, Combat{});
        w.add(e, Name{});
        w.add(e, Dialogue{});
        w.add(e, UnitSprite{ rng.range(1, UNIT_SPRITE_COUNT) });
        return e;
    };
    std::vector<const FactionDef*> camps;
    for (const FactionDef& f : factions()) {
        if (f.id != FACTION_PLAYER && f.units > 0) camps.push_back(&f);
    }

    // Camps (claims.h): every faction's, the player's included, gets the kit: a flag, a vault on a small foundation with
    // three days of logs, a campfire with four bedrolls, a barrel and two trees, all owned by the faction. The player's
    // is in the middle, just south of the squad; the others are at the compass points around it (north, east, south,
    // west, then the corners between, should there be more), CAMP_DISTANCE away, so no two claims overlap. A camp whose
    // spot the kit doesn't fit on moves to the nearest one it does, within CAMP_SHIFT. The kits go down before the level's
    // props, so those don't take their ground; their vaults go down after all of them (below).
    int campSerial = CAMP_SERIAL_BASE;
    std::vector<Vec2> flagSpots;
    auto clearOfCamps = [&](float x, float z) {
        for (const Vec2& f : flagSpots) {
            if ((f.x - x) * (f.x - x) + (f.z - z) * (f.z - z) < CAMP_GAP * CAMP_GAP) return false;
        }
        return true;
    };
    // The kit at (cx, cz), or the nearest spot within CAMP_SHIFT it fits on, ring by ring outward
    auto placeCamp = [&](int faction, float cx, float cz) {
        for (int ring = 0; ring <= (int)CAMP_SHIFT; ++ring) {
            const int steps = ring == 0 ? 1 : 8 * ring;
            for (int k = 0; k < steps; ++k) {
                const float a = 6.2831853f * (float)k / (float)steps;
                const float x = cx + std::cos(a) * (float)ring, z = cz + std::sin(a) * (float)ring;
                if (x < 8.0f || z < 8.0f || x > size - 8.0f || z > size - 8.0f) continue;
                if (!clearOfCamps(x, z) || !campKitFits(w, t, x, z)) continue;
                CampKit kit = placeCampKit(w, t, faction, x, z, 0.0f, &campSerial);
                if (kit.placed) {
                    flagSpots.push_back(Vec2{ kit.x, kit.z });
                    return kit;
                }
            }
        }
        return CampKit{};
    };
    // A few random props in the claim, owned by its faction: storage holds what belongs in it and is locked to the
    // faction (as anything with contents in a claim is), a forge starts cold
    static const int EXTRA_KINDS[] = {
        PROP_WAGON, PROP_BARREL, PROP_ORE, PROP_BOULDER, PROP_MUSHROOM, PROP_BONES, PROP_STALL, PROP_LOCKED_CHEST,
        PROP_CHAIR, PROP_BENCH, PROP_STOOL, PROP_TABLE, PROP_END_TABLE, PROP_ROUND_TABLE, PROP_STANDING_TORCH,
        PROP_TRAINING_DUMMY, PROP_RAW_FOOD_STORAGE, PROP_FOOD_STORAGE, PROP_WEAPON_RACK, PROP_ARMOR_STAND,
        PROP_WATER_TANK, PROP_WATER_PUMP, PROP_FORGE,
    };
    auto addCampExtras = [&](int faction, const CampKit& kit) {
        if (!kit.placed) return;
        const int want = rng.range(CAMP_EXTRA_MIN, CAMP_EXTRA_MAX);
        int placed = 0;
        for (int attempt = 0; attempt < 400 && placed < want; ++attempt) {
            const float a = rng.unit() * 6.2831853f;
            const float d = CAMP_EXTRA_NEAR + rng.unit() * (CAMP_EXTRA_FAR - CAMP_EXTRA_NEAR);
            const int gx = tileAt(kit.x + std::cos(a) * d), gz = tileAt(kit.z + std::sin(a) * d);
            if (!t.walkable(gx, gz)) continue;
            const float x = gx + 0.5f, z = gz + 0.5f;
            bool taken = false;
            for (Entity e : w.view<Prop, Transform>()) {
                const Transform* pt = w.get<Transform>(e);
                if (std::fabs(pt->x - x) < 0.5f && std::fabs(pt->z - z) < 0.5f) { taken = true; break; }
            }
            if (taken) continue;
            const int kind = EXTRA_KINDS[rng.range(0, (int)(sizeof(EXTRA_KINDS) / sizeof(EXTRA_KINDS[0])) - 1)];
            const PropInfo& info = propInfo(kind);
            Entity prop = w.create();
            w.add(prop, Transform{ x, t.heightAt(x, z), z });
            w.add(prop, Prop{ kind, campSerial++, rng.unit() * 6.2831853f });
            if (info.use != PropUse::Resource) w.add(prop, Owner{ faction });
            if (hasStorage(info)) w.add(prop, info.use == PropUse::Storage ? startingStorage(kind, rng) : Storage{});
            if (info.use == PropUse::PassiveCrafting) {
                w.add(prop, Output{});
                w.add(prop, Fuel{});
                w.add(prop, PropWork{});
            }
            if (info.lockLevel > 0) w.add(prop, Lock{ true, info.lockLevel, faction });
            else if (info.use == PropUse::Storage) w.add(prop, Lock{ true, BUILT_LOCK_LEVEL, faction });
            ++placed;
        }
    };
    // One unit of each camp keeps its vault fed: Auto-Haul to the vault above Auto-Harvest of Trees, so it cuts wood at
    // the camp's trees and stores each load in the vault. It is given once the vaults are down, below.
    std::vector<std::pair<Entity, CampKit>> woodcutters;
    auto giveWoodcutting = [&](Entity u, const CampKit& kit) {
        if (u == NULL_ENTITY || !kit.placed || kit.vault == NULL_ENTITY || kit.trees.empty()) return;
        // A camp's woodcutter starts with an empty pack, so the logs it carries don't weigh it down
        if (!w.has<Commandable>(u)) {
            if (Inventory* inv = w.get<Inventory>(u)) *inv = Inventory{};
        }
        Behaviors* b = w.get<Behaviors>(u);
        if (!b) return;
        BehaviorRule haul;
        haul.kind = BehaviorKind::AutoHaul;
        haul.prop = kit.vault;
        haul.propKind = PROP_VAULT;
        BehaviorRule harvest;
        harvest.kind = BehaviorKind::AutoHarvest;
        harvest.propKind = PROP_TREE;
        addBehavior(b->rules, haul);
        addBehavior(b->rules, harvest);
    };
    std::vector<CampKit> kits;

    // The player's camp, in the middle
    const CampKit playerKit = placeCamp(FACTION_PLAYER, spawn.x, spawn.z + PLAYER_CAMP_OFFSET);
    addCampExtras(FACTION_PLAYER, playerKit);
    kits.push_back(playerKit);
    {
        Entity first = NULL_ENTITY;
        for (Entity u : w.view<Unit, Commandable>()) {
            if (first == NULL_ENTITY || u < first) first = u;
        }
        woodcutters.push_back({ first, playerKit });
    }
    const Vec2 middle = playerKit.placed ? Vec2{ playerKit.x, playerKit.z } : spawn;

    for (size_t ci = 0; ci < camps.size(); ++ci) {
        const FactionDef& f = *camps[ci];
        // North, east, south, west first; any more go between them
        static const float DIRS[8][2] = { { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 },
                                          { 0.7071f, -0.7071f }, { 0.7071f, 0.7071f }, { -0.7071f, 0.7071f }, { -0.7071f, -0.7071f } };
        float dirX = DIRS[ci % 8][0], dirZ = DIRS[ci % 8][1];
        if (ci >= 8) {
            const float a = 6.2831853f * (float)ci / (float)camps.size();
            dirX = std::cos(a);
            dirZ = std::sin(a);
        }
        const float dist = CAMP_DISTANCE * (ci >= 4 ? 1.4f : 1.0f);   // the corners sit farther out, clear of their neighbours
        CampKit kit = placeCamp(f.id, middle.x + dirX * dist, middle.z + dirZ * dist);
        // A map too small for the full distance: try closer in, down to just clear of the other claims
        for (float d = dist - 10.0f; !kit.placed && d >= CAMP_GAP; d -= 10.0f) {
            kit = placeCamp(f.id, middle.x + dirX * d, middle.z + dirZ * d);
        }
        addCampExtras(f.id, kit);
        // The camp's units stand around its flag
        const float cx = kit.placed ? kit.x : middle.x + dirX * dist, cz = kit.placed ? kit.z : middle.z + dirZ * dist;
        Entity firstUnit = NULL_ENTITY;
        const float radius = CAMP_RADIUS_BASE + CAMP_RADIUS_PER_UNIT * std::sqrt((float)f.units);
        int placed = 0;
        for (int attempt = 0; attempt < 4000 && placed < f.units; ++attempt) {
            GridPos g{ rng.range(tileAt(cx - radius), tileAt(cx + radius)), rng.range(tileAt(cz - radius), tileAt(cz + radius)) };
            const float dx = g.x + 0.5f - cx, dz = g.y + 0.5f - cz;
            if (dx * dx + dz * dz > radius * radius || !tileFree(g)) continue;
            const Entity e = placeUnit(g, f.id);
            if (firstUnit == NULL_ENTITY) firstUnit = e;
            ++placed;
        }
        for (int attempt = 0; attempt < 20000 && placed < f.units; ++attempt) {
            GridPos g{ rng.range(tileAt(spawn.x - ENEMY_RANGE), tileAt(spawn.x + ENEMY_RANGE)),
                       rng.range(tileAt(spawn.z - ENEMY_RANGE), tileAt(spawn.z + ENEMY_RANGE)) };
            const float dx = g.x + 0.5f - spawn.x, dz = g.y + 0.5f - spawn.z;
            if (dx * dx + dz * dz > ENEMY_RANGE * ENEMY_RANGE || !tileFree(g)) continue;
            const Entity e = placeUnit(g, f.id);
            if (firstUnit == NULL_ENTITY) firstUnit = e;
            ++placed;
        }
        kits.push_back(kit);
        woodcutters.push_back({ firstUnit, kit });
    }

    // Props on free ground near the spawn: PROP_COPIES of each kind (props.h), wagons included, each on its
    // own tile. They do not block movement. They go down a round of every kind at a time (wagon, campfire, ... forge,
    // then wagon again), so if the ground near the spawn ever runs out, every kind has been placed before any has its
    // last copies.
    std::vector<GridPos> propTiles;
    const int propTotal = PROP_PLACED_KIND_COUNT * PROP_COPIES;
    for (int attempt = 0; attempt < 400 * propTotal && (int)propTiles.size() < propTotal; ++attempt) {
        GridPos g{ rng.range(tileAt(spawn.x - PROP_RANGE), tileAt(spawn.x + PROP_RANGE)),
                   rng.range(tileAt(spawn.z - PROP_RANGE), tileAt(spawn.z + PROP_RANGE)) };
        if (!t.walkable(g.x, g.y)) continue;
        if (std::find(propTiles.begin(), propTiles.end(), g) != propTiles.end()) continue;
        // A tile a camp's kit already stands on is not free: the kit was placed first
        bool kitHere = false;
        for (Entity e : w.view<Prop, Transform>()) {
            const Transform* kt = w.get<Transform>(e);
            if (std::fabs(kt->x - (g.x + 0.5f)) < 0.5f && std::fabs(kt->z - (g.y + 0.5f)) < 0.5f) { kitHere = true; break; }
        }
        if (kitHere) continue;
        const int serial = (int)propTiles.size();
        const int kind = serial % PROP_PLACED_KIND_COUNT;
        propTiles.push_back(g);
        const float x = g.x + 0.5f, z = g.y + 0.5f;
        Entity prop = w.create();
        w.add(prop, Transform{ x, t.heightAt(x, z), z });
        w.add(prop, Prop{ kind, serial });
        if (hasStorage(propInfo(kind))) {
            // A storage prop starts with a few things that belong in it; a crafting prop (the campfire) starts empty
            w.add(prop, propInfo(kind).use == PropUse::Storage ? startingStorage(kind, rng) : Storage{});
        }
        if (propInfo(kind).use == PropUse::PassiveCrafting) {     // a forge starts cold, with an empty output
            w.add(prop, Output{});
            w.add(prop, Fuel{});
            w.add(prop, PropWork{});
        }
        if (propInfo(kind).lockLevel > 0) w.add(prop, Lock{ true, propInfo(kind).lockLevel });   // Lockable: starts locked
    }

    // The vaults go down last, once every other prop stands, each on its camp's foundation. Then each sealed claim's
    // props become its faction's, and each camp's woodcutter is given its vault to haul to.
    for (CampKit& kit : kits) placeCampVault(w, t, kit, &campSerial);
    settleClaimOwnership(w);
    for (auto& [unit, kit] : woodcutters) {
        for (const CampKit& k : kits) {
            if (k.flag == kit.flag) kit.vault = k.vault;
        }
        giveWoodcutting(unit, kit);
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
        if (w.has<Hostile>(u) || w.has<Neutral>(u)) {
            w.add(u, randomHostileGear(rng));
            // An NPC given a bow or a crossbow gets some of its ammo to shoot, so it fights at range
            const Equipment* worn = w.get<Equipment>(u);
            if (const RangedProfile* rp = worn ? rangedProfileOf(worn->items[GEAR_MAIN_HAND]) : nullptr) {
                std::vector<int> ammo;
                for (const ItemDef& d : itemDatabase()) {
                    if (hasTag(d, rp->ammoTag)) ammo.push_back(d.id);
                }
                Inventory* inv = w.get<Inventory>(u);
                if (inv && !ammo.empty()) {
                    addToInventory(*inv, ammo[(size_t)rng.range(0, (int)ammo.size() - 1)], rng.range(NPC_AMMO_MIN, NPC_AMMO_MAX));
                }
            }
        }
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
        if (!cannotTakeOrder(w, u)) units.push_back(u);
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
        if (cannotTakeOrder(w, u)) continue;
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
        if (!w.has<Commandable>(u) || !w.get<Transform>(u) || cannotTakeOrder(w, u)) continue;
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
        if (cannotTakeOrder(w, u)) continue;
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
void issueTalkOrder(World& w, const std::vector<Entity>& units, Entity npc, bool trade, bool shop) {
    if (!w.has<Unit>(npc) || !w.get<Transform>(npc)) return;
    for (Entity u : units) {
        if (!w.has<Commandable>(u) || !w.get<Transform>(u) || cannotTakeOrder(w, u)) continue;
        w.remove<SquadMember>(u);
        w.remove<SoloMove>(u);
        w.remove<PickupOrder>(u);
        w.remove<AttackOrder>(u);
        w.remove<TalkOrder>(u);
        endPropTasks(w, u);
        w.add(u, TalkOrder{ npc, trade, shop });
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
    queueUnitLabel(unit, itemLossLabelFor(s.count, s.item), true);   // "-Iron Ore" over its head
    queueSound(Sfx::Drop);
    Entity e = w.create();
    w.add(e, Transform{ tr->x, tr->y, tr->z });
    w.add(e, GroundItem{ s.item, s.count });
    s = ItemStack{};
    return true;
}

void needsSystem(World& w, int hours, double gameSeconds) {
    for (Entity e : w.view<Needs>()) {
        if (w.has<Corpse>(e)) continue;   // a corpse needs nothing
        Needs* n = w.get<Needs>(e);
        // A unit that moved at run pace in the hour that just ended pays double for its next tick. The mark is
        // used up here, so later hours cost the usual amount.
        int ticks = hours;
        if (hours > 0 && n->hurried) {
            ticks += 1;
            n->hurried = false;
        }
        tickNeeds(*n, ticks);

        // An Unconscious unit does not heal: only Doom falling to 0 in a bed wakes it
        if (w.has<Unconscious>(e)) continue;
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
                queueUnitLabel(u, itemGainLabel(count - left, itemName));   // "+Iron Ore" over its head
                queueSound(Sfx::Pickup);
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
    // Every unit that moves: player units by their orders, hostiles by their chase. An Unconscious unit does not move
    // and is not pushed, nor does it push anyone: it lies where it fell, or goes with the unit carrying it.
    std::vector<Entity> units = w.view<Unit, Transform>();
    units.erase(std::remove_if(units.begin(), units.end(), [&](Entity e) { return w.has<Unconscious>(e) || w.has<Carried>(e); }),
                units.end());
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
                if (std::sqrt(gx * gx + gz * gz) > reachOf(w, e) * CHASE_STOP_SHARE) {
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
        } else if (!w.has<Commandable>(e)) {
            // A unit that is not yours walks at its target while it has one, and stops once it is close enough to hit
            if (const Combat* c = w.get<Combat>(e); c && c->target != NULL_ENTITY) {
                if (const Transform* tt = w.get<Transform>(c->target)) {
                    const float gx = tt->x - tr->x, gz = tt->z - tr->z;
                    if (std::sqrt(gx * gx + gz * gz) > reachOf(w, e) * CHASE_STOP_SHARE) {
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
        if (w.has<PlayingDead>(u)) breakFree(w, u);
        if (w.has<Unconscious>(u) || w.has<Carried>(u)) continue;
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
        if (u == target || !w.has<Commandable>(u) || !w.get<Transform>(u) || cannotTakeOrder(w, u)) continue;
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
        if (u == ward || !w.has<Commandable>(u) || !w.get<Transform>(u) || cannotTakeOrder(w, u)) continue;
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
    // It is provoked by your faction (factionSystem keeps it hostile while that lasts), and turns at once
    if (!w.has<Unit>(unit) || w.has<Commandable>(unit)) return;
    provoke(w, unit, FACTION_PLAYER);
    if (w.has<Hostile>(unit)) return;
    w.remove<Neutral>(unit);
    w.add(unit, Hostile{});
    queueSound(Sfx::TurnHostile);
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
            const bool automatic = w.get<PropOrder>(u)->automatic;
            w.remove<PropOrder>(u);
            w.remove<SoloMove>(u);
            if (action == PropAction::Store) {
                // An Auto-Haul's step: what the unit carries that the prop takes goes in, if the prop is open to it
                if (!lockShutFor(w, prop, u)) storeCarried(w, u, prop, 0);
                continue;
            }
            PropArrival a;
            a.unit = u;
            a.prop = prop;
            a.action = action;
            a.automatic = automatic;
            const PropInfo& info = propInfo(w.get<Prop>(prop)->kind);
            const Lock* lock = w.get<Lock>(prop);
            if (action == PropAction::Unlock) {
                // Unlock: start picking the lock, if the unit has the skill and nobody else is at it
                a.craft = tryStartUnlock(w, u, prop);
                a.started = a.craft == WorkStart::Started;
                out.push_back(a);
                continue;
            }
            if (lockShutFor(w, prop, u)) {
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
                // A unit carrying someone to a bed or bedroll puts it in there, instead of lying down itself
                if (const Carrying* cr = w.get<Carrying>(u); cr && doomRecoveryPerHour(w.get<Prop>(prop)->kind) > 0.0f) {
                    const Entity carried = cr->unit;
                    const Entity occupant = propOccupant(w, prop);
                    if (w.has<Corpse>(carried)) {
                        a.corpse = true;
                    } else if (propInTheWay(w, u, prop)) {
                        a.unreachable = true;
                    } else if (occupant != NULL_ENTITY) {
                        a.taken = true;
                    } else if (Transform* ct = w.get<Transform>(carried)) {
                        w.remove<Carrying>(u);
                        w.remove<Carried>(carried);
                        w.remove<PlayingDead>(carried);
                        ct->x = pt->x;
                        ct->y = pt->y;
                        ct->z = pt->z;
                        w.add(carried, Occupying{ prop });
                        a.putInBed = true;
                        a.started = true;
                        queueSound(Sfx::Occupy);
                        queueLog(langf("MISC_PUTS_IN_BED", { nameOf(w, u), nameOf(w, carried), lang(info.nameKey) }), LogKind::Other);
                    }
                    break;
                }
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
                    queueSound(w.has<Lock>(prop) ? Sfx::LockedIn : Sfx::Occupy);
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
        // Picking a lock that is still shut to this unit, or working a prop that is not
        const bool shut = lockShutFor(w, prop, u);
        const bool valid = wk->unlocking ? shut : (isWorkProp(useOf(w, prop)) && !shut);
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
        // Each gain says which kind of prop it came from, for its sound
        const int propKind = w.get<Prop>(prop)->kind;
        auto addGain = [&](WorkGain g) {
            g.propKind = propKind;
            gains.push_back(g);
        };
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
                    addGain(WorkGain{ picker, 0, WorkGain::Kind::Unlocked });
                    queueLog(langf("MISC_UNLOCKS", { nameOf(w, picker), lang(info.nameKey) }), LogKind::Other);
                    grantXp(w, picker, STAT_LOCKPICKING, LOCKPICK_XP_SUCCESS, lang("MISC_REASON_LOCKPICKING"));
                } else {
                    addGain(WorkGain{ picker, 0, WorkGain::Kind::LockpickFailed });
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
                    giveOrDrop(w, got, outItem, 1);   // checked to fit above; should it not, it falls at the cook's feet
                    addGain(WorkGain{ got, outItem });
                    queueLog(langf("MISC_CRAFTS", { nameOf(w, got), outName }), LogKind::Pickup);
                    grantXp(w, got, info.stat, WORK_XP_PER_ITEM, lang("MISC_REASON_CRAFTING"));
                } else {
                    // Training: no item, only the skill's XP
                    addGain(WorkGain{ got, info.stat, WorkGain::Kind::Trained, WORK_XP_PER_ITEM });
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
            // Now and then a resource prop gives its other item instead (a Poison Mushroom, one time in ten)
            int gives = info.item;
            if (info.altItem > 0 && info.altOneIn > 0 && rng.range(1, info.altOneIn) == 1) gives = info.altItem;
            const std::string itemName = itemNameFor(gives);
            Inventory* inv = w.get<Inventory>(got);
            if (inv && addToInventory(*inv, gives, 1) == 0) {
                // A unit that is not yours (a camp's woodcutter) gathers quietly: no label, log line or sound
                const bool yours = w.has<Commandable>(got);
                if (yours) {
                    addGain(WorkGain{ got, gives });
                    queueLog(langf("MISC_GATHERS", { nameOf(w, got), itemName }), LogKind::Pickup);
                }
                grantXp(w, got, info.stat, WORK_XP_PER_ITEM, yours ? lang("MISC_REASON_GATHERING") : std::string());
            } else {
                // No room: the item falls at the unit's feet, and the unit stops working
                if (const Transform* ut = w.get<Transform>(got)) {
                    const Entity drop = w.create();
                    w.add(drop, Transform{ ut->x, ut->y, ut->z });
                    w.add(drop, GroundItem{ gives, 1 });
                }
                queueLog(langf("MISC_GATHER_NO_ROOM", { nameOf(w, got), itemName }), LogKind::Drop);
                queueUnitLabel(got, itemLossLabel(1, itemName), true);   // it fell at the unit's feet
                queueSound(Sfx::Drop);
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

int storeCarried(World& w, Entity unit, Entity prop, int item) {
    Inventory* inv = w.get<Inventory>(unit);
    Storage* st = w.get<Storage>(prop);
    const Prop* p = w.get<Prop>(prop);
    if (!inv || !st || !p) return 0;
    int moved = 0;
    int movedItem = 0;
    for (ItemStack& s : inv->slots) {
        if (s.item <= 0 || s.count <= 0) continue;
        if (item != 0 && s.item != item) continue;
        if (!propAcceptsItem(p->kind, s.item)) continue;
        const int left = addToSlots(st->slots.data(), STORAGE_SLOTS, s.item, s.count);
        moved += s.count - left;
        if (s.count - left > 0) movedItem = s.item;
        s.count = left;
        if (left <= 0) s = ItemStack{};
    }
    if (moved > 0) {
        propStorageChanged(w, prop);
        if (w.has<Commandable>(unit)) {
            queueLog(langf("MISC_AUTO_HAUL_STORES", { nameOf(w, unit), std::to_string(moved), itemNameFor(movedItem),
                                                    lang(propInfo(p->kind).nameKey) }), LogKind::Pickup);
        }
    }
    return moved;
}

namespace {

// How many of an item a unit carries
int carriedCount(World& w, Entity u, int item) {
    int n = 0;
    if (const Inventory* inv = w.get<Inventory>(u)) {
        for (const ItemStack& s : inv->slots) {
            if (s.item == item && s.count > 0) n += s.count;
        }
    }
    return n;
}

// A unit's faction, the Player Faction for one with none
int unitFaction(World& w, Entity u) {
    const Faction* f = w.get<Faction>(u);
    return f ? f->id : FACTION_PLAYER;
}

// Whether a prop stands inside a claim of this faction, and so may be worked or hauled to by its automation
bool inOwnClaim(World& w, const std::vector<Claim>& claims, Entity prop, int faction) {
    const Transform* t = w.get<Transform>(prop);
    return t && claimAt(claims, t->x, t->z, faction) != nullptr;
}

// Whether a unit is full for an item: no room for another, or another would weigh it down to Encumbered (and soon to a
// stop)
bool fullFor(World& w, Entity u, int item) {
    const Inventory* inv = w.get<Inventory>(u);
    if (!inv || slotsRoomFor(inv->slots.data(), INVENTORY_SLOTS, item) <= 0) return true;
    const ItemDef* def = findItem(item);
    return encumbranceFor(carriedWeightOf(w, u) + (def ? def->weight : 0.0f), statsOf(w, u)) >= ENCUMBERED_SHARE;
}

// Whether an Auto-Haul can be done now: its prop is there, open to the unit, in a claim of the unit's faction, and has
// room for at least one item the unit carries. item 0: any item the prop takes; otherwise only that item.
bool haulable(World& w, const std::vector<Claim>& claims, Entity u, const BehaviorRule& r, int item = 0) {
    const Prop* p = w.get<Prop>(r.prop);
    const Storage* st = w.get<Storage>(r.prop);
    const Inventory* inv = w.get<Inventory>(u);
    if (!p || !st || !inv || lockShutFor(w, r.prop, u) || !inOwnClaim(w, claims, r.prop, unitFaction(w, u))) return false;
    for (const ItemStack& s : inv->slots) {
        if (s.item <= 0 || s.count <= 0 || (item != 0 && s.item != item)) continue;
        if (propAcceptsItem(p->kind, s.item) && slotsRoomFor(st->slots.data(), STORAGE_SLOTS, s.item) > 0) return true;
    }
    return false;
}

// Whether one of the unit's Auto-Hauls that is on could take this item now
bool canHaulItem(World& w, const std::vector<Claim>& claims, Entity u, const Behaviors& b, int item) {
    for (const BehaviorRule& r : b.rules) {
        if (r.on && r.kind == BehaviorKind::AutoHaul && haulable(w, claims, u, r, item)) return true;
    }
    return false;
}

// Whether an Auto-Harvest has a load: the unit carries AUTO_LOAD of what it gathers and an Auto-Haul can take it, or
// it has no room for more
bool harvestLoaded(World& w, const std::vector<Claim>& claims, Entity u, const Behaviors& b, int item) {
    if (fullFor(w, u, item)) return true;
    return carriedCount(w, u, item) >= AUTO_LOAD && canHaulItem(w, claims, u, b, item);
}

// The nearest resource prop of the kind an Auto-Harvest works, in a claim of the unit's faction, open to it and with
// room for another worker. NULL_ENTITY when there is none.
Entity harvestTarget(World& w, const std::vector<Claim>& claims, Entity u, int kind) {
    const Transform* ut = w.get<Transform>(u);
    if (!ut || kind < 0 || kind >= PROP_KIND_COUNT || propInfo(kind).use != PropUse::Resource) return NULL_ENTITY;
    const int faction = unitFaction(w, u);
    Entity best = NULL_ENTITY;
    float bestD2 = 0.0f;
    for (Entity p : w.view<Prop, Transform>()) {
        if (w.get<Prop>(p)->kind != kind || !inOwnClaim(w, claims, p, faction) || lockShutFor(w, p, u)) continue;
        if (workersAt(w, p) >= propInfo(kind).workers) continue;
        const Transform* pt = w.get<Transform>(p);
        const float d2 = (pt->x - ut->x) * (pt->x - ut->x) + (pt->z - ut->z) * (pt->z - ut->z);
        if (best == NULL_ENTITY || d2 < bestD2) {
            best = p;
            bestD2 = d2;
        }
    }
    return best;
}

// Whether a unit is busy with something that is not its automation: an order of yours, a seat or bed, carrying someone
bool onOtherOrder(World& w, Entity u, const Behaviors& b) {
    if (const PropOrder* po = w.get<PropOrder>(u); po && !po->automatic) return true;
    if (const Working* wk = w.get<Working>(u); wk && (wk->unlocking || !b.busy || wk->prop != b.stepProp)) return true;
    if (w.has<Occupying>(u) || w.has<FollowOrder>(u) || w.has<DefendOrder>(u) || w.has<PickupOrder>(u) ||
        w.has<LootOrder>(u) || w.has<CarryOrder>(u) || w.has<PutDownOrder>(u) || w.has<TalkOrder>(u) ||
        w.has<Carrying>(u) || w.has<AttackOrder>(u)) {
        return true;
    }
    if (const SoloMove* so = w.get<SoloMove>(u); so && so->next < so->waypoints.size() && !w.has<PropOrder>(u)) return true;
    if (const SquadMember* sm = w.get<SquadMember>(u)) {
        if (const Squad* sq = w.get<Squad>(sm->squad); sq && !sq->arrived) return true;
    }
    return false;
}

// Sends a unit on an automated step: it walks to the prop, and in reach does the action
void autoGo(World& w, Terrain& t, Entity u, Entity prop, PropAction action) {
    const Transform* pt = w.get<Transform>(prop);
    if (!pt) return;
    w.remove<SquadMember>(u);
    w.remove<PickupOrder>(u);
    w.remove<TalkOrder>(u);
    endPropTasks(w, u);
    w.remove<IdleSpot>(u);
    w.add(u, SoloMove{});
    planSolo(w, t, u, Vec2{ pt->x, pt->z });
    w.add(u, PropOrder{ prop, action, true });
}

// Ends the automation's own step, if the unit is on one
void stopAutoStep(World& w, Entity u, Behaviors& b) {
    if (const PropOrder* po = w.get<PropOrder>(u); po && po->automatic) {
        w.remove<PropOrder>(u);
        w.remove<SoloMove>(u);
    }
    if (const Working* wk = w.get<Working>(u); wk && !wk->unlocking && b.busy && wk->prop == b.stepProp) w.remove<Working>(u);
    b.busy = false;
}

// The behavior in the list that the unit's step is for, or nullptr if it was deleted
BehaviorRule* stepRule(Behaviors& b) {
    for (BehaviorRule& r : b.rules) {
        if (sameBehavior(r, b.step)) return &r;
    }
    return nullptr;
}

// Tries to start an automated behavior's step. False, with nothing changed, when it can't be done now.
bool startStep(World& w, Terrain& t, const std::vector<Claim>& claims, Entity u, Behaviors& b, const BehaviorRule& r) {
    Entity prop = NULL_ENTITY;
    PropAction action = PropAction::Use;
    if (r.kind == BehaviorKind::AutoHaul) {
        if (!haulable(w, claims, u, r)) return false;
        prop = r.prop;
        action = PropAction::Store;
    } else {
        if (harvestLoaded(w, claims, u, b, propInfo(r.propKind).item)) return false;   // a load waiting, or no room
        prop = harvestTarget(w, claims, u, r.propKind);
        if (prop == NULL_ENTITY) return false;
    }
    b.busy = true;
    b.step = r;
    b.stepProp = prop;
    autoGo(w, t, u, prop, action);
    return true;
}

}  // namespace

void automationSystem(World& w, Terrain& t, float dt) {
    const std::vector<Claim> claims = claimsOf(w);
    for (Entity u : w.view<Behaviors, Transform>()) {
        Behaviors* b = w.get<Behaviors>(u);
        bool any = false;
        for (BehaviorRule& r : b->rules) {
            if (!isAutomated(r.kind)) continue;
            r.cooldown = std::max(0.0f, r.cooldown - dt);
            any = any || r.on;
        }
        // The step's behavior was switched off or deleted: the step stops
        if (b->busy) {
            const BehaviorRule* current = stepRule(*b);
            if (!current || !current->on) stopAutoStep(w, u, *b);
        }
        if (!any) continue;
        const bool yours = w.has<Commandable>(u);
        const float resume = yours ? AUTO_RESUME_SECONDS : 0.0f;
        // Out cold, carried, shut in or downed: nothing to do
        const Needs* n = w.get<Needs>(u);
        if (w.has<Unconscious>(u) || w.has<Carried>(u) || w.has<PlayingDead>(u) || lockedIn(w, u) || (n && n->hp <= 0)) {
            b->busy = false;
            continue;
        }
        // A fight comes first: the step stops, and the automation waits for the fight to end
        const Combat* c = w.get<Combat>(u);
        if ((c && c->target != NULL_ENTITY) || w.has<AttackOrder>(u)) {
            if (b->busy) stopAutoStep(w, u, *b);
            b->wait = resume;
            continue;
        }
        const PropOrder* po = w.get<PropOrder>(u);
        const bool going = po && po->automatic;
        const Working* wk = w.get<Working>(u);
        const bool working = b->busy && wk && !wk->unlocking && wk->prop == b->stepProp;

        if (b->busy) {
            if (going) continue;   // on its way
            if (working) {
                // A harvest goes on until the unit has a load for its haul, or no room
                if (!harvestLoaded(w, claims, u, *b, propInfo(b->step.propKind).item)) continue;
                w.remove<Working>(u);
                b->busy = false;
            } else if (onOtherOrder(w, u, *b)) {   // you gave it something else to do
                b->busy = false;
                b->wait = resume;
                continue;
            } else {
                // The step ended by itself: the haul was put in, or the harvest did not start (the prop had all its
                // workers, or the way was blocked). A harvest that did not start waits before it is tried again.
                b->busy = false;
                if (b->step.kind == BehaviorKind::AutoHarvest) {
                    if (BehaviorRule* r = stepRule(*b)) r->cooldown = BEHAVIOR_RETRY_SECONDS;
                }
            }
        } else {
            if (onOtherOrder(w, u, *b) || going) {
                // Busy with something of yours: wait until it has been idle a while
                b->wait = resume;
                continue;
            }
            if (b->wait > 0.0f) {
                b->wait -= dt;
                continue;
            }
        }

        // The next step: the first automated behavior, from the top, that is on and can be done now. One that can't
        // waits BEHAVIOR_RETRY_SECONDS before it is tried again.
        for (BehaviorRule& r : b->rules) {
            if (!isAutomated(r.kind) || !r.on || r.cooldown > 0.0f) continue;
            if (startStep(w, t, claims, u, *b, r)) break;
            r.cooldown = BEHAVIOR_RETRY_SECONDS;
        }
    }
}

WorkStart tryStartWork(World& w, Entity unit, Entity prop) {
    const Prop* p = w.get<Prop>(prop);
    if (!p || !isWorkProp(propInfo(p->kind).use) || !w.get<Transform>(unit)) return WorkStart::TooFar;
    if (const Working* wk = w.get<Working>(unit); wk && wk->prop == prop) return WorkStart::Started;   // already at it
    if (flatGap(w, unit, prop) > STORAGE_CLOSE_RANGE) return WorkStart::TooFar;
    if (const Needs* n = w.get<Needs>(unit); n && n->hp <= 0) return WorkStart::TooFar;
    if (lockShutFor(w, prop, unit)) return WorkStart::Locked;
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
    if (!lockShutFor(w, prop, unit)) return WorkStart::NotLocked;
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
    // A caught lockpick: one of your units picking another faction's lock is an attack on that faction, and costs
    // its standing the way knocking one of its units out does
    if (const Faction* f = w.get<Faction>(unit); (!f || f->id == FACTION_PLAYER) && lock->faction >= 0 &&
                                                 lock->faction != FACTION_PLAYER) {
        const int now = changePlayerReputation(lock->faction, -REP_LOCKPICK_PENALTY);
        queueLog(langf("MISC_REPUTATION_DROPS", { factionName(lock->faction), std::to_string(REP_LOCKPICK_PENALTY),
                                                  std::to_string(now) }), LogKind::Other);
    }
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
    if (w.has<Corpse>(u)) return lang("UILABEL_TASK_DEAD");
    if (w.has<Unconscious>(u)) return lang("UILABEL_TASK_UNCONSCIOUS");
    if (w.has<PlayingDead>(u)) return lang("UILABEL_TASK_PLAYING_DEAD");
    if (w.has<Carried>(u)) return lang("UILABEL_TASK_CARRIED");
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
    if (w.has<Carrying>(u) || w.has<CarryOrder>(u)) return lang("UILABEL_TASK_CARRYING");
    if (const PropOrder* po = w.get<PropOrder>(u); po && po->action == PropAction::Store) return lang("UILABEL_TASK_STORING");
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
    // Out cold or dead, that is all it shows
    if (w.has<Corpse>(u)) return { EMOTE_DEAD };
    if (w.has<Unconscious>(u)) return { EMOTE_UNCONSCIOUS };
    if (w.has<PlayingDead>(u)) return { EMOTE_PLAYING_DEAD };
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
    if (info.use == PropUse::PassiveCrafting && info.item == 0) return item == info.input;   // a Vault: its upkeep only
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
    return lockShutFor(w, oc->prop, u);
}

PassiveState passiveStateOf(World& w, Entity prop) {
    const Prop* p = w.get<Prop>(prop);
    const Output* out = w.get<Output>(prop);
    const Fuel* fuel = w.get<Fuel>(prop);
    if (!p || !out || !fuel) return PassiveState::NoFuel;
    const PropInfo& info = propInfo(p->kind);
    if (info.item > 0 && !slotsCanTake(out->slots.data(), STORAGE_SLOTS, info.item, 1)) return PassiveState::OutputFull;
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
            // A prop with no output item (a Vault's upkeep) finishes nothing: its hour of fuel is all it does.
            if (pw.progress >= 1.0 - 1e-9) {
                if (info.item > 0) {
                    if (!slotsCanTake(out.slots.data(), STORAGE_SLOTS, info.item, 1)) break;
                    addToSlots(out.slots.data(), STORAGE_SLOTS, info.item, 1);
                    ++pw.made;   // the ring hops for it
                    queueSound(Sfx::DoneForge);
                }
                pw.progress = 0.0;
            }
            // No room for the next item: pause, burning nothing
            if (info.item > 0 && !slotsCanTake(out.slots.data(), STORAGE_SLOTS, info.item, 1)) break;
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

bool cannotAct(World& w, Entity u) {
    // Out cold, shut in a cage, or carried (one playing dead can still break free with an order: cannotTakeOrder)
    return w.has<Unconscious>(u) || lockedIn(w, u) || (w.has<Carried>(u) && !w.has<PlayingDead>(u));
}

bool cannotTakeOrder(World& w, Entity u) {
    if (w.has<PlayingDead>(u)) breakFree(w, u);
    return cannotAct(w, u);
}

void breakFree(World& w, Entity u) {
    w.remove<PlayingDead>(u);
    const Carried* c = w.get<Carried>(u);
    if (!c) return;
    const Entity carrier = c->by;
    w.remove<Carried>(u);
    if (const Carrying* cr = w.get<Carrying>(carrier); cr && cr->unit == u) w.remove<Carrying>(carrier);
    if (Transform* t = w.get<Transform>(u)) {
        if (const Transform* ct = w.get<Transform>(carrier)) *t = *ct;
    }
    queueLog(langf("MISC_BREAKS_FREE", { nameOf(w, u) }), LogKind::Other);
}

bool canBeCarried(World& w, Entity target) {
    if (!w.has<Unit>(target) || !w.has<Transform>(target) || w.has<Carried>(target) || w.has<Carrying>(target)) return false;
    return w.has<Unconscious>(target) || w.has<Commandable>(target);
}

// ---- Unconscious, Doom and Corpses ----

float doomPerHour(const Stats& s) {
    const int vit = std::clamp(levelOf(s, STAT_VITALITY), 1, MAX_LEVEL);
    return 20.0f + 30.0f * (1.0f - (float)vit / 100.0f);
}

float doomRecoveryPerHour(int propKind) {
    if (propKind == PROP_BED) return 20.0f;
    if (propKind == PROP_BEDROLL) return 10.0f;
    return 0.0f;
}

void knockOut(World& w, Entity u) {
    if (!w.has<Unit>(u) || w.has<Unconscious>(u)) return;
    // Whatever it was doing ends. It keeps its bed if it is lying on one, and drops anyone it carried.
    const Entity bed = occupiedProp(w, u);
    const Prop* bp = bed != NULL_ENTITY ? w.get<Prop>(bed) : nullptr;
    const bool keepBed = bp && doomRecoveryPerHour(bp->kind) > 0.0f;
    if (w.has<Carrying>(u)) putDown(w, u);
    if (const SquadMember* sm = w.get<SquadMember>(u)) {
        if (Squad* sq = w.get<Squad>(sm->squad); sq && sq->size > 0) --sq->size;
        w.remove<SquadMember>(u);
    }
    w.remove<SoloMove>(u);
    w.remove<PickupOrder>(u);
    w.remove<AttackOrder>(u);
    w.remove<TalkOrder>(u);
    endPropTasks(w, u);
    w.remove<IdleSpot>(u);
    if (keepBed) w.add(u, Occupying{ bed });
    if (Combat* c = w.get<Combat>(u)) c->target = NULL_ENTITY;
    if (Needs* n = w.get<Needs>(u)) {
        if (n->hp != 0) n->changed |= NEEDS_HP_CHANGED;
        n->hp = 0;
    }
    w.remove<PlayingDead>(u);
    // Who knocked it out: a hit from a unit of some faction, or nothing (hunger). Your units knocking out a unit of
    // another faction cost reputation with it.
    Unconscious un;
    if (Combat* c = w.get<Combat>(u)) {
        un.koFaction = c->downedByFaction;
        c->downedByFaction = -1;
    }
    w.add(u, un);
    queueLog(langf("MISC_FALLS_UNCONSCIOUS", { nameOf(w, u) }), LogKind::Other);
    queueSound(Sfx::Downed);
    const int victim = factionOf(w, u);
    if (un.koFaction == FACTION_PLAYER && victim != FACTION_PLAYER) {
        const int now = changePlayerReputation(victim, -REP_KO_PENALTY);
        queueLog(langf("MISC_REPUTATION_DROPS", { factionName(victim), std::to_string(REP_KO_PENALTY), std::to_string(now) }),
                 LogKind::Other);
    }
}

void downedSystem(World& w) {
    for (Entity u : w.view<Unit, Needs>()) {
        if (w.get<Needs>(u)->hp <= 0 && !w.has<Unconscious>(u)) knockOut(w, u);
    }
}

DoomChanges doomSystem(World& w, double gameSeconds) {
    DoomChanges out;
    const float hours = (float)(gameSeconds / 3600.0);
    if (hours <= 0.0f) return out;
    for (Entity u : w.view<Unconscious>()) {
        if (w.has<Corpse>(u)) continue;
        Unconscious* un = w.get<Unconscious>(u);
        const Entity bed = occupiedProp(w, u);
        const Prop* bp = bed != NULL_ENTITY ? w.get<Prop>(bed) : nullptr;
        const float rest = bp ? doomRecoveryPerHour(bp->kind) : 0.0f;
        if (rest > 0.0f) {
            // In a bed or bedroll Doom falls, and at 0 the unit wakes with a little health, still lying there
            un->doom = std::clamp(un->doom - rest * hours, 0.0f, 100.0f);
            if (un->doom <= 0.0f) {
                w.remove<Unconscious>(u);
                if (Needs* n = w.get<Needs>(u)) {
                    n->hp = std::min(std::max(n->maxHp, 1), RECOVERY_HEALTH);
                    n->changed |= NEEDS_HP_CHANGED;
                }
                queueLog(langf("MISC_RECOVERS", { nameOf(w, u) }), LogKind::Other);
                out.woke.push_back(u);
            }
            continue;
        }
        un->doom = std::clamp(un->doom + doomPerHour(statsOf(w, u)) * hours, 0.0f, 100.0f);
        if (un->doom >= 100.0f) {
            // Dead for good: a corpse, no longer anyone's to command, still there to carry and to loot
            un->doom = 100.0f;
            w.add(u, Corpse{});
            w.remove<Commandable>(u);
            w.remove<Selected>(u);
            w.remove<Occupying>(u);
            queueLog(langf("MISC_DIES", { nameOf(w, u) }), LogKind::Other);
            out.died.push_back(u);
            // Knocked out by your units, and dead before it woke: more reputation lost with its faction
            const int victim = factionOf(w, u);
            if (un->koFaction == FACTION_PLAYER && victim != FACTION_PLAYER) {
                const int now = changePlayerReputation(victim, -REP_DEATH_PENALTY);
                queueLog(langf("MISC_REPUTATION_DROPS", { factionName(victim), std::to_string(REP_DEATH_PENALTY),
                                                          std::to_string(now) }),
                         LogKind::Other);
            }
        }
    }
    return out;
}

bool issueCarryOrder(World& w, Terrain& t, const std::vector<Entity>& units, Entity target) {
    const Transform* tt = w.get<Transform>(target);
    if (!tt || !canBeCarried(w, target)) return false;
    for (Entity u : units) {
        if (u == target || !w.has<Commandable>(u) || !w.get<Transform>(u) || w.has<Carrying>(u)) continue;
        if (cannotTakeOrder(w, u)) continue;
        w.remove<SquadMember>(u);
        w.remove<PickupOrder>(u);
        w.remove<AttackOrder>(u);
        w.remove<TalkOrder>(u);
        endPropTasks(w, u);
        w.add(u, SoloMove{});
        planSolo(w, t, u, Vec2{ tt->x, tt->z });
        w.add(u, CarryOrder{ target });
        return true;
    }
    return false;
}

void issuePutDownOrder(World& w, Terrain& t, const std::vector<Entity>& units, Vec2 point) {
    for (Entity u : units) {
        if (!w.has<Carrying>(u) || !w.get<Transform>(u) || cannotTakeOrder(w, u)) continue;
        w.remove<SquadMember>(u);
        w.remove<PickupOrder>(u);
        w.remove<AttackOrder>(u);
        w.remove<TalkOrder>(u);
        endPropTasks(w, u);
        w.add(u, SoloMove{});
        planSolo(w, t, u, point);
        w.add(u, PutDownOrder{ point.x, point.z });
    }
}

void putDown(World& w, Entity carrier) {
    const Carrying* cr = w.get<Carrying>(carrier);
    if (!cr) return;
    const Entity carried = cr->unit;
    w.remove<Carrying>(carrier);
    w.remove<Carried>(carried);
    w.remove<PlayingDead>(carried);
    if (Transform* ct = w.get<Transform>(carried)) {
        if (const Transform* t = w.get<Transform>(carrier)) *ct = *t;
        queueLog(langf("MISC_PUTS_DOWN", { nameOf(w, carrier), nameOf(w, carried) }), LogKind::Other);
        queueSound(Sfx::PutDown);
    }
}

CarryEvents carrySystem(World& w) {
    CarryEvents ev;
    std::vector<Entity>& picked = ev.picked;
    auto gap = [&](Entity a, Entity b) {
        const Transform* ta = w.get<Transform>(a);
        const Transform* tb = w.get<Transform>(b);
        return ta && tb ? std::hypot(ta->x - tb->x, ta->z - tb->z) : 1e9f;
    };
    // Walking to pick someone up: in reach, it is lifted, unless someone else got there first
    for (Entity u : w.view<CarryOrder, Transform>()) {
        const Entity target = w.get<CarryOrder>(u)->target;
        if (!canBeCarried(w, target) || w.has<Carrying>(u) || cannotAct(w, u)) {
            w.remove<CarryOrder>(u);
            w.remove<SoloMove>(u);
            continue;
        }
        if (gap(u, target) <= CARRY_REACH) {
            w.remove<CarryOrder>(u);
            w.remove<SoloMove>(u);
            // Whatever it was doing ends: it is lifted, out of a bed if it lay in one
            if (const SquadMember* sm = w.get<SquadMember>(target)) {
                if (Squad* sq = w.get<Squad>(sm->squad); sq && sq->size > 0) --sq->size;
                w.remove<SquadMember>(target);
            }
            w.remove<SoloMove>(target);
            w.remove<PickupOrder>(target);
            w.remove<AttackOrder>(target);
            w.remove<TalkOrder>(target);
            endPropTasks(w, target);
            w.remove<IdleSpot>(target);
            if (Combat* c = w.get<Combat>(target)) c->target = NULL_ENTITY;
            w.add(target, Carried{ u });
            w.add(u, Carrying{ target });
            queueLog(langf("MISC_PICKS_UP_UNIT", { nameOf(w, u), nameOf(w, target) }), LogKind::Other);
            queueSound(Sfx::CarryUp);
            picked.push_back(u);
            continue;
        }
        // Stopped short with no route left: it can't be reached, so give up
        const SoloMove* so = w.get<SoloMove>(u);
        if (!so || so->next >= so->waypoints.size()) {
            w.remove<CarryOrder>(u);
            w.remove<SoloMove>(u);
        }
    }
    // Walking to put someone down: at the spot, or as near as the route got, it is put down
    for (Entity u : w.view<PutDownOrder, Transform>()) {
        if (!w.has<Carrying>(u)) {
            w.remove<PutDownOrder>(u);
            continue;
        }
        const PutDownOrder pd = *w.get<PutDownOrder>(u);
        const Transform* tr = w.get<Transform>(u);
        const SoloMove* so = w.get<SoloMove>(u);
        const bool there = std::hypot(pd.x - tr->x, pd.z - tr->z) <= 0.6f;
        if (there || !so || so->next >= so->waypoints.size()) {
            w.remove<PutDownOrder>(u);
            w.remove<SoloMove>(u);
            putDown(w, u);
        }
    }
    // Every carried unit goes where its carrier is. A carrier that is gone, knocked out or no longer holding it drops it.
    // Awake: with its own squad it rides along; with a hostile it plays dead; with anyone else it gets free at once.
    for (Entity c : w.view<Carried>()) {
        const Entity by = w.get<Carried>(c)->by;
        const Carrying* cr = w.get<Carrying>(by);
        if (!cr || cr->unit != c || w.has<Unconscious>(by) || !w.has<Transform>(by)) {
            w.remove<Carried>(c);
            w.remove<PlayingDead>(c);
            if (cr && cr->unit == c) w.remove<Carrying>(by);
            continue;
        }
        if (w.has<Unconscious>(c)) {
            w.remove<PlayingDead>(c);
        } else if (w.has<Commandable>(c) && w.has<Commandable>(by)) {
            w.remove<PlayingDead>(c);
        } else if (w.has<Hostile>(by)) {
            if (!w.has<PlayingDead>(c)) {
                w.add(c, PlayingDead{});
                queueLog(langf("MISC_PLAYS_DEAD", { nameOf(w, c) }), LogKind::Other);
            }
        } else {
            breakFree(w, c);
            continue;
        }
        if (Transform* ct = w.get<Transform>(c)) *ct = *w.get<Transform>(by);
    }
    for (Entity u : w.view<Carrying>()) {
        const Entity c = w.get<Carrying>(u)->unit;
        const Carried* cd = w.get<Carried>(c);
        if (!cd || cd->by != u) w.remove<Carrying>(u);
    }
    // Walking to loot: in reach, the game opens the panel
    for (Entity u : w.view<LootOrder, Transform>()) {
        const Entity target = w.get<LootOrder>(u)->target;
        if (!w.has<Unconscious>(target) || w.has<Unconscious>(u)) {
            w.remove<LootOrder>(u);
            w.remove<SoloMove>(u);
            continue;
        }
        if (gap(u, target) <= CARRY_REACH) {
            w.remove<LootOrder>(u);
            w.remove<SoloMove>(u);
            ev.loot.push_back({ u, target });
            continue;
        }
        const SoloMove* so = w.get<SoloMove>(u);
        if (!so || so->next >= so->waypoints.size()) {
            w.remove<LootOrder>(u);
            w.remove<SoloMove>(u);
        }
    }
    return ev;
}

bool issueLootOrder(World& w, Terrain& t, const std::vector<Entity>& units, Entity target) {
    const Transform* tt = w.get<Transform>(target);
    if (!w.has<Unconscious>(target) || !tt) return false;
    for (Entity u : units) {
        if (u == target || !w.has<Commandable>(u) || !w.has<Inventory>(u) || !w.get<Transform>(u) || cannotTakeOrder(w, u)) continue;
        w.remove<SquadMember>(u);
        w.remove<PickupOrder>(u);
        w.remove<AttackOrder>(u);
        w.remove<TalkOrder>(u);
        endPropTasks(w, u);
        w.add(u, SoloMove{});
        planSolo(w, t, u, Vec2{ tt->x, tt->z });
        w.add(u, LootOrder{ target });
        return true;
    }
    return false;
}

bool lootTake(World& w, Entity looter, Entity target, bool gear, int slot, std::string& why) {
    Inventory* into = w.get<Inventory>(looter);
    if (!into || !w.has<Unconscious>(target)) return false;
    int item = 0, count = 0;
    if (gear) {
        Equipment* worn = w.get<Equipment>(target);
        if (!worn || slot < 0 || slot >= GEAR_SLOTS || worn->items[slot] == 0) return false;
        item = worn->items[slot];
        count = 1;
        if (addToInventory(*into, item, 1) > 0) {
            why = lang("MISC_LOOT_NO_ROOM");
            return false;
        }
        worn->items[slot] = 0;
    } else {
        Inventory* from = w.get<Inventory>(target);
        if (!from || slot < 0 || slot >= INVENTORY_SLOTS || from->slots[slot].item == 0) return false;
        ItemStack& st = from->slots[slot];
        item = st.item;
        const int left = addToInventory(*into, st.item, st.count);
        count = st.count - left;
        if (count <= 0) {
            why = lang("MISC_LOOT_NO_ROOM");
            return false;
        }
        if (left > 0) st.count = left;
        else st = ItemStack{};
    }
    queueUnitLabel(looter, itemGainLabelFor(count, item));
    queueSound(Sfx::Take);
    return true;
}

int lootAll(World& w, Entity looter, Entity target) {
    int moved = 0;
    std::string why;
    for (int g = 0; g < GEAR_SLOTS; ++g) moved += lootTake(w, looter, target, true, g, why) ? 1 : 0;
    for (int i = 0; i < INVENTORY_SLOTS; ++i) moved += lootTake(w, looter, target, false, i, why) ? 1 : 0;
    return moved;
}
