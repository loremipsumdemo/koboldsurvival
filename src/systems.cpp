#include "systems.h"
#include "need_behaviors.h"
#include "claims.h"
#include "mobs.h"
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

constexpr float UNIT_CLEARANCE = 0.2f;   // extra gap kept between units in a formation
constexpr float UNIT_BOX = 1.0f;         // the side of the square every unit and mob takes up: one tile
constexpr float PICKUP_REACH = 2.0f;     // a unit this close to a ground item (in tiles) can take it
constexpr int CHAIN_REPLANS = 3;         // new routes a unit on a pickup chain tries to an item it stopped short of

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

// Stops a unit's pickup: the order and its walk both end. A pickup chain the unit is on goes on (pickupChainSystem).
void endPickup(World& w, Entity u) {
    w.remove<PickupOrder>(u);
    w.remove<SoloMove>(u);
}

// Another order replaces the unit's pickup: its pickup order and any pickup chain it was on both end
void cancelPickup(World& w, Entity u) {
    w.remove<PickupOrder>(u);
    w.remove<PickupChain>(u);
}

// Plans a new route for an active squad's center. Only the center paths; members follow slots.
void planActiveSquad(World& w, Terrain& t, Entity squadE, Vec2 target) {
    ActiveSquad* sq = w.get<ActiveSquad>(squadE);
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

// Formation spacing grows with the active squad's size, up to the full spacing at 10 units.
// Small active squads stay compact so they don't spread out around the click point.
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
constexpr float CAMP_GAP = 2.0f * CLAIM_RADIUS + 6.0f;
constexpr float CAMP_MOB_RADIUS = 10.0f;    // a camp's mobs stand within this many tiles of its flag
constexpr float CAMP_CAGE_GAP = 1.5f;       // a camp mob's cage stands at least this far from any other prop
constexpr float WILD_MOB_NEAR = 30.0f;      // the wild herds are this far past the farthest camp's claim, and up to
constexpr float WILD_MOB_FAR = 70.0f;       // this far
constexpr float WILD_HERD_RADIUS = 8.0f;    // a herd's mobs stand within this many tiles of its middle   // the least distance between two camps' flags: no claims overlap
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
        const bool attribute = statInfo((StatId)i).group == StatGroup::Attributes;
        s.xp[i] = xpForLevel(attribute ? rng.range(5, 15) : rng.range(1, 5));
    }
    return s;
}

} // namespace

Stats randomStartingStats(Rng& rng) {
    return startingStats(rng);
}

BodyType randomBodyType(Rng& rng) {
    const int r = rng.range(1, 100);
    return r <= 45 ? BodyType::Masculine : r <= 90 ? BodyType::Feminine : BodyType::Androgynous;
}

BodyType bodyForSavedName(const std::string& name) {
    const size_t h = std::hash<std::string>{}(name) % 100;
    return h < 45 ? BodyType::Masculine : h < 90 ? BodyType::Feminine : BodyType::Androgynous;
}

// Player units never share a name. The names come from the set for the body type.
std::string randomUnitName(Rng& rng, BodyType body, const std::vector<std::string>& taken) {
    const NameSet set = body == BodyType::Feminine ? NameSet::Feminine
                      : body == BodyType::Androgynous ? NameSet::Androgynous : NameSet::Masculine;
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
    const BodyType body = randomBodyType(rng);
    w.add(u, UnitSprite{ modelForBody(body) });
    w.add(u, Body{ body });
    w.add(u, Faction{ FACTION_PLAYER });
}

// Makes there be exactly `count` player units: the missing ones are added at the spawn, and any beyond count go. The
// party is put into them in the order they were made (applyParty), so the last made are the ones that go.
void ensurePlayerUnits(World& w, Terrain& t, Rng& rng, int count) {
    std::vector<Entity> players;   // your people: your mobs are not the party's
    for (Entity u : w.view<Unit, Commandable>()) {
        if (!w.has<Mob>(u)) players.push_back(u);
    }
    std::sort(players.begin(), players.end());
    for (size_t i = (size_t)std::max(0, count); i < players.size(); ++i) w.destroy(players[i]);
    const int have = std::min((int)players.size(), std::max(0, count));
    if (have >= count) return;
    const Vec2 spawn = findSpawn(t);
    for (int i = have; i < count; ++i) spawnPlayerUnit(w, t, rng, spawn, i);
}

void ensurePlayerMobs(World& w, Terrain& t, Rng& rng, const std::vector<MobKind>& kinds, int firstSlot) {
    for (Entity u : w.view<Mob, Commandable>()) w.destroy(u);
    if (kinds.empty()) return;
    const Vec2 spawn = findSpawn(t);
    for (size_t j = 0; j < kinds.size(); ++j) {
        const int i = firstSlot + (int)j;
        const Vec2 p = t.snap({ spawn.x - 3.0f + (i % SQUAD_COLUMNS) * 1.5f, spawn.z - 3.0f + (i / SQUAD_COLUMNS) * 1.5f });
        spawnMob(w, t, rng, kinds[j], FACTION_PLAYER, p.x, p.z);
    }
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
Storage startingStorage(PropKind kind, Rng& rng) {
    Storage st;
    std::vector<int> choices;
    for (const ItemDef& d : itemDatabase()) {
        if (!propAcceptsItem(kind, d.id)) continue;
        const bool gear = !d.equipSlot.empty();
        if (kind == PropKind::Barrel && gear) continue;
        if (kind == PropKind::LockedChest && !gear && d.buyValue < STORAGE_VALUABLE) continue;
        choices.push_back(d.id);
    }
    if (choices.empty()) return st;
    const bool chest = kind == PropKind::LockedChest;
    const int stacks = rng.range(chest ? 3 : STORAGE_STACKS_MIN, chest ? 6 : STORAGE_STACKS_MAX);
    int slot = 0;
    for (int i = 0; i < stacks && slot < STORAGE_SLOTS; ++i) {
        const int item = choices[(size_t)rng.range(0, (int)choices.size() - 1)];
        const int most = std::max(1, std::min(stackLimit(item), STORAGE_STACK_MOST));
        st.slots[(size_t)slot++] = ItemStack{ item, rng.range(1, most) };
    }
    return st;
}

Stats mobStats(MobKind kind, Rng& rng) {
    const MobInfo& m = mobInfo(kind);
    Stats s;
    auto level = [&](int base) { return std::clamp(base + rng.range(-1, 1), 1, MAX_LEVEL); };
    for (int i = 0; i < STAT_COUNT; ++i) {
        s.xp[i] = xpForLevel(mobHasStat((StatId)i) && statInfo((StatId)i).group == StatGroup::Attributes ? level(10) : 1);
    }
    s.xp[StatId::Strength] = xpForLevel(level(m.strength));
    s.xp[StatId::Dexterity] = xpForLevel(level(m.dexterity));
    s.xp[StatId::Vitality] = xpForLevel(level(m.vitality));
    s.xp[StatId::Endurance] = xpForLevel(level(m.endurance));
    s.xp[StatId::Agility] = xpForLevel(level(m.agility));
    s.xp[StatId::Melee] = xpForLevel(level(m.melee));
    s.xp[StatId::Block] = xpForLevel(level(m.block));
    // A kind at its most: every stat a mob has at MAX_LEVEL
    if (m.maxStats) {
        for (int i = 0; i < STAT_COUNT; ++i) {
            if (mobHasStat((StatId)i)) s.xp[i] = xpForLevel(MAX_LEVEL);
        }
    }
    return s;
}

Entity spawnMob(World& w, const Terrain& t, Rng& rng, MobKind kind, int faction, float x, float z) {
    const MobInfo& m = mobInfo(kind);
    Entity e = w.create();
    w.add(e, Transform{ x, t.heightAt(x, z), z });
    w.add(e, Mob{ kind });
    w.add(e, Faction{ faction });
    if (faction == FACTION_PLAYER) {
        w.add(e, Unit{});
        w.add(e, Commandable{});
    } else {
        w.add(e, Unit{ HOSTILE_WALK_SPEED });
        if (factionsHostile(faction, FACTION_PLAYER)) w.add(e, Hostile{});
        else w.add(e, Neutral{});
    }
    Inventory inv;
    addToInventory(inv, m.meat, m.meatCount);
    addToInventory(inv, mobBones(kind), mobBoneCount(kind));
    w.add(e, inv);
    w.add(e, Equipment{});   // always empty: a mob has no gear slots
    const Stats st = mobStats(kind, rng);
    w.add(e, st);
    Needs n;
    n.maxHp = n.hp = m.hp > 0 ? m.hp : maxHealthFor(st);
    w.add(e, n);
    Behaviors b;
    b.rules = mobBehaviors();
    if (isMobFaction(faction)) {
        // A wild mob roams (or Patrols Spawn, keeping near where it was put), and an Avoidant or Passive one never fights
        BehaviorRule roam;
        roam.kind = m.patrolSpawn ? BehaviorKind::PatrolSpawn : BehaviorKind::Roam;
        addBehavior(b.rules, roam);
        if (m.patrolSpawn) w.add(e, SpawnHome{ x, z, false });
        const MobTemperament tm = mobTemperament(kind);
        b.passive = tm == MobTemperament::Avoidant || tm == MobTemperament::Passive;
    }
    w.add(e, b);
    w.add(e, Combat{});
    w.add(e, Name{ lang(m.nameKey) });
    w.add(e, UnitSprite{ 1 });
    return e;
}

void setupWorld(World& w, Terrain& t, Rng& rng, bool playerCamp) {
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
    static const PropKind EXTRA_KINDS[] = {
        PropKind::Wagon, PropKind::Barrel, PropKind::Ore, PropKind::Boulder, PropKind::Mushroom, PropKind::Bones, PropKind::Stall, PropKind::LockedChest,
        PropKind::Chair, PropKind::Bench, PropKind::Stool, PropKind::Table, PropKind::EndTable, PropKind::RoundTable, PropKind::StandingTorch,
        PropKind::TrainingDummy, PropKind::RawFoodStorage, PropKind::FoodStorage, PropKind::WeaponRack, PropKind::ArmorStand,
        PropKind::WaterTank, PropKind::WaterPump, PropKind::Forge,
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
            const PropKind kind = EXTRA_KINDS[rng.range(0, (int)(sizeof(EXTRA_KINDS) / sizeof(EXTRA_KINDS[0])) - 1)];
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
            claimOwnership(w, prop);
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
        haul.propKind = idx(PropKind::Vault);
        BehaviorRule harvest;
        harvest.kind = BehaviorKind::AutoHarvest;
        harvest.propKind = idx(PropKind::Tree);
        addBehavior(b->rules, haul);
        addBehavior(b->rules, harvest);
    };
    std::vector<CampKit> kits;

    // The player's camp, in the middle, when the scenario has one
    const CampKit playerKit = playerCamp ? placeCamp(FACTION_PLAYER, spawn.x, spawn.z + PLAYER_CAMP_OFFSET) : CampKit{};
    addCampExtras(FACTION_PLAYER, playerKit);
    if (playerKit.placed) kits.push_back(playerKit);
    if (playerKit.placed) {
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
        const PropKind kind = (PropKind)(serial % PROP_PLACED_KIND_COUNT);
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
        claimOwnership(w, prop);   // one that lands in a camp's claim is the camp's
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
        if (w.has<Mob>(u)) continue;   // a mob is made whole by spawnMob
        if (!w.has<Stats>(u)) w.add(u, startingStats(rng));
        if (Needs* nd = w.get<Needs>(u)) {
            nd->maxHp = maxHealthFor(*w.get<Stats>(u));
            nd->hp = nd->maxHp;
        }
        if (w.has<Hostile>(u) || w.has<Neutral>(u)) {
            w.add(u, randomHostileGear(rng));
            // An NPC given a bow or a crossbow gets some of its ammo to shoot, so it fights at range
            const Equipment* worn = w.get<Equipment>(u);
            if (const RangedProfile* rp = worn ? rangedProfileOf(worn->items[GearSlot::MainHand]) : nullptr) {
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
            GearItems gear = worn ? worn->items : GearItems{};
            keepUnderEncumbrance(inv->slots, gear, *w.get<Stats>(u));
            if (worn) worn->items = gear;
        }
        // The body comes first: the name is drawn from the set for it, and the model matches it
        if (!w.has<Body>(u)) w.add(u, Body{ randomBodyType(rng) });
        const BodyType body = w.get<Body>(u)->type;
        if (UnitSprite* us = w.get<UnitSprite>(u)) us->id = modelForBody(body);
        Name* nm = w.get<Name>(u);
        if (!nm) continue;
        if (w.has<Commandable>(u)) {
            nm->text = randomUnitName(rng, body, takenNames);
            takenNames.push_back(nm->text);
        } else {
            nm->text = randomUnitName(rng, body, {});
        }
    }

    // Mobs (mobs.h). Each camp's faction has CAMP_MOBS of random kinds standing about its flag, the player's included.
    // Out past every camp, WILD_MOBS of each kind roam in a herd, in their own faction, each herd at its own bearing.
    auto freeSpot = [&](float cx, float cz, float radius, float& x, float& z) {
        for (int attempt = 0; attempt < 200; ++attempt) {
            const float a = rng.unit() * 6.2831853f, d = std::sqrt(rng.unit()) * radius;
            const GridPos g{ tileAt(cx + std::cos(a) * d), tileAt(cz + std::sin(a) * d) };
            if (!t.walkable(g.x, g.y) || std::find(enemyTiles.begin(), enemyTiles.end(), g) != enemyTiles.end()) continue;
            enemyTiles.push_back(g);
            x = g.x + 0.5f;
            z = g.y + 0.5f;
            return true;
        }
        return false;
    };
    // Whether no prop stands within CAMP_CAGE_GAP of (x, z), so a cage put there sits clear of the others
    auto clearOfProps = [&](float x, float z) {
        for (Entity e : w.view<Prop, Transform>()) {
            const Transform* pt = w.get<Transform>(e);
            if ((pt->x - x) * (pt->x - x) + (pt->z - z) * (pt->z - z) < CAMP_CAGE_GAP * CAMP_CAGE_GAP) return false;
        }
        return true;
    };
    for (const CampKit& kit : kits) {
        // Your camp's mobs are the party's own (ensurePlayerMobs), placed once the party is known
        if (!kit.placed || kit.faction == FACTION_PLAYER) continue;
        for (int i = 0; i < CAMP_MOBS; ++i) {
            float x = 0.0f, z = 0.0f;
            MobKind kind = (MobKind)rng.range(1, MOB_KIND_COUNT);
            while (!campMobKind(kind)) kind = (MobKind)rng.range(1, MOB_KIND_COUNT);
            // Each in a Prisoner Cage of its own, locked to the camp's faction, on a spot clear of the camp's props
            bool found = false;
            for (int tries = 0; tries < 20 && !found; ++tries) {
                if (!freeSpot(kit.x, kit.z, CAMP_MOB_RADIUS, x, z)) break;
                found = clearOfProps(x, z);
            }
            if (!found) continue;
            const Entity mob = spawnMob(w, t, rng, kind, kit.faction, x, z);
            const PropInfo& cageInfo = propInfo(PropKind::PrisonerCage);
            const Entity cage = w.create();
            w.add(cage, Transform{ x, t.heightAt(x, z), z });
            w.add(cage, Prop{ PropKind::PrisonerCage, campSerial++, 0.0f });
            w.add(cage, Owner{ kit.faction });
            w.add(cage, Lock{ true, cageInfo.lockLevel, kit.faction });
            claimOwnership(w, cage);
            w.add(mob, Occupying{ cage });
            // A regular faction's mob, not a wild one: its name says so
            if (!isMobFaction(kit.faction)) {
                if (Name* nm = w.get<Name>(mob)) nm->text += " [!]";
            }
        }
    }
    float farthest = 0.0f;   // how far out the camps reach from the spawn
    for (const CampKit& kit : kits) {
        if (kit.placed) farthest = std::max(farthest, std::hypot(kit.x - spawn.x, kit.z - spawn.z) + CLAIM_RADIUS);
    }
    const float turn = rng.unit() * 6.2831853f;
    for (int k = 1; k <= MOB_KIND_COUNT; ++k) {
        const MobKind kind = (MobKind)k;
        // Each herd at its own bearing, out between WILD_MOB_NEAR and WILD_MOB_FAR past the camps
        const float a = turn + 6.2831853f * (float)(k - 1) / (float)MOB_KIND_COUNT;
        const float d = farthest + WILD_MOB_NEAR + rng.unit() * (WILD_MOB_FAR - WILD_MOB_NEAR);
        const float hx = std::clamp(spawn.x + std::cos(a) * d, 8.0f, size - 8.0f);
        const float hz = std::clamp(spawn.z + std::sin(a) * d, 8.0f, size - 8.0f);
        for (int i = 0; i < mobInfo(kind).wild; ++i) {
            float x = 0.0f, z = 0.0f;
            if (freeSpot(hx, hz, WILD_HERD_RADIUS, x, z) || freeSpot(hx, hz, WILD_HERD_RADIUS * 3.0f, x, z)) {
                const Entity m = spawnMob(w, t, rng, kind, mobFaction(kind), x, z);
                if (kind != MobKind::Gorlicorn) continue;
                // The Gorlicorn's Rabbits: placed about it, and Patrolling Spawn round the same spot, so they stay by it
                const SpawnHome home = *w.get<SpawnHome>(m);
                for (int r = 0; r < GORLICORN_RABBITS; ++r) {
                    float rx = 0.0f, rz = 0.0f;
                    if (!freeSpot(home.x, home.z, 4.0f, rx, rz)) continue;
                    const Entity rabbit = spawnMob(w, t, rng, MobKind::Rabbit, mobFaction(MobKind::Rabbit), rx, rz);
                    Behaviors* rb = w.get<Behaviors>(rabbit);
                    for (BehaviorRule& rule : rb->rules) {
                        if (rule.kind == BehaviorKind::Roam) rule.kind = BehaviorKind::PatrolSpawn;
                    }
                    w.add(rabbit, SpawnHome{ home.x, home.z, false });
                }
            }
        }
    }

    // Every unit of the other factions (their mobs too) patrols its faction's claim when it has nothing else to do:
    // Patrol is the last behavior on its list
    for (Entity u : w.view<Unit, Behaviors, Faction>()) {
        const int f = w.get<Faction>(u)->id;
        if (f == FACTION_PLAYER || isMobFaction(f)) continue;
        BehaviorRule patrol;
        patrol.kind = BehaviorKind::Patrol;
        addBehavior(w.get<Behaviors>(u)->rules, patrol);
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
        w.remove<ActiveSquadMember>(u);
        cancelPickup(w, u);
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

    ActiveSquad s;
    s.cx = cx;
    s.cz = cz;
    s.size = (int)units.size();
    Entity sq = w.create();
    w.add(sq, s);

    // Each selected unit joins the new active squad. Overwriting ActiveSquadMember drops any old one.
    for (size_t i = 0; i < units.size(); ++i) {
        w.remove<SoloMove>(units[i]);
        cancelPickup(w, units[i]);
        w.remove<AttackOrder>(units[i]);
        w.remove<TalkOrder>(units[i]);
        endPropTasks(w, units[i]);
        w.add(units[i], ActiveSquadMember{ sq, (int)i });
    }

    planActiveSquad(w, t, sq, clickPoint);
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
        if (cannotTakeOrder(w, u) || !canHoldItem(w, u, w.get<GroundItem>(item)->item)) continue;
        cancelPickup(w, u);
        w.remove<ActiveSquadMember>(u);
        w.remove<AttackOrder>(u);
        w.remove<TalkOrder>(u);
        endPropTasks(w, u);
        w.add(u, SoloMove{});
        planSolo(w, t, u, target);
        w.add(u, PickupOrder{ item });
    }
}

void issuePickupChain(World& w, Terrain& t, const std::vector<Entity>& units, const std::vector<Entity>& items) {
    PickupJob job;
    for (Entity it : items) {
        if (w.has<GroundItem>(it) && w.get<Transform>(it)) job.items.push_back(it);
    }
    if (job.items.empty()) return;

    std::vector<Entity> crew;
    for (Entity u : units) {
        if (!w.has<Inventory>(u) || !w.get<Transform>(u) || cannotTakeOrder(w, u)) continue;   // only units that carry items
        cancelPickup(w, u);
        w.remove<ActiveSquadMember>(u);
        w.remove<SoloMove>(u);
        w.remove<AttackOrder>(u);
        w.remove<TalkOrder>(u);
        endPropTasks(w, u);
        crew.push_back(u);
    }
    if (crew.empty()) return;

    const Entity jobE = w.create();
    w.add(jobE, std::move(job));
    for (Entity u : crew) w.add(u, PickupChain{ jobE, {}, 0 });
    pickupChainSystem(w, t);   // hand out the first items at once
}

void pickupChainSystem(World& w, Terrain& t) {
    // The job's listed items that are still on the ground, and the units on each job
    std::map<Entity, std::vector<Entity>> crews;
    for (Entity u : w.view<PickupChain>()) {
        const Entity jobE = w.get<PickupChain>(u)->job;
        if (!w.has<PickupJob>(jobE) || !w.get<Transform>(u) || !w.has<Inventory>(u)) {
            cancelPickup(w, u);
            continue;
        }
        crews[jobE].push_back(u);
    }
    for (Entity jobE : w.view<PickupJob>()) {
        PickupJob* job = w.get<PickupJob>(jobE);
        // An item someone took (or that is gone some other way) drops off the list
        job->items.erase(std::remove_if(job->items.begin(), job->items.end(),
                                        [&](Entity it) { return !w.has<GroundItem>(it) || !w.get<Transform>(it); }),
                         job->items.end());
        auto found = crews.find(jobE);
        if (found == crews.end() || job->items.empty()) {
            if (found != crews.end()) {
                for (Entity u : found->second) cancelPickup(w, u);
            }
            w.destroy(jobE);
            continue;
        }
        std::vector<Entity>& crew = found->second;

        // Items already being walked to by a unit of this job are left to it
        std::vector<Entity> claimed;
        std::vector<Entity> idle;
        for (Entity u : crew) {
            if (const PickupOrder* po = w.get<PickupOrder>(u)) claimed.push_back(po->item);
            else idle.push_back(u);
        }

        // Idle units take free items, nearest pair first, so the closest unit goes for each and two units never
        // walk to the same item
        auto freeFor = [&](Entity u, Entity it) {
            if (std::find(claimed.begin(), claimed.end(), it) != claimed.end()) return false;
            const PickupChain* ch = w.get<PickupChain>(u);
            if (std::find(ch->skipped.begin(), ch->skipped.end(), it) != ch->skipped.end()) return false;
            return canHoldItem(w, u, w.get<GroundItem>(it)->item);
        };
        while (!idle.empty()) {
            float bestD2 = -1.0f;
            size_t bu = 0;
            Entity bi = NULL_ENTITY;
            for (size_t i = 0; i < idle.size(); ++i) {
                const Transform* ut = w.get<Transform>(idle[i]);
                for (Entity it : job->items) {
                    if (!freeFor(idle[i], it)) continue;
                    const Transform* itr = w.get<Transform>(it);
                    const float dx = itr->x - ut->x, dz = itr->z - ut->z;
                    const float d2 = dx * dx + dz * dz;
                    if (bestD2 < 0.0f || d2 < bestD2) {
                        bestD2 = d2;
                        bu = i;
                        bi = it;
                    }
                }
            }
            if (bi == NULL_ENTITY) break;   // every item left is taken by another unit, or out of these units' reach
            const Entity u = idle[bu];
            const Transform* itr = w.get<Transform>(bi);
            w.add(u, SoloMove{});
            planSolo(w, t, u, Vec2{ itr->x, itr->z });
            w.add(u, PickupOrder{ bi });
            w.get<PickupChain>(u)->replans = 0;
            claimed.push_back(bi);
            idle.erase(idle.begin() + bu);
        }

        // A unit left idle waits in case another unit's item frees up, but once nothing on the list is left that it
        // could take, its chain ends
        for (Entity u : idle) {
            const PickupChain* ch = w.get<PickupChain>(u);
            bool any = false;
            for (Entity it : job->items) {
                if (std::find(ch->skipped.begin(), ch->skipped.end(), it) == ch->skipped.end() &&
                    canHoldItem(w, u, w.get<GroundItem>(it)->item)) {
                    any = true;
                    break;
                }
            }
            if (!any) cancelPickup(w, u);
        }
    }
}

void steerUnits(World& w, const std::vector<Entity>& units, Vec2 target) {
    for (Entity u : units) {
        if (!w.has<Commandable>(u) || !w.get<Transform>(u) || cannotTakeOrder(w, u)) continue;
        if (w.has<ActiveSquadMember>(u)) w.remove<ActiveSquadMember>(u);
        if (w.has<AttackOrder>(u)) w.remove<AttackOrder>(u);
        w.remove<TalkOrder>(u);
        endPropTasks(w, u);
        cancelPickup(w, u);
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
        w.remove<ActiveSquadMember>(u);
        w.remove<SoloMove>(u);
        cancelPickup(w, u);
        w.remove<TalkOrder>(u);
        endPropTasks(w, u);
        w.add(u, AttackOrder{ target });
        ensureCombat(w, u);
        w.get<Combat>(u)->target = target;
    }
}

// Right-click on an NPC: each selected unit walks up to it. The first in talk range starts the talk.
void issueTalkOrder(World& w, const std::vector<Entity>& units, Entity npc, bool trade, bool shop) {
    if (!w.has<Unit>(npc) || !w.get<Transform>(npc) || w.has<Mob>(npc)) return;   // a mob has nothing to say
    for (Entity u : units) {
        if (!w.has<Commandable>(u) || !w.get<Transform>(u) || cannotTakeOrder(w, u)) continue;
        if (w.has<Mob>(u)) continue;   // nor does one of yours talk or trade for you (mobs.h)
        w.remove<ActiveSquadMember>(u);
        w.remove<SoloMove>(u);
        cancelPickup(w, u);
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

    queueLog(langf("MISC_DROPS", { nameOf(w, unit), std::to_string(s.count), itemNameOf(s.item) }),
             LogKind::Drop, npcOnly(w, unit));
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
        const bool mob = w.has<Mob>(e);
        const double gameHours = gameSeconds / 3600.0;
        // The most Rested follows Vitality. A unit that was fully rested stays fully rested as it grows.
        if (const int maxR = maxRestedFor(statsOf(w, e)); maxR != n->maxRested) {
            const bool full = n->rested >= n->maxRested;
            n->maxRested = maxR;
            n->rested = full ? maxR : std::min(n->rested, maxR);
        }
        if (mob) {
            // A mob can't feed itself, so it has no hunger to lose (and none to starve from), and no Comfort. Its
            // Rested never falls by itself and rises a little every hour, more in a bed or bedroll.
            n->hunger = n->maxHunger;
            n->hurried = false;
            const Entity bed = occupiedProp(w, e);
            const Prop* bp = bed != NULL_ENTITY ? w.get<Prop>(bed) : nullptr;
            const double restGain = std::max(MOB_RESTED_GAIN_PER_HOUR, bp ? restedGainPerHour(bp->kind) : 0.0);
            driftNeed(n->rested, n->maxRested, n->restedPart, restGain, gameHours);
        } else {
            // A unit that moved at run pace in the hour that just ended pays double for its next tick. The mark is
            // used up here, so later hours cost the usual amount.
            int ticks = hours;
            if (hours > 0 && n->hurried) {
                ticks += 1;
                n->hurried = false;
            }
            tickNeeds(*n, ticks);
            // An Exile never goes hungry
            if (const Faction* fa = w.get<Faction>(e); fa && fa->id == FACTION_EXILE) n->hunger = n->maxHunger;

            // Rested rises lying in a bed or bedroll and falls while awake anywhere else; Comfort rises in a seat or
            // bed without a lock and falls anywhere else
            const Entity seat = occupiedProp(w, e);
            const Prop* sp = seat != NULL_ENTITY ? w.get<Prop>(seat) : nullptr;
            const double restGain = sp ? restedGainPerHour(sp->kind) : 0.0;
            const double comfortGain = sp ? comfortGainPerHour(sp->kind) : 0.0;
            const double restRate = restGain > 0.0 ? restGain : w.has<Unconscious>(e) ? 0.0 : -RESTED_LOSS_PER_HOUR;
            driftNeed(n->rested, n->maxRested, n->restedPart, restRate, gameHours);
            driftNeed(n->comfort, n->maxComfort, n->comfortPart, comfortGain > 0.0 ? comfortGain : -COMFORT_LOSS_PER_HOUR,
                      gameHours);
        }

        // An Unconscious unit does not heal: only Doom falling to 0 in a bed, or a rested one's wait running out, wakes it
        if (w.has<Unconscious>(e)) continue;
        const Combat* c = w.get<Combat>(e);
        regenHealth(*n, c && c->target != NULL_ENTITY, gameSeconds, mob);
    }
}

int pickupSystem(World& w, Terrain& t) {
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
            // and the unit stays where it is. A mob takes nothing that is not its own (mobs.h).
            if (!canHoldItem(w, u, gi->item)) {
                if (PickupChain* ch = w.get<PickupChain>(u)) ch->skipped.push_back(item);   // others may still take it
                endPickup(w, u);
                continue;
            }
            const int count = gi->count;
            const std::string itemName = itemNameOf(gi->item);
            const int left = addToInventory(*w.get<Inventory>(u), gi->item, gi->count);
            if (left < count) {
                queueLog(langf("MISC_PICKS_UP", { nameOf(w, u), std::to_string(count - left), itemName }), LogKind::Pickup, npcOnly(w, u));
                queueUnitLabel(u, itemGainLabel(count - left, itemName));   // "+Iron Ore" over its head
                queueSoundFrom(w, Sfx::Pickup, u);
            }
            if (left == 0) w.destroy(item);
            else gi->count = left;
            if (w.has<Commandable>(u) && left < count) ++taken;
            endPickup(w, u);
            continue;
        }

        // Stopped short with no route left (the item can't be reached), so give up. On a pickup chain the unit may only
        // have been pushed off its way, so it plans a new route a few times before it leaves the item to the others.
        const SoloMove* so = w.get<SoloMove>(u);
        if (!so || so->next >= so->waypoints.size()) {
            PickupChain* ch = w.get<PickupChain>(u);
            if (ch && ch->replans < CHAIN_REPLANS) {
                ++ch->replans;
                if (!w.has<SoloMove>(u)) w.add(u, SoloMove{});
                planSolo(w, t, u, Vec2{ it->x, it->z });
                if (!w.get<SoloMove>(u)->waypoints.empty()) continue;
            }
            if (ch) ch->skipped.push_back(item);
            endPickup(w, u);
        }
    }
    pickupChainSystem(w, t);   // units on a chain that just took (or lost) their item go for the next one
    return taken;
}

void squadSystem(World& w, float dt, bool running) {
    for (Entity e : w.view<ActiveSquad>()) {
        ActiveSquad* sq = w.get<ActiveSquad>(e);
        if (sq->arrived) continue;

        // The active squad keeps to the pace of its slowest member that can still move. Members that can't
        // move at all (downed, or carrying too much) are left behind, and don't hold the active squad back.
        // Walking, the active squad is also held to its own pace. Running, each member runs at its own speed,
        // so the center goes at the slowest member's run pace.
        float slowest = -1.0f;
        for (Entity m : w.view<ActiveSquadMember, Unit>()) {
            if (w.get<ActiveSquadMember>(m)->squad != e) continue;
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
    const float unitWidth = 2.0f * 0.5f + UNIT_CLEARANCE;   // one unit's room: its tile, and the gap

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
        } else if (ActiveSquadMember* sm = w.get<ActiveSquadMember>(e)) {
            if (ActiveSquad* sq = w.get<ActiveSquad>(sm->squad)) {
                // Slots are centerd on the active squad's center, so the group's middle sits on
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

    // How big each unit is: a smaller one can't push a bigger one, which walks on through and shoves it aside
    std::vector<float> sizes(units.size(), 1.0f);
    for (size_t i = 0; i < units.size(); ++i) sizes[i] = unitSize(w, units[i]);

    // Pass 1b: each unit's velocity: toward its goal, plus separation from the units too close to it.
    // A unit is pushed only by units at least as big as it is.
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
            // Every unit and mob is the same one-tile square, lined up with the tiles: two overlap when they are less
            // than a tile apart on both axes, and are pushed apart along the axis they overlap least on
            const float overX = UNIT_BOX - std::fabs(sx), overZ = UNIT_BOX - std::fabs(sz);
            if (overX <= 0.0f || overZ <= 0.0f) continue;
            if (sizes[j] < sizes[i] - 1e-3f) continue;   // a smaller unit can't move this one
            // Two settled units following the same unit leave each other be: no pushing back and forth between them
            if (calmLead[i] != NULL_ENTITY && calmLead[i] == calmLead[j]) continue;
            if (std::fabs(sx) < 1e-4f && std::fabs(sz) < 1e-4f) sx = (e & 1u) ? 1e-3f : -1e-3f;   // right on top: pick a side
            const bool alongX = overX < overZ;
            const float push = (alongX ? overX : overZ) * 8.0f;
            if (alongX) vx += (sx >= 0.0f ? 1.0f : -1.0f) * push;
            else vz += (sz >= 0.0f ? 1.0f : -1.0f) * push;
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
            grantXp(w, e, StatId::Endurance, ENDURANCE_XP_PER_TILE * moved, lang("MISC_REASON_WALKING"));
            grantXp(w, e, StatId::Strength, strengthXpWhileWalking(load, dt), lang("MISC_REASON_CARRYING"), LogKind::XpCarrying);
            if (tr->y > y0) grantXp(w, e, StatId::Athletics, tr->y - y0, lang("MISC_REASON_CLIMBING"));
            // Moving at run pace marks the unit for a double hunger tick at the next hour. A nudge from a neighbor,
            // a hundredth of a tile or less in a frame, is not walking.
            if (player && running && moved >= 0.01f) {
                if (Needs* nd = w.get<Needs>(e)) nd->hurried = true;
            }
        }
    }
}

void cleanupActiveSquads(World& w) {
    std::vector<Entity> used;
    for (Entity e : w.view<ActiveSquadMember>()) used.push_back(w.get<ActiveSquadMember>(e)->squad);

    for (Entity sq : w.view<ActiveSquad>()) {
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
        queueLog(langf("MISC_CRAFT_STOPS", { nameOf(w, u) }), LogKind::Other, npcOnly(w, u));
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
        w.remove<ActiveSquadMember>(u);
        cancelPickup(w, u);
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
        w.remove<ActiveSquadMember>(u);
        w.remove<SoloMove>(u);
        cancelPickup(w, u);
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
        w.remove<ActiveSquadMember>(u);
        w.remove<SoloMove>(u);
        cancelPickup(w, u);
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
            if (action == PropAction::Lock) {
                // Lock: shut an open lock again. It stays shut until someone unlocks it. A lock with no owner becomes
                // the locker's faction's. A mob works no locks.
                if (Lock* lk = w.get<Lock>(prop); lk && !lk->locked && !w.has<Mob>(u)) {
                    lk->locked = true;
                    if (lk->faction < 0) {
                        const Faction* f = w.get<Faction>(u);
                        lk->faction = f ? f->id : FACTION_PLAYER;
                    }
                    a.started = true;
                    queueSoundFrom(w, Sfx::LockedIn, prop);
                    queueLog(langf("MISC_LOCKS", { nameOf(w, u), lang(info.nameKey) }), LogKind::Other, npcOnly(w, u));
                    const Entity inside = propOccupant(w, prop);
                    if (inside != NULL_ENTITY && lockedIn(w, inside)) {
                        queueLog(langf("MISC_LOCKED_IN", { nameOf(w, inside), lang(info.nameKey) }), LogKind::Other, npcOnly(w, inside));
                    }
                }
                out.push_back(a);
                continue;
            }
            if (action == PropAction::Unlock && lock && lock->locked && !lockShutFor(w, prop, u) && !w.has<Mob>(u)) {
                // Unlock of a lock that is the unit's own (its faction's, or in its faction's claim): opened with the
                // key, at once, no lockpicking
                w.get<Lock>(prop)->locked = false;
                a.started = true;
                queueSoundFrom(w, Sfx::Unlocked, prop);
                queueLog(langf("MISC_UNLOCKS", { nameOf(w, u), lang(info.nameKey) }), LogKind::Other, npcOnly(w, u));
                out.push_back(a);
                continue;
            }
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
                // Place: the unit places the one it carries on the prop, and stays off it itself. With nobody carried
                // there is nothing to place.
                if (action == PropAction::Place) {
                    const Carrying* cr = w.get<Carrying>(u);
                    if (!cr) break;
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
                        w.remove<IdleSpot>(carried);
                        w.add(carried, Occupying{ prop });
                        a.placed = true;
                        a.started = true;
                        queueSoundFrom(w, w.has<Lock>(prop) ? Sfx::LockedIn : Sfx::Occupy, prop);
                        queueLog(langf("MISC_PLACES_IN", { nameOf(w, u), nameOf(w, carried), lang(info.nameKey) }), LogKind::Other, npcOnly(w, u, carried));
                        // A lockable one (the Prisoner Cage) locks behind the unit placed in it
                        if (Lock* lk = w.get<Lock>(prop)) {
                            lk->locked = true;
                            queueLog(langf("MISC_LOCKED_IN", { nameOf(w, carried), lang(info.nameKey) }), LogKind::Other, npcOnly(w, carried));
                        }
                    }
                    break;
                }
                // Occupy. A unit carrying someone is sent to Place instead (the menu offers no Occupy then), but if one
                // gets here anyway it puts the carried unit down where it stands as it occupies the prop.
                // One try, once in reach. Another prop on the straight line to the bed makes it unreachable; another
                // unit already on it makes it taken. Either way the order ends and the unit stays where it is.
                // Otherwise the unit is put on the bed, right on its spot, wherever in reach it stopped.
                const Entity occupant = propOccupant(w, prop);
                if (propInTheWay(w, u, prop)) {
                    a.unreachable = true;
                } else if (occupant != NULL_ENTITY && occupant != u) {
                    a.taken = true;
                } else {
                    // A unit already in it (left in after the lock was opened) does not lock itself in again
                    const bool newOccupant = occupant != u;
                    Transform* ut = w.get<Transform>(u);
                    ut->x = pt->x;
                    ut->y = pt->y;
                    ut->z = pt->z;
                    if (w.has<Carrying>(u)) putDown(w, u);
                    w.remove<IdleSpot>(u);
                    w.add(u, Occupying{ prop });
                    a.started = true;
                    queueSoundFrom(w, w.has<Lock>(prop) && newOccupant ? Sfx::LockedIn : Sfx::Occupy, prop);
                    // A lockable one (the Prisoner Cage) locks behind a new unit
                    if (Lock* lk = newOccupant ? w.get<Lock>(prop) : nullptr) {
                        lk->locked = true;
                        queueLog(langf("MISC_LOCKED_IN", { nameOf(w, u), lang(info.nameKey) }), LogKind::Other, npcOnly(w, u));
                    }
                }
                break;
            }
            case PropUse::Resource:
                if (w.has<Mob>(u)) break;   // a mob works no props (mobs.h)
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
    // A unit being carried, or on a prop that is no longer one to occupy, is not on it either: its Occupying goes, so
    // it is never left drawn lying on a bed it is not in (Renderer::bedLift).
    std::vector<Entity> lying = w.view<Occupying, Transform>();
    std::sort(lying.begin(), lying.end());
    std::vector<Entity> bedsTaken;
    for (Entity u : lying) {
        const Entity bed = w.get<Occupying>(u)->prop;
        const Transform* pt = w.get<Transform>(bed);
        const Prop* bp = w.get<Prop>(bed);
        if (!pt || !bp || propInfo(bp->kind).use != PropUse::Occupy || w.has<Carried>(u) ||
            std::find(bedsTaken.begin(), bedsTaken.end(), bed) != bedsTaken.end()) {
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
        const PropKind propKind = w.get<Prop>(prop)->kind;
        auto addGain = [&](WorkGain g) {
            g.propKind = idx(propKind);
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
                perHour += 1.0 / (LOCKPICK_SHARE * workHoursFor(levelOf(statsOf(w, u), StatId::Lockpicking)));
            }
            pw.progress += gameSeconds / 3600.0 * perHour;
            Lock* lock = w.get<Lock>(prop);
            while (pw.progress >= 1.0 && lock->locked) {
                pw.progress -= 1.0;
                ++pw.made;   // the ring hops at the end of each attempt
                const Entity picker = workers[pw.turn % workers.size()];
                ++pw.turn;
                const int level = levelOf(statsOf(w, picker), StatId::Lockpicking);
                if ((double)rng.unit() < lockpickChance(level)) {
                    lock->locked = false;
                    addGain(WorkGain{ picker, 0, WorkGain::Kind::Unlocked });
                    queueLog(langf("MISC_UNLOCKS", { nameOf(w, picker), lang(info.nameKey) }), LogKind::Other, npcOnly(w, picker));
                    grantXp(w, picker, StatId::Lockpicking, LOCKPICK_XP_SUCCESS, lang("MISC_REASON_LOCKPICKING"));
                } else {
                    addGain(WorkGain{ picker, 0, WorkGain::Kind::LockpickFailed });
                    grantXp(w, picker, StatId::Lockpicking, LOCKPICK_XP_FAIL, lang("MISC_REASON_LOCKPICKING"));
                }
            }
            if (!lock->locked) {
                for (Entity u : workers) w.remove<Working>(u);
                pw.progress = 0.0;
                pw.workers = 0;
            }
            continue;
        }
        if (propKind == PropKind::Debris) {
            // Debris: DEBRIS_CLEAR_HOURS of work, shared among those at it, whatever their skill, and it is gone
            PropWork& pw = w.has<PropWork>(prop) ? *w.get<PropWork>(prop) : w.add(prop, PropWork{});
            pw.workers = (int)workers.size();
            pw.progress += gameSeconds / 3600.0 * (double)workers.size() / DEBRIS_CLEAR_HOURS;
            if (pw.progress >= 1.0) {
                for (Entity u : workers) {
                    w.remove<Working>(u);
                    grantXp(w, u, StatId::Strength, DEBRIS_CLEAR_XP, w.has<Commandable>(u) ? lang("MISC_REASON_CLEARING") : std::string());
                }
                if (w.has<Commandable>(workers[0])) queueLog(langf("MISC_CLEARS_DEBRIS", { nameOf(w, workers[0]) }), LogKind::Other);
                queueSoundFrom(w, Sfx::Drop, prop);
                w.destroy(prop);
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

        // Each worker adds one item per workHoursFor(its level) in-game hours, sped up at a resource prop
        double perHour = 0.0;
        for (Entity u : workers) perHour += resourceSpeedFor(w.get<Prop>(prop)->kind) / workHoursFor(levelOf(statsOf(w, u), info.stat));
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
                const std::string outName = itemNameOf(outItem);
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
                        queueLog(langf("MISC_GATHER_NO_ROOM", { nameOf(w, u), outName }), LogKind::Other, npcOnly(w, u));
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
                    queueLog(langf("MISC_CRAFTS", { nameOf(w, got), outName }), LogKind::Pickup, npcOnly(w, got));
                    grantXp(w, got, info.stat, WORK_XP_PER_ITEM, lang("MISC_REASON_CRAFTING"));
                } else {
                    // Training: no item, only the skill's XP
                    addGain(WorkGain{ got, idx(info.stat), WorkGain::Kind::Trained, WORK_XP_PER_ITEM });
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
            const std::string itemName = itemNameOf(gives);
            Inventory* inv = w.get<Inventory>(got);
            if (inv && addToInventory(*inv, gives, 1) == 0) {
                // A unit that is not yours (a camp's woodcutter) gathers quietly: no label, log line or sound
                const bool yours = w.has<Commandable>(got);
                if (yours) {
                    addGain(WorkGain{ got, gives });
                    queueLog(langf("MISC_GATHERS", { nameOf(w, got), itemName }), LogKind::Pickup, npcOnly(w, got));
                }
                grantXp(w, got, info.stat, WORK_XP_PER_ITEM, yours ? lang("MISC_REASON_GATHERING") : std::string());
            } else {
                // No room: the item falls at the unit's feet, and the unit stops working
                if (const Transform* ut = w.get<Transform>(got)) {
                    const Entity drop = w.create();
                    w.add(drop, Transform{ ut->x, ut->y, ut->z });
                    w.add(drop, GroundItem{ gives, 1 });
                }
                queueLog(langf("MISC_GATHER_NO_ROOM", { nameOf(w, got), itemName }), LogKind::Drop, npcOnly(w, got));
                queueUnitLabel(got, itemLossLabel(1, itemName), true);   // it fell at the unit's feet
                queueSoundFrom(w, Sfx::Drop, got);
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
            queueLog(langf("MISC_AUTO_HAUL_STORES", { nameOf(w, unit), std::to_string(moved), itemNameOf(movedItem),
                                                    lang(propInfo(p->kind).nameKey) }), LogKind::Pickup, npcOnly(w, unit));
        }
    }
    return moved;
}

namespace {

// How many of an item a unit carries
int carriedCount(World& w, Entity u, int item) {
    const Inventory* inv = w.get<Inventory>(u);
    return inv ? carriedCount(*inv, item) : 0;
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

// Why an Auto-Haul can't be done now, or BehaviorWait::None when it can: its prop is there, open to the unit, in a
// claim of the unit's faction, and has room for at least one item the unit carries. item 0: any item the prop takes;
// otherwise only that item.
BehaviorWait haulWait(World& w, const std::vector<Claim>& claims, Entity u, const BehaviorRule& r, int item = 0) {
    const Prop* p = w.get<Prop>(r.prop);
    const Storage* st = w.get<Storage>(r.prop);
    const Inventory* inv = w.get<Inventory>(u);
    if (!p || !st || !inv) return BehaviorWait::PropGone;
    if (!inOwnClaim(w, claims, r.prop, unitFaction(w, u))) return BehaviorWait::OutsideClaim;
    if (lockShutFor(w, r.prop, u)) return BehaviorWait::Locked;
    bool carries = false;
    for (const ItemStack& s : inv->slots) {
        if (s.item <= 0 || s.count <= 0 || (item != 0 && s.item != item) || !propAcceptsItem(p->kind, s.item)) continue;
        carries = true;
        if (slotsRoomFor(st->slots.data(), STORAGE_SLOTS, s.item) > 0) return BehaviorWait::None;
    }
    return carries ? BehaviorWait::PropFull : BehaviorWait::NothingToHaul;
}
bool haulable(World& w, const std::vector<Claim>& claims, Entity u, const BehaviorRule& r, int item = 0) {
    return haulWait(w, claims, u, r, item) == BehaviorWait::None;
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
// room for another worker. NULL_ENTITY when there is none. ruleKind is the rule's propKind, -1 for none.
Entity harvestTarget(World& w, const std::vector<Claim>& claims, Entity u, int ruleKind) {
    const Transform* ut = w.get<Transform>(u);
    if (!ut || ruleKind < 0 || ruleKind >= PROP_KIND_COUNT) return NULL_ENTITY;
    const PropKind kind = (PropKind)ruleKind;
    if (!yieldsItems(kind)) return NULL_ENTITY;
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
    if (w.has<Occupying>(u) || w.has<FollowOrder>(u) || w.has<DefendOrder>(u) || w.has<PickupOrder>(u) || w.has<PickupChain>(u) ||
        w.has<LootOrder>(u) || w.has<CarryOrder>(u) || w.has<PutDownOrder>(u) || w.has<TalkOrder>(u) ||
        w.has<Carrying>(u) || w.has<AttackOrder>(u)) {
        return true;
    }
    if (const SoloMove* so = w.get<SoloMove>(u); so && so->next < so->waypoints.size() && !w.has<PropOrder>(u) && !b.seeking &&
        !(b.busy && isWalkBehavior(b.step.kind))) {
        return true;
    }
    if (const ActiveSquadMember* sm = w.get<ActiveSquadMember>(u)) {
        if (const ActiveSquad* sq = w.get<ActiveSquad>(sm->squad); sq && !sq->arrived) return true;
    }
    return false;
}

// Sends a unit on an automated step: it walks to the prop, and in reach does the action
void autoGo(World& w, Terrain& t, Entity u, Entity prop, PropAction action) {
    const Transform* pt = w.get<Transform>(prop);
    if (!pt) return;
    w.remove<ActiveSquadMember>(u);
    cancelPickup(w, u);
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
    if (b.busy && isWalkBehavior(b.step.kind)) w.remove<SoloMove>(u);
    if (const Occupying* oc = w.get<Occupying>(u); oc && b.busy && b.step.kind == BehaviorKind::Rest && oc->prop == b.stepProp) {
        w.remove<Occupying>(u);
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

// Sends a unit on a walk step: it walks to the spot, and the step ends when it gets there
void autoWalk(World& w, Terrain& t, Entity u, Vec2 to) {
    w.remove<ActiveSquadMember>(u);
    cancelPickup(w, u);
    w.remove<TalkOrder>(u);
    endPropTasks(w, u);
    w.remove<IdleSpot>(u);
    w.add(u, SoloMove{});
    planSolo(w, t, u, to);
}

// Whether a tile is walkable ground a walk may end on
bool openTile(const Terrain& t, float x, float z) {
    return x >= 0.0f && z >= 0.0f && x < t.size() && z < t.size() && t.walkable((int)std::floor(x), (int)std::floor(z));
}

// Patrol: a walkable spot inside the claim the unit stands in, when that claim is its faction's
BehaviorWait patrolSpot(World& w, const Terrain& t, const std::vector<Claim>& claims, Entity u, Rng& rng, Vec2& to) {
    const Transform* ut = w.get<Transform>(u);
    const Claim* c = ut ? claimAt(claims, ut->x, ut->z, unitFaction(w, u)) : nullptr;
    if (!c) return BehaviorWait::NotInClaim;
    for (int attempt = 0; attempt < 12; ++attempt) {
        const float a = rng.unit() * 6.2831853f, d = std::sqrt(rng.unit()) * (CLAIM_RADIUS - 2.0f);
        const float x = c->x + std::cos(a) * d, z = c->z + std::sin(a) * d;
        if (!openTile(t, x, z)) continue;
        to = Vec2{ std::floor(x) + 0.5f, std::floor(z) + 0.5f };
        return BehaviorWait::None;
    }
    return BehaviorWait::NotInClaim;
}

// Roam: a walkable spot a short way off, outside every claim
BehaviorWait roamSpot(World& w, const Terrain& t, const std::vector<Claim>& claims, Entity u, Rng& rng, Vec2& to) {
    const Transform* ut = w.get<Transform>(u);
    if (!ut) return BehaviorWait::NowhereToRoam;
    for (int attempt = 0; attempt < 12; ++attempt) {
        const float a = rng.unit() * 6.2831853f, d = ROAM_STEP_MIN + rng.unit() * (ROAM_STEP_MAX - ROAM_STEP_MIN);
        const float x = ut->x + std::cos(a) * d, z = ut->z + std::sin(a) * d;
        if (!openTile(t, x, z) || claimAt(claims, x, z)) continue;
        to = Vec2{ std::floor(x) + 0.5f, std::floor(z) + 0.5f };
        return BehaviorWait::None;
    }
    return BehaviorWait::NowhereToRoam;
}

// Rest: the nearest bed or bedroll nobody lies in or is on the way to, that is not locked to the unit
Entity restTarget(World& w, Entity u) {
    const Transform* ut = w.get<Transform>(u);
    if (!ut) return NULL_ENTITY;
    std::vector<Entity> wanted;   // the beds others are already on their way to
    for (Entity o : w.view<PropOrder>()) {
        if (o != u) wanted.push_back(w.get<PropOrder>(o)->prop);
    }
    Entity best = NULL_ENTITY;
    float bestD2 = 0.0f;
    for (Entity p : w.view<Prop, Transform>()) {
        const PropKind kind = w.get<Prop>(p)->kind;
        if (restedGainPerHour(kind) <= 0.0) continue;
        if (propOccupant(w, p) != NULL_ENTITY || lockShutFor(w, p, u)) continue;
        if (std::find(wanted.begin(), wanted.end(), p) != wanted.end()) continue;
        const Transform* pt = w.get<Transform>(p);
        const float d2 = (pt->x - ut->x) * (pt->x - ut->x) + (pt->z - ut->z) * (pt->z - ut->z);
        if (best == NULL_ENTITY || d2 < bestD2) {
            best = p;
            bestD2 = d2;
        }
    }
    return best;
}

// Rescue: the nearest unit of its own faction lying out cold on the ground (not a corpse, not in a bed, carried by
// nobody and not being fetched by anyone else) within RESCUE_RANGE
Entity rescueTarget(World& w, Entity u) {
    const Transform* ut = w.get<Transform>(u);
    if (!ut) return NULL_ENTITY;
    const int mine = factionOf(w, u);
    std::vector<Entity> fetched;
    for (Entity o : w.view<CarryOrder>()) {
        if (o != u) fetched.push_back(w.get<CarryOrder>(o)->target);
    }
    Entity best = NULL_ENTITY;
    float bestD2 = RESCUE_RANGE * RESCUE_RANGE;
    for (Entity v : w.view<Unconscious, Transform>()) {
        if (v == u || w.has<Corpse>(v) || w.has<Carried>(v) || w.has<Mob>(v) || factionOf(w, v) != mine) continue;
        const Entity bed = occupiedProp(w, v);
        if (bed != NULL_ENTITY && w.has<Prop>(bed) && restedGainPerHour(w.get<Prop>(bed)->kind) > 0.0) continue;
        if (std::find(fetched.begin(), fetched.end(), v) != fetched.end()) continue;
        const Transform* vt = w.get<Transform>(v);
        const float d2 = (vt->x - ut->x) * (vt->x - ut->x) + (vt->z - ut->z) * (vt->z - ut->z);
        if (d2 <= bestD2) {
            best = v;
            bestD2 = d2;
        }
    }
    return best;
}

// Patrol Spawn: a walkable spot within SPAWN_PATROL_RADIUS of where the mob was placed
BehaviorWait spawnSpot(World& w, const Terrain& t, Entity u, Rng& rng, Vec2& to) {
    const SpawnHome* h = w.get<SpawnHome>(u);
    if (!h) return BehaviorWait::NowhereToRoam;
    for (int attempt = 0; attempt < 12; ++attempt) {
        const float a = rng.unit() * 6.2831853f, d = std::sqrt(rng.unit()) * SPAWN_PATROL_RADIUS;
        const float x = h->x + std::cos(a) * d, z = h->z + std::sin(a) * d;
        if (!openTile(t, x, z)) continue;
        to = Vec2{ std::floor(x) + 0.5f, std::floor(z) + 0.5f };
        return BehaviorWait::None;
    }
    return BehaviorWait::NowhereToRoam;
}

// Tries to start an automated behavior's step. False, with nothing else changed, when it can't be done now; r.why
// then says why.
bool startStep(World& w, Terrain& t, const std::vector<Claim>& claims, Entity u, Behaviors& b, BehaviorRule& r, Rng& rng) {
    if (r.kind == BehaviorKind::Rescue) {
        // To a unit of its faction out cold, to pick it up, when there is a bed or bedroll to take it to
        r.why = BehaviorWait::None;
        if (w.has<Mob>(u) || w.has<Carrying>(u)) return false;
        const Entity target = rescueTarget(w, u);
        if (target == NULL_ENTITY) return false;
        if (restTarget(w, u) == NULL_ENTITY) {
            r.why = BehaviorWait::NoBed;
            return false;
        }
        const Transform* tt = w.get<Transform>(target);
        b.busy = true;
        b.step = r;
        b.stepProp = NULL_ENTITY;
        w.remove<ActiveSquadMember>(u);
        cancelPickup(w, u);
        w.remove<TalkOrder>(u);
        endPropTasks(w, u);
        w.remove<IdleSpot>(u);
        w.add(u, SoloMove{});
        planSolo(w, t, u, Vec2{ tt->x, tt->z });
        w.add(u, CarryOrder{ target });
        return true;
    }
    if (r.kind == BehaviorKind::Rest) {
        // Only below its percent of Rested: then to the nearest free bed or bedroll, to lie in it
        const Needs* n = w.get<Needs>(u);
        r.why = BehaviorWait::None;
        if (!n || w.has<Mob>(u) || percentOf(n->rested, n->maxRested) >= r.percent) return false;
        const Entity bed = restTarget(w, u);
        if (bed == NULL_ENTITY) {
            r.why = BehaviorWait::NoBed;
            return false;
        }
        b.busy = true;
        b.step = r;
        b.stepProp = bed;
        autoGo(w, t, u, bed, PropAction::Occupy);
        return true;
    }
    if (isWalkBehavior(r.kind)) {
        Vec2 to{ 0.0f, 0.0f };
        r.why = r.kind == BehaviorKind::Patrol ? patrolSpot(w, t, claims, u, rng, to)
              : r.kind == BehaviorKind::PatrolSpawn ? spawnSpot(w, t, u, rng, to) : roamSpot(w, t, claims, u, rng, to);
        if (r.why != BehaviorWait::None) return false;
        b.busy = true;
        b.step = r;
        b.stepProp = NULL_ENTITY;
        autoWalk(w, t, u, to);
        return true;
    }
    Entity prop = NULL_ENTITY;
    PropAction action = PropAction::Use;
    if (r.kind == BehaviorKind::AutoHaul) {
        r.why = haulWait(w, claims, u, r);
        if (r.why != BehaviorWait::None) return false;
        prop = r.prop;
        action = PropAction::Store;
    } else {
        const int item = propInfo((PropKind)r.propKind).item;
        if (fullFor(w, u, item)) r.why = BehaviorWait::CarryFull;
        else if (harvestLoaded(w, claims, u, b, item)) r.why = BehaviorWait::LoadWaiting;
        else if ((prop = harvestTarget(w, claims, u, r.propKind)) == NULL_ENTITY) r.why = BehaviorWait::NoProp;
        if (r.why != BehaviorWait::None) return false;
    }
    r.why = BehaviorWait::None;
    b.busy = true;
    b.step = r;
    b.stepProp = prop;
    autoGo(w, t, u, prop, action);
    return true;
}

}  // namespace

// Leave Bed: the unit getting up from a bed, bedroll or other resting prop steps off it onto the open ground beside it.
// The tiles around the prop are tried ring by ring, nearest first (a big prop's own tiles are skipped): the first
// walkable one no other prop stands on is taken, or failing that the first walkable one at all. With no walkable
// tile near, it stays where it is.
void stepOffProp(World& w, Terrain& t, Entity u, Entity prop) {
    Transform* ut = w.get<Transform>(u);
    const Transform* pt = w.get<Transform>(prop);
    const Prop* pp = w.get<Prop>(prop);
    if (!ut || !pt || !pp) return;
    const Footprint& pf = propInfo(pp->kind).foot;
    const int cx = (int)std::floor(pt->x), cz = (int)std::floor(pt->z);
    // Whether a point lies on a prop other than the one being left. A foundation is a floor, and blocks nothing.
    auto onOtherProp = [&](float x, float z) {
        for (Entity o : w.view<Prop, Transform>()) {
            if (o == prop) continue;
            const PropInfo& oi = propInfo(w.get<Prop>(o)->kind);
            if (oi.foot.foundation) continue;
            const Transform* ot = w.get<Transform>(o);
            const float dx = x - ot->x, dz = z - ot->z;
            const float reach = footprintReach(oi.foot) + PROP_BLOCK_RADIUS;
            if (dx * dx + dz * dz > reach * reach) continue;
            if (insideFootprint(oi.foot, dx, dz, w.get<Prop>(o)->yaw)) return true;
            if (dx * dx + dz * dz < PROP_BLOCK_RADIUS * PROP_BLOCK_RADIUS) return true;
        }
        return false;
    };
    constexpr int MAX_RING = 3;
    bool haveFallback = false;
    float fx = 0.0f, fy = 0.0f, fz = 0.0f;
    for (int ring = 1; ring <= MAX_RING; ++ring) {
        // The ring's edge-sharing tiles come before its corners, so the unit steps straight off a side when it can
        std::vector<std::pair<int, int>> tiles;
        for (int dz = -ring; dz <= ring; ++dz) {
            for (int dx = -ring; dx <= ring; ++dx) {
                if (std::max(std::abs(dx), std::abs(dz)) == ring) tiles.push_back({ dx, dz });
            }
        }
        std::stable_sort(tiles.begin(), tiles.end(), [](const auto& a, const auto& b) {
            return a.first * a.first + a.second * a.second < b.first * b.first + b.second * b.second;
        });
        for (const auto& [dx, dz] : tiles) {
            const int tx = cx + dx, tz = cz + dz;
            const float x = tx + 0.5f, z = tz + 0.5f;
            if (insideFootprint(pf, x - pt->x, z - pt->z, pp->yaw)) continue;   // still on the prop it is leaving
            bool walk = false;
            float h = 0.0f;
            if (!t.tileGround(tx, tz, walk, h) || !walk) continue;
            if (!onOtherProp(x, z)) {
                ut->x = x;
                ut->y = h;
                ut->z = z;
                return;
            }
            if (!haveFallback) {
                haveFallback = true;
                fx = x;
                fy = h;
                fz = z;
            }
        }
    }
    if (haveFallback) {
        ut->x = fx;
        ut->y = fy;
        ut->z = fz;
    }
}

void automationSystem(World& w, Terrain& t, float dt, Rng& rng) {
    const std::vector<Claim> claims = claimsOf(w);
    for (Entity u : w.view<Behaviors, Transform>()) {
        Behaviors* b = w.get<Behaviors>(u);
        bool any = false;
        for (BehaviorRule& r : b->rules) {
            if (!isAutoStep(r.kind)) continue;
            r.cooldown = std::max(0.0f, r.cooldown - dt);
            if (!r.on) r.why = BehaviorWait::None;
            any = any || r.on;
        }
        // The step's behavior was switched off or deleted: the step stops
        if (b->busy) {
            const BehaviorRule* current = stepRule(*b);
            if (!current || !current->on) stopAutoStep(w, u, *b);
        }
        if (!any && b->seek == NULL_ENTITY && !b->seeking) continue;
        const bool yours = w.has<Commandable>(u);
        const float resume = yours ? AUTO_RESUME_SECONDS : 0.0f;
        // Out cold, carried, shut in or downed: nothing to do
        const Needs* n = w.get<Needs>(u);
        if (w.has<Unconscious>(u) || w.has<Carried>(u) || w.has<PlayingDead>(u) || lockedIn(w, u) || (n && n->hp <= 0)) {
            b->busy = false;
            b->seeking = false;
            b->recovered = 0.0f;
            continue;
        }
        // Leave Bed: lying in a bed or bedroll at full health and fully rested for a while, it gets up
        {
            const Entity bed = occupiedProp(w, u);
            const Prop* bp = bed != NULL_ENTITY ? w.get<Prop>(bed) : nullptr;
            bool leaveOn = false;
            for (const BehaviorRule& r : b->rules) leaveOn = leaveOn || (r.on && r.kind == BehaviorKind::LeaveBed);
            if (leaveOn && bp && restedGainPerHour(bp->kind) > 0.0 && n && n->hp >= n->maxHp && n->rested >= n->maxRested) {
                b->recovered += dt;
                if (b->recovered >= LEAVE_BED_SECONDS) {
                    w.remove<Occupying>(u);
                    stepOffProp(w, t, u, bed);   // off the bed, onto the open ground beside it
                    b->recovered = 0.0f;
                    if (b->busy && b->step.kind == BehaviorKind::Rest) b->busy = false;
                }
            } else {
                b->recovered = 0.0f;
            }
        }
        // A fight comes first: the step stops, and the automation waits for the fight to end
        const Combat* c = w.get<Combat>(u);
        if ((c && c->target != NULL_ENTITY) || w.has<AttackOrder>(u)) {
            if (b->busy) stopAutoStep(w, u, *b);
            if (b->seeking) w.remove<SoloMove>(u);
            b->seeking = false;
            b->wait = resume;
            continue;
        }
        // Share: a unit in need walks to the unit that can share with it. The walk ends when it is close enough (Eat or
        // Heal then takes the item), when the need is gone, or when you give it an order. It may cut an automated
        // step short, but never an order of yours.
        if (b->seeking) {
            const SoloMove* so = w.get<SoloMove>(u);
            const bool walking = so && so->next < so->waypoints.size();
            if (b->seek == NULL_ENTITY || !walking || flatGap(w, u, b->seek) <= SHARE_RANGE) {
                if (walking) w.remove<SoloMove>(u);
                b->seeking = false;
            } else if (onOtherOrder(w, u, *b)) {
                b->seeking = false;
                b->wait = resume;
            }
            continue;
        }
        if (b->seek != NULL_ENTITY && w.has<Transform>(b->seek) && flatGap(w, u, b->seek) > SHARE_RANGE &&
            !onOtherOrder(w, u, *b) && (b->busy || b->wait <= 0.0f)) {
            if (b->busy) stopAutoStep(w, u, *b);
            const Transform* gt = w.get<Transform>(b->seek);
            const Transform* ut = w.get<Transform>(u);
            // Stop just short of the giver, on the near side, so the two don't push each other about
            const float dx = gt->x - ut->x, dz = gt->z - ut->z, d = std::max(1e-3f, std::hypot(dx, dz));
            const float stop = std::min(SHARE_WALK_STOP, d);
            w.remove<ActiveSquadMember>(u);
            w.remove<IdleSpot>(u);
            w.add(u, SoloMove{});
            planSolo(w, t, u, Vec2{ gt->x - dx / d * stop, gt->z - dz / d * stop });
            b->seeking = true;
            continue;
        }
        if (!any) {
            // No automated behavior: only the wait before a walk to a sharer is kept
            b->wait = onOtherOrder(w, u, *b) ? resume : std::max(0.0f, b->wait - dt);
            continue;
        }
        const PropOrder* po = w.get<PropOrder>(u);
        const SoloMove* walk = w.get<SoloMove>(u);
        const bool walkingStep = b->busy && isWalkBehavior(b->step.kind) && walk && walk->next < walk->waypoints.size();
        const bool going = (po && po->automatic) || walkingStep;
        const Working* wk = w.get<Working>(u);
        const bool working = b->busy && wk && !wk->unlocking && wk->prop == b->stepProp;

        // Rescue: on its way to pick the unit up, then carrying it to a bed or bedroll; done once it is put in one.
        // An order of yours takes over.
        if (b->busy && b->step.kind == BehaviorKind::Rescue) {
            const PropOrder* rpo = w.get<PropOrder>(u);
            const bool yoursNow = w.has<ActiveSquadMember>(u) || w.has<PutDownOrder>(u) || (rpo && !rpo->automatic) ||
                                  w.has<AttackOrder>(u) || w.has<FollowOrder>(u) || w.has<DefendOrder>(u) ||
                                  w.has<PickupOrder>(u) || w.has<PickupChain>(u) || w.has<LootOrder>(u) || w.has<TalkOrder>(u);
            if (yoursNow) {
                b->busy = false;
                b->wait = resume;
                continue;
            }
            if (w.has<CarryOrder>(u)) continue;   // on its way to pick it up
            if (w.has<Carrying>(u)) {
                if (rpo && rpo->automatic) continue;   // taking it to the bed
                // To the nearest free bed: a new one if the last could not be got into, else it is put down here
                const Entity bed = restTarget(w, u);
                if (bed != NULL_ENTITY && bed != b->stepProp) {
                    b->stepProp = bed;
                    autoGo(w, t, u, bed, PropAction::Place);
                    continue;
                }
                putDown(w, u);
            }
            b->busy = false;
            w.remove<SoloMove>(u);
            if (BehaviorRule* r = stepRule(*b)) r->cooldown = BEHAVIOR_RETRY_SECONDS;
            continue;
        }
        // Resting: it stays in the bed until fully rested, then gets up and the automation goes on
        if (b->busy && b->step.kind == BehaviorKind::Rest) {
            const Occupying* oc = w.get<Occupying>(u);
            if (oc && oc->prop == b->stepProp) {
                if (n && n->rested < n->maxRested) continue;
                w.remove<Occupying>(u);
                b->busy = false;
                if (BehaviorRule* r = stepRule(*b)) r->cooldown = BEHAVIOR_RETRY_SECONDS;
                continue;
            }
        }
        if (b->busy) {
            if (going) continue;   // on its way
            if (working) {
                // A harvest goes on until the unit has a load for its haul, or no room
                if (!harvestLoaded(w, claims, u, *b, propInfo((PropKind)b->step.propKind).item)) continue;
                w.remove<Working>(u);
                b->busy = false;
            } else if (onOtherOrder(w, u, *b)) {   // you gave it something else to do
                b->busy = false;
                b->wait = resume;
                continue;
            } else {
                // The step ended by itself: the haul was put in, the walk got there, or the harvest did not start (the
                // prop had all its workers, or the way was blocked). A harvest that did not start waits before it is
                // tried again; a Patrol or a Roam pauses where it got to before its next walk.
                b->busy = false;
                if (BehaviorRule* r = stepRule(*b)) {
                    if (b->step.kind == BehaviorKind::AutoHarvest) r->cooldown = BEHAVIOR_RETRY_SECONDS;
                    if (b->step.kind == BehaviorKind::Patrol || b->step.kind == BehaviorKind::PatrolSpawn) {
                        r->cooldown = PATROL_PAUSE_MIN + rng.unit() * (PATROL_PAUSE_MAX - PATROL_PAUSE_MIN);
                    }
                    if (b->step.kind == BehaviorKind::Roam) r->cooldown = ROAM_PAUSE_MIN + rng.unit() * (ROAM_PAUSE_MAX - ROAM_PAUSE_MIN);
                }
                w.remove<SoloMove>(u);
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
            if (!isAutoStep(r.kind) || !r.on || r.cooldown > 0.0f) continue;
            if (startStep(w, t, claims, u, *b, r, rng)) break;
            r.cooldown = BEHAVIOR_RETRY_SECONDS;
        }
    }
}

void patrolSpawnSystem(World& w, Terrain& t, float dt) {
    for (Entity u : w.view<SpawnHome, Transform, Behaviors>()) {
        if (w.has<Unconscious>(u) || w.has<Carried>(u)) continue;
        SpawnHome* h = w.get<SpawnHome>(u);
        Behaviors* b = w.get<Behaviors>(u);
        BehaviorRule* rule = nullptr;
        for (BehaviorRule& r : b->rules) {
            if (r.kind == BehaviorKind::PatrolSpawn) rule = &r;
        }
        if (!rule || !rule->on) {
            h->returning = false;
            h->returnSeconds = 0.0f;
            continue;
        }
        Transform* tr = w.get<Transform>(u);
        float away = std::hypot(tr->x - h->x, tr->z - h->z);
        if (!h->returning && away > SPAWN_LEASH_RANGE) {
            // Too far from home: the chase is given up, it is whole again, and it walks back
            h->returning = true;
            h->returnSeconds = 0.0f;
            queueLog(langf("MISC_GOES_HOME", { nameOf(w, u) }), LogKind::Other, npcOnly(w, u));
        }
        if (!h->returning) continue;
        // A boss that has not made it home in time is put there at once
        h->returnSeconds += dt;
        if (const Mob* m = w.get<Mob>(u); m && mobInfo(m->kind).category == MobCategory::Boss
            && h->returnSeconds >= BOSS_RETURN_TELEPORT_SECONDS && away > SPAWN_HOME_REACH) {
            tr->x = h->x;
            tr->z = h->z;
            tr->y = t.heightAt(h->x, h->z);
            away = 0.0f;
        }
        // On the way home: no fight, no damage taken (combat.cpp), and everything back to full
        if (Combat* c = w.get<Combat>(u)) {
            c->target = NULL_ENTITY;
            c->stamina = -1.0f;   // refilled to its most by the combat step
        }
        w.remove<AttackOrder>(u);
        b->passive = true;
        if (Needs* n = w.get<Needs>(u); n && n->hp != n->maxHp) {
            n->hp = n->maxHp;
            n->changed |= NEEDS_HP_CHANGED;
        }
        if (away <= SPAWN_HOME_REACH) {
            // Home: back to its patrol, and it can be hurt again
            h->returning = false;
            h->returnSeconds = 0.0f;
            w.remove<SoloMove>(u);
            b->busy = false;
            rule->cooldown = 0.0f;
            continue;
        }
        const SoloMove* so = w.get<SoloMove>(u);
        const bool walking = so && so->next < so->waypoints.size();
        if (!walking || !b->busy || b->step.kind != BehaviorKind::PatrolSpawn) {
            b->busy = true;
            b->step = *rule;
            b->stepProp = NULL_ENTITY;
            autoWalk(w, t, u, Vec2{ h->x, h->z });
        }
    }
}

// Sends a unit running from another, WILD_FLEE_DISTANCE away, turning a little either side when the way straight off is
// not walkable. The run is a Roam step, so it ends like one. Already running, it is left to run.
void fleeFrom(World& w, Terrain& t, Entity u, Behaviors& b, Entity from) {
    const SoloMove* so = w.get<SoloMove>(u);
    if (b.busy && b.step.kind == BehaviorKind::Roam && so && so->next < so->waypoints.size()) return;
    const Transform* ut = w.get<Transform>(u);
    const Transform* ot = w.get<Transform>(from);
    if (!ut || !ot) return;
    const float away = std::atan2(ut->z - ot->z, ut->x - ot->x);
    static const float TURNS[] = { 0.0f, 0.5f, -0.5f, 1.0f, -1.0f, 1.6f, -1.6f };
    for (float turn : TURNS) {
        const float x = ut->x + std::cos(away + turn) * WILD_FLEE_DISTANCE;
        const float z = ut->z + std::sin(away + turn) * WILD_FLEE_DISTANCE;
        if (!openTile(t, x, z)) continue;
        if (b.busy) stopAutoStep(w, u, b);
        BehaviorRule roam;
        roam.kind = BehaviorKind::Roam;
        for (const BehaviorRule& r : b.rules) {
            if (r.kind == BehaviorKind::Roam) roam = r;
        }
        b.busy = true;
        b.step = roam;
        b.stepProp = NULL_ENTITY;
        autoWalk(w, t, u, Vec2{ std::floor(x) + 0.5f, std::floor(z) + 0.5f });
        return;
    }
}

void wildMobSystem(World& w, Terrain& t, float dt) {
    // Exiles fight like any neutral unit, until their health is low (EXILE_FLEE_PERCENT or less): then they are passive,
    // break off any fight, and run from the nearest unit attacking them. Healed above it, they fight again.
    for (Entity u : w.view<Behaviors, Transform, Faction>()) {
        if (w.get<Faction>(u)->id != FACTION_EXILE || w.has<Mob>(u)) continue;
        Behaviors* b = w.get<Behaviors>(u);
        const Needs* n = w.get<Needs>(u);
        const bool low = n && n->maxHp > 0 && n->hp * 100 <= n->maxHp * EXILE_FLEE_PERCENT;
        b->passive = low;
        if (!low) continue;
        if (Combat* c = w.get<Combat>(u)) c->target = NULL_ENTITY;
        if (w.has<Unconscious>(u) || w.has<Carried>(u) || w.has<PlayingDead>(u) || lockedIn(w, u) || n->hp <= 0) continue;
        b->sense -= dt;
        if (b->sense > 0.0f) continue;
        b->sense = 0.25f;
        const Transform* ut = w.get<Transform>(u);
        Entity near = NULL_ENTITY;
        float nearD2 = 0.0f;
        for (Entity o : w.view<Combat, Transform>()) {
            if (o == u || w.get<Combat>(o)->target != u || w.has<Unconscious>(o)) continue;
            const Transform* ot = w.get<Transform>(o);
            const float d2 = (ot->x - ut->x) * (ot->x - ut->x) + (ot->z - ut->z) * (ot->z - ut->z);
            if (near == NULL_ENTITY || d2 < nearD2) {
                near = o;
                nearD2 = d2;
            }
        }
        if (near == NULL_ENTITY) continue;
        if (w.has<Occupying>(u)) w.remove<Occupying>(u);   // it gets up off a seat or bed to run
        fleeFrom(w, t, u, *b, near);
    }
    for (Entity u : w.view<Mob, Behaviors, Transform, Faction>()) {
        const int faction = w.get<Faction>(u)->id;
        if (!isMobFaction(faction)) continue;   // a camp's mob is its faction's, and goes by its reputations
        Behaviors* b = w.get<Behaviors>(u);
        const MobTemperament tm = mobTemperament(w.get<Mob>(u)->kind);
        const SpawnHome* home = w.get<SpawnHome>(u);
        b->passive = tm == MobTemperament::Avoidant || tm == MobTemperament::Passive || (home && home->returning);
        if (tm != MobTemperament::Avoidant) continue;
        const Needs* n = w.get<Needs>(u);
        if (w.has<Unconscious>(u) || w.has<Carried>(u) || (n && n->hp <= 0)) continue;
        b->sense -= dt;
        if (b->sense > 0.0f) continue;
        b->sense = 0.5f;
        // The nearest outsider on its feet within WILD_AVOID_RANGE
        const Transform* ut = w.get<Transform>(u);
        Entity near = NULL_ENTITY;
        float nearD2 = WILD_AVOID_RANGE * WILD_AVOID_RANGE;
        for (Entity o : w.view<Unit, Transform>()) {
            if (o == u || w.has<Unconscious>(o) || w.has<Carried>(o)) continue;
            const Faction* of = w.get<Faction>(o);
            if ((of ? of->id : FACTION_PLAYER) == faction) continue;
            if (const Mob* om = w.get<Mob>(o); om && mobKindsFriendly(w.get<Mob>(u)->kind, om->kind)) continue;
            const Transform* ot = w.get<Transform>(o);
            const float d2 = (ot->x - ut->x) * (ot->x - ut->x) + (ot->z - ut->z) * (ot->z - ut->z);
            if (d2 < nearD2) {
                near = o;
                nearD2 = d2;
            }
        }
        if (near == NULL_ENTITY) continue;
        fleeFrom(w, t, u, *b, near);   // away from it
    }
}

WorkStart tryStartWork(World& w, Entity unit, Entity prop) {
    const Prop* p = w.get<Prop>(prop);
    if (!p || !isWorkProp(propInfo(p->kind).use) || !w.get<Transform>(unit)) return WorkStart::TooFar;
    if (w.has<Mob>(unit)) return WorkStart::TooFar;   // a mob works no props (mobs.h)
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
    w.remove<ActiveSquadMember>(unit);
    w.remove<SoloMove>(unit);
    cancelPickup(w, unit);
    w.remove<AttackOrder>(unit);
    w.remove<TalkOrder>(unit);
    endPropTasks(w, unit);
    w.add(unit, Working{ prop });
    return WorkStart::Started;
}

WorkStart tryStartUnlock(World& w, Entity unit, Entity prop) {
    const Lock* lock = w.get<Lock>(prop);
    if (!lock || !w.has<Prop>(prop) || !w.get<Transform>(unit)) return WorkStart::NotLocked;
    if (w.has<Mob>(unit)) return WorkStart::SkillTooLow;   // a mob has no Lockpicking (mobs.h)
    if (!lockShutFor(w, prop, unit)) return WorkStart::NotLocked;
    if (const Working* wk = w.get<Working>(unit); wk && wk->prop == prop && wk->unlocking) return WorkStart::Started;
    if (flatGap(w, unit, prop) > STORAGE_CLOSE_RANGE) return WorkStart::TooFar;
    if (const Needs* n = w.get<Needs>(unit); n && n->hp <= 0) return WorkStart::TooFar;
    if (levelOf(statsOf(w, unit), StatId::Lockpicking) < lock->level) return WorkStart::SkillTooLow;
    if (workersAt(w, prop) >= 1) return WorkStart::Full;   // one unit at a lock at a time
    const bool inside = occupiedProp(w, unit) == prop;     // picking the cage it is shut in, from the inside
    if (!inside && lockedIn(w, unit)) return WorkStart::Locked;
    w.remove<ActiveSquadMember>(unit);
    w.remove<SoloMove>(unit);
    cancelPickup(w, unit);
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

bool disbandUnit(World& w, Entity u) {
    if (!w.has<Commandable>(u) || !w.has<Unit>(u)) return false;
    // Whatever it was doing ends. One locked in a cage stays in it; anyone it carried is put down.
    if (const ActiveSquadMember* sm = w.get<ActiveSquadMember>(u)) {
        if (ActiveSquad* sq = w.get<ActiveSquad>(sm->squad); sq && sq->size > 0) --sq->size;
        w.remove<ActiveSquadMember>(u);
    }
    const bool caged = lockedIn(w, u);
    const Occupying keep = w.has<Occupying>(u) ? *w.get<Occupying>(u) : Occupying{};
    if (w.has<Carrying>(u)) putDown(w, u);
    w.remove<SoloMove>(u);
    cancelPickup(w, u);
    w.remove<AttackOrder>(u);
    w.remove<TalkOrder>(u);
    endPropTasks(w, u);
    if (caged) w.add(u, keep);
    if (Combat* c = w.get<Combat>(u)) c->target = NULL_ENTITY;
    w.remove<Commandable>(u);
    w.remove<Selected>(u);
    w.get<Unit>(u)->speed = HOSTILE_WALK_SPEED;   // it walks at the pace of every unit that is not yours

    const Transform* tr = w.get<Transform>(u);
    Behaviors& b = w.has<Behaviors>(u) ? *w.get<Behaviors>(u) : w.add(u, Behaviors{});
    b.busy = false;
    b.seeking = false;
    b.seek = NULL_ENTITY;
    b.stepProp = NULL_ENTITY;
    b.wait = 0.0f;
    b.pending = true;
    b.block = false;
    b.sneak = false;
    int faction = FACTION_EXILE;
    if (const Mob* mb = w.get<Mob>(u)) {
        // Back to the wild, in its kind's own faction, as a wild mob of that kind is made (spawnMob)
        const MobInfo& m = mobInfo(mb->kind);
        faction = mobFaction(mb->kind);
        b.rules = mobBehaviors();
        BehaviorRule roam;
        roam.kind = m.patrolSpawn ? BehaviorKind::PatrolSpawn : BehaviorKind::Roam;
        addBehavior(b.rules, roam);
        if (m.patrolSpawn && tr) w.add(u, SpawnHome{ tr->x, tr->z, false });
        const MobTemperament tm = mobTemperament(mb->kind);
        b.passive = tm == MobTemperament::Avoidant || tm == MobTemperament::Passive;
    } else {
        // An Exile: feeds and heals itself and roams, and has something to say like any other NPC. It fights as any
        // neutral unit does, but runs once its health is low (wildMobSystem).
        b.rules = exileBehaviors();
        b.passive = false;
        if (!w.has<Dialogue>(u)) w.add(u, Dialogue{});
        if (Needs* n = w.get<Needs>(u)) n->hunger = n->maxHunger;
    }
    if (Faction* fa = w.get<Faction>(u)) fa->id = faction;
    else w.add(u, Faction{ faction });
    if (factionsHostile(faction, FACTION_PLAYER)) w.add(u, Hostile{});
    else w.add(u, Neutral{});
    queueLog(langf(w.has<Mob>(u) ? "MISC_DISBANDED_MOB" : "MISC_DISBANDED", { nameOf(w, u) }), LogKind::Other);
    return true;
}

bool unitIdle(World& w, Entity u) {
    if (w.has<Corpse>(u) || w.has<Unconscious>(u) || w.has<PlayingDead>(u) || w.has<Carried>(u)) return false;
    if (const Behaviors* b = w.get<Behaviors>(u); b && (b->busy || b->seeking)) return false;
    if (w.has<Working>(u) || w.has<PropOrder>(u) || w.has<Occupying>(u) || w.has<Carrying>(u) || w.has<CarryOrder>(u) ||
        w.has<PutDownOrder>(u) || w.has<LootOrder>(u) || w.has<DefendOrder>(u) || w.has<FollowOrder>(u) ||
        w.has<AttackOrder>(u) || w.has<TalkOrder>(u) || w.has<PickupOrder>(u) || w.has<PickupChain>(u)) {
        return false;
    }
    if (const Combat* c = w.get<Combat>(u); c && c->target != NULL_ENTITY) return false;
    if (const SoloMove* so = w.get<SoloMove>(u); so && so->next < so->waypoints.size()) return false;
    if (const ActiveSquadMember* sm = w.get<ActiveSquadMember>(u)) {
        if (const ActiveSquad* sq = w.get<ActiveSquad>(sm->squad); sq && !sq->arrived) return false;
    }
    return true;
}

std::string taskNameOf(World& w, Entity u) {
    if (w.has<Corpse>(u)) return lang("UILABEL_TASK_DEAD");
    if (w.has<Unconscious>(u)) return lang("UILABEL_TASK_UNCONSCIOUS");
    if (w.has<PlayingDead>(u)) return lang("UILABEL_TASK_PLAYING_DEAD");
    if (w.has<Carried>(u)) return lang("UILABEL_TASK_CARRIED");
    // A step of its own behaviors: the behavior's name, such as Auto-Harvest: Tree, while it is on its way or at it
    if (const Behaviors* b = w.get<Behaviors>(u)) {
        if (b->seeking) return langf("UILABEL_TASK_SHARE_WALK", { nameOf(w, b->seek) });
        const PropOrder* po = w.get<PropOrder>(u);
        const Working* wk = w.get<Working>(u);
        if (b->busy && po && po->automatic) return langf("UILABEL_TASK_AUTO_GOING", { behaviorName(b->step) });
        if (b->busy && wk && !wk->unlocking && wk->prop == b->stepProp) return behaviorName(b->step);
    }
    Entity prop = NULL_ENTITY;
    if (const Working* g = w.get<Working>(u); g && g->unlocking) return statName(StatId::Lockpicking);
    if (const Working* g = w.get<Working>(u); g && w.has<Prop>(g->prop) && w.get<Prop>(g->prop)->kind == PropKind::Debris) {
        return lang("UILABEL_TASK_CLEARING");
    }
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
    if (const ActiveSquadMember* sm = w.get<ActiveSquadMember>(u)) {
        if (const ActiveSquad* sq = w.get<ActiveSquad>(sm->squad); sq && !sq->arrived) return lang("UILABEL_TASK_MOVING");
    }
    return lang("UILABEL_TASK_IDLE");
}

std::vector<EmoteId> emotesOf(World& w, Entity u) {
    // Out cold or dead, that is all it shows
    if (w.has<Corpse>(u)) return { EmoteId::Dead };
    if (w.has<Unconscious>(u)) return { EmoteId::Unconscious };
    if (w.has<PlayingDead>(u)) return { EmoteId::PlayingDead };
    std::vector<EmoteId> out;
    auto propEmoteOf = [&](Entity prop) {
        const Prop* p = w.get<Prop>(prop);
        return p ? propEmote(p->kind) : EmoteId::None;
    };
    if (const Working* wk = w.get<Working>(u)) {
        if (wk->unlocking) out.push_back(EmoteId::Lockpicking);
        else if (propEmoteOf(wk->prop) != EmoteId::None) out.push_back(propEmoteOf(wk->prop));
    }
    if (const Occupying* oc = w.get<Occupying>(u); oc && propEmoteOf(oc->prop) != EmoteId::None) out.push_back(propEmoteOf(oc->prop));
    if (lockedIn(w, u)) out.push_back(EmoteId::LockedIn);
    if (w.has<Inventory>(u) && w.has<Stats>(u)) {
        const float limit = carryLimit(statsOf(w, u));
        if (limit > 0.0f && carriedWeightOf(w, u) >= ENCUMBERED_SHARE * limit) out.push_back(EmoteId::Encumbered);
    }
    if (const Needs* n = w.get<Needs>(u); n && n->hunger < 1) out.push_back(EmoteId::Hungry);
    return out;
}

bool propAcceptsItem(PropKind kind, int item) {
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

int recipeOutput(PropKind kind, int input) {
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
    if (lockShutFor(w, oc->prop, u)) return true;
    // A mob can't work a lock, even its own faction's: in a locked cage it stays in
    const Lock* lk = w.get<Lock>(oc->prop);
    return lk && lk->locked && w.has<Mob>(u);
}

// The first item in a passive crafting prop's input that it burns: its input item, or at a campfire any raw meat it
// can cook. 0 when there is none.
static int nextPassiveInput(const Storage* st, PropKind kind) {
    if (!st) return 0;
    const PropInfo& info = propInfo(kind);
    for (const ItemStack& s : st->slots) {
        if (s.count <= 0 || s.item <= 0) continue;
        if (info.input == INPUT_COOKABLE ? recipeOutput(kind, s.item) > 0 : s.item == info.input) return s.item;
    }
    return 0;
}

// What a passive crafting prop's next finished item is: its item, or at a campfire the burning meat cooked (or, with
// nothing burning, the next meat in its input cooked). 0 for a Vault, and for a campfire with nothing to cook.
static int nextPassiveOutput(const Storage* st, const Fuel& fuel, PropKind kind) {
    const PropInfo& info = propInfo(kind);
    if (info.input != INPUT_COOKABLE) return info.item;
    if (fuel.item > 0) {
        if (const int made = recipeOutput(kind, fuel.item); made > 0) return made;
    }
    const int next = nextPassiveInput(st, kind);
    return next > 0 ? recipeOutput(kind, next) : 0;
}

PassiveState passiveStateOf(World& w, Entity prop) {
    const Prop* p = w.get<Prop>(prop);
    const Output* out = w.get<Output>(prop);
    const Fuel* fuel = w.get<Fuel>(prop);
    if (!p || !out || !fuel) return PassiveState::NoFuel;
    const Storage* st = w.get<Storage>(prop);
    const int made = nextPassiveOutput(st, *fuel, p->kind);
    if (made > 0 && !slotsCanTake(out->slots.data(), STORAGE_SLOTS, made, 1)) return PassiveState::OutputFull;
    if (fuel->hours > 1e-9) return PassiveState::Burning;
    if (nextPassiveInput(st, p->kind) > 0) return PassiveState::Burning;   // it lights the next one now
    return PassiveState::NoFuel;
}

void passiveCraftSystem(World& w, double gameSeconds) {
    for (Entity e : w.view<Prop, Output, Fuel>()) {
        const PropKind kind = w.get<Prop>(e)->kind;
        const PropInfo& info = propInfo(kind);
        if (info.use != PropUse::PassiveCrafting) continue;
        const bool cooks = info.input == INPUT_COOKABLE;   // the campfire: what it makes is the meat it burns, cooked
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
                // A campfire makes the meat that burned cooked; a forge its item
                const int made = cooks ? (fuel.item > 0 ? recipeOutput(kind, fuel.item) : 0) : info.item;
                if (made > 0) {
                    if (!slotsCanTake(out.slots.data(), STORAGE_SLOTS, made, 1)) break;
                    addToSlots(out.slots.data(), STORAGE_SLOTS, made, 1);
                    ++pw.made;   // the ring hops for it
                    queueSoundFrom(w, cooks ? Sfx::DoneCooking : Sfx::DoneForge, e);
                }
                pw.progress = 0.0;
                if (cooks && fuel.hours <= 1e-9) fuel.item = 0;   // that meat is done
            }
            // No room for the next item: pause, burning nothing
            const int next = nextPassiveOutput(st, fuel, kind);
            if (next > 0 && !slotsCanTake(out.slots.data(), STORAGE_SLOTS, next, 1)) break;
            if (hours <= 1e-12) {
                burning = fuel.hours > 1e-9 || nextPassiveInput(st, kind) > 0;
                break;
            }
            // Out of fuel: the next input item is used up, and burns for its hours
            if (fuel.hours <= 1e-9) {
                fuel.hours = 0.0;
                const int in = nextPassiveInput(st, kind);
                if (in <= 0 || !takeOne(st->slots.data(), STORAGE_SLOTS, in)) break;
                fuel.hours = perInput;
                fuel.item = in;
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
    queueLog(langf("MISC_BREAKS_FREE", { nameOf(w, u) }), LogKind::Other, npcOnly(w, u));
}

bool canBeCarried(World& w, Entity target) {
    if (!w.has<Unit>(target) || !w.has<Transform>(target) || w.has<Carried>(target) || w.has<Carrying>(target)) return false;
    return w.has<Unconscious>(target) || w.has<Commandable>(target);
}

// ---- Unconscious, Doom and Corpses ----

float doomPerHour(const Stats& s) {
    const int vit = std::clamp(levelOf(s, StatId::Vitality), 1, MAX_LEVEL);
    return 20.0f + 30.0f * (1.0f - (float)vit / 100.0f);
}

float doomRecoveryPerHour(PropKind propKind) {
    if (propKind == PropKind::Bed) return 20.0f;
    if (propKind == PropKind::Bedroll) return 10.0f;
    return 0.0f;
}

void knockOut(World& w, Entity u) {
    if (!w.has<Unit>(u) || w.has<Unconscious>(u)) return;
    // Whatever it was doing ends. It keeps its bed if it is lying on one, and drops anyone it carried.
    const Entity bed = occupiedProp(w, u);
    const Prop* bp = bed != NULL_ENTITY ? w.get<Prop>(bed) : nullptr;
    const bool keepBed = bp && doomRecoveryPerHour(bp->kind) > 0.0f;
    if (w.has<Carrying>(u)) putDown(w, u);
    if (const ActiveSquadMember* sm = w.get<ActiveSquadMember>(u)) {
        if (ActiveSquad* sq = w.get<ActiveSquad>(sm->squad); sq && sq->size > 0) --sq->size;
        w.remove<ActiveSquadMember>(u);
    }
    w.remove<SoloMove>(u);
    cancelPickup(w, u);
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
    // Well rested, and not starving: no Doom, and it wakes by itself after a while (a mob has no Rested)
    if (const Needs* n = w.get<Needs>(u); n && !w.has<Mob>(u) && n->hunger > 0
        && percentOf(n->rested, n->maxRested) >= RESTED_SAFE_PERCENT) {
        un.rested = true;
        un.wake = RESTED_WAKE_HOURS;
    }
    w.add(u, un);
    queueLog(langf("MISC_FALLS_UNCONSCIOUS", { nameOf(w, u) }), LogKind::Other, npcOnly(w, u));
    queueSoundFrom(w, Sfx::Downed, u);
    const int victim = factionOf(w, u);
    if (un.koFaction == FACTION_PLAYER && victim != FACTION_PLAYER && !isMobFaction(victim)) {
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
        if (un->rested) {
            // Knocked out well rested: no Doom. It wakes when its wait runs out, and lies still playing dead if a
            // hostile unit is near (a carried one: carrySystem decides, by who carries it).
            un->doom = 0.0f;
            un->wake -= hours;
            if (un->wake <= 0.0f) {
                w.remove<Unconscious>(u);
                if (Needs* n = w.get<Needs>(u)) {
                    n->hp = std::max(n->hp, std::min(std::max(n->maxHp, 1), RECOVERY_HEALTH));
                    n->changed |= NEEDS_HP_CHANGED;
                    // Coming round from a knock-out is tiring
                    n->rested = std::max(0, n->rested - RESTED_WAKE_COST);
                }
                // Carried by anyone but a hostile, it is put down and stands up again
                if (const Carried* cd = w.get<Carried>(u); cd && !w.has<Hostile>(cd->by)) putDown(w, cd->by);
                queueLog(langf("MISC_RECOVERS", { nameOf(w, u) }), LogKind::Other, npcOnly(w, u));
                out.woke.push_back(u);
                if (!w.has<Carried>(u) && hostileNear(w, u)) {
                    w.add(u, PlayingDead{});
                    queueLog(langf("MISC_PLAYS_DEAD", { nameOf(w, u) }), LogKind::Other, npcOnly(w, u));
                }
            }
            continue;
        }
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
                queueLog(langf("MISC_RECOVERS", { nameOf(w, u) }), LogKind::Other, npcOnly(w, u));
                out.woke.push_back(u);
            }
            continue;
        }
        un->doom = std::clamp(un->doom + doomPerHour(statsOf(w, u)) * hours, 0.0f, 100.0f);
        if (un->doom >= 100.0f) {
            finishDoom(w, u);
            out.died.push_back(u);
        }
    }
    return out;
}

bool hostileNear(World& w, Entity u) {
    const Transform* ut = w.get<Transform>(u);
    if (!ut) return false;
    const int mine = factionOf(w, u);
    for (Entity o : w.view<Unit, Transform>()) {
        if (o == u || w.has<Unconscious>(o) || w.has<PlayingDead>(o) || w.has<Carried>(o)) continue;
        if (!factionsHostile(mine, factionOf(w, o))) continue;
        const Transform* ot = w.get<Transform>(o);
        const float dx = ot->x - ut->x, dz = ot->z - ut->z;
        if (dx * dx + dz * dz <= RESTED_HOSTILE_RANGE * RESTED_HOSTILE_RANGE) return true;
    }
    return false;
}

void playDeadSystem(World& w) {
    for (Entity u : w.view<PlayingDead>()) {
        if (w.has<Carried>(u) || w.has<Unconscious>(u)) continue;   // carried: carrySystem looks after it
        if (hostileNear(w, u)) continue;
        w.remove<PlayingDead>(u);
        queueLog(langf("MISC_GETS_UP", { nameOf(w, u) }), LogKind::Other, npcOnly(w, u));
    }
}

void finishDoom(World& w, Entity u) {
    Unconscious* un = w.get<Unconscious>(u);
    if (!un || w.has<Corpse>(u)) return;
    // Dead for good: a corpse, no longer anyone's to command, still there to carry and to loot
    un->doom = 100.0f;
    w.add(u, Corpse{});
    w.remove<Commandable>(u);
    w.remove<Selected>(u);
    w.remove<Occupying>(u);
    queueLog(langf("MISC_DIES", { nameOf(w, u) }), LogKind::Other, npcOnly(w, u));
    // Knocked out by your units, and dead before it woke: more reputation lost with its faction (a mob of a mob
    // faction is game, and costs none)
    const int victim = factionOf(w, u);
    if (un->koFaction == FACTION_PLAYER && victim != FACTION_PLAYER && !isMobFaction(victim)) {
        const int now = changePlayerReputation(victim, -REP_DEATH_PENALTY);
        queueLog(langf("MISC_REPUTATION_DROPS", { factionName(victim), std::to_string(REP_DEATH_PENALTY), std::to_string(now) }),
                 LogKind::Other);
    }
}

bool issueCarryOrder(World& w, Terrain& t, const std::vector<Entity>& units, Entity target) {
    const Transform* tt = w.get<Transform>(target);
    if (!tt || !canBeCarried(w, target)) return false;
    for (Entity u : units) {
        if (u == target || !w.has<Commandable>(u) || !w.get<Transform>(u) || w.has<Carrying>(u)) continue;
        if (cannotTakeOrder(w, u)) continue;
        w.remove<ActiveSquadMember>(u);
        cancelPickup(w, u);
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
        w.remove<ActiveSquadMember>(u);
        cancelPickup(w, u);
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
        queueLog(langf("MISC_PUTS_DOWN", { nameOf(w, carrier), nameOf(w, carried) }), LogKind::Other, npcOnly(w, carrier, carried));
        queueSoundFrom(w, Sfx::PutDown, carrier);
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
            if (const ActiveSquadMember* sm = w.get<ActiveSquadMember>(target)) {
                if (ActiveSquad* sq = w.get<ActiveSquad>(sm->squad); sq && sq->size > 0) --sq->size;
                w.remove<ActiveSquadMember>(target);
            }
            w.remove<SoloMove>(target);
            cancelPickup(w, target);
            w.remove<AttackOrder>(target);
            w.remove<TalkOrder>(target);
            endPropTasks(w, target);
            w.remove<IdleSpot>(target);
            if (Combat* c = w.get<Combat>(target)) c->target = NULL_ENTITY;
            w.add(target, Carried{ u });
            w.add(u, Carrying{ target });
            queueLog(langf("MISC_PICKS_UP_UNIT", { nameOf(w, u), nameOf(w, target) }), LogKind::Other, npcOnly(w, u, target));
            queueSoundFrom(w, Sfx::CarryUp, u);
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
                queueLog(langf("MISC_PLAYS_DEAD", { nameOf(w, c) }), LogKind::Other, npcOnly(w, c));
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
        else if (w.has<Occupying>(u)) putDown(w, u);   // a unit never carries anyone while it occupies a prop
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
        if (w.has<Mob>(u)) continue;   // a mob takes nothing (mobs.h)
        w.remove<ActiveSquadMember>(u);
        cancelPickup(w, u);
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

bool canHoldItem(World& w, Entity unit, int item) {
    const Mob* m = w.get<Mob>(unit);
    return !m || mobCanHold(m->kind, item);
}

bool inventoryLocked(World& w, Entity unit) {
    return w.has<Mob>(unit) && !w.has<Unconscious>(unit);
}

bool lootTake(World& w, Entity looter, Entity target, bool gear, int slot, std::string& why) {
    Inventory* into = w.get<Inventory>(looter);
    if (!into || !w.has<Unconscious>(target)) return false;
    if (w.has<Mob>(looter)) {   // a mob takes nothing but what it was born with
        why = lang("MISC_MOB_CANT_HOLD");
        return false;
    }
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
    // Anything taken out of an Unconscious mob kills it: it goes to 0 health and its Doom completes at once
    if (w.has<Mob>(target) && !w.has<Corpse>(target)) {
        if (Needs* n = w.get<Needs>(target)) {
            n->hp = 0;
            n->changed |= NEEDS_HP_CHANGED;
        }
        queueLog(langf("MISC_MEAT_TAKEN_DIES", { nameOf(w, target) }), LogKind::Other, npcOnly(w, target));
        finishDoom(w, target);
        queueSoundFrom(w, Sfx::Dies, target);
    }
    return true;
}

int lootAll(World& w, Entity looter, Entity target) {
    int moved = 0;
    std::string why;
    for (int g = 0; g < GEAR_SLOTS; ++g) moved += lootTake(w, looter, target, true, g, why) ? 1 : 0;
    for (int i = 0; i < INVENTORY_SLOTS; ++i) moved += lootTake(w, looter, target, false, i, why) ? 1 : 0;
    return moved;
}
