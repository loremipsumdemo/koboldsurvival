#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/math.hpp"
#include "core/debug_options.hpp"
#include "core/render_options.hpp"
#include "render/gl_api.hpp"
#include "render/gpu_timer.hpp"
#include "render/skybox.hpp"
#include "theme/theme.hpp"
#include "world/world.hpp"

namespace wm {


struct Camera {
    V3 eye;
    V3 fwd;
    V3 right;
    V3 up;
    float fovy;
    float aspect;
    Mat4 viewProj;
    float farPlane = 1000.0f;   // far clip distance, so the sea can fade out before it
};

// One end of a line: position and colour, 24 bytes, no padding.
struct LineVertex {
    float x, y, z;
    float r, g, b;
};

// Adds the floor grid around centre to out. Lines on multiples of `major` use majorCol.
// Used when gOpt.shaderGrid is off.
void AppendGrid(std::vector<LineVertex>& out, V3 centre, float radius, int major, V3 minorCol, V3 majorCol);

// Adds a wireframe box to out.
void AppendWireBox(std::vector<LineVertex>& out, V3 centre, V3 size, V3 colour);

// One square of terrain. Each chunk has a full-detail mesh and a coarse mesh
// (every 4th tile), and DrawTerrain picks one by distance.
struct TerrainChunk {
    GLuint vaoFull = 0;
    GLuint vboFull = 0;
    GLuint vaoCoarse = 0;
    GLuint vboCoarse = 0;
    float cx = 0.0f;   // world-space centre
    float cz = 0.0f;
    float minY = 0.0f; // height range of the chunk, skirts included, for culling
    float maxY = 0.0f;
};

class Renderer {
public:
    // fontBytes is the whole TrueType file, read by the host
    bool Init(const std::vector<unsigned char>& fontBytes, std::string& err);
    void Shutdown();

    // Sets the viewport and clears the frame to the sky's horizon colour.
    void BeginFrame(int w, int h, const SkyState& sky);

    // Loads the six cube faces (posx, negx, posy, negy, posz, negz .jpg or .png) from
    // folder. On failure err says why and the procedural sky stays in use.
    bool LoadSkybox(const std::string& folder, std::string& err);
    bool UsingSkybox() const { return skybox_.loaded() && gDebug.skybox; }
    // Average colour of the skybox at the horizon. Used for fog when the skybox is shown.
    V3 SkyboxHorizon() const { return skybox_.horizon(); }

    // Draws the sky dome, sun and stars. With gOpt.skyLast it is drawn after the
    // terrain and water, and only fills pixels they did not cover.
    void DrawSky(const Camera& cam, const SkyState& sky);

    // Draws the line list with lighting and distance fog.
    void DrawLines(const Camera& cam, const SkyState& sky, const std::vector<LineVertex>& lines);

    // Floor grid around the eye, drawn in the shader. Used when gOpt.shaderGrid is on.
    void DrawGrid(const Camera& cam, const SkyState& sky, float radius, int major, V3 minorCol, V3 majorCol);

    // 2D drawing in pixels, origin at the top left. Call between BeginUI and EndUI.
    void BeginUI(int w, int h);
    void EndUI();
    void Rect(float x, float y, float w, float h, V3 colour, float alpha);
    void Text(float x, float top, const std::string& s, V3 colour);
    // Same as Text, with the whole line at the given opacity, 0 to 1.
    void TextAlpha(float x, float top, const std::string& s, V3 colour, float alpha);
    // Text drawn at scale times the baked size, scaled about the start of the string.
    void TextScaled(float x, float top, const std::string& s, V3 colour, float alpha, float scale);
    float MeasureText(const std::string& s) const;
    float LineHeight() const;

    // Map image: a side x side RGBA picture, row 0 at the top. Uploaded once per world.
    void UploadMap(const uint8_t* rgba, int side);
    // Draws part of the map image into a pixel rectangle. (u0,v0) is the top-left texel
    // coordinate and (u1,v1) the bottom-right, both 0 to 1.
    void DrawMap(float x, float y, float w, float h, float u0, float v0, float u1, float v1, float alpha);

    // Terrain: BuildTerrain replaces any previous mesh. Chunks beyond viewDist are
    // skipped, and chunks closer than lodDist use the full-detail mesh.
    void BuildTerrain(const World& w);
    void ClearTerrain();
    void DrawTerrain(const Camera& cam, const SkyState& sky, float viewDist, float lodDist);

    // Sea plane at height seaY, drawn around the camera out to viewDist.
    void DrawWater(const Camera& cam, const SkyState& sky, float seaY, float viewDist);

    // Seabed plane beyond the map, far below the sea. Draw before DrawWater.
    void DrawFloor(const Camera& cam, const SkyState& sky);

    // GPU timing for the performance panel. Each pass is bracketed by GpuBegin and
    // GpuEnd. Nothing is timed unless SetGpuTiming(true).
    void SetGpuTiming(bool on) { gpu_.enabled = on; }
    void GpuBegin(Pass p) { gpu_.Begin(p); }
    void GpuEnd() { gpu_.End(); }
    double GpuMs(Pass p) const { return gpu_.Ms(p); }

private:
    bool InitTerrain(std::string& err);
    bool InitGrid(std::string& err);     // render/grid.cpp
    void ShutdownTerrain();
    void DrawUI(const float* verts, std::size_t floats, bool useTex, V3 colour, float alpha);
    void FlushBatch();

    GLuint skyProg_ = 0;
    GLuint lineProg_ = 0;
    GLuint uiProg_ = 0;

    GLuint mapProg_ = 0;
    GLuint mapVao_ = 0;
    GLuint mapVbo_ = 0;
    GLuint mapTex_ = 0;
    GLint mapProjLoc_ = -1;
    GLint mapAlphaLoc_ = -1;
    GLint mapTexLoc_ = -1;
    void EnsureMap();

    GLuint skyVao_ = 0;
    GLuint lineVao_ = 0;
    GLuint lineVbo_ = 0;
    GLuint uiVao_ = 0;
    GLuint uiVbo_ = 0;

    struct SkyLocs {
        GLint forward, right, up, tanHalfFov, aspect;
        GLint top, horizon, sunDir, sunColour, sunStrength, stars, depth, cube, useCube;
        GLint starRgb, starAmount, moonDir, moonRgb, moonStrength, moonCos;
    } sky_{};

    struct LineLocs {
        GLint viewProj, eye, light, fogColour, fog;
    } line_{};

    struct UiLocs {
        GLint proj, colour, useTex, tex;
    } ui_{};

    Mat4 uiProj_{};

    // Batched 2D drawing: every vertex carries its own colour and texture mode.
    GLuint batchProg_ = 0;
    GLuint batchVao_ = 0;
    GLuint batchVbo_ = 0;
    GLint batchProj_ = -1;
    GLint batchTex_ = -1;
    bool batching_ = false;
    std::vector<float> batch_;   // 9 floats per vertex: x y, u v mode, r g b a
    mutable std::unordered_map<std::string, float> widthCache_;

    // Terrain, water and grid (render/terrain.cpp, grid.cpp).
    GLuint terrainProg_ = 0;
    GLuint terrainIbo_ = 0;          // full detail indices
    GLuint terrainIboCoarse_ = 0;    // coarse indices
    GLsizei terrainIndexFull_ = 0;
    GLsizei terrainIndexCoarse_ = 0;
    GLuint waterProg_ = 0;
    GLuint floorProg_ = 0;
    float floorY_ = 0.0f;            // height of the seabed plane and of the map's border walls
    GLuint gridProg_ = 0;
    GLuint emptyVao_ = 0;            // water and grid build their vertices in the shader
    GpuTimers gpu_;
    Skybox skybox_;
    GLuint groundTex_ = 0;           // tile for the terrain: normal in RGB, height in alpha
    GLuint waterTex_ = 0;            // tile for the sea, same layout
    float seaY_ = 0.0f;              // sea level of the loaded world, used for the underwater tint
    std::vector<TerrainChunk> chunks_;

    // Seabed apron around the map (see BuildApronMesh in terrain.cpp).
    GLuint apronVao_ = 0;
    GLuint apronVbo_ = 0;
    GLuint apronIbo_ = 0;
    GLsizei apronCount_ = 0;
    float apronMin_ = 0.0f;   // world x and z range, edge to edge including the apron
    float apronMax_ = 0.0f;

    struct TerrainLocs {
        GLint viewProj, eye, sunDir, sunLight, ambient, fogColour, fog, tile, seaY, tint, rockRgb, rockSlope, slopeView, underwater;
    } terr_{};

    struct WaterLocs {
        GLint viewProj, eye, centre, half, seaY, sunDir, sunLight, ambient, horizon, fog, colour, time, tile, fadeStart, fadeEnd, opacity, pattern, speed;
    } water_{};

    struct FloorLocs {
        GLint viewProj, eye, centre, half, ambient, sunLight, fogColour, fog, underwater;
    } floor_{};

    struct GridLocs {
        GLint viewProj, eye, centreX, centreZ, half, minor, major, majorEvery, horizon, fog;
    } grid_{};
};

}  // namespace wm
