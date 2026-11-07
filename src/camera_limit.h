#pragma once
#include "camera.h"
#include "ecs.h"

// In regular play the camera's focus stays this close to the nearest selected unit, or the nearest of your units if none is
inline constexpr float CAMERA_UNIT_RANGE = 100.0f;
void keepCameraNearUnits(World& w, Camera& cam);
