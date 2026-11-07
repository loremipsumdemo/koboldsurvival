#pragma once
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
#include <cmath>
#include <random>
#include <string>
#include <vector>

namespace render_detail {
inline constexpr float GLYPH_H = 1.2f;
inline constexpr float CARRIED_LIFT = 0.55f;
inline constexpr float CARRIED_MODEL_LIFT = 0.95f;
inline constexpr float ITEM_GLYPH_H = GLYPH_H * 0.5f;

// How an item on the ground shows it is an item: it floats up and down over a round shadow, and a white shine crosses it
// every few seconds. Each item is at its own point in these loops. This is only how it is drawn: its place does not change.
inline constexpr float ITEM_BOB_LOW = 0.05f, ITEM_BOB_HIGH = 0.17f;
inline constexpr double ITEM_BOB_SECONDS = 2.4;
inline constexpr double ITEM_SHINE_EVERY_SECONDS = 4.0;
inline constexpr double ITEM_SHINE_SECONDS = 0.6;
inline constexpr float ITEM_SHADOW_RADIUS = 0.2f;
inline constexpr float ITEM_SHADOW_ALPHA = 0.45f;

inline double itemLoopPlace(Entity e, unsigned long long ticks, double loopSeconds) {
    const double at = (double)ticks / 1000.0 / loopSeconds + (double)e * 0.61803398875;
    return at - std::floor(at);
}
// From 0 at the bottom of the float to 1 at the top
inline float itemBobShare(Entity e, unsigned long long ticks) {
    return 0.5f + 0.5f * (float)std::sin(6.283185307179586 * itemLoopPlace(e, ticks, ITEM_BOB_SECONDS));
}
inline float itemBobLift(Entity e, unsigned long long ticks) {
    return ITEM_BOB_LOW + (ITEM_BOB_HIGH - ITEM_BOB_LOW) * itemBobShare(e, ticks);
}
// How far the shine has crossed the item, from just above 0 to 1, or 0 between shines
inline float itemShineShare(Entity e, unsigned long long ticks) {
    const double seconds = itemLoopPlace(e, ticks, ITEM_SHINE_EVERY_SECONDS) * ITEM_SHINE_EVERY_SECONDS;
    return seconds < ITEM_SHINE_SECONDS ? (float)(seconds / ITEM_SHINE_SECONDS) : 0.0f;
}

inline constexpr RGBA WHITE{ 1.0f, 1.0f, 1.0f, 1.0f };

struct ItemOnScreen {
    float cx = 0.0f, top = 0.0f, bottom = 0.0f, halfW = 0.0f;
};

bool itemOnScreen(const Mat4& vp, int W, int H, const Transform& tr, float aspect, ItemOnScreen& out);

struct V3 { float x, y, z; };
struct Vertex { V3 p; RGBA c; };

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

GLuint MakeRgbaTexture(int w, int h, const uint8_t* rgba, GLint filter = GL_LINEAR);
TextTex loadOptionalImage(const char* path);
void addMipmaps(const TextTex& t);
bool loadDrawableModel(const char* path, Model& m, bool needsHeight);

GLuint MakeDotTexture(bool white = false);

float unitSaturation(World& w, Entity u);

RGBA mixColor(RGBA a, RGBA b, float t);
RGBA rgb255(int r, int g, int b, float a);

std::string withCommas(long long n);

void cutCornerShape(std::vector<UiVert>& v, const Rect& r, float cut, RGBA top, RGBA bottom);

RGBA logTint(Tint t);
}
