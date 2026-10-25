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
};

bool loadGLBModel(const char* path, Model& m);
void destroyModel(Model& m);
