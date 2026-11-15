#include "game_tables.h"
#include "systems_internal.h"
#include "build.h"
#include "camp_kit.h"
#include "squads.h"
#include "npc_roles.h"

using namespace systems_detail;

namespace {
constexpr int SPAWN_SQUARE = 16;
constexpr int SPAWN_SEARCH_STEP = 32;
constexpr float ENEMY_RANGE = 128.0f;
constexpr float ENEMY_MIN_RANGE = 12.0f;
constexpr float CAMP_SHIFT = 18.0f;
constexpr float CAMP_GAP = 2.0f * CLAIM_RADIUS + 6.0f;
constexpr float CAMP_MOB_RADIUS = 10.0f;
constexpr float CAMP_CAGE_GAP = 1.5f;
constexpr float WILD_MOB_NEAR = 30.0f;
constexpr float WILD_MOB_FAR = 70.0f;
constexpr float WILD_HERD_RADIUS = 8.0f;
constexpr float PLAYER_CAMP_OFFSET = 8.0f;
constexpr int RESOURCE_CELL = 16;          // the resource nodes' grid, in tiles
constexpr int RESOURCE_SPAN = 2048;        // the most of a map they are made over (the middle of it, on a bigger one)
constexpr float RESOURCE_HIGH_SHARE = 0.4f;  // the share of the land that counts as the higher ground
constexpr float RESOURCE_MIN_HEIGHT = 0.3f;   // the least height above sea level a node stands at, in tiles
constexpr int RESOURCE_SHORE_GAP = 2;      // trees keep this far from water
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
            PropKind::Wagon, PropKind::Barrel, PropKind::Ore, PropKind::Boulder, PropKind::Mushroom, PropKind::Boneheap, PropKind::Stall, PropKind::LockedChest,
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
            if (info.lockLevel > 0) w.add(prop, Lock{ !(info.perimeter && !info.solid), info.lockLevel, faction });
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

    // Your camp at `at`, if there is to be one
    void placePlayerCamp(bool playerCamp, Vec2 at) {
        const CampKit playerKit = playerCamp ? placeCamp(FACTION_PLAYER, at.x, at.z) : CampKit{};
        if (playerKit.placed) kits.push_back(playerKit);
        if (playerKit.placed) {
            ObjectId first = NO_OBJECT;
            for (ObjectId u : w.view<Entity, Commandable>()) {
                if (first == NO_OBJECT || u < first) first = u;
            }
            woodcutters.push_back({ first, playerKit });
        }
    }

    // A camp of this faction where planFactionLand put it: first with room all round it, then anywhere it fits. False if it will not
    // go there after all, and the faction is left to be nomadic.
    bool placeCampFaction(int faction, int units, const FactionLanding& at) {
        CampKit kit;
        bool roomy = false;
        for (const bool needRoom : { true, false }) {
            kit = placeCamp(faction, at.x, at.z, needRoom);
            if (kit.placed) {
                roomy = needRoom;
                break;
            }
        }
        if (!kit.placed) return false;
        plan.push_back(CampPlan{ faction, kit.x, kit.z, roomy });
        if (planning) return true;
        addCampExtras(faction, kit);
        const ObjectId firstUnit = placeFactionUnits(faction, units, kit.x, kit.z, false);
        kits.push_back(kit);
        woodcutters.push_back({ firstUnit, kit });
        return true;
    }

    // A faction with no land of its own: its units together round (x, z), clear of every claim, and no Altar (it keeps camps as it goes)
    void placeNomadFaction(int faction, int units, float x, float z) {
        if (planning) return;
        placeFactionUnits(faction, units, x, z, true);
        w.resource<FactionLandNotes>().nomadic.push_back(faction);
    }

    bool outsideClaims(GridPos g) const {
        const float x = g.x + 0.5f, z = g.y + 0.5f;
        for (const Vec2& f : flagSpots) {
            if ((f.x - x) * (f.x - x) + (f.z - z) * (f.z - z) < CLAIM_RADIUS * CLAIM_RADIUS) return false;
        }
        return true;
    }

    // The units of a faction, together round (cx, cz). Returns the first.
    ObjectId placeFactionUnits(int faction, int units, float cx, float cz, bool keepClearOfClaims) {
        ObjectId firstUnit = NO_OBJECT;
        const float radius = CAMP_RADIUS_BASE + CAMP_RADIUS_PER_UNIT * std::sqrt((float)units);
        int placed = 0;
        for (const float reach : { 1.0f, 3.0f, 8.0f }) {
            const float r = radius * reach;
            for (int attempt = 0; attempt < 4000 && placed < units; ++attempt) {
                GridPos g{ rng.range(tileAt(cx - r), tileAt(cx + r)), rng.range(tileAt(cz - r), tileAt(cz + r)) };
                if (distSq(g.x + 0.5f, g.y + 0.5f, cx, cz) > r * r || !tileFree(g)) continue;
                if (keepClearOfClaims && !outsideClaims(g)) continue;
                const ObjectId e = placeUnit(g, faction);
                if (firstUnit == NO_OBJECT) firstUnit = e;
                ++placed;
            }
            if (placed >= units) break;
        }
        return firstUnit;
    }

    // Resource nodes, worked out from the map's seed alone (the same map always has the same ones; nothing of them is kept in the map file).
    // The map is looked over in cells of RESOURCE_CELL tiles; each kind has its own roll in each cell for whether it has a node there and
    // where in the cell. Each kind has a zone it keeps to: Ore the higher ground, the others the lower, by the heights of the land (above
    // sea level) on this map: the higher ground is the top RESOURCE_HIGH_SHARE of it. Nine in ten of a kind are in its own zone, the rest
    // out of it. Trees keep two tiles from the shore, and every node is on walkable ground and clear of the camps and where you start.
    void placeResourceNodes() {
        if (planning) return;
        struct Resource {
            PropKind kind;
            float density;   // the chance a cell has one, before its zone is looked at
            bool high;       // keeps to the higher ground, else the lower
            bool inland;     // keeps RESOURCE_SHORE_GAP tiles from water
        };
        std::vector<Resource> kinds{ { PropKind::Tree, 0.22f, false, true }, { PropKind::Boulder, 0.09f, false, false },
                                     { PropKind::Ore, 0.06f, true, false }, { PropKind::Mushroom, 0.03f, false, false } };
        PropKind crystal = PropKind::Ore;
        if (propKindNamed("DragonCrystal", crystal)) kinds.push_back({ crystal, 0.008f, true, false });

        const uint64_t seed = (uint64_t)t.surface().seed;
        const int span = std::min(size, RESOURCE_SPAN);   // a map bigger than that is done round the middle
        const int x0 = std::clamp((int)spawn.x - span / 2, 0, size - span), z0 = std::clamp((int)spawn.z - span / 2, 0, size - span);

        // Where the higher ground begins: the heights of the land above sea level, sampled over the map
        std::vector<float> heights;
        for (int z = z0 + 8; z < z0 + span; z += 16) {
            for (int x = x0 + 8; x < x0 + span; x += 16) {
                if (!t.walkable(x, z)) continue;
                const float h = t.heightAt(x + 0.5f, z + 0.5f);
                if (h >= RESOURCE_MIN_HEIGHT) heights.push_back(h);
            }
        }
        if (heights.size() < 2) return;
        const size_t cut = (size_t)((1.0f - RESOURCE_HIGH_SHARE) * (float)heights.size());
        std::nth_element(heights.begin(), heights.begin() + (long)cut, heights.end());
        const float highFrom = heights[cut];
        // The chance a node rolled out of its zone is let stand, so that nine in ten of its kind are in it: with the zone a share f of the
        // land, f / (f + r (1 - f)) = 0.9
        auto strayChance = [](float zoneShare) { return zoneShare / (9.0f * (1.0f - zoneShare)); };
        const float strayHigh = strayChance(RESOURCE_HIGH_SHARE), strayLow = strayChance(1.0f - RESOURCE_HIGH_SHARE);

        auto mix = [](uint64_t z) {
            z += 0x9E3779B97F4A7C15ull;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            return z ^ (z >> 31);
        };
        auto roll = [&](uint64_t h, int salt) { return (float)(mix(h + (uint64_t)salt * 0x632BE59BD9B4E019ull) >> 40) / 16777216.0f; };
        std::vector<GridPos> taken;
        auto isTaken = [&](GridPos g) { return std::find(taken.begin(), taken.end(), g) != taken.end(); };
        const int cells = span / RESOURCE_CELL;
        for (int cz = 0; cz < cells; ++cz) {
            for (int cx = 0; cx < cells; ++cx) {
                for (size_t k = 0; k < kinds.size(); ++k) {
                    const Resource& r = kinds[k];
                    const uint64_t h = mix(mix(seed ^ (uint64_t)(k + 1) * 0xD6E8FEB86659FD93ull) + (uint64_t)cx * 0x9E3779B1ull + (uint64_t)cz * 0x85EBCA6Bull * 4096ull);
                    if (roll(h, 0) >= r.density) continue;
                    const int gx = x0 + cx * RESOURCE_CELL + std::min(RESOURCE_CELL - 1, (int)(roll(h, 1) * RESOURCE_CELL));
                    const int gz = z0 + cz * RESOURCE_CELL + std::min(RESOURCE_CELL - 1, (int)(roll(h, 2) * RESOURCE_CELL));
                    if (!t.walkable(gx, gz)) continue;
                    const float x = gx + 0.5f, z = gz + 0.5f;
                    const float height = t.heightAt(x, z);
                    if (height < RESOURCE_MIN_HEIGHT) continue;   // not at the water's edge, where the water laps over it
                    const bool inZone = r.high ? height >= highFrom : height < highFrom;
                    if (!inZone && roll(h, 3) >= (r.high ? strayHigh : strayLow)) continue;
                    // Every node keeps a tile clear of the water and stays out of it (every tile round it is walkable and above the waterline);
                    // trees keep RESOURCE_SHORE_GAP tiles
                    const int gap = r.inland ? RESOURCE_SHORE_GAP : 1;
                    bool shore = false;
                    for (int dz = -gap; dz <= gap && !shore; ++dz) {
                        for (int dx = -gap; dx <= gap; ++dx) {
                            if (!t.walkable(gx + dx, gz + dz) || t.heightAt(gx + dx + 0.5f, gz + dz + 0.5f) < RESOURCE_MIN_HEIGHT * 0.5f) {
                                shore = true;
                                break;
                            }
                        }
                    }
                    if (shore) continue;
                    if (distSq(x, z, spawn.x, spawn.z) < ENEMY_MIN_RANGE * ENEMY_MIN_RANGE) continue;
                    bool inCamp = false;
                    for (const Vec2& f : flagSpots) inCamp = inCamp || distSq(x, z, f.x, f.z) <= CAMP_ROOM * CAMP_ROOM;
                    if (inCamp) continue;
                    const GridPos g{ gx, gz };
                    if (isTaken(g)) continue;
                    taken.push_back(g);
                    makeProp(w, r.kind, Transform{ x, height, z }, roll(h, 4) * TWO_PI);
                }
            }
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
            if (!kit.placed || kit.faction == FACTION_PLAYER) continue;   // nothing is put in your camp for you
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
                // The animals in your own camp are wild ones, shut in a cage that is yours to tame them in
                const ObjectId mob = spawnMob(w, t, rng, kind, kit.faction == FACTION_PLAYER ? mobFaction(kind) : kit.faction, x, z);
                PropKind cageKind = PropKind::PrisonerCage;   // the Taming Cage, a prop of props.txt only
                if (!propKindNamed("TamingCage", cageKind)) cageKind = PropKind::PrisonerCage;
                const PropInfo& cageInfo = propInfo(cageKind);
                const ObjectId cage = makeProp(w, cageKind, Transform{ x, t.heightAt(x, z), z });
                w.add(cage, Owner{ kit.faction });
                w.add(cage, Lock{ true, cageInfo.lockLevel, kit.faction });
                claimOwnership(w, cage);
                w.add(mob, Occupying{ cage });
                if (!isMobFaction(kit.faction) && kit.faction != FACTION_PLAYER) {
                    if (Name* nm = w.get<Name>(mob)) nm->text += " [!]";
                }
            }
        }
    }

    void placeWildMobs(const std::vector<Vec2>& open) {
        float farthest = 0.0f;
        for (const CampKit& kit : kits) {
            if (kit.placed) farthest = std::max(farthest, std::hypot(kit.x - spawn.x, kit.z - spawn.z) + CLAIM_RADIUS);
        }
        auto clearOfKits = [&](float x, float z, float gap) {
            for (const CampKit& kit : kits) {
                if (kit.placed && distSq(kit.x, kit.z, x, z) < gap * gap) return false;
            }
            return true;
        };
        const float turn = rng.unit() * TWO_PI;
        for (int k = 1; k <= mobKindCount(); ++k) {
            const MobKind kind = (MobKind)k;
            // A herd's home: open ground somewhere over the land, away from the camps. Failing that, a ring out past the farthest of them.
            float hx = 0.0f, hz = 0.0f;
            bool homed = false;
            for (const float gap : { CLAIM_RADIUS + WILD_MOB_NEAR, CLAIM_RADIUS + 10.0f }) {
                for (int attempt = 0; attempt < 120 && !homed && !open.empty(); ++attempt) {
                    const Vec2 at = open[(size_t)rng.range(0, (int)open.size() - 1)];
                    if (!clearOfKits(at.x, at.z, gap)) continue;
                    hx = at.x;
                    hz = at.z;
                    homed = true;
                }
            }
            if (!homed) {
                const float a = turn + TWO_PI * (float)(k - 1) / (float)mobKindCount();
                const float d = farthest + WILD_MOB_NEAR + rng.unit() * (WILD_MOB_FAR - WILD_MOB_NEAR);
                hx = std::clamp(spawn.x + std::cos(a) * d, 8.0f, size - 8.0f);
                hz = std::clamp(spawn.z + std::sin(a) * d, 8.0f, size - 8.0f);
            }
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
    const FactionLandPlan land = planFactionLand(t);
    const FactionLanding mine = land.slots.empty() ? FactionLanding{} : land.slots[(size_t)playerSlot()];
    const bool campForYou = mine.tier == LandTier::Spread || mine.tier == LandTier::Scattered;

    // You start where your faction's place on the loop is: just north of your camp, if you have one. Never without units, whatever the land.
    Vec2 spawn = findSpawn(t);
    if (mine.tier != LandTier::Eliminated) {
        spawn = Vec2{ mine.x, mine.z };
        const Vec2 north{ mine.x, mine.z - PLAYER_CAMP_OFFSET };
        if (campForYou && playerCamp && t.walkable((int)std::floor(north.x), (int)std::floor(north.z))) spawn = north;
    }
    LevelBuilder b{ w, t, rng, spawn, t.size() };
    FactionLandNotes& notes = w.resource<FactionLandNotes>();
    notes = FactionLandNotes{};

    for (int i = 0; i < START_UNIT_COUNT; ++i) spawnPlayerUnit(w, t, rng, spawn, i);
    b.placePlayerCamp(campForYou && playerCamp, Vec2{ mine.x, mine.z });
    if (!campForYou) notes.nomadic.push_back(FACTION_PLAYER);

    // The camps first, then the factions with no land (they keep clear of every camp)
    std::vector<int> landless;
    for (int slot = 0; slot < PALETTE_COUNT && !land.slots.empty(); ++slot) {
        if (slot == playerSlot()) continue;
        const FactionDef* f = paletteFaction(slot);
        if (!f) continue;
        const FactionLanding& at = land.slots[(size_t)slot];
        const bool camped = (at.tier == LandTier::Spread || at.tier == LandTier::Scattered) && b.placeCampFaction(f->id, f->units, at);
        if (!camped) landless.push_back(slot);
    }
    for (int slot : landless) {
        const FactionDef* f = paletteFaction(slot);
        const FactionLanding& at = land.slots[(size_t)slot];
        if (at.tier == LandTier::Eliminated) {
            notes.eliminated.push_back(f->id);
            continue;
        }
        b.placeNomadFaction(f->id, f->units, at.x, at.z);
    }
    b.placeResourceNodes();
    b.settleCamps();
    b.finishUnits();

    b.placeCampMobs();
    b.placeWildMobs(land.open);
    b.giveFactionPatrols();
}

LayoutPlan planRandomLayout(Terrain& t, bool, const std::atomic<bool>* cancel) {
    LayoutPlan out;
    const FactionLandPlan land = planFactionLand(t, cancel);
    if (land.slots.empty()) return LayoutPlan{};
    World w;
    Rng rng;
    const Vec2 spawn = findSpawn(t);
    LevelBuilder b{ w, t, rng, spawn, t.size() };
    b.planning = true;
    // All 16 are shown as their own colors, since which of them is you is picked after the map
    for (int slot = 0; slot < PALETTE_COUNT; ++slot) {
        if (cancel && cancel->load()) return LayoutPlan{};
        const FactionLanding& at = land.slots[(size_t)slot];
        const FactionDef* f = paletteFaction(slot);
        if (f && (at.tier == LandTier::Spread || at.tier == LandTier::Scattered)) b.placeCampFaction(f->id, f->units, at);
    }
    out.spawnX = land.slots[(size_t)playerSlot()].x;
    out.spawnZ = land.slots[(size_t)playerSlot()].z;
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
        // A faction that found no land has no Altar: it keeps its camps as it goes
        const std::vector<int>& nomads = w.resource<FactionLandNotes>().nomadic;
        const bool nomadic = std::find(nomads.begin(), nomads.end(), f.id) != nomads.end();
        if (altar == NO_OBJECT && !nomadic) {
            float x = 0.0f, z = 0.0f;
            const float mx = sx / (float)units.size(), mz = sz / (float)units.size();
            if (campSpot(w, t, mx, mz, x, z)) {
                const CampKit kit = placeCampKit(w, t, f.id, x, z, 0.0f);
                altar = kit.altar;
                settleClaimOwnership(w);
            }
        }
        if (altar == NO_OBJECT && !nomadic) {
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

        // The faction's own shares (its Faction Rules): the first are Fortified, guarding its Perimeter, then citizens at home by the Altar,
        // and the rest military, roaming. (Looked at once an hour from then on, npc_roles.h.)
        const int total = (int)units.size();
        const FactionRules& shares = factionRules(w, f.id);
        const bool housed = home && total >= 2;
        const int fortified = housed ? (int)std::lround((float)shares.fortified * 0.01f * (float)total) : 0;
        const int military = housed ? std::min((int)std::lround((float)shares.military * 0.01f * (float)total), std::max(0, total - fortified - 1)) : total;
        const Vec2 homeAt = home ? Vec2{ home->x, home->z } : Vec2{ 0.0f, 0.0f };
        for (int i = 0; i < total; ++i) {
            const ObjectId u = units[(size_t)i];
            if (!w.get<Behaviors>(u)) continue;
            if (i < fortified) makeNpcFortified(w, u, homeAt);
            else if (i < total - military) makeNpcCitizen(w, u, homeAt);
            else makeNpcMilitary(w, u);
        }
    }
}

void spawnStartItems(World& w, Terrain& t, Rng& rng, int count, float range) {
    const Vec2 spawn = systems_detail::findSpawn(t);
    const std::vector<ItemDef>& items = itemDatabase();
    constexpr int TRIES_PER_ITEM = 500;
    if (items.empty()) return;
    for (int n = 0; n < count; ++n) {
        for (int attempt = 0; attempt < TRIES_PER_ITEM; ++attempt) {
            const int x = rng.range((int)std::floor(spawn.x - range), (int)std::floor(spawn.x + range));
            const int z = rng.range((int)std::floor(spawn.z - range), (int)std::floor(spawn.z + range));
            if (distSq(x + 0.5f, z + 0.5f, spawn.x, spawn.z) > range * range) continue;
            bool walk = false;
            float h = 0.0f;
            if (!t.tileGround(x, z, walk, h) || !walk) continue;
            spawnGroundItem(w, Transform{ x + 0.5f, h, z + 0.5f }, items[(size_t)rng.range(0, (int)items.size() - 1)].id, 1);
            break;
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
