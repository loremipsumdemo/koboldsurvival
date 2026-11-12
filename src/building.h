#pragma once
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>
#include "behavior.h"
#include "build.h"
#include "claims.h"
#include "ecs.h"
#include "geometry.h"
#include "terrain.h"

// Buildings. A foundation is laid tile by tile: dragging a rectangle (at least 3 by 3 to start a building) lays one
// foundation tile per tile, and a rectangle of any size touching a foundation of yours extends it. Every piece (the
// foundation tiles, then walls, doors, ladders and upper floors on them) starts as a blueprint that needs its material
// brought to it and then building, both done by Auto-Construct. Walls, doors, ladders, upper floors and other props go
// on any floor: the ground floor (the foundation) or an upper floor, which is level 1, 2 and so on. A ladder on a floor
// climbs to the floor above. A piece isn't finished while a unit stands where it goes or on a tile beside it (so nobody is
// walled into a corner): the unit is asked to move, and the piece is tried again later.
inline constexpr int FOUNDATION_MIN = 3;
inline constexpr int FOUNDATION_MAX = 64;
inline constexpr int STOREY_MAX = 7;                 // the top floor: 8 floors in all, the ground floor 0 to 7
inline constexpr float BLUEPRINT_REACH = 8.0f;       // logs are handed over and building is done from this far, on the builder's floor or one above or below
inline constexpr float BUILD_STAND_OFF = 1.5f;       // a builder walks to this much short of that reach, not up to the piece
inline constexpr float FLOOR_HEIGHT = 2.4f;          // walls' height: the upper floor sits on them
inline constexpr float FLOOR_THICK = 0.14f;          // an upper floor's slab
inline constexpr float FLOOR_TOP = FLOOR_HEIGHT + FLOOR_THICK;   // where a unit on the first upper floor stands
inline constexpr float CLIMB_SECONDS = 1.5f;
inline constexpr double BUILD_SPEED = 4.0;           // every piece builds this many times faster than props.txt's build_seconds
inline constexpr float BLOCKED_RETRY_SECONDS = 4.0f; // a piece with a unit in the way waits this long before it is tried again
inline constexpr float BUILD_GIVE_UP_SECONDS = 12.0f;
inline constexpr double DOOR_HEALTH = 60.0;
inline constexpr double WALL_HEALTH = 2.0 * DOOR_HEALTH;   // a wall takes twice the breaking a door does
inline constexpr float AT_DOOR_FORGET_SECONDS = 0.5f;
// A foundation is laid only on ground it can be levelled from: its corners, the rest of its building's and (once one of
// its tiles is finished) the building's level may differ by at most this much
inline constexpr float FOUNDATION_LEVEL_SPREAD = 1.5f;

// How far above the ground a floor's top is: 0 for the ground floor. Each storey is a wall's height plus the slab on
// top of it, so a wall ends just under the slab above it and never pokes through.
inline float storeyBaseHeight(int level) { return level <= 0 ? 0.0f : level * FLOOR_TOP; }
// The underside of the upper floor at this level: it rests on the tops of the walls below
inline float floorSlabHeight(int level) { return level <= 0 ? 0.0f : storeyBaseHeight(level) - FLOOR_THICK; }
// The floor something this far above the ground stands on
inline int storeyAtHeight(float aboveGround) {
    if (aboveGround < FLOOR_HEIGHT * 0.5f) return 0;
    return std::max(1, (int)std::lround(aboveGround / FLOOR_TOP));
}

// A foundation: the tiles it covers. Each is a single tile now; older saves had whole rectangles, split when opened.
struct Foundation {
    int x0 = 0, z0 = 0, w = 0, h = 0;
    bool contains(int x, int z) const { return x >= x0 && z >= z0 && x < x0 + w && z < z0 + h; }
    bool onEdge(int x, int z) const { return contains(x, z) && (x == x0 || z == z0 || x == x0 + w - 1 || z == z0 + h - 1); }
};
inline long long tileKey(int x, int z) { return (long long)z * 1000003LL + (long long)x; }
inline long long levelKey(int x, int z, int level) { return tileKey(x, z) * 16 + level; }

// A piece not finished yet: the material brought to it so far, and how far it is built
struct Blueprint {
    int supplied = 0;
    double progress = 0.0;
    float retryIn = 0.0f;
    // Laid from a Building Blueprint (build_plan.h) by you or an NPC faction: an Auto Placed Blueprint. A piece placed by hand, or
    // one that is built, is not. A building with any Auto Placed Blueprint left is not finished; cancelling one just takes it off.
    bool autoPlaced = false;
    // An Auto Placed piece laid ahead of the floor or foundation it stands on: drawn faint, never built or supplied, until its
    // stage (below) is reached, when the pieces before it are built.
    bool ghost = false;
    int stage = 0;   // the plan's stage it is of (planStageOf)
};
// A free blueprint (a prop whose props.txt materials is 0): nothing is brought to it and nobody builds it. It finishes itself
// FREE_BUILD_HOURS of game time after it is placed (a unit standing on it is asked to move first), and is let go if it is still
// not finished FREE_BUILD_EXPIRE_HOURS after. Saved.
inline constexpr double FREE_BUILD_HOURS = 1.0;
inline constexpr double FREE_BUILD_EXPIRE_HOURS = 24.0;
struct FreeBuild {
    double hours = 0.0;
};
// On every piece of an NPC building that is being pulled down (npc_builders.h): drawn tinted red. Saved.
struct Condemned {};

// A unit climbing a ladder, up or down, or walking up or down stairs. Not saved.
struct Climbing {
    ObjectId ladder = NO_OBJECT;   // a ladder or stairs
    float left = 0.0f;
    bool down = false;
    float whole = CLIMB_SECONDS;   // seconds the whole climb takes
    // Stairs: the walk, point to point (from where it set off, over the steps, to where it gets off), and where on it
    // (by distance) it hops from one height to the next, as heights above the ground at the bottom step
    struct Hop {
        float at, from, to;
    };
    std::vector<Vec2> path;
    std::vector<Hop> hops;
    float startHeight = 0.0f;
};

// A unit's building task under way: fetching logs from a store, bringing them to a blueprint, or building it. Not saved.
struct BuildJob {
    enum class Phase { Fetch, Deliver, Build };
    Phase phase = Phase::Deliver;
    ObjectId target = NO_OBJECT;
    ObjectId source = NO_OBJECT;
    int item = 0;   // the building material being brought
    float stuck = 0.0f;
};

// A unit held back by a locked door it may not pass. Not saved.
struct AtDoor {
    ObjectId door = NO_OBJECT;
    float since = 0.0f;
};

// A walk that goes up or down ladders: walks on one floor each, each but the last ending at a ladder it climbs. Not saved.
struct RouteLeg {
    int storey = 0;
    Vec2 to{ 0.0f, 0.0f };
    ObjectId ladder = NO_OBJECT;
    Vec2 off{ 0.0f, 0.0f };   // a leg that climbs: where it gets off (stairs down: the tile beside the bottom step)
};
struct Route {
    std::vector<RouteLeg> legs;
    size_t leg = 0;
    bool planned = false;
    Vec2 goal{ 0.0f, 0.0f };
};

// What a foundation, wall, door or floor is made of: Wood Logs, Stone, Iron Ingots (shown as Metal) or Dragon Crystals.
// The tougher, the harder to break. The numbers are saved, so Stone, added last, comes after Crystal; MATERIAL_TIERS
// gives the order they are offered in, weakest first.
enum class BuildMaterial : int { Wood = 0, Iron, Crystal, Stone, Count };
inline constexpr BuildMaterial MATERIAL_TIERS[(int)BuildMaterial::Count] = { BuildMaterial::Wood, BuildMaterial::Stone,
                                                                              BuildMaterial::Iron, BuildMaterial::Crystal };
struct Material {
    BuildMaterial kind = BuildMaterial::Wood;
};
// Whether this kind is made of a material picked when it is placed (a foundation, wall, door, floor, ladder or stairs)
bool takesMaterial(PropKind kind);
BuildMaterial materialOf(World& w, ObjectId prop);
// The item a material is, and how many times tougher than wood it is
int materialItem(BuildMaterial m);
double materialToughness(BuildMaterial m);
const char* materialNameKey(BuildMaterial m);

// Set when walls appear or go, so the terrain's closed tiles are worked out again
// What the walkable ground and the foundation decks need: dirty for all of it (a game just opened), or else just the
// tiles a building piece was finished or lost on, which refreshBuildingTiles updates and replans the walks that cross
struct BuildingsChanged {
    bool dirty = true;
    std::vector<std::pair<int, int>> tiles;
};
void buildingTilesChanged(World& w, int x0, int z0, int x1, int z1);
void refreshBuildingTiles(World& w, Terrain& t);

// The ground reshaped in play, kept with the game: corner heights by Terrain::cornerKey. The Terrain holds the same, to
// sample; this is what a save keeps.
struct GroundEdits {
    std::unordered_map<long long, float> corners;
};
// Sets a corner's height in the world and on the terrain
void editGroundCorner(World& w, Terrain& t, int x, int z, float h);
// The ground under a building is levelled when its first foundation tile is finished: every unfinished foundation tile
// of the building, to the level of its finished tiles (or, with none finished yet, to the average of its ground). What
// stands on the reshaped ground moves with it.
void levelFoundationGround(World& w, Terrain& t, ObjectId foundation);

// Every building piece by where it is, made in one pass over the props: for the checks that look at many tiles
struct BuildingMap {
    std::unordered_map<long long, ObjectId> foundation;   // tileKey: a foundation tile
    std::unordered_map<long long, ObjectId> piece;        // levelKey: a wall, door or ladder on that floor
    std::unordered_map<long long, ObjectId> floor;        // levelKey: an upper floor (level 1 and up)
    std::unordered_map<long long, ObjectId> stairTop;     // levelKey: the top step of stairs, a tile of the floor above them
};
BuildingMap buildingMap(World& w);
// The same map kept until the props, foundations or floors change (their component revisions say so), and the foundation tiles
// joined to a tile kept with it, so a check made many times in a frame (the placement preview) works the world out once. Only
// to be read at once: what it gives is replaced when the buildings change, so don't hold it across a change to them.
const BuildingMap& cachedBuildingMap(World& w);
const std::vector<long long>& cachedBuildingTiles(World& w, int x, int z);
// Changes when anything cachedBuildingMap is made from does
struct BuildingStamp {
    std::uint64_t props = 0, foundations = 0, storeys = 0;
    bool operator==(const BuildingStamp& o) const { return props == o.props && foundations == o.foundations && storeys == o.storeys; }
    bool operator!=(const BuildingStamp& o) const { return !(*this == o); }
};
BuildingStamp buildingStamp(World& w);
// A finished floor of owner's (any owner for -1) on this tile and level: the foundation for level 0, else an upper floor
// or the top step of stairs from the floor below
bool floorSurfaceOn(World& w, const BuildingMap& m, int x, int z, int level, int owner = -1);
// Stairs standing on this floor whose step this tile is: the step (0 the bottom, 2 the top), or -1
int stairsStepAt(World& w, const BuildingMap& m, int x, int z, int level, ObjectId& stairs);

enum class PieceCheck {
    Ok,
    TooSmall,
    TooBig,
    Unwalkable,
    Blocked,
    OutsideClaim,
    NeedsFoundation,
    FoundationUnfinished,
    Taken,
    NeedsFloor,
    TooSteep,
};
const char* pieceRefusalKey(PieceCheck c);

int woodLogItem();
// A prop that is not a building piece, placed from the Build tab: finished at once when props.txt gives it no materials,
// else a blueprint of the placer's faction that is built like any piece
ObjectId placeCostedProp(World& w, const Terrain& t, PropKind kind, float x, float z, float yaw, int owner, int storey = 0);
// A free blueprint of this prop for the owner (above), whatever its kind. Only the developer's placing skips it.
ObjectId placeFreeBlueprint(World& w, const Terrain& t, PropKind kind, float x, float z, float yaw, int owner, int storey = 0);
// Free blueprints grow, finish and expire as game hours pass
void freeBuildSystem(World& w, Terrain& t, double gameHours);
bool isBuildingPiece(PropKind kind);
// Building material this piece needs in all (one unit for most), and seconds of building at Carpentry 1
int materialsNeeded(World& w, ObjectId prop);
double buildSecondsNeeded(World& w, ObjectId prop);
bool isFinished(World& w, ObjectId prop);
// Tiles a piece covers
void pieceTiles(World& w, ObjectId prop, int& x0, int& z0, int& x1, int& z1);

// The foundation tile here, or NO_OBJECT
ObjectId foundationAt(World& w, int x, int z);
// A wall, door or ladder on this tile and floor, or NO_OBJECT
ObjectId groundPieceAt(World& w, int x, int z, int level = 0);
// A finished upper floor on this tile at this level (1 and up), or NO_OBJECT
ObjectId upperFloorAt(World& w, int x, int z, int level = 1);
// Whether this tile has a finished floor of the faction's on this level: its foundation (level 0) or an upper floor
bool floorSurfaceAt(World& w, int x, int z, int level, int owner);
// The foundation tiles joined to this one, side by side: one building
std::vector<long long> buildingTiles(const BuildingMap& map, int x, int z);

// Every building (its foundation tiles joined side by side) in the world, and how far it has come. A building is complete
// when all its foundation is built, it is at least 3 by 3, every edge tile of its ground floor has a built wall or door
// (a 3 by 3 has 7 walls and a door), and it has a built door.
struct BuildingCluster {
    int owner = -1;
    BuildMaterial material = BuildMaterial::Wood;   // what its first foundation tile is made of
    std::vector<long long> tiles;                   // by tileKey
    int x0 = 0, z0 = 0, x1 = 0, z1 = 0;             // the box round it
    bool foundationBuilt = false;                   // no foundation tile is still a blueprint
    bool complete = false;
    float centerX() const { return (float)(x0 + x1 + 1) * 0.5f; }
    float centerZ() const { return (float)(z0 + z1 + 1) * 0.5f; }
};
std::vector<BuildingCluster> buildingClusters(World& w, const BuildingMap& m);

PieceCheck checkFoundation(World& w, const Terrain& t, int x0, int z0, int x1, int z1, int owner = FACTION_PLAYER);
// Lays a foundation tile on every tile of the rectangle that has none: the first laid, or NO_OBJECT
ObjectId placeFoundation(World& w, const Terrain& t, int x0, int z0, int x1, int z1, int owner = FACTION_PLAYER,
                         BuildMaterial material = BuildMaterial::Wood);

// A piece on this floor (level) of a tile.
// Stairs go on 3 tiles in a row, x and z the middle one, rising toward dir (0 north, 1 east, 2 south, 3 west). Their
// bottom step is stepped onto from any open floor tile beside it, and their top step is a tile of the floor above. Their
// steps can't be walked across on the floor they stand on.
PieceCheck checkPiece(World& w, PropKind kind, int x, int z, int owner = FACTION_PLAYER, int level = 0, int dir = 0);
ObjectId placePiece(World& w, const Terrain& t, PropKind kind, int x, int z, int owner = FACTION_PLAYER,
                    BuildMaterial material = BuildMaterial::Wood, int level = 0, int dir = 0);
// A ghost: an Auto Placed Blueprint of this plan stage laid with none of placePiece's rules, ahead of the foundation or floor it
// stands on (a piece that costs nothing, or a level the game has no floor for, gives NO_OBJECT). Nothing builds or supplies it
// until it is made a real blueprint (Blueprint::ghost off).
ObjectId placeGhostPiece(World& w, const Terrain& t, PropKind kind, int x, int z, int owner, BuildMaterial material, int level, int dir,
                         int stage);
// Stairs: the step a direction takes, and the direction stairs rise toward
inline constexpr int STAIRS_DX[4] = { 0, 1, 0, -1 };
inline constexpr int STAIRS_DZ[4] = { -1, 0, 1, 0 };
inline constexpr float STAIRS_SECONDS = 1.2f;
int stairsDir(World& w, ObjectId stairs);
// Upper floors with no finished wall or door under them on their building's floor below fall in
void collapseCheck(World& w);
// A building (its foundation tiles, by tileKey) being pulled down (npc_builders.h): asks the units on its ground floor to walk out,
// and says whether any are still there
bool evacuateBuilding(World& w, Terrain& t, const std::vector<long long>& tiles);
// Takes the whole building down at once: every piece of it goes, blueprints and ghosts too; props, items and units above the ground
// floor are put down on it
void pullDownBuilding(World& w, Terrain& t, const std::vector<long long>& tiles);

// Material is brought to a blueprint: how much it took
int supplyBlueprint(World& w, ObjectId blueprint, int logs);
// Builds every blueprint at once, material and time and all (the Building Blueprints editor): foundations and the pieces on them
// in as many passes as it takes
void finishAllBlueprints(World& w, Terrain& t);
// Finishes a blueprint whose building is done. False, with the units in the way asked to move, if any stand there.
bool finishBlueprint(World& w, Terrain& t, ObjectId prop);
// The terrain's closed tiles, from the finished walls
void rebuildBlocking(World& w, Terrain& t);

// A wall's shape and turn, from the walls and doors around it on its floor: 0 none around (a post), 1 end, 2 straight,
// 3 corner, 4 tee, 5 cross. Each shape is modelled for its arms east, south, west and north, and turned by yaw.
struct WallShape {
    int shape = 0;
    float yaw = 0.0f;
};
WallShape wallShapeOf(World& w, ObjectId wall);
WallShape wallShapeFor(int mask);

// Whether u may walk through this door: it is unlocked, or u is of its faction or an ally of it
bool doorLets(World& w, ObjectId door, ObjectId u);
// Strangers held back by a door or wall they are hostile to pick a door's lock if they can, and smash it if not
void doorSystem(World& w, float dt);

// Whether a bed may be used by this unit: a bed on a faction's foundation is that faction's alone, unless set Public
bool bedOpenTo(World& w, ObjectId bed, ObjectId u);
// The faction whose building a bed stands in, or -1 for none
int bedHouseOwner(World& w, ObjectId bed);

// Ladders and the upper floors
bool canClimbUp(World& w, ObjectId ladder);
// Where a unit stands to climb up a ladder or stairs (their foot: for stairs, an open tile beside the bottom step, the
// one in front first), and where it steps off at the top (for stairs, their top step)
Vec2 climbFoot(World& w, ObjectId ladder);
bool climbTop(World& w, ObjectId ladder, Vec2& out);
// Whether u may start up (or down) this ladder or stairs from where it stands: inside their building, on their floor
// (or the one above to go down), within a tile of their foot (or head). Not from outside, through a wall.
bool atClimbSpot(World& w, ObjectId u, ObjectId ladder);
// off: stairs down, the tile beside the bottom step to get off onto (else the one nearest the unit)
bool startClimb(World& w, const Terrain& t, ObjectId u, ObjectId ladder, const Vec2* off = nullptr);
bool isUpstairs(World& w, ObjectId u);
// The unit heads for a spot: on its own floor, or by way of ladders on another
void moveUpstairs(World& w, Terrain& t, ObjectId u, Vec2 target);
// An order to walk to a spot on this floor, by way of ladders if need be
void moveToFloor(World& w, Terrain& t, ObjectId u, Vec2 target, int storey);
// The nearest finished floor tile's middle on this level to target, within range tiles of it (0 for anywhere)
Vec2 nearestFloorSpot(World& w, ObjectId foundation, Vec2 target, int level = 1);
void climbSystem(World& w, Terrain& t, float dt);

// Walking anywhere, across floors: a walk on the unit's own floor, or else a Route by way of ladders. storey -1: the
// unit's own floor if the target tile has one there, else the ground.
void planSoloMove(World& w, Terrain& t, ObjectId e, Vec2 target);
void walkSoloTo(World& w, Terrain& t, ObjectId e, Vec2 target, int storey = -1);
// Units following a Route, and units chasing a foe on another floor
void routeSystem(World& w, Terrain& t, float dt);

// The building tasks: Auto-Haul Blueprint Supplies and Auto-Construct
bool startBuildStep(World& w, Terrain& t, ObjectId u, Behaviors& b, BehaviorRule& r);
void buildJobSystem(World& w, Terrain& t, float dt);
// The whole building step: jobs, climbing, routes, doors, and the terrain's closed tiles
void buildingSystem(World& w, Terrain& t, float dt);
// Pieces on a foundation tile that is going: they go too, and units above come down
void foundationLost(World& w, ObjectId foundation);
// After a game is opened: older foundations split into tiles, and the floors of things above the ground worked out
void settleBuildingsAfterLoad(World& w, Terrain& t);
