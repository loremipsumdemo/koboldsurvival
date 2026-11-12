// Building plans, claim growth and the NPC builders: a plan reads and writes as it was, turns four ways back to itself, is built at
// once, and a faction with an Altar builds houses from it in its claim, a tier of material at a time. Runs beside data.pak and the
// MAP folder. Prints each failure and returns non-zero if there were any.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include "build.h"
#include "build_plan.h"
#include "building.h"
#include "camera.h"
#include "claims.h"
#include "factions.h"
#include "game_tables.h"
#include "items.h"
#include "language.h"
#include "level.h"
#include "npc_builders.h"
#include "npc_camps.h"
#include "stuck.h"
#include "pak.h"
#include "savegame.h"
#include "simulation.h"
#include "systems_internal.h"
#include "terrain.h"

using namespace systems_detail;

namespace {
int failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

int npcFaction() {
    for (const FactionDef& f : factions()) {
        if (f.id != FACTION_PLAYER && !isMobFaction(f.id) && factionCanClaim(f.id)) return f.id;
    }
    return -1;
}

int otherNpcFaction(int not_) {
    for (const FactionDef& f : factions()) {
        if (f.id != FACTION_PLAYER && f.id != not_ && !isMobFaction(f.id) && factionCanClaim(f.id)) return f.id;
    }
    return -1;
}

ObjectId altarAt(World& w, const Terrain& t, float x, float z, int faction) {
    return placeBuiltProp(w, t, PropKind::Altar, x, z, 0.0f, faction);
}

// A store of the faction's in its claim, holding this many of an item
ObjectId stockAt(World& w, const Terrain& t, PropKind kind, float x, float z, int faction, int item, int count) {
    const ObjectId e = placeBuiltProp(w, t, kind, x, z, 0.0f, faction);
    if (Storage* st = w.get<Storage>(e)) {
        st->capacity = 0;
        st->slots.assign(64, ItemStack{});
        for (ItemStack& s : st->slots) {
            const int put = std::min(count, 99);
            if (put <= 0) break;
            s = ItemStack{ item, put };
            count -= put;
        }
    }
    return e;
}

int completeBuildings(World& w, int faction) {
    int n = 0;
    for (const BuildingCluster& c : buildingClusters(w, buildingMap(w))) n += c.complete && c.owner == faction ? 1 : 0;
    return n;
}

// The planner runs once for each call: ten seconds of real time, and an hour of game time
void lookAround(World& w, Terrain& t, Rng& rng) {
    npcBuilderSystem(w, t, rng, NPC_BUILDERS_LOOK_SECONDS, 3600.0);
}
}

int main(int, char**) {
    const char* base = SDL_GetBasePath();
    if (!pakOpen(std::string(base ? base : "") + "data.pak")) {
        std::printf("FAIL: data.pak not found\n");
        return 1;
    }
    std::string err;
    if (!loadGameTables(err)) {
        std::printf("FAIL: %s\n", err.c_str());
        return 1;
    }
    loadLanguage(LANGUAGE_DEFAULT_ID, err);

    const int npc = npcFaction();
    check(npc >= 0, "there is a faction that holds a claim, other than yours");

    // ---- The first plan, and what a plan is
    BuildPlan house;
    check(readPlanFile(planFilePath("small_house"), house, err), "MAP/BLUEPRINT/small_house.blueprint reads: " + err);
    check(house.w == 3 && house.d == 3 && house.floors == 2, "it is a 3 by 3, two floors");
    check(house.pieces.size() == 9 + 9 + 7 + 1 + 1, "it has 9 foundation tiles, 9 floor tiles, 7 walls, a door and a chest");
    check(planProblem(house).empty(), "it is sound: a foundation and closed edges");
    check(planStageCount(house) == 3, "it is built in three stages: foundation, the ground floor's pieces, the floor above");
    BuildPlan again;
    check(planFromText(planToText(house), again, err) && planToText(again) == planToText(house), "a plan is written and read back the same");
    check(planCost(house).material == 26 && planCost(house).logs == 5, "it costs 26 of its material and the chest's 5 logs");

    BuildPlan open = house;
    open.pieces.erase(std::remove_if(open.pieces.begin(), open.pieces.end(), [](const PlanPiece& p) { return propInfo(p.kind).door; }), open.pieces.end());
    check(planProblem(open).empty(), "a plan with no door is saved all the same");
    BuildPlan gap = house;
    gap.pieces.erase(std::remove_if(gap.pieces.begin(), gap.pieces.end(), [](const PlanPiece& p) { return propInfo(p.kind).wallRule && p.x < 1.0f && p.z < 1.0f; }), gap.pieces.end());
    check(planProblem(gap).empty(), "a plan with a gap in its walls is saved all the same");

    // A plan that is not square turns its box with it, and four turns bring it back
    BuildPlan wide;
    wide.name = "wide";
    wide.w = 5;
    wide.d = 3;
    wide.floors = 1;
    for (int z = 0; z < 3; ++z) {
        for (int x = 0; x < 5; ++x) {
            PlanPiece p;
            p.kind = PropKind::Foundation;
            p.x = (float)x + 0.5f;
            p.z = (float)z + 0.5f;
            wide.pieces.push_back(p);
        }
    }
    PlanPiece chair;
    chair.kind = PropKind::Chair;
    chair.x = 4.2f;
    chair.z = 0.7f;
    chair.yaw = 1.0f;
    wide.pieces.push_back(chair);
    const BuildPlan once = planTurned(wide, 1);
    check(once.w == 3 && once.d == 5, "a quarter turn swaps the box's width and depth");
    bool inside = true;
    for (const PlanPiece& p : once.pieces) inside = inside && p.x >= 0.0f && p.z >= 0.0f && p.x < (float)once.w && p.z < (float)once.d;
    check(inside, "everything stays inside the turned box");
    check(planToText(planTurned(wide, 4)) == planToText(wide), "four quarter turns bring a plan back to itself");
    check(planFoundationRects(wide).size() == 1, "a rectangular foundation is one rectangle");

    // ---- A tower of every floor the game has: built at once, read off the world whole, and the same when it is built again
    {
        BuildPlan tower;
        tower.name = "tower";
        tower.w = tower.d = 3;
        tower.floors = PLAN_FLOORS_MAX;
        auto add = [&](PropKind kind, float x, float z, int level) {
            PlanPiece p;
            p.kind = kind;
            p.x = x + 0.5f;
            p.z = z + 0.5f;
            p.level = level;
            tower.pieces.push_back(p);
        };
        for (int z = 0; z < 3; ++z) {
            for (int x = 0; x < 3; ++x) add(PropKind::Foundation, (float)x, (float)z, 0);
        }
        for (int level = 0; level < PLAN_FLOORS_MAX; ++level) {
            for (int z = 0; z < 3; ++z) {
                for (int x = 0; x < 3; ++x) {
                    if (x == 1 && z == 1) continue;
                    add(level == 0 && x == 1 && z == 2 ? PropKind::WoodDoor : PropKind::WoodWall, (float)x, (float)z, level);
                }
            }
            if (level + 1 < PLAN_FLOORS_MAX) {
                for (int z = 0; z < 3; ++z) {
                    for (int x = 0; x < 3; ++x) add(PropKind::UpperFloor, (float)x, (float)z, level);
                }
            }
        }
        check(planStageCount(tower) == 2 * (PLAN_FLOORS_MAX - 1) + 2, "a tower of 8 floors has a stage for each floor and the floor on it");
        World tw;
        Terrain tt(256);
        altarAt(tw, tt, 10.5f, 10.5f, FACTION_PLAYER);
        placePlanInstant(tw, tt, tower, 20, 20, FACTION_PLAYER);
        const BuildPlan read = capturePlan(tw, "tower", 3, 3, PLAN_FLOORS_MAX, 20, 20);
        {
            int perLevel[PLAN_FLOORS_MAX + 1] = {};
            for (const PlanPiece& p : read.pieces) ++perLevel[std::min(PLAN_FLOORS_MAX, propInfo(p.kind).upperFloor ? p.level + 1 : p.level)];
            for (int i = 0; i < PLAN_FLOORS_MAX; ++i) std::printf("      floor %d: %d pieces\n", i + 1, perLevel[i]);
        }
        check(read.pieces.size() == tower.pieces.size(), "all 8 floors stand, and are read back, with nothing left off the top");
        int topFloor = 0;
        for (const PlanPiece& p : read.pieces) topFloor = std::max(topFloor, propInfo(p.kind).upperFloor ? p.level + 1 : p.level);
        check(topFloor == PLAN_FLOORS_MAX - 1, "the top floor is the game's eighth");
    }

    // ---- Building one at once, and claims that grow
    Terrain terrain(256);
    World world;
    Rng rng;
    rng.gen.seed(7);
    const ObjectId altar = altarAt(world, terrain, 40.5f, 40.5f, npc);
    placePlanInstant(world, terrain, house, 50, 40, npc);
    int walls = 0, doors = 0, floors = 0, chests = 0, foundations = 0, blueprints = 0;
    for (ObjectId e : world.view<Prop>()) {
        const PropInfo& info = propInfo(world.get<Prop>(e)->kind);
        walls += info.wallRule;
        doors += info.door;
        floors += info.upperFloor;
        foundations += info.foundation;
        chests += world.get<Prop>(e)->kind == PropKind::LockedChest;
        blueprints += world.has<Blueprint>(e);
    }
    std::printf("      foundations %d, walls %d, doors %d, floors %d, chests %d\n", foundations, walls, doors, floors, chests);
    check(foundations == 9 && walls == 7 && doors == 1 && floors == 9 && chests == 1, "placed at once: its foundation, walls, door, floor and chest all stand");
    check(blueprints == 0, "and all of it is built");
    for (ObjectId e : world.view<Prop, Transform>()) {
        if (world.get<Prop>(e)->kind != PropKind::LockedChest) continue;
        check(world.get<Transform>(e)->y >= terrain.heightAt(world.get<Transform>(e)->x, world.get<Transform>(e)->z) + DECK_HEIGHT - 0.01f, "a prop placed from a plan stands on the foundation, not sunk into it");
    }
    check(planPiecesOutside(world, 3, 3, 50, 40) == 0 && planPiecesOutside(world, 3, 3, 10, 10) == 18 + 7 + 2, "pieces outside a box are counted, and an Altar is not");
    check(completeBuildings(world, npc) == 1, "it is a complete building");

    const Claim* claim = claimOf(claimsOf(world), altar);
    check(claim && claim->radius == CLAIM_RADIUS, "the claim starts at its usual radius");
    claimGrowthSystem(world, 5.0f);
    claim = claimOf(claimsOf(world), altar);
    check(claim && claim->radius == CLAIM_RADIUS + CLAIM_GROWTH_STEP, "a complete building in the claim makes it 2 bigger");
    claimGrowthSystem(world, 5.0f);
    claim = claimOf(claimsOf(world), altar);
    check(claim && claim->radius == CLAIM_RADIUS + CLAIM_GROWTH_STEP, "and it grows no more without another");

    // Another Altar's claim in the way: no room, until it is gone
    World crowded;
    Terrain crowdedGround(256);
    const ObjectId home = altarAt(crowded, crowdedGround, 40.5f, 40.5f, npc);
    placePlanInstant(crowded, crowdedGround, house, 50, 40, npc);
    const int rival = otherNpcFaction(npc);
    check(rival >= 0, "there is another faction that holds a claim");
    const ObjectId rivalAltar = altarAt(crowded, crowdedGround, 40.5f + 51.0f, 40.5f, rival);
    claimGrowthSystem(crowded, 5.0f);
    check(claimOf(claimsOf(crowded), home)->radius == CLAIM_RADIUS, "with another Altar's claim in reach it does not grow");
    crowded.destroy(rivalAltar);
    claimGrowthSystem(crowded, 5.0f);
    check(claimOf(claimsOf(crowded), home)->radius == CLAIM_RADIUS + CLAIM_GROWTH_STEP, "once it is gone it does");

    // A camp of another faction under the larger claim is destroyed, and one under an Altar's claim never holds land
    World camps;
    Terrain campGround(256);
    const ObjectId altarC = altarAt(camps, campGround, 40.5f, 40.5f, npc);
    placePlanInstant(camps, campGround, house, 50, 40, npc);
    const ObjectId nearCamp = makeProp(camps, PropKind::Campfire, Transform{ 40.5f + 36.0f, 0.0f, 40.5f });
    camps.add(nearCamp, Owner{ rival });
    const ObjectId sheltered = makeProp(camps, PropKind::Campfire, Transform{ 40.5f + 8.0f, 0.0f, 40.5f + 8.0f });
    camps.add(sheltered, Owner{ rival });
    int campClaims = 0;
    for (const Claim& c : claimsOf(camps)) campClaims += c.camp ? 1 : 0;
    check(campClaims == 1, "a camp inside another faction's Altar claim holds no claim of its own");
    claimGrowthSystem(camps, 5.0f);
    check(!camps.has<Prop>(nearCamp), "the claim grows over another faction's camp and destroys it");
    check(camps.has<Prop>(altarC), "the Altar is untouched");

    // ---- Saving what a claim and a building have come to
    {
        ObjectId site = NO_OBJECT;
        for (ObjectId e : world.view<Prop, Foundation>()) {
            site = e;
            break;
        }
        world.add(site, PlanSite{ "small_house", 50, 40, 1, (int)BuildMaterial::Stone, 2, 3.5 });
        GameState gs;
        Terrain keep(256);
        const MapChoice map{ "packed", "test", "test", "" };
        const std::string file = "building_plan_check.sav";
        check(writeSaveFile(file, captureWorld(world, gs, Camera{}, rng, 1000, "test", map)), "a game with a grown claim and a building under way is saved");
        SaveData loaded;
        check(readSaveFile(file, loaded), "and read back");
        std::remove(file.c_str());
        World back;
        Rng after;
        restoreWorld(back, keep, after, loaded);
        int steps = -1, sites = 0;
        for (ObjectId e : back.view<Prop>()) {
            if (const ClaimGrowth* g = back.get<ClaimGrowth>(e)) steps = g->steps;
            if (const PlanSite* ps = back.get<PlanSite>(e)) {
                ++sites;
                check(ps->plan == "small_house" && ps->x == 50 && ps->z == 40 && ps->turns == 1 && ps->material == (int)BuildMaterial::Stone &&
                          ps->stage == 2 && ps->hours == 3.5,
                      "a plan site comes back as it was");
            }
        }
        check(steps == 1 && sites == 1, "a grown claim comes back grown");
    }

    // ---- The NPC builders: a faction with an Altar, wood in its stores and a citizen. They build from the plans in MAP/BLUEPRINT,
    // which holds whatever the player has saved, so these run in a folder of their own with the one plan.
    const std::filesystem::path home_ = std::filesystem::current_path();
    std::filesystem::create_directories("plan_check_tmp/MAP/BLUEPRINT");
    std::filesystem::current_path("plan_check_tmp");
    check(writePlanFile(planFilePath("small_house"), house, err), "the plan is written to a folder of its own: " + err);
    reloadPlanLibrary();
    {
        World town;
        Terrain flat(256);
        Rng trng;
        trng.gen.seed(11);
        const ObjectId homeAltar = altarAt(town, flat, 100.5f, 100.5f, npc);
        stockAt(town, flat, PropKind::WoodStockpile, 103.5f, 100.5f, npc, woodLogItem(), 700);
        const ObjectId citizen = makeEntity(town, Transform{ 100.5f, 0.0f, 102.5f }, npc);
        town.add(citizen, Citizen{});
        lookAround(town, flat, trng);
        bool builder = false;
        for (const BehaviorRule& r : town.get<Behaviors>(citizen)->rules) builder = builder || r.kind == BehaviorKind::AutoConstruct;
        check(builder, "its citizens are given Auto-Construct");

        // Houses are begun one at a time, and a faction with only wood in its stores stops at three
        for (int round = 0; round < 80; ++round) {
            lookAround(town, flat, trng);
            finishAllBlueprints(town, flat);
        }
        auto buildings = [&]() { return (int)buildingClusters(town, buildingMap(town)).size(); };
        check(buildings() == 3 && completeBuildings(town, npc) == 3, "it builds 3 of wood, all complete, and with no stone in its stores no more");
        bool sites = false;
        for (ObjectId e : town.view<PlanSite>()) sites = sites || e != NO_OBJECT;
        check(!sites, "and none is still under way");

        // Each stands apart: a tile at least between any two
        const std::vector<BuildingCluster> all = buildingClusters(town, buildingMap(town));
        bool apart = true;
        for (size_t a = 0; a < all.size(); ++a) {
            for (size_t b = a + 1; b < all.size(); ++b) {
                for (long long ka : all[a].tiles) {
                    for (long long kb : all[b].tiles) {
                        const int dx = std::abs((int)(ka % 1000003LL) - (int)(kb % 1000003LL)), dz = std::abs((int)(ka / 1000003LL) - (int)(kb / 1000003LL));
                        apart = apart && std::max(dx, dz) >= 2;
                    }
                }
            }
        }
        check(apart, "no two buildings touch");

        // Once the wood ones are complete, stone and metal are allowed: 2 and 1
        stockAt(town, flat, PropKind::StoneStockpile, 97.5f, 100.5f, npc, materialItem(BuildMaterial::Stone), 400);
        stockAt(town, flat, PropKind::StoneStockpile, 97.5f, 98.5f, npc, materialItem(BuildMaterial::Iron), 400);
        for (int round = 0; round < 120; ++round) {
            lookAround(town, flat, trng);
            finishAllBlueprints(town, flat);
        }
        int wood = 0, stone = 0, iron = 0;
        for (const BuildingCluster& c : buildingClusters(town, buildingMap(town))) {
            wood += c.material == BuildMaterial::Wood;
            stone += c.material == BuildMaterial::Stone;
            iron += c.material == BuildMaterial::Iron;
        }
        check(wood == 3 && stone == 2 && iron == 1, "it has 3 of wood, 2 of stone and 1 of metal, and builds no more");
        check(completeBuildings(town, npc) == 6, "and all 6 are complete");
        // Six complete buildings in its claim make it bigger, as far as the room goes
        claimGrowthSystem(town, 5.0f);
        const Claim* grown = claimOf(claimsOf(town), homeAltar);
        check(grown && grown->radius >= CLAIM_RADIUS + CLAIM_GROWTH_STEP, "and the claim has grown");
    }

    // ---- A whole plan laid at once: the foundation and ghosts, which become blueprints a stage at a time
    {
        World g;
        Terrain gt(256);
        Rng grng;
        grng.gen.seed(5);
        altarAt(g, gt, 100.5f, 100.5f, FACTION_PLAYER);
        const BuildPlan turned = planTurned(house, 0);
        const ObjectId anchor = placePlanAll(g, gt, turned, planFoundationRects(turned), 110, 100, FACTION_PLAYER, BuildMaterial::Wood);
        check(anchor != NO_OBJECT, "a whole plan is laid in the player's claim");
        g.add(anchor, PlanSite{ "small_house", 110, 100, 0, (int)BuildMaterial::Wood, 0, 0.0 });
        auto count = [&](int& real, int& ghosts, int& notAuto) {
            real = ghosts = notAuto = 0;
            for (ObjectId e : g.view<Blueprint>()) {
                const Blueprint* bp = g.get<Blueprint>(e);
                (bp->ghost ? ghosts : real) += 1;
                notAuto += bp->autoPlaced ? 0 : 1;
            }
        };
        int real = 0, ghosts = 0, notAuto = 0;
        count(real, ghosts, notAuto);
        check(real == 9 && ghosts >= 17 && notAuto == 0, "9 foundation blueprints and the walls, door and floors as ghosts, all Auto Placed");
        lookAround(g, gt, grng);
        count(real, ghosts, notAuto);
        check(real == 9, "ghosts stay ghosts while the foundation is not built");
        for (ObjectId e : g.view<Blueprint, Foundation>()) {
            g.get<Blueprint>(e)->supplied = 99;
            finishBlueprint(g, gt, e);
        }
        lookAround(g, gt, grng);
        count(real, ghosts, notAuto);
        check(real >= 8 && ghosts >= 9, "the foundation built, the walls and door become blueprints and the floor above stays a ghost");
        // Cancelling an Auto Placed piece takes it off what is left to build
        ObjectId wall = NO_OBJECT;
        for (ObjectId e : g.view<Blueprint, Prop>()) {
            if (propInfo(g.get<Prop>(e)->kind).wallRule) wall = e;
        }
        const int before = real + ghosts;
        check(wall != NO_OBJECT && destroyBuilding(g, wall), "a blueprint wall is cancelled");
        count(real, ghosts, notAuto);
        check(real + ghosts == before - 1, "and it no longer counts");
        // Built to the end it needs no more looking after
        for (int round = 0; round < 6; ++round) {
            finishAllBlueprints(g, gt);
            lookAround(g, gt, grng);
        }
        bool site = false;
        for (ObjectId e : g.view<PlanSite>()) site = site || e != NO_OBJECT;
        check(!site && g.count<Blueprint>() == 0, "once everything is built the plan is let go");
    }

    // ---- Ghosts, the idle time and a condemned building are saved
    {
        World r;
        Terrain rt(256);
        altarAt(r, rt, 100.5f, 100.5f, FACTION_PLAYER);
        const BuildPlan turned = planTurned(house, 0);
        const ObjectId anchor = placePlanAll(r, rt, turned, planFoundationRects(turned), 110, 100, FACTION_PLAYER, BuildMaterial::Wood);
        r.add(anchor, PlanSite{ "small_house", 110, 100, 0, (int)BuildMaterial::Wood, 0, 1.5, 33.25, 77, 5.5 });
        r.add(anchor, Condemned{});
        int ghostsBefore = 0;
        for (ObjectId e : r.view<Blueprint>()) ghostsBefore += r.get<Blueprint>(e)->ghost ? 1 : 0;
        GameState gs;
        Terrain keep(256);
        Rng rrng;
        const MapChoice map{ "packed", "test", "test", "" };
        const std::string file = "building_plan_check_ghost.sav";
        check(writeSaveFile(file, captureWorld(r, gs, Camera{}, rrng, 1000, "test", map)), "a game with ghosts is saved");
        SaveData loaded;
        check(readSaveFile(file, loaded), "and read back");
        std::remove(file.c_str());
        World back;
        Rng after;
        restoreWorld(back, keep, after, loaded);
        int ghostsAfter = 0, autoAfter = 0, condemnedAfter = 0;
        for (ObjectId e : back.view<Blueprint>()) {
            ghostsAfter += back.get<Blueprint>(e)->ghost ? 1 : 0;
            autoAfter += back.get<Blueprint>(e)->autoPlaced ? 1 : 0;
        }
        for (ObjectId e : back.view<Condemned>()) condemnedAfter += e != NO_OBJECT;
        check(ghostsAfter == ghostsBefore && ghostsBefore > 0 && autoAfter == (int)back.count<Blueprint>(), "ghosts and Auto Placed pieces come back");
        check(condemnedAfter == 1, "a condemned piece comes back condemned");
        for (ObjectId e : back.view<PlanSite>()) {
            const PlanSite* ps = back.get<PlanSite>(e);
            check(ps->idleHours == 33.25 && ps->mark == 77 && ps->demolishHours == 5.5, "a site's idle time and its coming down come back");
        }
    }

    // ---- An NPC building that makes no progress is condemned, and comes down a day later
    {
        World s;
        Terrain st(256);
        Rng srng;
        srng.gen.seed(21);
        altarAt(s, st, 100.5f, 100.5f, npc);
        bool condemned = false, gone = false;
        for (int round = 0; round < 400 && !gone; ++round) {
            lookAround(s, st, srng);
            int n = 0;
            for (ObjectId e : s.view<Condemned>()) n += e != NO_OBJECT;
            if (n > 0) condemned = true;
            else if (condemned) gone = true;
        }
        check(condemned, "an NPC building with no material to build with is condemned after 10 days");
        check(gone, "and is pulled down, all of it, a day after that");
    }

    // ---- Free blueprints: a prop with nothing to bring finishes itself in an hour, and is let go after a day if held up
    {
        World f;
        Terrain ft(256);
        altarAt(f, ft, 100.5f, 100.5f, FACTION_PLAYER);
        const ObjectId roll = placeCostedProp(f, ft, PropKind::Bedroll, 102.5f, 100.5f, 0.0f, FACTION_PLAYER);
        check(f.has<Blueprint>(roll) && f.has<FreeBuild>(roll), "a Bedroll is placed as a free blueprint");
        check(f.has<Health>(roll), "and has health");
        freeBuildSystem(f, ft, 0.5);
        check(f.has<Blueprint>(roll) && f.get<Blueprint>(roll)->progress > 0.4 && f.get<Blueprint>(roll)->progress < 0.6, "half an hour in it is half done");
        freeBuildSystem(f, ft, 0.6);
        check(!f.has<Blueprint>(roll) && !f.has<FreeBuild>(roll), "an hour in it is built");
        const ObjectId held = placeCostedProp(f, ft, PropKind::Campfire, 104.5f, 100.5f, 0.0f, FACTION_PLAYER);
        makeEntity(f, Transform{ 104.5f, 0.0f, 100.5f }, FACTION_PLAYER);   // stands on it and stays
        for (int hour = 0; hour < 30; ++hour) freeBuildSystem(f, ft, 1.0);
        check(!f.has<Prop>(held), "one that is held up is deleted a day after");
        // Health mends inside an Altar's claim
        f.get<Health>(roll)->value = 5.0;
        claimDecaySystem(f, 3600.0 * 2.0);
        check(f.get<Health>(roll)->value > 5.0, "a prop below its health mends inside an Altar's claim");
        const ObjectId far = placeBuiltProp(f, ft, PropKind::Bedroll, 200.5f, 200.5f, 0.0f, FACTION_PLAYER);
        f.get<Health>(far)->value = 5.0;
        claimDecaySystem(f, 3600.0 * 2.0);
        check(f.get<Health>(far)->value == 5.0, "and does not outside one");
    }

    // ---- A door lies along the walls beside it, a wall pulling more than another door; a building leaves a tile clear all round
    {
        World d;
        Terrain dt(256);
        altarAt(d, dt, 100.5f, 100.5f, FACTION_PLAYER);
        placeFoundation(d, dt, 110, 110, 118, 118, FACTION_PLAYER);
        finishAllBlueprints(d, dt);
        placePiece(d, dt, PropKind::WoodWall, 111, 110, FACTION_PLAYER);
        placePiece(d, dt, PropKind::WoodWall, 113, 110, FACTION_PLAYER);
        const ObjectId along = placePiece(d, dt, PropKind::WoodDoor, 112, 110, FACTION_PLAYER);
        placePiece(d, dt, PropKind::WoodWall, 115, 111, FACTION_PLAYER);
        placePiece(d, dt, PropKind::WoodWall, 115, 113, FACTION_PLAYER);
        const ObjectId across = placePiece(d, dt, PropKind::WoodDoor, 115, 112, FACTION_PLAYER);
        placePiece(d, dt, PropKind::WoodWall, 117, 115, FACTION_PLAYER);
        placePiece(d, dt, PropKind::WoodDoor, 117, 117, FACTION_PLAYER);
        const ObjectId beside = placePiece(d, dt, PropKind::WoodDoor, 117, 116, FACTION_PLAYER);
        check(std::fabs(d.get<Prop>(along)->yaw) < 0.1f, "a door between walls to its east and west lies east-west");
        check(std::fabs(d.get<Prop>(across)->yaw - 1.5707963f) < 0.1f, "one between walls to its north and south lies north-south");
        check(std::fabs(d.get<Prop>(beside)->yaw - 1.5707963f) < 0.1f, "a wall on one side and a door on the other: toward the wall");
        const std::vector<PlanRect> rects = planFoundationRects(planTurned(house, 0));
        check(planSurroundingsClear(d, rects, 140, 140), "open ground is clear");
        const ObjectId chest = placeBuiltProp(d, dt, PropKind::LockedChest, 143.5f, 140.5f, 0.0f, FACTION_PLAYER);
        check(!planSurroundingsClear(d, rects, 140, 140), "a prop in the ring of tiles round the building is not");
        d.destroy(chest);
        placeBuiltProp(d, dt, PropKind::LockedChest, 141.5f, 141.5f, 0.0f, FACTION_PLAYER);
        check(!planSurroundingsClear(d, rects, 140, 140), "nor one on the building");
        check(!planSurroundingsClear(d, rects, 107, 107) && planSurroundingsClear(d, rects, 120, 125), "nor another building's foundation within a tile of it");
    }

    // ---- A claim has a bed for each of its units, and lays no more than that
    {
        World b;
        Terrain bt(256);
        altarAt(b, bt, 100.5f, 100.5f, npc);
        for (int i = 0; i < 3; ++i) makeEntity(b, Transform{ 100.5f + (float)i, 0.0f, 103.5f }, npc);
        auto beds = [&]() {
            int n = 0;
            for (ObjectId e : b.view<Prop>()) n += isBed(b.get<Prop>(e)->kind) ? 1 : 0;
            return n;
        };
        for (int i = 0; i < 6; ++i) claimBedSystem(b, bt);
        check(beds() == 3, "3 units in a claim and no bed: 3 bedroll blueprints, and no more however often it looks");
        bool allBlueprints = true;
        for (ObjectId e : b.view<Prop>()) allBlueprints = allBlueprints && (!isBed(b.get<Prop>(e)->kind) || b.has<FreeBuild>(e));
        check(allBlueprints, "each a free blueprint");
        for (int hour = 0; hour < 3; ++hour) freeBuildSystem(b, bt, 1.0);
        for (int i = 0; i < 3; ++i) claimBedSystem(b, bt);
        check(beds() == 3 && b.count<Blueprint>() == 0, "once built they count, and still no more are laid");
    }

    // ---- A unit shut in breaks its way out, after trying to move about five times; a day of it and it loses consciousness
    {
        World s;
        Terrain st(256);
        Rng srng;
        srng.gen.seed(3);
        altarAt(s, st, 40.5f, 40.5f, FACTION_PLAYER);
        placeFoundation(s, st, 44, 44, 48, 48, FACTION_PLAYER);
        for (int i = 44; i <= 48; ++i) {
            placePiece(s, st, PropKind::WoodWall, i, 44, FACTION_PLAYER);
            placePiece(s, st, PropKind::WoodWall, i, 48, FACTION_PLAYER);
            if (i > 44 && i < 48) {
                placePiece(s, st, PropKind::WoodWall, 44, i, FACTION_PLAYER);
                placePiece(s, st, PropKind::WoodWall, 48, i, FACTION_PLAYER);
            }
        }
        finishAllBlueprints(s, st);
        rebuildBlocking(s, st);
        auto walls = [&]() {
            int n = 0;
            for (ObjectId e : s.view<Prop>()) n += propInfo(s.get<Prop>(e)->kind).wallRule ? 1 : 0;
            return n;
        };
        const int before = walls();
        const ObjectId prisoner = makeEntity(s, Transform{ 46.5f, st.standHeight(46.5f, 46.5f), 46.5f }, FACTION_PLAYER);
        check(before == 16 && !stuckReachable(s, st, Vec2{ 46.5f, 46.5f }, Vec2{ 56.5f, 46.5f }), "a closed room: no way to a spot outside it");
        check(stuckBlocker(s, st, Vec2{ 46.5f, 46.5f }, Vec2{ 56.5f, 46.5f }) != NO_OBJECT, "and a wall is what is in the way");
        walkSoloTo(s, st, prisoner, Vec2{ 56.5f, 46.5f }, 0);
        int rounds = 0, brokeAt = -1;
        for (int step = 0; step < 1200 && brokeAt < 0; ++step) {
            stuckSystem(s, st, srng, 0.5f);
            if (const Stuck* k = s.get<Stuck>(prisoner)) rounds = std::max(rounds, k->rounds);
            // It moves where it is told at once, and has stopped when it gets there
            if (SoloMove* so = s.get<SoloMove>(prisoner); so && !so->waypoints.empty()) {
                Transform* pt = s.get<Transform>(prisoner);
                pt->x = so->waypoints.back().x;
                pt->z = so->waypoints.back().z;
                s.remove<SoloMove>(prisoner);
            }
            if (walls() < before) brokeAt = step;
        }
        check(rounds >= STUCK_ROUNDS_BEFORE_BREAKING && brokeAt >= 0, "it moves about 5 times, and then breaks a wall");
        check(brokeAt > (int)(STUCK_ROUNDS_BEFORE_BREAKING * STUCK_IDLE_SECONDS / 0.5f) - 2, "and not before 5 times its 10 idle seconds");
        // A day of it
        World d;
        Terrain dt2(256);
        const ObjectId lost = makeEntity(d, Transform{ 10.5f, 0.0f, 10.5f }, FACTION_PLAYER);
        d.add(lost, PathGoal{ Vec2{ 30.5f, 10.5f }, 0.0f });
        Stuck forever;
        forever.hours = STUCK_FAINT_HOURS - 0.0001;
        forever.wander = 0.0f;
        d.add(lost, forever);
        d.add(lost, SoloMove{});
        d.get<SoloMove>(lost)->next = 0;
        d.get<SoloMove>(lost)->waypoints.clear();
        Rng drng;
        for (int step = 0; step < 20; ++step) stuckSystem(d, dt2, drng, 0.5f);
        check(d.has<Unconscious>(lost) || d.count<Stuck>() == 0, "shut in for a day, a unit loses consciousness");
    }

    std::filesystem::current_path(home_);
    std::filesystem::remove_all("plan_check_tmp");
    reloadPlanLibrary();

    // ---- Which material comes next
    NpcBuildCount have;
    BuildMaterial next = BuildMaterial::Crystal;
    check(npcNextMaterial(have, next) && next == BuildMaterial::Wood, "none yet: wood");
    have.total = have.wood = 3;
    check(!npcNextMaterial(have, next), "3 of wood begun but not complete: nothing yet");
    have.woodComplete = 3;
    check(npcNextMaterial(have, next) && next == BuildMaterial::Stone, "3 of wood complete: stone");
    have.total = 5;
    have.stone = 2;
    check(npcNextMaterial(have, next) && next == BuildMaterial::Iron, "2 of stone: metal");
    have.total = 6;
    have.metal = 1;
    check(!npcNextMaterial(have, next), "and that is 6");

    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
