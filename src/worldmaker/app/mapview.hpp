#pragma once

// The map views: a north-up minimap, the full map shown while M is held, and the compass bar.

#include <cstdint>
#include <vector>

#include "app/menu.hpp"
#include "player/walker.hpp"
#include "render/renderer.hpp"
#include "world/world.hpp"

namespace wm {


// A north-up picture of the world, one texel per sampled tile. Row 0 is the top (north),
// and the left column is east.
struct MapImage {
    int side = 0;
    std::vector<uint8_t> rgba;
};
MapImage BuildMapImage(const World& w, int maxSide);

// State of the full map: zoom, the view centre when focus is on the mouse, and dragging.
struct MapView {
    bool focusSelf = true;        // true: centre and zoom on the player. false: zoom about the mouse and drag to pan
    float zoom = 1.0f;            // 1 shows the whole world
    float cx = 0.0f, cz = 0.0f;   // view centre in world units
    float lastX = 0.0f, lastY = 0.0f;
    bool dragging = false;
};

void DrawCompass(Renderer& r, const Player& p, int winW, int winH);
void DrawMinimap(Renderer& r, const World& w, const Player& p, int winW, int winH);
void DrawFullMap(Renderer& r, const World& w, const Player& p, MapView& mv, const Mouse& mouse, int winW, int winH);

}  // namespace wm
