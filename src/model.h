#pragma once
#include <GL/glew.h>
#include <vector>

struct DrawRange {
    GLsizei firstIndex;   // offset into the index buffer (in indices, not bytes)
    GLsizei indexCount;
    GLuint  texture;      // base colour texture for this primitive
};

struct Model {
    GLuint vao = 0, vbo = 0, ebo = 0;
    std::vector<DrawRange> draws;

    // Bounds in model space, after node transforms are applied
    float minX = 0, minY = 0, minZ = 0;
    float maxX = 0, maxY = 0, maxZ = 0;
};

// Loads a .glb file into GPU buffers. Returns false on failure.
bool loadGLBModel(const char* path, Model& out);

// Frees GPU resources and resets the struct.
void destroyModel(Model& m);