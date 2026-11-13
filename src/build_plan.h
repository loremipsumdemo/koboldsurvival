#pragma once
#include <string>
#include <vector>
#include "building.h"
#include "components.h"
#include "ecs.h"
#include "props.h"
#include "terrain.h"

// Building plans (the Building Blueprints editor's files, in MAP/BLUEPRINT): a building as pieces on a grid, which an NPC
// faction with an Altar builds in its claim. A plan names no model and no material: each piece is a prop's key (props.txt),
// where it stands in the plan's box in tiles, which floor, its turn, and how it was adjusted (the Adjust tool's PropVisual).
// What it is made of is chosen when it is built. The game's own props.txt gives each piece its model and its cost.
//
// (Code and files say plan: the game's Blueprint is a piece still to be built.)
inline constexpr const char* PLAN_FOLDER = "MAP/BLUEPRINT";
inline constexpr int PLAN_SIZE_MIN = FOUNDATION_MIN;
inline constexpr int PLAN_SIZE_MAX = FOUNDATION_MAX;
inline constexpr int PLAN_FLOORS_MAX = STOREY_MAX + 1;

struct PlanPiece {
    PropKind kind = PropKind::Foundation;
    float x = 0.0f, z = 0.0f;   // in tiles from the plan's corner: the middle of the tile a building piece is on
    int level = 0;              // a floor: the level it is laid on (0 the ground floor); the piece's floor otherwise
    float yaw = 0.0f;           // stairs: dir * a quarter turn, as the game keeps it
    PropVisual visual;
    bool bedPublic = false;     // a bed or bedroll set Public (open to anyone), not kept for its faction
    bool unlocked = false;      // a door or a lockable prop left unlocked
};

struct BuildPlan {
    std::string name;
    int w = PLAN_SIZE_MIN, d = PLAN_SIZE_MIN;   // the box, in tiles
    int floors = 1;
    // Whether NPC factions may build it (the Load list's NPC toggle). A file with no npcs line predates the toggle, and is allowed.
    bool npcAllowed = true;
    std::vector<PlanPiece> pieces;
};

// The order a plan is built in, which is the order the game allows its pieces to be put down: the foundation first, then each
// floor's walls, doors, ladders, stairs and props, then the upper floor on top of them, then the next floor's pieces.
//   0 foundation, 1 the ground floor's pieces, 2 the first upper floor, 3 the first upper floor's pieces, and so on
int planStageOf(const PlanPiece& p);
int planStageCount(const BuildPlan& plan);
std::vector<const PlanPiece*> planStagePieces(const BuildPlan& plan, int stage);

// The plan turned a quarter turn clockwise (as seen from above) that many times
BuildPlan planTurned(const BuildPlan& plan, int turns);
// The tiles of the plan's foundation, by x + z * w, true where there is one
std::vector<bool> planFootprint(const BuildPlan& plan);
// What the whole plan costs: material for the pieces made of one (foundation, walls, doors, floors, ladders, stairs), and
// wood logs for the props
struct PlanCost {
    int material = 0;
    int logs = 0;
};
PlanCost planCost(const BuildPlan& plan);
// The foundation as rectangles, the biggest first, which cover its tiles exactly once
struct PlanRect {
    int x0 = 0, z0 = 0, x1 = 0, z1 = 0;   // tiles of the plan, both ends included
};
std::vector<PlanRect> planFoundationRects(const BuildPlan& plan);

std::string planFilePath(const std::string& name);
std::string planToText(const BuildPlan& plan);
bool planFromText(const std::string& text, BuildPlan& out, std::string& err);
bool writePlanFile(const std::string& path, const BuildPlan& plan, std::string& err);
bool readPlanFile(const std::string& path, BuildPlan& out, std::string& err);
std::vector<std::string> listPlanFiles();
bool deletePlanFile(const std::string& name, std::string& err);
// Reads a saved plan, sets whether NPCs may build it, and writes it back at once (no scene needed)
bool setPlanNpcAllowed(const std::string& name, bool allowed, std::string& err);
// Every plan in MAP/BLUEPRINT that reads and is sound (a 3 by 3 foundation at least, a door), read once. Asking again after a plan is saved
// or deleted needs reloadPlanLibrary.
const std::vector<BuildPlan>& planLibrary();
void reloadPlanLibrary();
// What is wrong with a plan for NPCs to build it, as a language key, or empty if nothing: it needs a foundation with a 3 by 3 of
// it at least, and a door on the ground floor. (A plan may be any shape and have any walls: it adds to its faction's claim when
// it is a complete building, building.h, and not before.)
std::string planProblem(const BuildPlan& plan);

// How many props stand outside the box (an Altar, nature and debris not counted): a plan saved from there leaves them out
int planPiecesOutside(World& w, int boxW, int boxD, int x, int z);

// Reads what stands inside the box (its corner at x, z in tiles) off the world: building pieces and props, as finished.
BuildPlan capturePlan(World& w, const std::string& name, int boxW, int boxD, int floors, int x, int z);

// The foundation of the plan, at the plan's corner (x, z) in tiles: as blueprints of the owner's, laid as the game lays them, the
// biggest rectangle first. The first tile laid, or NO_OBJECT if the game refuses the biggest rectangle there.
ObjectId placePlanFoundation(World& w, const Terrain& t, const BuildPlan& plan, int x, int z, int owner, BuildMaterial material);
ObjectId placePlanFoundation(World& w, const Terrain& t, const std::vector<PlanRect>& rects, int x, int z, int owner, BuildMaterial material);
// Whether nothing stands on the plan's foundation tiles, or on the ring of tiles all round them (a tile's width, diagonals too): no
// prop (trees and rocks too), and no other building's foundation. Pieces put down so close together could shut a way, or a unit in.
// Props above the ground floor are not looked at.
bool planSurroundingsClear(World& w, const std::vector<PlanRect>& rects, int x, int z);
// The whole plan (turned already, rects its foundation's rectangles) as a Building Blueprint at the corner (x, z), all at once: the
// foundation as Auto Placed Blueprints (Blueprint::autoPlaced), everything on it as ghosts of their stage, which become blueprints
// as the stage before them is built (npcBuilderSystem). Props that cost nothing are put down when their stage comes. The first
// foundation tile, or NO_OBJECT if the game refuses the foundation there.
ObjectId placePlanAll(World& w, const Terrain& t, const BuildPlan& turned, const std::vector<PlanRect>& rects, int x, int z, int owner,
                      BuildMaterial material);
// Whether the plan's foundation would be allowed at the corner (x, z): its biggest rectangle, as the game checks one. The
// rectangles may be worked out once (planFoundationRects) when many corners are tried.
bool planFoundationFits(World& w, const Terrain& t, const BuildPlan& plan, int x, int z, int owner);
bool planFoundationFits(World& w, const Terrain& t, const std::vector<PlanRect>& rects, int x, int z, int owner);

// Puts one piece down at the plan's corner (x, z): a building piece through the game's own placing, any other prop as it is
// placed from the Build tab. It is a blueprint of the owner's, to be built, if its kind costs materials. NO_OBJECT if the game
// refuses it there.
ObjectId placePlanPiece(World& w, const Terrain& t, const PlanPiece& p, int x, int z, int owner, BuildMaterial material);
// Whether the game allows this piece there now
bool planPieceFits(World& w, const Terrain& t, const PlanPiece& p, int x, int z, int owner);
// Puts the whole plan down and builds it at once, a stage at a time: the Building Blueprints editor's way
void placePlanInstant(World& w, Terrain& t, const BuildPlan& plan, int x, int z, int owner);
