#include "systems_internal.h"
#include "build.h"
#include "camp_kit.h"

using namespace systems_detail;

namespace {
constexpr int NPC_AMMO_MIN = 10;
constexpr int NPC_AMMO_MAX = 30;
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
        const Entity e = makeUnit(w, Transform{ x, t.heightAt(x, z), z }, faction);
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

    CampKit placeCamp(int faction, float cx, float cz) {
        for (int ring = 0; ring <= (int)CAMP_SHIFT; ++ring) {
            const int steps = ring == 0 ? 1 : 8 * ring;
            for (int k = 0; k < steps; ++k) {
                const float a = TWO_PI * (float)k / (float)steps;
                const float x = cx + std::cos(a) * (float)ring, z = cz + std::sin(a) * (float)ring;
                if (x < 8.0f || z < 8.0f || x > size - 8.0f || z > size - 8.0f) continue;
                if (!clearOfCamps(x, z) || !campKitFits(w, t, x, z)) continue;
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
            for (Entity e : w.view<Prop, Transform>()) {
                const Transform* pt = w.get<Transform>(e);
                if (std::fabs(pt->x - x) < 0.5f && std::fabs(pt->z - z) < 0.5f) { taken = true; break; }
            }
            if (taken) continue;
            const PropKind kind = EXTRA_KINDS[rng.range(0, (int)(sizeof(EXTRA_KINDS) / sizeof(EXTRA_KINDS[0])) - 1)];
            const PropInfo& info = propInfo(kind);
            const Entity prop = makeProp(w, kind, Transform{ x, t.heightAt(x, z), z }, rng.unit() * TWO_PI);
            if (info.use != PropUse::Resource) w.add(prop, Owner{ faction });
            if (info.use == PropUse::Storage) w.add(prop, startingStorage(kind, rng));
            if (info.lockLevel > 0) w.add(prop, Lock{ true, info.lockLevel, faction });
            else if (info.use == PropUse::Storage) w.add(prop, Lock{ true, BUILT_LOCK_LEVEL, faction });
            claimOwnership(w, prop);
            ++placed;
        }
    }

    void giveWoodcutting(Entity u, const CampKit& kit) {
        if (u == NULL_ENTITY || !kit.placed || kit.altar == NULL_ENTITY || kit.trees.empty()) return;
        if (!w.has<Commandable>(u)) {
            if (Inventory* inv = w.get<Inventory>(u)) *inv = Inventory{};
        }
        Behaviors* b = w.get<Behaviors>(u);
        if (!b) return;
        BehaviorRule haul;
        haul.kind = BehaviorKind::AutoHaul;
        haul.prop = kit.altar;
        haul.propKind = idx(PropKind::Altar);
        BehaviorRule harvest;
        harvest.kind = BehaviorKind::AutoHarvest;
        harvest.propKind = idx(PropKind::Tree);
        addBehavior(b->rules, haul);
        addBehavior(b->rules, harvest);
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
        for (Entity e : w.view<Prop, Transform>()) {
            const Transform* pt = w.get<Transform>(e);
            if ((pt->x - x) * (pt->x - x) + (pt->z - z) * (pt->z - z) < CAMP_CAGE_GAP * CAMP_CAGE_GAP) return false;
        }
        return true;
    }

    Vec2 placePlayerCamp(bool playerCamp) {
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
        static const float DIRS[8][2] = { { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 },
                                          { 0.7071f, -0.7071f }, { 0.7071f, 0.7071f }, { -0.7071f, 0.7071f }, { -0.7071f, -0.7071f } };
        float dirX = DIRS[ci % 8][0], dirZ = DIRS[ci % 8][1];
        if (ci >= 8) {
            const float a = TWO_PI * (float)ci / (float)camps.size();
            dirX = std::cos(a);
            dirZ = std::sin(a);
        }
        const float dist = CAMP_DISTANCE * (ci >= 4 ? 1.4f : 1.0f);
        CampKit kit = placeCamp(f.id, middle.x + dirX * dist, middle.z + dirZ * dist);
        for (float d = dist - 10.0f; !kit.placed && d >= CAMP_GAP; d -= 10.0f) {
            kit = placeCamp(f.id, middle.x + dirX * d, middle.z + dirZ * d);
        }
        addCampExtras(f.id, kit);
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
        std::vector<GridPos> propTiles;
        const int propTotal = PROP_PLACED_KIND_COUNT * PROP_COPIES;
        for (int attempt = 0; attempt < 400 * propTotal && (int)propTiles.size() < propTotal; ++attempt) {
            GridPos g{ rng.range(tileAt(spawn.x - PROP_RANGE), tileAt(spawn.x + PROP_RANGE)),
                       rng.range(tileAt(spawn.z - PROP_RANGE), tileAt(spawn.z + PROP_RANGE)) };
            if (!t.walkable(g.x, g.y)) continue;
            if (std::find(propTiles.begin(), propTiles.end(), g) != propTiles.end()) continue;
            bool kitHere = false;
            for (Entity e : w.view<Prop, Transform>()) {
                const Transform* kt = w.get<Transform>(e);
                if (std::fabs(kt->x - (g.x + 0.5f)) < 0.5f && std::fabs(kt->z - (g.y + 0.5f)) < 0.5f) { kitHere = true; break; }
            }
            if (kitHere) continue;
            const PropKind kind = (PropKind)(propTiles.size() % PROP_PLACED_KIND_COUNT);
            propTiles.push_back(g);
            const float x = g.x + 0.5f, z = g.y + 0.5f;
            const Entity prop = makeProp(w, kind, Transform{ x, t.heightAt(x, z), z });
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
        for (Entity u : w.view<Unit>()) {
            if (w.has<Mob>(u)) continue;
            if (!w.has<Stats>(u)) w.add(u, randomStartingStats(rng));
            if (Needs* nd = w.get<Needs>(u)) {
                nd->maxHp = maxHealthFor(*w.get<Stats>(u));
                nd->hp = nd->maxHp;
            }
            if (w.has<Hostile>(u) || w.has<Neutral>(u)) {
                w.add(u, randomHostileGear(rng));
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
            if (Inventory* inv = w.get<Inventory>(u)) {
                Equipment* worn = w.get<Equipment>(u);
                GearItems gear = worn ? worn->items : GearItems{};
                keepUnderEncumbrance(inv->slots, gear, *w.get<Stats>(u));
                if (worn) worn->items = gear;
            }
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
            if (!kit.placed || kit.faction == FACTION_PLAYER) continue;
            for (int i = 0; i < CAMP_MOBS; ++i) {
                float x = 0.0f, z = 0.0f;
                MobKind kind = (MobKind)rng.range(1, MOB_KIND_COUNT);
                while (!campMobKind(kind)) kind = (MobKind)rng.range(1, MOB_KIND_COUNT);
                bool found = false;
                for (int tries = 0; tries < 20 && !found; ++tries) {
                    if (!freeSpot(kit.x, kit.z, CAMP_MOB_RADIUS, x, z)) break;
                    found = clearOfProps(x, z);
                }
                if (!found) continue;
                const Entity mob = spawnMob(w, t, rng, kind, kit.faction, x, z);
                const PropInfo& cageInfo = propInfo(PropKind::PrisonerCage);
                const Entity cage = makeProp(w, PropKind::PrisonerCage, Transform{ x, t.heightAt(x, z), z });
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
        for (int k = 1; k <= MOB_KIND_COUNT; ++k) {
            const MobKind kind = (MobKind)k;
            const float a = turn + TWO_PI * (float)(k - 1) / (float)MOB_KIND_COUNT;
            const float d = farthest + WILD_MOB_NEAR + rng.unit() * (WILD_MOB_FAR - WILD_MOB_NEAR);
            const float hx = std::clamp(spawn.x + std::cos(a) * d, 8.0f, size - 8.0f);
            const float hz = std::clamp(spawn.z + std::sin(a) * d, 8.0f, size - 8.0f);
            for (int i = 0; i < mobInfo(kind).wild; ++i) {
                float x = 0.0f, z = 0.0f;
                if (freeSpot(hx, hz, WILD_HERD_RADIUS, x, z) || freeSpot(hx, hz, WILD_HERD_RADIUS * 3.0f, x, z)) {
                    const Entity m = spawnMob(w, t, rng, kind, mobFaction(kind), x, z);
                    if (kind != MobKind::Gorlicorn) continue;
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
        for (Entity u : w.view<Unit, Behaviors, Faction>()) {
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
