#pragma once
#include <vector>
#include "components.h"
#include "ecs.h"
#include "rng.h"
#include "terrain.h"

// Spawning a cluster, from the level editor's and the Developer panel's spawn lists: count spots around a point, on
// open ground and kept apart from each other (and, for props, from the props already there), the first at the point
// itself. Fewer come back when there is not enough open ground nearby.
inline constexpr int SPAWN_CLUSTER_MAX = 50;
std::vector<Vec2> clusterSpots(World& w, const Terrain& t, Rng& rng, float x, float z, int count, float spacing, bool props);
