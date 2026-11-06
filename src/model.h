#pragma once
#include <vector>
#include <GL/glew.h>

struct DrawRange {
    GLuint texture = 0;
    GLsizei indexCount = 0;
    GLsizei firstIndex = 0;
};

struct Model {
    GLuint vao = 0, vbo = 0, ebo = 0;
    float minX = 0, minY = 0, minZ = 0;
    float maxX = 0, maxY = 0, maxZ = 0;
    std::vector<DrawRange> draws;
    std::vector<GLuint> textures;
    std::vector<float> positions;
    std::vector<GLuint> triangles;
};

bool loadGLBModel(const char* path, Model& m);
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
float rayHitModelMatrix(const Model& m, const float matrix[16], const float o[3], const float d[3]);
