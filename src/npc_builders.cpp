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
#include "npc_camps.h"
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

// Whether a ghost may become a blueprint now: a piece on an upper floor needs that floor built under it
bool ghostReady(World& w, ObjectId g, int faction) {
    const Prop* p = w.get<Prop>(g);
    const Transform* tr = w.get<Transform>(g);
    if (!p || !tr) return false;
    const int level = storeyOf(w, g);
    if (level <= 0 || propInfo(p->kind).upperFloor) return true;
    return floorSurfaceAt(w, (int)std::floor(tr->x), (int)std::floor(tr->z), level, faction);
}

// Where a site's building stands, for the ghosts that no site owns
struct SiteBox {
    int faction, x, z, w, d;
};

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

// Auto-Haul Ground Items and an Auto-Haul to each of the faction's stores in its claim, for its citizens, so what lies on the
// ground in its claim is put away, as it is for yours (behavior_groups.cpp). Stores that are gone leave their lists.
void giveHaulers(World& w, const std::vector<Claim>& claims, int faction) {
    std::vector<BehaviorRule> wanted;
    BehaviorRule ground;
    ground.kind = BehaviorKind::AutoHaulGround;
    wanted.push_back(ground);
    for (ObjectId p : w.view<Prop, Storage, Transform>()) {
        if (w.has<Blueprint>(p)) continue;
        const PropInfo& info = propInfo(w.get<Prop>(p)->kind);
        if (info.use != PropUse::Storage || info.claimsLand || info.stall) continue;
        const Owner* own = w.get<Owner>(p);
        if (own && own->faction != faction) continue;
        const Transform* t = w.get<Transform>(p);
        if (!claimAt(claims, t->x, t->z, faction)) continue;
        BehaviorRule haul;
        haul.kind = BehaviorKind::AutoHaul;
        haul.prop = p;
        haul.propKind = idx(w.get<Prop>(p)->kind);
        wanted.push_back(haul);
    }
    for (ObjectId u : w.view<Citizen, Behaviors>()) {
        if (factionOf(w, u) != faction || w.has<Corpse>(u)) continue;
        Behaviors* b = w.get<Behaviors>(u);
        bool changed = false;
        const size_t before = b->rules.size();
        b->rules.erase(std::remove_if(b->rules.begin(), b->rules.end(),
                                      [&](const BehaviorRule& r) { return r.kind == BehaviorKind::AutoHaul && !w.has<Storage>(r.prop); }),
                       b->rules.end());
        changed = b->rules.size() != before;
        for (const BehaviorRule& r : wanted) {
            if (!holdsBehavior(b->rules, r) && addBehavior(b->rules, r)) changed = true;
        }
        if (changed) b->pending = true;
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

    // The buildings under way, NPC factions' and yours (laid from the Build tab's Building Blueprint): when what is laid of one is
    // built, the ghosts of its next stage become blueprints. An NPC building that makes no progress is condemned, then pulled down.
    std::unordered_set<int> building;
    std::vector<SiteBox> boxes;
    // Auto Placed pieces of the world's blueprints, by faction, read once
    std::vector<ObjectId> autoPlaced;
    for (ObjectId b : w.view<Blueprint, Prop, Transform>()) {
        if (w.get<Blueprint>(b)->autoPlaced) autoPlaced.push_back(b);
    }
    for (ObjectId e : w.view<PlanSite, Prop>()) {
        const int faction = ownerOf(w, e);
        PlanSite* sitePtr = w.get<PlanSite>(e);
        if (!sitePtr) continue;
        PlanSite& site = *sitePtr;
        const BuildPlan* plan = findPlan(site.plan);
        if (!plan || faction < 0) {
            w.remove<PlanSite>(e);
            continue;
        }
        const BuildPlan turned = planTurned(*plan, site.turns);
        site.hours += hours;
        boxes.push_back(SiteBox{ faction, site.x, site.z, turned.w, turned.d });
        building.insert(faction);

        // Condemned: its units are asked to leave, and once the day is up and nobody is on its ground floor it comes down
        if (site.demolishHours >= 0.0) {
            site.demolishHours += hours;
            const Transform* at = w.get<Transform>(e);
            const BuildingMap map = buildingMap(w);
            const std::vector<long long> tiles = buildingTiles(map, (int)std::floor(at->x), (int)std::floor(at->z));
            const bool inside = evacuateBuilding(w, t, tiles);
            if (site.demolishHours >= NPC_DEMOLISH_HOURS && !inside) pullDownBuilding(w, t, tiles);
            continue;
        }

        // What is left of it to build: its Auto Placed pieces in its box, those laid and those still ghosts
        int remaining = 0, supplied = 0, lowestGhost = 1 << 20, ghostCount = 0;
        bool laid = false;
        for (ObjectId b : autoPlaced) {
            if (!w.has<Blueprint>(b)) continue;   // built, or cancelled, since the list was made
            const Transform* bt = w.get<Transform>(b);
            if (ownerOf(w, b) != faction || !inBox(site, turned, bt->x, bt->z)) continue;
            const Blueprint* bp = w.get<Blueprint>(b);
            ++remaining;
            if (bp->ghost) {
                ++ghostCount;
                lowestGhost = std::min(lowestGhost, bp->stage);
            } else {
                laid = true;
                supplied += bp->supplied;
            }
        }
        if (remaining == 0) {
            w.remove<PlanSite>(e);   // built, or what is left of it cancelled
            boxes.pop_back();
            continue;
        }
        // Everything before the next stage is built: its ghosts become blueprints, and what costs nothing is put down
        if (!laid && ghostCount > 0) {
            int made = 0;
            for (ObjectId b : autoPlaced) {
                if (!w.has<Blueprint>(b)) continue;
                Blueprint* bp = w.get<Blueprint>(b);
                const Transform* bt = w.get<Transform>(b);
                if (!bp->ghost || bp->stage != lowestGhost || ownerOf(w, b) != faction || !inBox(site, turned, bt->x, bt->z)) continue;
                if (!ghostReady(w, b, faction)) continue;
                bp->ghost = false;
                ++made;
            }
            if (made > 0 && lowestGhost > site.stage) {
                for (const PlanPiece* p : planStagePieces(turned, lowestGhost)) {
                    if (propInfo(p->kind).materials <= 0.0) placePlanPiece(w, t, *p, site.x, site.z, faction, (BuildMaterial)site.material);
                }
                site.stage = lowestGhost;
            }
            supplied += made * 7919;   // a ghost made a blueprint is progress
        }
        // Progress, or not: an NPC building that stands idle for NPC_STALL_HOURS is condemned. Yours never are.
        const int mark = remaining * 10007 + supplied;
        if (mark != site.mark) {
            site.mark = mark;
            site.idleHours = 0.0;
        } else {
            site.idleHours += hours;
        }
        if (faction != FACTION_PLAYER && site.idleHours >= NPC_STALL_HOURS) {
            const Transform* at = w.get<Transform>(e);
            const BuildingMap map = buildingMap(w);
            const std::vector<long long> tiles = buildingTiles(map, (int)std::floor(at->x), (int)std::floor(at->z));
            const std::unordered_set<long long> in(tiles.begin(), tiles.end());
            for (ObjectId p : w.view<Prop, Transform>()) {
                const Transform* pt = w.get<Transform>(p);
                if (in.count(tileKey((int)std::floor(pt->x), (int)std::floor(pt->z)))) w.add(p, Condemned{});
            }
            site.demolishHours = 0.0;
        }
    }
    claimBedSystem(w, t);
    // Ghosts that no building owns any more (the first tile of its foundation was cancelled) go
    for (ObjectId b : autoPlaced) {
        if (!w.has<Blueprint>(b) || !w.get<Blueprint>(b)->ghost) continue;
        const Transform* bt = w.get<Transform>(b);
        const int faction = ownerOf(w, b);
        const bool owned = std::any_of(boxes.begin(), boxes.end(), [&](const SiteBox& s) {
            return s.faction == faction && bt->x >= (float)s.x && bt->z >= (float)s.z && bt->x < (float)(s.x + s.w) && bt->z < (float)(s.z + s.d);
        });
        if (owned) continue;
        releasePropLinks(w, b);
        w.destroy(b);
    }

    // A faction that is not building looks for a house it could start
    BuildingMap map;
    bool mapped = false;
    std::vector<BuildingCluster> clusters;
    for (const Claim& c : claims) {
        if (!npcAltarClaim(c)) continue;
        giveBuilders(w, c.owner);
        giveHaulers(w, claims, c.owner);
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
        // Wood is what it can always try for. Stone and metal buildings wait until it has some of that in its stores, so that a
        // faction with none does not lay one it will never finish, have it condemned, and lay it again.
        if (material != BuildMaterial::Wood && stockOf(w, claims, c.owner, materialItem(material)) <= 0) continue;

        // Any plan it may build, one of them by chance: a free slot and room are all it needs to start, not the stock
        std::vector<const BuildPlan*> plans;
        for (const BuildPlan& plan : planLibrary()) {
            if (plan.npcAllowed) plans.push_back(&plan);   // the Load list's NPC toggle off: only you place it
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
                for (int j = 0; j < 2; ++j) inside = inside && distSq(cx[i], cz[j], c.x, c.z) <= c.radius * c.radius;
            }
            if (!inside) continue;
            // A tile clear all round it of any other foundation, so that it is a building of its own
            bool clear = true;
            for (int tz = z - 1; tz <= z + turned.d && clear; ++tz) {
                for (int tx = x - 1; tx <= x + turned.w && clear; ++tx) clear = !map.foundation.count(tileKey(tx, tz));
            }
            if (!clear || !planFoundationFits(w, t, rectsBy[turns], x, z, c.owner)) continue;
            const ObjectId anchor = placePlanAll(w, t, turned, rectsBy[turns], x, z, c.owner, material);
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
