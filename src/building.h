#pragma once
#include <vector>
#include "behavior.h"
#include "build.h"
#include "claims.h"
#include "ecs.h"
#include "geometry.h"
#include "terrain.h"

// Buildings. A foundation is laid as a blueprint by dragging a rectangle (3 to 64 tiles a side). Every piece (the
// foundation, then walls, doors, ladders and upper floors on it) starts as a blueprint that needs its material brought to
// it and then building, both done by Auto-Construct. A piece
// isn't finished while a unit stands where it goes: the unit is asked to move, and the piece is tried again later.
inline constexpr int FOUNDATION_MIN = 3;
inline constexpr int FOUNDATION_MAX = 64;
inline constexpr float BLUEPRINT_REACH = 5.0f;       // logs are handed over and building is done from this far
inline constexpr float FLOOR_HEIGHT = 2.4f;          // walls' height: the upper floor sits on them
inline constexpr float FLOOR_TOP = FLOOR_HEIGHT + 0.14f;   // where a unit upstairs stands
inline constexpr float CLIMB_SECONDS = 1.5f;
inline constexpr float BLOCKED_RETRY_SECONDS = 4.0f; // a piece with a unit in the way waits this long before it is tried again
inline constexpr float BUILD_GIVE_UP_SECONDS = 12.0f;
inline constexpr double DOOR_HEALTH = 60.0;
inline constexpr double WALL_HEALTH = 2.0 * DOOR_HEALTH;   // a wall takes twice the breaking a door does
inline constexpr float AT_DOOR_FORGET_SECONDS = 0.5f;

// A foundation: the tiles it covers. entrances is no longer used (doors go anywhere walls do) but is kept for old saves
struct Foundation {
    int x0 = 0, z0 = 0, w = 0, h = 0;
    std::vector<long long> entrances;
    bool contains(int x, int z) const { return x >= x0 && z >= z0 && x < x0 + w && z < z0 + h; }
    bool onEdge(int x, int z) const { return contains(x, z) && (x == x0 || z == z0 || x == x0 + w - 1 || z == z0 + h - 1); }
};
inline long long tileKey(int x, int z) { return (long long)z * 1000003LL + (long long)x; }

// A piece not finished yet: the logs brought to it so far, and how far it is built
struct Blueprint {
    int supplied = 0;
    double progress = 0.0;
    float retryIn = 0.0f;
};

// A unit on an upper floor, of this foundation's building
struct Upstairs {
    ObjectId foundation = NO_OBJECT;
};

// A unit climbing a ladder, up or down. Not saved.
struct Climbing {
    ObjectId ladder = NO_OBJECT;
    float left = 0.0f;
    bool down = false;
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

// What a foundation, wall or door is made of: Wood Logs, Iron Ingots or Dragon Crystals. The tougher, the harder to break.
enum class BuildMaterial : int { Wood = 0, Iron, Crystal, Count };
struct Material {
    BuildMaterial kind = BuildMaterial::Wood;
};
// Whether this kind is made of a material picked when it is placed (a foundation, wall or door)
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
    EdgesOpen,
};
const char* pieceRefusalKey(PieceCheck c);

int woodLogItem();
bool isBuildingPiece(PropKind kind);
// Building material this piece needs in all (one unit for most), and seconds of building at Carpentry 1
int materialsNeeded(World& w, ObjectId prop);
double buildSecondsNeeded(World& w, ObjectId prop);
bool isFinished(World& w, ObjectId prop);
// Tiles a piece covers (a foundation's whole rectangle)
void pieceTiles(World& w, ObjectId prop, int& x0, int& z0, int& x1, int& z1);

// The foundation covering this tile, or NO_OBJECT
ObjectId foundationAt(World& w, int x, int z);
// A piece other than a foundation or upper floor on this tile (a wall, door or ladder), or NO_OBJECT
ObjectId groundPieceAt(World& w, int x, int z);
// A finished upper floor on this tile, or NO_OBJECT
ObjectId upperFloorAt(World& w, int x, int z);

PieceCheck checkFoundation(World& w, const Terrain& t, int x0, int z0, int x1, int z1, int owner = FACTION_PLAYER);
ObjectId placeFoundation(World& w, const Terrain& t, int x0, int z0, int x1, int z1, int owner = FACTION_PLAYER,
                         BuildMaterial material = BuildMaterial::Wood);

PieceCheck checkPiece(World& w, PropKind kind, int x, int z, int owner = FACTION_PLAYER);
// A ladder and an upper floor need every edge tile of the foundation to have a wall or a door
ObjectId placePiece(World& w, const Terrain& t, PropKind kind, int x, int z, int owner = FACTION_PLAYER,
                    BuildMaterial material = BuildMaterial::Wood);
// Every edge tile of the foundation has a wall or door (built or not), but for none: its entrances may wait
bool edgesClosed(World& w, ObjectId foundation);
// A building whose ground floor has lost all its walls and doors: its upper floors fall in
void collapseCheck(World& w);

// Material is brought to a blueprint: how much it took
int supplyBlueprint(World& w, ObjectId blueprint, int logs);
// Finishes a blueprint whose building is done. False, with the units in the way asked to move, if any stand there.
bool finishBlueprint(World& w, Terrain& t, ObjectId prop);
// The terrain's closed tiles, from the finished walls
void rebuildBlocking(World& w, Terrain& t);

// A wall's shape and turn, from the walls and doors around it: 0 none around (a post), 1 end, 2 straight, 3 corner,
// 4 tee, 5 cross. Each shape is modelled for its arms east, south, west and north, and turned by yaw.
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

// Ladders and the upper floor
bool canClimbUp(World& w, ObjectId ladder);
bool startClimb(World& w, ObjectId u, ObjectId ladder);
bool isUpstairs(World& w, ObjectId u);
// The unit, upstairs, heads for a spot on its floor: the nearest finished floor tile to target
void moveUpstairs(World& w, ObjectId u, Vec2 target);
Vec2 nearestFloorSpot(World& w, ObjectId foundation, Vec2 target);
void climbSystem(World& w, Terrain& t, float dt);

// The building tasks: Auto-Haul Blueprint Supplies and Auto-Construct
bool startBuildStep(World& w, Terrain& t, ObjectId u, Behaviors& b, BehaviorRule& r);
void buildJobSystem(World& w, Terrain& t, float dt);
// The whole building step: jobs, climbing, doors, and the terrain's closed tiles
void buildingSystem(World& w, Terrain& t, float dt);
// Pieces on a foundation that is going: they go too, and units upstairs come down
void foundationLost(World& w, ObjectId foundation);
