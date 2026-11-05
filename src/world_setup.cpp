// The start of a level: finding the spawn, the starting units and their stats, the factions' camps, mobs,
// starting loot and storage, and the items scattered on the ground.
#include "systems_internal.h"
#include "camp_kit.h"

using namespace systems_detail;

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

namespace {

// What the stages of setting up a level share. The stages run in a fixed order and draw from one Rng, and a level is
// rebuilt from its seed when a save loads, so the order of every draw must stay as it is.
struct LevelBuilder {
    World& w;
    Terrain& t;
    Rng& rng;
    const Vec2 spawn;
    const int size;
    std::vector<GridPos> enemyTiles;
    int campSerial = CAMP_SERIAL_BASE;
    std::vector<Vec2> flagSpots;
    std::vector<std::pair<Entity, CampKit>> woodcutters;
    std::vector<CampKit> kits;

    int tileAt(float v) const { return std::clamp((int)std::floor(v), 0, size - 1); }

    bool tileFree(GridPos g) {
        if (!t.walkable(g.x, g.y)) return false;
        const float x = g.x + 0.5f, z = g.y + 0.5f;
        const float dx = x - spawn.x, dz = z - spawn.z;
        if (dx * dx + dz * dz < ENEMY_MIN_RANGE * ENEMY_MIN_RANGE) return false;
        return std::none_of(enemyTiles.begin(), enemyTiles.end(), [&](GridPos k) { return k == g; });
    }

    Entity placeUnit(GridPos g, int faction) {
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
    }

    bool clearOfCamps(float x, float z) {
        for (const Vec2& f : flagSpots) {
            if ((f.x - x) * (f.x - x) + (f.z - z) * (f.z - z) < CAMP_GAP * CAMP_GAP) return false;
        }
        return true;
    }

    // The kit at (cx, cz), or the nearest spot within CAMP_SHIFT it fits on, ring by ring outward
    CampKit placeCamp(int faction, float cx, float cz) {
        for (int ring = 0; ring <= (int)CAMP_SHIFT; ++ring) {
            const int steps = ring == 0 ? 1 : 8 * ring;
            for (int k = 0; k < steps; ++k) {
                const float a = TWO_PI * (float)k / (float)steps;
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
    }

    // A few random props in the claim, owned by its faction: storage holds what belongs in it and is locked to the
    // faction (as anything with contents in a claim is), a forge starts cold
    void addCampExtras(int faction, const CampKit& kit) {
        static const PropKind EXTRA_KINDS[] = {
            PropKind::Wagon, PropKind::Barrel, PropKind::Ore, PropKind::Boulder, PropKind::Mushroom, PropKind::Bones, PropKind::Stall, PropKind::LockedChest,
            PropKind::Chair, PropKind::Bench, PropKind::Stool, PropKind::Table, PropKind::EndTable, PropKind::RoundTable, PropKind::StandingTorch,
            PropKind::TrainingDummy, PropKind::RawFoodStorage, PropKind::FoodStorage, PropKind::WeaponRack, PropKind::ArmorStand,
            PropKind::WaterTank, PropKind::WaterPump, PropKind::Forge,
        };
        if (!kit.placed) return;
        const int want = rng.range(CAMP_EXTRA_MIN, CAMP_EXTRA_MAX);
        int placed = 0;
        for (int attempt = 0; attempt < 400 && placed < want; ++attempt) {
            const float a = rng.unit() * TWO_PI;
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
            w.add(prop, Prop{ kind, campSerial++, rng.unit() * TWO_PI });
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
    }

    // One unit of each camp keeps its vault fed: Auto-Haul to the vault above Auto-Harvest of Trees, so it cuts wood at
    // the camp's trees and stores each load in the vault. It is given once the vaults are down, below.
    void giveWoodcutting(Entity u, const CampKit& kit) {
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
    }

    // Mobs (mobs.h). Each camp's faction has CAMP_MOBS of random kinds standing about its flag, the player's included.
    // Out past every camp, WILD_MOBS of each kind roam in a herd, in their own faction, each herd at its own bearing.
    bool freeSpot(float cx, float cz, float radius, float& x, float& z) {
        for (int attempt = 0; attempt < 200; ++attempt) {
            const float a = rng.unit() * TWO_PI, d = std::sqrt(rng.unit()) * radius;
            const GridPos g{ tileAt(cx + std::cos(a) * d), tileAt(cz + std::sin(a) * d) };
            if (!t.walkable(g.x, g.y) || std::find(enemyTiles.begin(), enemyTiles.end(), g) != enemyTiles.end()) continue;
            enemyTiles.push_back(g);
            x = g.x + 0.5f;
            z = g.y + 0.5f;
            return true;
        }
        return false;
    }

    // Whether no prop stands within CAMP_CAGE_GAP of (x, z), so a cage put there sits clear of the others
    bool clearOfProps(float x, float z) {
        for (Entity e : w.view<Prop, Transform>()) {
            const Transform* pt = w.get<Transform>(e);
            if ((pt->x - x) * (pt->x - x) + (pt->z - z) * (pt->z - z) < CAMP_CAGE_GAP * CAMP_CAGE_GAP) return false;
        }
        return true;
    }

    // Where the other camps go around: the player's camp if it has one, else the spawn
    Vec2 placePlayerCamp(bool playerCamp) {
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
        return playerKit.placed ? Vec2{ playerKit.x, playerKit.z } : spawn;
    }

    void placeFactionCamp(const std::vector<const FactionDef*>& camps, size_t ci, Vec2 middle) {
        const FactionDef& f = *camps[ci];
        // North, east, south, west first; any more go between them
        static const float DIRS[8][2] = { { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 },
                                          { 0.7071f, -0.7071f }, { 0.7071f, 0.7071f }, { -0.7071f, 0.7071f }, { -0.7071f, -0.7071f } };
        float dirX = DIRS[ci % 8][0], dirZ = DIRS[ci % 8][1];
        if (ci >= 8) {
            const float a = TWO_PI * (float)ci / (float)camps.size();
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

    void placeLevelProps() {
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
    }

    void placeVaults() {
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
    }

    void finishUnits() {
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
    }

    void placeCampMobs() {
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
    }

    void placeWildMobs() {
        float farthest = 0.0f;   // how far out the camps reach from the spawn
        for (const CampKit& kit : kits) {
            if (kit.placed) farthest = std::max(farthest, std::hypot(kit.x - spawn.x, kit.z - spawn.z) + CLAIM_RADIUS);
        }
        const float turn = rng.unit() * TWO_PI;
        for (int k = 1; k <= MOB_KIND_COUNT; ++k) {
            const MobKind kind = (MobKind)k;
            // Each herd at its own bearing, out between WILD_MOB_NEAR and WILD_MOB_FAR past the camps
            const float a = turn + TWO_PI * (float)(k - 1) / (float)MOB_KIND_COUNT;
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
    }

    void giveFactionPatrols() {
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

};

} // namespace

void setupWorld(World& w, Terrain& t, Rng& rng, bool playerCamp) {
    const Vec2 spawn = findSpawn(t);
    // Read in the ground around the spawn, so the units and red units can stand on it
    if (t.streamed()) t.stream({ spawn }, SPAWN_LOAD_RADIUS, 0);
    LevelBuilder b{ w, t, rng, spawn, t.size() };

    // Units: a block centred on the spawn
    for (int i = 0; i < START_UNIT_COUNT; ++i) spawnPlayerUnit(w, t, rng, spawn, i);

    // The other factions' units (data/factioninfo.txt): the same kind of unit, but they can't be commanded. Each
    // faction's units stand together in a camp of their own, the camps spread around the spawn at even angles and far
    // enough apart that two camps don't see each other at the start. A camp's units are on walkable tiles, one each;
    // a camp that can't find room for all of them (water, a small map) spills over into the wider area. The attempt
    // limits stop a small map from looping forever.
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
    const Vec2 middle = b.placePlayerCamp(playerCamp);
    for (size_t ci = 0; ci < camps.size(); ++ci) b.placeFactionCamp(camps, ci, middle);
    b.placeLevelProps();
    b.placeVaults();
    b.finishUnits();

    // Mobs (mobs.h). Each camp's faction has CAMP_MOBS of random kinds standing about its flag, the player's included.
    // Out past every camp, WILD_MOBS of each kind roam in a herd, in their own faction, each herd at its own bearing.
    b.placeCampMobs();
    b.placeWildMobs();
    b.giveFactionPatrols();
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

                spawnGroundItem(w, Transform{ x + 0.5f, h, z + 0.5f }, def.id, 1);
                break;
            }
        }
    }
}
