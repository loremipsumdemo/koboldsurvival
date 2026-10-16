#pragma once
#include <vector>
#include <GL/glew.h>
#include "model.h"

// ---- Spatial data ----
struct GridPos { int x = 0, y = 0; };

// Visual world position on the XZ plane (interpolated between tiles)
struct Transform { float x = 0.0f, z = 0.0f; };

// Interpolation from one tile center to the next
struct Segment {
    float startX = 0.0f, startZ = 0.0f;
    float endX = 0.0f, endZ = 0.0f;
    float t = 1.0f;
};

// Queued tile-by-tile route plus the timer that paces steps
struct Path {
    std::vector<GridPos> steps;
    float accum = 0.0f;
};

// ---- Tags and small state ----
struct Player { bool hasKey = false; };
struct Key {};
struct Door {};
struct Wagon {};
struct Floor {};

// Destroyed and respawned when a new level is generated
struct LevelEntity {};

// ---- Rendering ----
struct TextTex {
    GLuint tex = 0;
    float aspect = 1.0f;   // width / height of the rendered glyph
};

struct Appearance {
    TextTex glyph;                        // used in ASCII mode
    GLuint spriteTex = 0;                 // used in sprite mode
    float spriteW = 1.2f, spriteH = 1.4f;
    const Model* model = nullptr;         // optional 3D model (sprite mode only)
    float modelSize = 0.9f;
};
