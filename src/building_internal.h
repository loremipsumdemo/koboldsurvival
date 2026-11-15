#pragma once
#include <unordered_map>
#include <vector>
#include "building.h"

// What building.cpp and building_climb.cpp share and nothing else uses: the building map's lookups, the ways between floors, and the route
// planning over them.
namespace building_detail {
int keyX(long long k);
int keyZ(long long k);
const PropInfo* infoOf(World& w, ObjectId e);
ObjectId mapGet(const std::unordered_map<long long, ObjectId>& m, long long key);
bool mapFloorSurface(World& w, const BuildingMap& m, int x, int z, int level, int owner);
bool floorWalkable(World& w, const BuildingMap& m, int x, int z, int level);
bool isStairs(World& w, ObjectId e);
void stairsTiles(World& w, ObjectId e, int xs[4], int zs[4]);
bool stairsFootOpen(World& w, const BuildingMap& m, int x, int z, int level, bool ground = false);
Vec2 nearestLanding(World& w, const BuildingMap& m, ObjectId e, Vec2 near);
bool ladderTop(World& w, const BuildingMap& m, ObjectId ladder, Vec2& out);
std::vector<Vec2> floorPath(World& w, const BuildingMap& m, int level, Vec2 from, Vec2 to);

struct ClimbLink {
    ObjectId ladder = NO_OBJECT;
    int level = 0;          // the floor its foot is on
    Vec2 foot{};            // got onto from here, on that floor
    bool topOpen = false;   // it comes out above: on a floor tile a unit can stand on
    Vec2 top{};             // where, on the floor above
    bool stairs = false;    // stairs or a Slope, walked up; else a ladder, climbed
};
struct RouteLinks {
    const World* world = nullptr;
    BuildingStamp stamp;
    std::uint64_t blueprints = 0;
    bool filled = false;
    std::vector<ClimbLink> links;
    std::unordered_map<int, std::unordered_map<long long, int>> parts;   // by floor
};
RouteLinks& routeLinks(World& w);
bool planRoute(World& w, const Terrain& t, ObjectId u, Vec2 goal, int goalStorey, Route& out);

// Units walking up or down stairs last step, and the stairs: to catch any stopped partway. Not saved.
struct StairWalkers {
    std::vector<std::pair<ObjectId, ObjectId>> units;
};
}
