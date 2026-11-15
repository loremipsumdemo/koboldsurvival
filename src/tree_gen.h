#pragma once
#include <cstdint>
#include <vector>

// Procedural trees. Each tree has genes (how many primary branches, how they turn and spread, how long they are) worked out from a seed,
// a wooden skeleton of limbs each in the coordinate system of its parent, and a crown of leaf puffs rolled from the same seed for the
// season it is in. A tree in the world takes one of TREE_VARIANTS gene sets from where it stands, and the foliage of one of TREE_STAGES
// points of the year. The meshes are made when first wanted (tree_gen.cpp is only the maths; the renderer uploads them).

constexpr int TREE_VARIANTS = 16;
constexpr int TREE_STAGES = 24;
constexpr int TREE_YEAR_DAYS = 365;   // the seasons are laid out over 365 days; the game's 360-day year is stretched to fit

struct TreeMesh {
    std::vector<float> verts;   // five floats a vertex: position, then the texture coordinates into the palette (shade, colour row)
    std::vector<uint32_t> indices;
};

// A tree of this variant (0 to TREE_VARIANTS - 1) with the foliage of this stage (0 to TREE_STAGES - 1). The wood does not change with
// the stage. detail 1 is the coarser copy for trees far off: the limbs down to the second branching only, and one puff to a tip.
// Units are those of the model: about 3.85 up the trunk to its top, the crown reaching to about 6.
TreeMesh generateTree(int variant, int stage, int detail);

// The palette the meshes' colours are rows of: TREE_PALETTE_ROWS rows of TREE_PALETTE_SHADES texels, RGBA
constexpr int TREE_PALETTE_SHADES = 8;
constexpr int TREE_PALETTE_ROWS = 30;
std::vector<uint8_t> treePalette();

// Which stage the foliage is at on a day of the game's year (1 to 360), and the stage of the clock
int treeStageOfDay(int dayOfYear);
int treeStageOfClock(double clockSeconds);
// The nominal height of a tree in model units, and the scale to put it at a size in tiles
constexpr float TREE_MODEL_HEIGHT = 6.0f;
