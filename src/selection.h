#pragma once
// Picking things under the pointer, selecting your squad, and pointing the camera at a unit
#include <optional>
#include <utility>
#include <vector>
#include "camera.h"
#include "ecs.h"
#include "geometry.h"
#include "terrain.h"

// Ray through a pixel, intersected with the terrain surface. Returns world (x, z).
std::optional<std::pair<float, float>> pickGround(const Camera& cam, const Terrain& t, float mx, float my, int W, int H);

// A unit or prop whose picture is under the pointer, but more than this many tiles farther from the eye than the
// ground the pointer is over, stands behind that ground and is not what was clicked. It allows for a tall prop (a
// tree) whose top is seen over the ground in front of its foot.
constexpr float HIDDEN_BEHIND_GROUND = 3.0f;

void clearSelection(World& w);
std::vector<Entity> selectedUnits(World& w);

// The unit the Unit Panel shows: the talking unit during a trade, the unit using a storage prop while its panel is
// open, otherwise the lone selected unit
Entity sidebarUnitOf(World& w);
// The units the Unit Panel acts on: the talking unit during a trade, the storage panel's unit, otherwise the selected units.
// An awake mob's inventory is shut (mobs.h), so a mob is never one of them: its items can't be dragged, used or dropped.
std::vector<Entity> panelUnits(World& w);
// The unit a click lands on: the one whose picture is under the pointer (yours first), else the nearest within one
// tile of the clicked ground point. NULL_ENTITY for none.
Entity unitClicked(World& w, const Terrain& t, const Camera& cam, const std::vector<Entity>& underPointer,
                   float mx, float my, int W, int H);
// Click selection. One of your squad is selected (added, with Shift or Alt; else in place of the others), and the Unit
// Panel goes back to your selection. Any other unit (an NPC, a corpse) is shown in the Unit Panel without touching your
// selection. Empty ground changes nothing: your squad stays selected.
void clickSelect(World& w, Entity hit, bool additive);
// Box selection: every unit of your squad whose screen position falls inside the rectangle. A box with none of them in
// it changes nothing, so your squad is never left with nobody selected, and returns false.
bool boxSelect(World& w, const Camera& cam, float x0, float y0, float x1, float y1, int W, int H, bool additive);
// Every item on the ground whose screen position falls inside the rectangle
std::vector<Entity> groundItemsInBox(World& w, const Camera& cam, float x0, float y0, float x1, float y1, int W, int H);
// Right click: order the selected units to a ground point.
// Returns true, with the ground point in gx and gz, if an order was given.
bool rightClickOrder(World& w, Terrain& t, const Camera& cam, float mx, float my, int W, int H, float& gx, float& gz);

// The unit of your squad that is selected when none is (a new game, the selected unit died): the lowest-numbered one,
// preferring a person to a mob and an awake unit to an unconscious one. NULL_ENTITY with no squad left.
Entity firstSquadUnit(World& w);
// Puts the camera on the tile under the first unit of your squad (firstSquadUnit), the one selected at the start
void focusCameraOnFirstUnit(World& w, Camera& cam);
// Moves the camera's focus onto a unit, wherever it is on the map
void focusCameraOnUnit(World& w, Camera& cam, Entity u);
// Holds the camera's focus on a unit as it moves. False once the unit is gone, so following stops.
bool followUnitCamera(World& w, Camera& cam, Entity u);
