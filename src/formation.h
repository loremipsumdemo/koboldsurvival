#pragma once
#include <vector>
#include "geometry.h"

class Terrain;

// The shape a group of units stands in. Offsets are from the middle of the group: x to its right, z behind it.
enum class FormationShape : int { Line = 0, Circle, Rectangle, Wedge, Count };

constexpr float FORMATION_TIGHT_SPACING = 1.15f;   // two units need 1.0 to stand side by side
constexpr float FORMATION_LOOSE_SPACING = 2.0f;
constexpr float FORMATION_DRAG_LEAST = 1.5f;       // a shorter drag only says where, not how wide or which way

// The player's choice, in the World's resources
struct FormationChoice {
    FormationShape shape = FormationShape::Rectangle;
    bool loose = false;
};

struct FormationSpec {
    FormationChoice choice;
    float width = 0.0f;     // how wide to stand, from dragging. 0: the shape's own width.
    bool facing = false;    // end up looking along head, rather than the way it walked
    float headX = 0.0f, headZ = 1.0f;
};

inline float formationSpacing(const FormationChoice& c) { return c.loose ? FORMATION_LOOSE_SPACING : FORMATION_TIGHT_SPACING; }
const char* formationLabelKey(FormationShape shape);

std::vector<Vec2> formationOffsets(FormationShape shape, int count, float spacing, float width = 0.0f);
Vec2 formationSpot(Vec2 center, float headX, float headZ, Vec2 offset);

// Where each slot really goes on this ground: a slot that is blocked, or cut off from the middle by something, moves to the
// nearest place that isn't, so no two slots end up together.
std::vector<Vec2> fitFormation(const Terrain& t, Vec2 center, float headX, float headZ, const std::vector<Vec2>& offsets, float spacing);

// Which slot each unit takes, for the given positions: far slots are filled first, then pairs swap while that shortens their
// walks, so units don't cross each other's paths.
std::vector<int> assignFormationSlots(const std::vector<Vec2>& units, const std::vector<Vec2>& spots, float approachX, float approachZ);
