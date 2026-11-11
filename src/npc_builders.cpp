#include "npc_builders.h"
#include <algorithm>
#include <cmath>
#include <unordered_set>
#include "behavior.h"
#include "build.h"
#include "building.h"
#include "claims.h"
#include "components.h"
#include "factions.h"
#include "items.h"
#include "spatial.h"
#include "systems_internal.h"

using namespace systems_detail;

namespace {
struct BuilderClock {
    float timer = 0.0f;
    double hours = 0.0;
};

bool npcAltarClaim(const Claim& c) {
    return !c.camp && c.owner != FACTION_PLAYER && factionCanClaim(c.owner) && !isMobFaction(c.owner);
}

// How much of an item a faction has in its stores (its own, or in its claim)
int stockOf(World& w, const std::vector<Claim>& claims, int faction, int item) {
    int total = 0;
    for (ObjectId p : w.view<Prop, Storage>()) {
        if (w.has<Blueprint>(p) || !propOfFaction(w, claims, p, faction)) continue;
        for (const ItemStack& s : w.get<Storage>(p)->slots) {
            if (s.item > 0 && baseItemId(s.item) == item) total += s.count;
        }
    }
    return total;
}

// Whether a point is in the turned plan's box, from where it is
bool inBox(const PlanSite& s, const BuildPlan& turned, float x, float z) {
    return x >= (float)s.x && z >= (float)s.z && x < (float)(s.x + turned.w) && z < (float)(s.z + turned.d);
}

const BuildPlan* findPlan(const std::string& name) {
    for (const BuildPlan& p : planLibrary()) {
        if (p.name == name) return &p;
    }
    return nullptr;
}

// Lays the pieces of one stage of the plan for the faction. How many went down.
int layStage(World& w, Terrain& t, const BuildPlan& turned, const PlanSite& site, int stage, int faction) {
    int laid = 0;
    for (const PlanPiece* p : planStagePieces(turned, stage)) {
        if (placePlanPiece(w, t, *p, site.x, site.z, faction, (BuildMaterial)site.material) != NO_OBJECT) ++laid;
    }
    return laid;
}

// Auto-Construct for the faction's citizens, so they build what is laid
void giveBuilders(World& w, int faction) {
    BehaviorRule build;
    build.kind = BehaviorKind::AutoConstruct;
    for (ObjectId u : w.view<Citizen, Behaviors>()) {
        if (factionOf(w, u) != faction) continue;
        Behaviors* b = w.get<Behaviors>(u);
        const bool has = std::any_of(b->rules.begin(), b->rules.end(), [](const BehaviorRule& r) { return r.kind == BehaviorKind::AutoConstruct; });
        if (!has) addBehavior(b->rules, build);
    }
}
}

bool npcNextMaterial(const NpcBuildCount& have, BuildMaterial& out) {
    if (have.total >= NPC_BUILDINGS_MOST) return false;
    if (have.wood < NPC_BUILDINGS_WOOD) {
        out = BuildMaterial::Wood;
        return true;
    }
    if (have.woodComplete < NPC_BUILDINGS_WOOD) return false;
    if (have.stone < NPC_BUILDINGS_STONE) {
        out = BuildMaterial::Stone;
        return true;
    }
    if (have.metal < NPC_BUILDINGS_METAL) {
        out = BuildMaterial::Iron;
        return true;
    }
    return false;
}

void npcBuilderSystem(World& w, Terrain& t, Rng& rng, float realSeconds, double gameSeconds) {
    BuilderClock& clock = w.resource<BuilderClock>();
    clock.timer += realSeconds;
    clock.hours += gameSeconds / 3600.0;
    if (clock.timer < NPC_BUILDERS_LOOK_SECONDS) return;
    const double hours = clock.hours;
    clock.timer = 0.0f;
    clock.hours = 0.0;
    const std::vector<Claim> claims = claimsOf(w);
    bool any = false;
    for (const Claim& c : claims) any = any || npcAltarClaim(c);
    if (!any) return;

    // The buildings under way: lay the next stage of each when the last is built, or give up on it
    std::unordered_set<int> building;
    for (ObjectId e : w.view<PlanSite, Prop>()) {
        const int faction = ownerOf(w, e);
        PlanSite& site = *w.get<PlanSite>(e);
        const BuildPlan* plan = findPlan(site.plan);
        if (!plan || faction < 0) {
            w.remove<PlanSite>(e);
            continue;
        }
        const BuildPlan turned = planTurned(*plan, site.turns);
        site.hours += hours;
        int pending = 0;
        for (ObjectId b : w.view<Blueprint, Prop, Transform>()) {
            const Transform* bt = w.get<Transform>(b);
            if (ownerOf(w, b) == faction && inBox(site, turned, bt->x, bt->z)) ++pending;
        }
        if (pending > 0) {
            if (site.hours >= PLAN_STAGE_GIVE_UP_HOURS) w.remove<PlanSite>(e);
            else building.insert(faction);
            continue;
        }
        // Everything laid is built: the next stage that has anything to lay
        const int stages = planStageCount(turned);
        int laid = 0;
        int stage = site.stage;
        while (laid == 0 && ++stage < stages) laid = layStage(w, t, turned, site, stage, faction);
        if (laid == 0) {
            w.remove<PlanSite>(e);   // the plan is built
            continue;
        }
        site.stage = stage;
        site.hours = 0.0;
        building.insert(faction);
    }

    // A faction that is not building looks for a house it could start
    BuildingMap map;
    bool mapped = false;
    std::vector<BuildingCluster> clusters;
    for (const Claim& c : claims) {
        if (!npcAltarClaim(c)) continue;
        giveBuilders(w, c.owner);
        if (building.count(c.owner)) continue;
        if (!mapped) {
            map = buildingMap(w);
            clusters = buildingClusters(w, map);
            mapped = true;
        }
        NpcBuildCount have;
        for (const BuildingCluster& b : clusters) {
            if (b.owner != c.owner || distSq(b.centerX(), b.centerZ(), c.x, c.z) > c.radius * c.radius) continue;
            ++have.total;
            if (b.material == BuildMaterial::Stone) ++have.stone;
            else if (b.material == BuildMaterial::Iron) ++have.metal;
            else ++have.wood;
            if (b.material == BuildMaterial::Wood && b.complete) ++have.woodComplete;
        }
        BuildMaterial material = BuildMaterial::Wood;
        if (!npcNextMaterial(have, material)) continue;
        const int haveMaterial = stockOf(w, claims, c.owner, materialItem(material));
        const int haveLogs = material == BuildMaterial::Wood ? haveMaterial : stockOf(w, claims, c.owner, woodLogItem());

        // The plans it has the stock for, one of them by chance
        std::vector<const BuildPlan*> plans;
        for (const BuildPlan& plan : planLibrary()) {
            const PlanCost cost = planCost(plan);
            const bool enough = material == BuildMaterial::Wood ? haveMaterial >= cost.material + cost.logs
                                                                : haveMaterial >= cost.material && haveLogs >= cost.logs;
            if (enough) plans.push_back(&plan);
        }
        if (plans.empty()) continue;
        const BuildPlan& plan = *plans[(size_t)rng.range(0, (int)plans.size() - 1)];
        const BuildPlan turnedBy[4] = { planTurned(plan, 0), planTurned(plan, 1), planTurned(plan, 2), planTurned(plan, 3) };
        const std::vector<PlanRect> rectsBy[4] = { planFoundationRects(turnedBy[0]), planFoundationRects(turnedBy[1]),
                                                   planFoundationRects(turnedBy[2]), planFoundationRects(turnedBy[3]) };
        for (int attempt = 0; attempt < NPC_PLACE_TRIES; ++attempt) {
            const int turns = rng.range(0, 3);
            const BuildPlan& turned = turnedBy[turns];
            const float angle = rng.unit() * 6.2831853f, radius = std::sqrt(rng.unit()) * c.radius;
            const int x = (int)std::floor(c.x + std::cos(angle) * radius - (float)turned.w * 0.5f);
            const int z = (int)std::floor(c.z + std::sin(angle) * radius - (float)turned.d * 0.5f);
            // All of its box inside the claim
            const float cx[2] = { (float)x, (float)(x + turned.w) }, cz[2] = { (float)z, (float)(z + turned.d) };
            bool inside = true;
            for (int i = 0; i < 2; ++i) {
                for (int j = 0; j < 2; ++j) inside = inside && distSq(cx[i], cz[j], c.x, c.z) <= (c.radius - 1.0f) * (c.radius - 1.0f);
            }
            if (!inside) continue;
            // A tile clear all round it of any other foundation, so that it is a building of its own
            bool clear = true;
            for (int tz = z - 1; tz <= z + turned.d && clear; ++tz) {
                for (int tx = x - 1; tx <= x + turned.w && clear; ++tx) clear = !map.foundation.count(tileKey(tx, tz));
            }
            if (!clear || !planFoundationFits(w, t, rectsBy[turns], x, z, c.owner)) continue;
            const ObjectId anchor = placePlanFoundation(w, t, rectsBy[turns], x, z, c.owner, material);
            if (anchor == NO_OBJECT) continue;
            PlanSite site;
            site.plan = plan.name;
            site.x = x;
            site.z = z;
            site.turns = turns;
            site.material = (int)material;
            w.add(anchor, site);
            break;
        }
    }
}
