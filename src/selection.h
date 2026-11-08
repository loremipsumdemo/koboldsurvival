#pragma once
#include <optional>
#include <utility>
#include <vector>
#include "camera.h"
#include "ecs.h"
#include "geometry.h"
#include "terrain.h"

std::optional<std::pair<float, float>> pickGround(const Camera& cam, const Terrain& t, float mx, float my, int W, int H);
// Where the pointer's ray meets the flat plane at height y
std::optional<std::pair<float, float>> pickPlane(const Camera& cam, float mx, float my, int W, int H, float y);

constexpr float HIDDEN_BEHIND_GROUND = 3.0f;

void clearSelection(World& w);
std::vector<ObjectId> selectedUnits(World& w);

ObjectId sidebarUnitOf(World& w);
std::vector<ObjectId> panelUnits(World& w);
ObjectId unitClicked(World& w, const std::vector<ObjectId>& underPointer);
void clickSelect(World& w, ObjectId hit, bool additive);
// Your units a dragged box covers
std::vector<ObjectId> unitsInBox(World& w, const Camera& cam, float x0, float y0, float x1, float y1, int W, int H);
bool boxSelect(World& w, const Camera& cam, float x0, float y0, float x1, float y1, int W, int H, bool additive);
std::vector<ObjectId> groundItemsInBox(World& w, const Camera& cam, float x0, float y0, float x1, float y1, int W, int H);
bool rightClickOrder(World& w, Terrain& t, const Camera& cam, float mx, float my, int W, int H, float& gx, float& gz);

ObjectId firstSquadUnit(World& w);
void focusCameraOnFirstUnit(World& w, Camera& cam);
void focusCameraOnUnit(World& w, Camera& cam, ObjectId u);
bool followUnitCamera(World& w, Camera& cam, ObjectId u);
