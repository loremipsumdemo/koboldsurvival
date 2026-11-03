#pragma once
#include <vector>
#include <GL/glew.h>

struct DrawRange {
    GLuint texture = 0;
    GLsizei indexCount = 0;
    GLsizei firstIndex = 0;   // in indices, not bytes
};

// Static mesh from a .glb. Vertices are position (3 floats) then UV (2 floats).
struct Model {
    GLuint vao = 0, vbo = 0, ebo = 0;
    float minX = 0, minY = 0, minZ = 0;
    float maxX = 0, maxY = 0, maxZ = 0;
    std::vector<DrawRange> draws;
    std::vector<GLuint> textures;   // owned, freed by destroyModel
    // The same triangles kept in memory, for finding what the mouse is over (rayHitModel): each vertex's position, 3
    // floats, and 3 vertex numbers per triangle
    std::vector<float> positions;
    std::vector<GLuint> triangles;
};

bool loadGLBModel(const char* path, Model& m);
void destroyModel(Model& m);

// Where a model stands in the world. It is scaled by k so its largest side is the size asked for, then turned by yaw
// about the vertical axis through the centre of its bottom, and that centre is put on the point given. A model point p
// is at k * R(p - pivot) + t, where R turns the x and z parts by yaw. The turn is the one a footprint uses too
// (insideFootprint in props.h). Drawing and picking both use this one placement, so a prop is always hit where it is
// drawn, wherever it is moved and however it is turned.
struct ModelPlacement {
    float k = 0.0f;                 // the scale
    float cs = 1.0f, sn = 0.0f;     // the cosine and sine of the turn
    float px = 0.0f, py = 0.0f, pz = 0.0f;   // the pivot: the centre of the model's bottom, in model space
    float tx = 0.0f, ty = 0.0f, tz = 0.0f;   // the world point the pivot stands on
    float ox = 0.0f, oy = 0.0f, oz = 0.0f;   // the translation of the whole transform (what drawModel sends)
    bool ok = false;                // false for a model with no size
};
ModelPlacement placeModel(const Model& m, float x, float y, float z, float targetSize, float yaw = 0.0f);

// The nearest place a ray (origin o, direction d, in the world) meets a placed model's triangles, as the distance
// along d in units of d's length; below 0 when it misses
float rayHitModel(const Model& m, const ModelPlacement& p, const float o[3], const float d[3]);
