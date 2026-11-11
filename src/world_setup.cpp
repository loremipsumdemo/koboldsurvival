#include "game_tables.h"
#include "systems_internal.h"
#include "build.h"
#include "camp_kit.h"
#include "squads.h"

using namespace systems_detail;

namespace {
constexpr int SPAWN_SQUARE = 16;
constexpr int SPAWN_SEARCH_STEP = 32;
constexpr float ENEMY_RANGE = 128.0f;
constexpr float ENEMY_MIN_RANGE = 12.0f;
constexpr float CAMP_DISTANCE = 90.0f;
constexpr float CAMP_SHIFT = 18.0f;
constexpr float CAMP_GAP = 2.0f * CLAIM_RADIUS + 6.0f;
constexpr float CAMP_MOB_RADIUS = 10.0f;
constexpr float CAMP_CAGE_GAP = 1.5f;
constexpr float WILD_MOB_NEAR = 30.0f;
constexpr float WILD_MOB_FAR = 70.0f;
constexpr float WILD_HERD_RADIUS = 8.0f;
constexpr float PLAYER_CAMP_OFFSET = 8.0f;
constexpr int CAMP_EXTRA_MIN = 3;
constexpr int CAMP_EXTRA_MAX = 5;
constexpr float CAMP_EXTRA_NEAR = 5.0f;
constexpr float CAMP_EXTRA_FAR = 12.0f;
constexpr float CAMP_RADIUS_BASE = 4.0f;
constexpr float CAMP_RADIUS_PER_UNIT = 1.6f;

// Open ground all round, CAMP_ROOM tiles out from the camp's middle, for it to grow into
bool roomAround(const Terrain& t, float cx, float cz) {
    const int r = (int)std::ceil(CAMP_ROOM);
    const int x0 = (int)std::floor(cx), z0 = (int)std::floor(cz);
    for (int dz = -r; dz <= r; ++dz) {
        for (int dx = -r; dx <= r; ++dx) {
            if ((float)(dx * dx + dz * dz) > CAMP_ROOM * CAMP_ROOM) continue;
            if (!t.walkable(x0 + dx, z0 + dz)) return false;
        }
    }
    return true;
}

bool walkableSquare(const Terrain& t, int x0, int z0, int n) {
    for (int z = z0; z < z0 + n; ++z) {
        for (int x = x0; x < x0 + n; ++x) {
            if (!t.walkable(x, z)) return false;
        }
    }
    return true;
}
}

Vec2 systems_detail::findSpawn(Terrain& t) {
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
        if (walkableSquare(t, c.x, c.z, SPAWN_SQUARE)) return { c.x + half, c.z + half };
    }
    if (!t.streamed()) {
        for (int z = 0; z < size; ++z) {
            for (int x = 0; x < size; ++x) {
                if (t.walkable(x, z)) return { x + 0.5f, z + 0.5f };
            }
        }
    }
    return { (float)mid, (float)mid };
}

namespace {
struct LevelBuilder {
    World& w;
    Terrain& t;
    Rng& rng;
    const Vec2 spawn;
    const int size;
    std::vector<GridPos> enemyTiles;
    std::vector<Vec2> flagSpots;
    std::vector<std::pair<ObjectId, CampKit>> woodcutters;
    std::vector<CampKit> kits;
    bool planning = false;   // only finding where the camps go: no extras, no units
    std::vector<CampPlan> plan;

    int tileAt(float v) const { return std::clamp((int)std::floor(v), 0, size - 1); }

    bool tileFree(GridPos g) {
        if (!t.walkable(g.x, g.y)) return false;
        const float x = g.x + 0.5f, z = g.y + 0.5f;
        if (distSq(x, z, spawn.x, spawn.z) < ENEMY_MIN_RANGE * ENEMY_MIN_RANGE) return false;
        return std::none_of(enemyTiles.begin(), enemyTiles.end(), [&](GridPos k) { return k == g; });
    }

    ObjectId placeUnit(GridPos g, int faction) {
        enemyTiles.push_back(g);
        const float x = g.x + 0.5f, z = g.y + 0.5f;
        const ObjectId e = makeEntity(w, Transform{ x, t.heightAt(x, z), z }, faction);
        w.add(e, randomNpcLoot(rng));
        w.add(e, defaultDialogue());
        w.add(e, UnitSprite{ rng.range(1, UNIT_SPRITE_COUNT) });
        return e;
    }

    bool clearOfCamps(float x, float z) {
        for (const Vec2& f : flagSpots) {
            if ((f.x - x) * (f.x - x) + (f.z - z) * (f.z - z) < CAMP_GAP * CAMP_GAP) return false;
        }
        return true;
    }

    CampKit placeCamp(int faction, float cx, float cz, bool needRoom = false) {
        for (int ring = 0; ring <= (int)CAMP_SHIFT; ++ring) {
            const int steps = ring == 0 ? 1 : 8 * ring;
            for (int k = 0; k < steps; ++k) {
                const float a = TWO_PI * (float)k / (float)steps;
                const float x = cx + std::cos(a) * (float)ring, z = cz + std::sin(a) * (float)ring;
                if (x < 8.0f || z < 8.0f || x > size - 8.0f || z > size - 8.0f) continue;
                if (!clearOfCamps(x, z) || !campKitFits(w, t, x, z)) continue;
                if (needRoom && !roomAround(t, x, z)) continue;
                CampKit kit = placeCampKit(w, t, faction, x, z, 0.0f);
                if (kit.placed) {
                    flagSpots.push_back(Vec2{ kit.x, kit.z });
                    return kit;
                }
            }
        }
        return CampKit{};
    }

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
            for (ObjectId e : w.view<Prop, Transform>()) {
                const Transform* pt = w.get<Transform>(e);
                if (std::fabs(pt->x - x) < 0.5f && std::fabs(pt->z - z) < 0.5f) { taken = true; break; }
            }
            if (taken) continue;
            const PropKind kind = EXTRA_KINDS[rng.range(0, (int)(sizeof(EXTRA_KINDS) / sizeof(EXTRA_KINDS[0])) - 1)];
            const PropInfo& info = propInfo(kind);
            const ObjectId prop = makeProp(w, kind, Transform{ x, t.heightAt(x, z), z }, rng.unit() * TWO_PI);
            if (info.use != PropUse::Resource) w.add(prop, Owner{ faction });
            if (info.use == PropUse::Storage) w.add(prop, startingStorage(kind, rng));
            if (info.lockLevel > 0) w.add(prop, Lock{ true, info.lockLevel, faction });
            else if (info.use == PropUse::Storage) w.add(prop, Lock{ true, BUILT_LOCK_LEVEL, faction });
            claimOwnership(w, prop);
            ++placed;
        }
    }

    void giveWoodcutting(ObjectId u, const CampKit& kit) {
        if (u == NO_OBJECT || !kit.placed || kit.altar == NO_OBJECT || kit.trees.empty()) return;
        if (!w.has<Commandable>(u)) {
            if (Inventory* inv = w.get<Inventory>(u)) *inv = Inventory{};
        }
        Behaviors* b = w.get<Behaviors>(u);
        if (!b) return;
        // Logs go to the camp's Wood Stockpile
        BehaviorRule harvest;
        harvest.kind = BehaviorKind::AutoHarvest;
        harvest.propKind = idx(PropKind::Tree);
        if (addBehavior(b->rules, harvest)) addHaulForHarvest(w, u, harvest.propKind);
    }

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

    bool clearOfProps(float x, float z) {
        for (ObjectId e : w.view<Prop, Transform>()) {
            const Transform* pt = w.get<Transform>(e);
            if ((pt->x - x) * (pt->x - x) + (pt->z - z) * (pt->z - z) < CAMP_CAGE_GAP * CAMP_CAGE_GAP) return false;
        }
        return true;
    }

    Vec2 placePlayerCamp(bool playerCamp) {
        const CampKit playerKit = playerCamp ? placeCamp(FACTION_PLAYER, spawn.x, spawn.z + PLAYER_CAMP_OFFSET) : CampKit{};
        if (!planning) addCampExtras(FACTION_PLAYER, playerKit);
        if (playerKit.placed) kits.push_back(playerKit);
        if (playerKit.placed) {
            ObjectId first = NO_OBJECT;
            for (ObjectId u : w.view<Entity, Commandable>()) {
                if (first == NO_OBJECT || u < first) first = u;
            }
            woodcutters.push_back({ first, playerKit });
        }
        return playerKit.placed ? Vec2{ playerKit.x, playerKit.z } : spawn;
    }

    void placeFactionCamp(const std::vector<const FactionDef*>& camps, size_t ci, Vec2 middle) {
        const FactionDef& f = *camps[ci];
        static const float DIRS[8][2] = { { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 },
                                          { 0.7071f, -0.7071f }, { 0.7071f, 0.7071f }, { -0.7071f, 0.7071f }, { -0.7071f, -0.7071f } };
        float dirX = DIRS[ci % 8][0], dirZ = DIRS[ci % 8][1];
        if (ci >= 8) {
            const float a = TWO_PI * (float)ci / (float)camps.size();
            dirX = std::cos(a);
            dirZ = std::sin(a);
        }
        const float dist = CAMP_DISTANCE * (ci >= 4 ? 1.4f : 1.0f);
        // First anywhere along the way with room all round it, and only then anywhere the camp itself fits
        CampKit kit;
        bool roomy = false;
        for (const bool needRoom : { true, false }) {
            for (float d = dist; !kit.placed && d >= CAMP_GAP; d -= 10.0f) {
                kit = placeCamp(f.id, middle.x + dirX * d, middle.z + dirZ * d, needRoom);
            }
            if (kit.placed) {
                roomy = needRoom;
                break;
            }
        }
        if (kit.placed) plan.push_back(CampPlan{ f.id, kit.x, kit.z, roomy });
        if (planning) return;
        addCampExtras(f.id, kit);
        const float cx = kit.placed ? kit.x : middle.x + dirX * dist, cz = kit.placed ? kit.z : middle.z + dirZ * dist;
        ObjectId firstUnit = NO_OBJECT;
        const float radius = CAMP_RADIUS_BASE + CAMP_RADIUS_PER_UNIT * std::sqrt((float)f.units);
        int placed = 0;
        for (int attempt = 0; attempt < 4000 && placed < f.units; ++attempt) {
            GridPos g{ rng.range(tileAt(cx - radius), tileAt(cx + radius)), rng.range(tileAt(cz - radius), tileAt(cz + radius)) };
            if (distSq(g.x + 0.5f, g.y + 0.5f, cx, cz) > radius * radius || !tileFree(g)) continue;
            const ObjectId e = placeUnit(g, f.id);
            if (firstUnit == NO_OBJECT) firstUnit = e;
            ++placed;
        }
        for (int attempt = 0; attempt < 20000 && placed < f.units; ++attempt) {
            GridPos g{ rng.range(tileAt(spawn.x - ENEMY_RANGE), tileAt(spawn.x + ENEMY_RANGE)),
                       rng.range(tileAt(spawn.z - ENEMY_RANGE), tileAt(spawn.z + ENEMY_RANGE)) };
            if (distSq(g.x + 0.5f, g.y + 0.5f, spawn.x, spawn.z) > ENEMY_RANGE * ENEMY_RANGE || !tileFree(g)) continue;
            const ObjectId e = placeUnit(g, f.id);
            if (firstUnit == NO_OBJECT) firstUnit = e;
            ++placed;
        }
        kits.push_back(kit);
        woodcutters.push_back({ firstUnit, kit });
    }

    void placeLevelProps() {
        std::vector<GridPos> propTiles;
        // PROP_COPIES of each kind, and one of a rare kind
        std::vector<PropKind> scattered;
        for (PropKind k : scatteredPropKinds()) scattered.insert(scattered.end(), propInfo(k).rare ? 1 : PROP_COPIES, k);
        std::stable_sort(scattered.begin(), scattered.end(), [](PropKind a, PropKind b) { return propInfo(a).rare < propInfo(b).rare; });
        const int propTotal = (int)scattered.size();
        for (int attempt = 0; attempt < 400 * propTotal && (int)propTiles.size() < propTotal; ++attempt) {
            GridPos g{ rng.range(tileAt(spawn.x - PROP_RANGE), tileAt(spawn.x + PROP_RANGE)),
                       rng.range(tileAt(spawn.z - PROP_RANGE), tileAt(spawn.z + PROP_RANGE)) };
            if (!t.walkable(g.x, g.y)) continue;
            if (std::find(propTiles.begin(), propTiles.end(), g) != propTiles.end()) continue;
            // Each camp keeps its room clear
            bool inCamp = false;
            for (const CampPlan& c : plan) {
                inCamp = inCamp || distSq(g.x + 0.5f, g.y + 0.5f, c.x, c.z) <= CAMP_ROOM * CAMP_ROOM;
            }
            if (inCamp) continue;
            bool kitHere = false;
            for (ObjectId e : w.view<Prop, Transform>()) {
                const Transform* kt = w.get<Transform>(e);
                if (std::fabs(kt->x - (g.x + 0.5f)) < 0.5f && std::fabs(kt->z - (g.y + 0.5f)) < 0.5f) { kitHere = true; break; }
            }
            if (kitHere) continue;
            const PropKind kind = scattered[propTiles.size()];
            propTiles.push_back(g);
            const float x = g.x + 0.5f, z = g.y + 0.5f;
            const ObjectId prop = makeProp(w, kind, Transform{ x, t.heightAt(x, z), z });
            if (propInfo(kind).use == PropUse::Storage) w.add(prop, startingStorage(kind, rng));
            claimOwnership(w, prop);
        }
    }

    void settleCamps() {
        settleClaimOwnership(w);
        for (auto& [unit, kit] : woodcutters) giveWoodcutting(unit, kit);
    }

    void finishUnits() {
        std::vector<std::string> takenNames;
        for (ObjectId u : w.view<Entity>()) {
            if (!w.has<Mob>(u)) finishUnit(w, rng, u, takenNames);
        }
    }

    void placeCampMobs() {
        for (const CampKit& kit : kits) {
            if (!kit.placed || kit.faction == FACTION_PLAYER) continue;
            for (int i = 0; i < CAMP_MOBS; ++i) {
                float x = 0.0f, z = 0.0f;
                MobKind kind = (MobKind)rng.range(1, mobKindCount());
                while (!campMobKind(kind)) kind = (MobKind)rng.range(1, mobKindCount());
                bool found = false;
                for (int tries = 0; tries < 20 && !found; ++tries) {
                    if (!freeSpot(kit.x, kit.z, CAMP_MOB_RADIUS, x, z)) break;
                    found = clearOfProps(x, z);
                }
                if (!found) continue;
                const ObjectId mob = spawnMob(w, t, rng, kind, kit.faction, x, z);
                const PropInfo& cageInfo = propInfo(PropKind::PrisonerCage);
                const ObjectId cage = makeProp(w, PropKind::PrisonerCage, Transform{ x, t.heightAt(x, z), z });
                w.add(cage, Owner{ kit.faction });
                w.add(cage, Lock{ true, cageInfo.lockLevel, kit.faction });
                claimOwnership(w, cage);
                w.add(mob, Occupying{ cage });
                if (!isMobFaction(kit.faction)) {
                    if (Name* nm = w.get<Name>(mob)) nm->text += " [!]";
                }
            }
        }
    }

    void placeWildMobs() {
        float farthest = 0.0f;
        for (const CampKit& kit : kits) {
            if (kit.placed) farthest = std::max(farthest, std::hypot(kit.x - spawn.x, kit.z - spawn.z) + CLAIM_RADIUS);
        }
        const float turn = rng.unit() * TWO_PI;
        for (int k = 1; k <= mobKindCount(); ++k) {
            const MobKind kind = (MobKind)k;
            const float a = turn + TWO_PI * (float)(k - 1) / (float)mobKindCount();
            const float d = farthest + WILD_MOB_NEAR + rng.unit() * (WILD_MOB_FAR - WILD_MOB_NEAR);
            const float hx = std::clamp(spawn.x + std::cos(a) * d, 8.0f, size - 8.0f);
            const float hz = std::clamp(spawn.z + std::sin(a) * d, 8.0f, size - 8.0f);
            for (int i = 0; i < mobInfo(kind).wild; ++i) {
                float x = 0.0f, z = 0.0f;
                if (freeSpot(hx, hz, WILD_HERD_RADIUS, x, z) || freeSpot(hx, hz, WILD_HERD_RADIUS * 3.0f, x, z)) {
                    const ObjectId m = spawnMob(w, t, rng, kind, mobFaction(kind), x, z);
                    const MobKind escort = mobInfo(kind).escort;
                    if (escort == MobKind::None) continue;
                    const SpawnHome home = *w.get<SpawnHome>(m);
                    for (int r = 0; r < mobInfo(kind).escortCount; ++r) {
                        float rx = 0.0f, rz = 0.0f;
                        if (!freeSpot(home.x, home.z, 4.0f, rx, rz)) continue;
                        const ObjectId rabbit = spawnMob(w, t, rng, escort, mobFaction(escort), rx, rz);
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
        for (ObjectId u : w.view<Entity, Behaviors, Faction>()) {
            const int f = w.get<Faction>(u)->id;
            if (f == FACTION_PLAYER || isMobFaction(f)) continue;
            BehaviorRule patrol;
            patrol.kind = BehaviorKind::Patrol;
            addBehavior(w.get<Behaviors>(u)->rules, patrol);
        }
    }
};
}

void setupWorld(World& w, Terrain& t, Rng& rng, bool playerCamp) {
    const Vec2 spawn = findSpawn(t);
    LevelBuilder b{ w, t, rng, spawn, t.size() };

    for (int i = 0; i < START_UNIT_COUNT; ++i) spawnPlayerUnit(w, t, rng, spawn, i);

    std::vector<const FactionDef*> camps;
    for (const FactionDef& f : factions()) {
        if (f.id != FACTION_PLAYER && f.units > 0) camps.push_back(&f);
    }

    const Vec2 middle = b.placePlayerCamp(playerCamp);
    for (size_t ci = 0; ci < camps.size(); ++ci) b.placeFactionCamp(camps, ci, middle);
    b.placeLevelProps();
    b.settleCamps();
    b.finishUnits();

    b.placeCampMobs();
    b.placeWildMobs();
    b.giveFactionPatrols();
}

LayoutPlan planRandomLayout(Terrain& t, bool playerCamp, const std::atomic<bool>* cancel) {
    LayoutPlan out;
    World w;
    Rng rng;
    const Vec2 spawn = findSpawn(t);
    LevelBuilder b{ w, t, rng, spawn, t.size() };
    b.planning = true;
    out.spawnX = spawn.x;
    out.spawnZ = spawn.z;
    std::vector<const FactionDef*> camps;
    for (const FactionDef& f : factions()) {
        if (f.id != FACTION_PLAYER && f.units > 0) camps.push_back(&f);
    }
    const Vec2 middle = b.placePlayerCamp(playerCamp);
    for (size_t ci = 0; ci < camps.size(); ++ci) {
        if (cancel && cancel->load()) return LayoutPlan{};
        b.placeFactionCamp(camps, ci, middle);
    }
    out.camps = b.plan;
    return out;
}

namespace {
constexpr float FACTION_CITIZEN_SHARE = 0.4f;
constexpr int FACTION_ALTAR_SEARCH = 24;   // how far from its units' middle an Altar is looked for, in tiles

// A spot for a new Altar near (cx, cz): open ground, clear of props, and its claim clear of every other claim
bool altarSpot(World& w, const Terrain& t, float cx, float cz, float& x, float& z) {
    const std::vector<Claim> claims = claimsOf(w);
    const std::vector<ObjectId> props = w.view<Prop, Transform>();
    for (int ring = 0; ring <= FACTION_ALTAR_SEARCH; ++ring) {
        for (int dz = -ring; dz <= ring; ++dz) {
            for (int dx = -ring; dx <= ring; ++dx) {
                if (std::max(std::abs(dx), std::abs(dz)) != ring) continue;
                const int gx = (int)std::floor(cx) + dx, gz = (int)std::floor(cz) + dz;
                if (!t.walkable(gx, gz)) continue;
                const float px = gx + 0.5f, pz = gz + 0.5f;
                bool clear = true;
                for (const Claim& c : claims) clear = clear && !claimsOverlap(px, pz, CLAIM_RADIUS, c.x, c.z, c.radius);
                for (size_t i = 0; i < props.size() && clear; ++i) {
                    const Transform* tr = w.get<Transform>(props[i]);
                    clear = std::fabs(tr->x - px) >= 1.0f || std::fabs(tr->z - pz) >= 1.0f;
                }
                if (!clear) continue;
                x = px;
                z = pz;
                return true;
            }
        }
    }
    return false;
}

// A spot near (cx, cz) for a whole camp kit (Altar, campfire, four bedrolls and the rest), its claim clear of every other
// claim: first with room all round it, then anywhere it fits
bool campSpot(World& w, const Terrain& t, float cx, float cz, float& x, float& z) {
    const std::vector<Claim> claims = claimsOf(w);
    for (const bool needRoom : { true, false }) {
        for (int ring = 0; ring <= FACTION_ALTAR_SEARCH; ++ring) {
            for (int dz = -ring; dz <= ring; ++dz) {
                for (int dx = -ring; dx <= ring; ++dx) {
                    if (std::max(std::abs(dx), std::abs(dz)) != ring) continue;
                    const float px = std::floor(cx) + dx + 0.5f, pz = std::floor(cz) + dz + 0.5f;
                    bool clear = campKitFits(w, t, px, pz) && (!needRoom || roomAround(t, px, pz));
                    for (const Claim& c : claims) clear = clear && !claimsOverlap(px, pz, CLAIM_RADIUS, c.x, c.z, c.radius);
                    if (!clear) continue;
                    x = px;
                    z = pz;
                    return true;
                }
            }
        }
    }
    return false;
}
}

void settleFactionHomes(World& w, Terrain& t) {
    for (const FactionDef& f : factions()) {
        if (f.id == FACTION_PLAYER || !factionCanClaim(f.id)) continue;
        std::vector<ObjectId> units;
        float sx = 0.0f, sz = 0.0f;
        for (ObjectId u : w.view<Entity, Faction, Transform>()) {
            if (w.get<Faction>(u)->id != f.id || w.has<Mob>(u) || w.has<Corpse>(u)) continue;
            units.push_back(u);
            sx += w.get<Transform>(u)->x;
            sz += w.get<Transform>(u)->z;
        }
        if (units.empty()) continue;
        std::sort(units.begin(), units.end());

        ObjectId altar = NO_OBJECT;
        for (const Claim& c : claimsOf(w)) {
            if (c.owner == f.id) altar = c.altar;
        }
        if (altar == NO_OBJECT) {
            float x = 0.0f, z = 0.0f;
            const float mx = sx / (float)units.size(), mz = sz / (float)units.size();
            if (campSpot(w, t, mx, mz, x, z)) {
                const CampKit kit = placeCampKit(w, t, f.id, x, z, 0.0f);
                altar = kit.altar;
                settleClaimOwnership(w);
            }
        }
        if (altar == NO_OBJECT) {
            float x = 0.0f, z = 0.0f;
            if (altarSpot(w, t, sx / (float)units.size(), sz / (float)units.size(), x, z)) {
                altar = placeBuiltProp(w, t, PropKind::Altar, x, z, 0.0f, f.id);
                const PropInfo& info = propInfo(PropKind::Altar);
                if (Storage* st = w.get<Storage>(altar)) {
                    st->slots[0] = ItemStack{ itemTagged(ITEM_TAG_COMMON_BONES), (int)std::ceil(CAMP_UPKEEP_DAYS * 24.0 / info.fuelHours) };
                }
                settleClaimOwnership(w);
            }
        }
        const Transform* home = altar != NO_OBJECT ? w.get<Transform>(altar) : nullptr;

        // The first share are citizens, at home by the Altar; the rest are military, on patrol
        const int citizens = home && units.size() >= 2 ? std::max(1, (int)std::lround(FACTION_CITIZEN_SHARE * (float)units.size())) : 0;
        BehaviorRule patrol, stay;
        patrol.kind = BehaviorKind::Patrol;
        stay.kind = BehaviorKind::Patrol;
        for (size_t i = 0; i < units.size(); ++i) {
            const ObjectId u = units[i];
            Behaviors* b = w.get<Behaviors>(u);
            if (!b) continue;
            if ((int)i < citizens) {
                w.add(u, Citizen{});
                w.remove<SquadMember>(u);
                w.add(u, SpawnHome{ home->x, home->z, false });
                b->rules.erase(std::remove_if(b->rules.begin(), b->rules.end(), [&](const BehaviorRule& r) { return sameBehavior(r, patrol); }),
                               b->rules.end());
                addBehavior(b->rules, stay);
            } else {
                w.remove<Citizen>(u);
                w.add(u, SquadMember{ 0 });
                addBehavior(b->rules, patrol);
            }
        }
    }
}

void spawnGroundItems(World& w, Terrain& t, Rng& rng, int perItem) {
    const int size = t.size();
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
