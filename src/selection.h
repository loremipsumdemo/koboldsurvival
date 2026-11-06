#pragma once
#include <optional>
#include <utility>
#include <vector>
#include "camera.h"
#include "ecs.h"
#include "geometry.h"
#include "terrain.h"

std::optional<std::pair<float, float>> pickGround(const Camera& cam, const Terrain& t, float mx, float my, int W, int H);

constexpr float HIDDEN_BEHIND_GROUND = 3.0f;

void clearSelection(World& w);
std::vector<Entity> selectedUnits(World& w);

Entity sidebarUnitOf(World& w);
std::vector<Entity> panelUnits(World& w);
Entity unitClicked(World& w, const std::vector<Entity>& underPointer);
void clickSelect(World& w, Entity hit, bool additive);
bool boxSelect(World& w, const Camera& cam, float x0, float y0, float x1, float y1, int W, int H, bool additive);
std::vector<Entity> groundItemsInBox(World& w, const Camera& cam, float x0, float y0, float x1, float y1, int W, int H);
bool rightClickOrder(World& w, Terrain& t, const Camera& cam, float mx, float my, int W, int H, float& gx, float& gz);

Entity firstSquadUnit(World& w);
void focusCameraOnFirstUnit(World& w, Camera& cam);
void focusCameraOnUnit(World& w, Camera& cam, Entity u);
bool followUnitCamera(World& w, Camera& cam, Entity u);
