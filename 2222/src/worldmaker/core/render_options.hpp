#pragma once

namespace wm {


// Which optimizations are switched on. The benchmark runs the same scene with
// each variant so the effect of every change can be measured on the same machine.
struct RenderOptions {
    bool batchUi = false;      // all 2D drawing in one vertex buffer and one draw call
    bool cacheText = false;    // cache text widths, binary search when clipping console lines
    bool shaderGrid = false;   // floor grid drawn in the shader instead of uploading lines
    bool cullChunks = false;   // frustum cull terrain chunks
    bool skyLast = false;      // sky drawn after the opaque terrain, so covered pixels skip it
    bool dynamicFar = true;    // far plane follows the view distance
};

// Set once at start-up from the command line or the benchmark variant.
extern RenderOptions gOpt;

// Counted during a frame and reset at its start.
struct RenderStats {
    int drawCalls = 0;
    int chunks = 0;
    int triangles = 0;   // terrain triangles submitted this frame
};
extern RenderStats gStats;

}  // namespace wm
