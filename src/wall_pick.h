#pragma once
#include <functional>
#include <optional>
#include "camera.h"
#include "ecs.h"
#include "terrain.h"

// The side of a wall the pointer is on: the wall tile, which side of it (build.h's SIDE_DX/SIDE_DZ: 0 faces +z, 1 +x, 2 -z, 3 -x) and its floor.
// A wall decoration (props.txt's wall_mount flag) is put on a side of a wall, so this is what placing one points at, not a tile of ground.
struct WallSidePick {
    int tx = 0, tz = 0, side = 0, storey = 0;
};

// The first wall the pointer's ray meets (a tile whose wall is hidden by the cutaway is passed through), and the side of it the ray came in by.
// A ray that comes down onto the top of a wall takes the open side that faces the camera most. None if it meets no wall.
std::optional<WallSidePick> pickWallSide(World& w, const Terrain& t, const Camera& cam, float mx, float my, int W, int H,
                                         const std::function<bool(int tx, int tz, int storey)>& cutAway);
