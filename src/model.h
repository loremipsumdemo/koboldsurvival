#pragma once
#include <memory>
#include <string>
#include <vector>
#include <GL/glew.h>

struct DrawRange {
    GLuint texture = 0;
    GLsizei indexCount = 0;
    GLsizei firstIndex = 0;
};

// The rig of a skinned model (a glTF skin with its animations), kept on the CPU: a unit's pose is worked out each frame
// (poseSkeleton) and handed to the card as one matrix per joint.
struct Skeleton {
    static constexpr int MAX_JOINTS = 32;
    struct Channel {
        int joint = 0;
        int path = 0;                // 0 translation, 1 rotation, 2 scale
        std::vector<float> times;
        std::vector<float> values;   // 3 floats a key for translation and scale, 4 (x y z w) for rotation
    };
    struct Clip {
        std::string name;
        float duration = 0.0f;
        std::vector<Channel> channels;
    };
    int jointCount = 0;
    std::vector<int> parent;           // the joint above each joint, or -1; parents come before their children in `order`
    std::vector<int> order;
    std::vector<float> invBind;        // 16 floats (column-major) a joint
    std::vector<float> restT, restR, restS;   // each joint's own place, turn and scale when no clip moves it: 3, 4 and 3 floats
    std::vector<Clip> clips;
    int clipNamed(const char* name) const;   // -1 if there is none
};

struct Model {
    GLuint vao = 0, vbo = 0, ebo = 0;
    GLuint skinVbo = 0;   // a skinned model's joints and weights, 8 floats a vertex (4 joint numbers, then 4 weights)
    std::shared_ptr<const Skeleton> skeleton;
    bool hasPivot = false;   // a skinned model stands on its root joint's place, not the middle of its box (its spear and tail stick out)
    float pivotX = 0.0f, pivotZ = 0.0f;
    mutable GLuint instVao = 0;   // the same mesh with the renderer's per-instance data, made the first time it is instanced
    float minX = 0, minY = 0, minZ = 0;
    float maxX = 0, maxY = 0, maxZ = 0;
    std::vector<DrawRange> draws;
    std::vector<GLuint> textures;
    std::vector<float> positions;
    std::vector<GLuint> triangles;
    GLuint surface = 0;   // a building piece's material texture laid over it in world space (not owned), or 0
    // A coarser copy of a model with many triangles (the units and mobs), for when it is far from the camera: about a quarter of the
    // triangles, in vertex and index buffers of its own, with this model's textures (which it does not own). Empty for a model that
    // is light already.
    std::vector<Model> lods;
};

bool loadGLBModel(const char* path, Model& m);
// The matrices (column-major, 16 floats a joint, joint space to model space) for a skinned model's pose: clip `a` at time `ta`,
// blended toward clip `b` at `tb` by `blend` (0 is all `a`). A clip of -1 is the rest pose. Clips loop.
// Points vertex attributes 9 (joints) and 10 (weights) at the array buffer bound now: a skinned model's skinVbo
void setSkinLayout();
void poseSkeleton(const Skeleton& s, int clipA, float ta, int clipB, float tb, float blend, float* out);
// A model made from a mesh in memory: five floats a vertex (position, then texture coordinates), drawn with a texture that the caller owns (it is
// not freed with the model)
bool makeModelFromMesh(const std::vector<float>& verts, const std::vector<unsigned>& indices, GLuint texture, Model& m);
void destroyModel(Model& m);

struct ModelPlacement {
    float k = 0.0f;
    float cs = 1.0f, sn = 0.0f;
    float px = 0.0f, py = 0.0f, pz = 0.0f;
    float tx = 0.0f, ty = 0.0f, tz = 0.0f;
    float ox = 0.0f, oy = 0.0f, oz = 0.0f;
    bool ok = false;
};
ModelPlacement placeModel(const Model& m, float x, float y, float z, float targetSize, float yaw = 0.0f);

float rayHitModel(const Model& m, const ModelPlacement& p, const float o[3], const float d[3]);
// Where the ray meets the model's bounding box, its thinner side padded out to at least minSide (world units): a solid
// stand-in for clicking an open prop, such as a ladder, between its parts. -1 for a miss.
float rayHitModelBox(const Model& m, const ModelPlacement& p, const float o[3], const float d[3], float minSide);
float rayHitModelMatrix(const Model& m, const float matrix[16], const float o[3], const float d[3]);
