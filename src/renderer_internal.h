#pragma once
// Shared by the renderer_*.cpp files, and only by them: constants, vertex types and helpers more than one of them
// uses. What only one file uses stays in that file.
#include "renderer.h"
#include "combat.h"
#include "components.h"
#include "party_ui.h"
#include "terrain.h"
#include "ui_icons.h"
#include "ui_layout.h"
#include "language.h"
#include "loading.h"
#include "terrain_mesh.h"
#include "gl_util.h"
#include "equip.h"
#include "factions.h"
#include "inventory.h"
#include "items.h"
#include "mobs.h"
#include "pak.h"
#include "party.h"
#include "systems.h"
#include "build.h"
#include "claims.h"
#include "textcase.h"
#include <SDL3_image/SDL_image.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace render_detail {

inline constexpr float GLYPH_H = 1.2f;          // world height of unit sprites
inline constexpr float CARRIED_LIFT = 0.55f;    // how high a carried unit lies, over its carrier's shoulders
inline constexpr float CARRIED_MODEL_LIFT = 0.95f;   // the same for a unit drawn by its model
inline constexpr float ITEM_GLYPH_H = GLYPH_H * 0.5f;   // world height of an item sprite on the ground: half a unit's

inline constexpr RGBA WHITE{ 1.0f, 1.0f, 1.0f, 1.0f };

// Where an item's sprite is on screen: the center x, the top and bottom, and half its width
struct ItemOnScreen {
    float cx = 0.0f, top = 0.0f, bottom = 0.0f, halfW = 0.0f;
};

// An item on the ground stands in the world as a square picture ITEM_GLYPH_H high, half a unit's height, so it grows
// and shrinks with the zoom like the units. aspect is the picture's width over its height (1 for an item).
bool itemOnScreen(const Mat4& vp, int W, int H, const Transform& tr, float aspect, ItemOnScreen& out);

struct V3 { float x, y, z; };
struct Vertex { V3 p; RGBA c; };         // position, then color

// Colored triangles rebuilt every frame (selection rings, drag box)
struct Batch {
    std::vector<Vertex> v;

    void tri(V3 a, V3 b, V3 c, RGBA col) {
        v.push_back({ a, col });
        v.push_back({ b, col });
        v.push_back({ c, col });
    }

    void quad(V3 a, V3 b, V3 c, V3 d, RGBA col) {
        tri(a, b, c, col);
        tri(a, c, d, col);
    }
};

// A texture from RGBA pixels, clamped at its edges and filtered linearly (or by filter)
GLuint MakeRgbaTexture(int w, int h, const uint8_t* rgba, GLint filter = GL_LINEAR);

// A round green dot with a black outline, 16 pixels across, for the units on the maps
GLuint MakeDotTexture(bool white = false);

// How gray a unit is drawn: half for one Unconscious, fully for a corpse, not at all otherwise
float unitSaturation(World& w, Entity u);

// a blended toward b by t (0 to 1), and a colour from 0-255 channels
RGBA mixColor(RGBA a, RGBA b, float t);
RGBA rgb255(int r, int g, int b, float a);

// 1234567 as "1,234,567"
std::string withCommas(long long n);

// A shape with its bottom right corner cut off at 45 degrees, filled top to bottom from top to bottom colour
void cutCornerShape(std::vector<UiVert>& v, const Rect& r, float cut, RGBA top, RGBA bottom);

// The color of a damage number or log line: green for a player unit's, red for a hostile's, gray otherwise
RGBA logTint(Tint t);

} // namespace render_detail
