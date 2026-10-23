#pragma once

namespace wm {


// Settings from the Esc menu's Debug tab. They change what is drawn or shown, and
// they are not saved. The renderer and the main loop read them every frame.
struct RenderDebug {
    bool showFps = false;        // FPS and UPS counter in the top left corner
    bool showPerfPanel = false;  // timing and work panel on the right; also turns GPU timers on
    bool wireframe = false;      // terrain drawn as lines
    bool slopeView = false;      // terrain colored by slope: green flat, red steep
    bool lodTint = false;        // color chunks by detail: green full detail, orange coarse
    bool hideTerrain = false;
    bool hideFloor = false;      // the seabed plane beyond the map
    bool hideWater = false;
    bool hideSky = false;
    bool skybox = false;         // draw the skybox from data/skybox/01 when it loaded (off by default, toggle in Debug)
    bool vsync = true;           // swap interval 1 when on, 0 when off
};
extern RenderDebug gDebug;

// The render passes the performance panel times, in the order they run.
enum class Pass { Update, Sky, Terrain, Floor, Water, Lines, Ui, Count };

}  // namespace wm
