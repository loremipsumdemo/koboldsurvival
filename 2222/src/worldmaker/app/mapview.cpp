#include "app/mapview.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace wm {


namespace {

const V3 kDim{0.60f, 0.66f, 0.72f};
const V3 kBright{0.80f, 0.85f, 0.90f};
const V3 kAccent{0.0f, 0.86f, 1.0f};
const V3 kPanel{0.04f, 0.04f, 0.06f};

bool Inside(float px, float py, float x, float y, float w, float h) {
    return px >= x && px < x + w && py >= y && py < y + h;
}

// Heading in degrees: 0 is north (+Z), 90 is east (-X), and it grows clockwise as you turn right.
float Heading(float yawDeg) {
    float h = std::fmod(-yawDeg, 360.0f);
    if (h < 0.0f) h += 360.0f;
    return h;
}

// Signed difference b - a wrapped to [-180, 180).
float AngleDiff(float b, float a) {
    return std::fmod(b - a + 540.0f, 360.0f) - 180.0f;
}

// Map texture coordinates of a world position. Up is +Z and left is +X, so u and v grow
// to the right and down.
void MapUV(const World& w, float x, float z, float& u, float& v) {
    const float S = static_cast<float>(w.size);
    u = (S * 0.5f - x) / S;
    v = (S * 0.5f - z) / S;
}

// Draws the window of the world centerd on (cx, cz), wide tiles across, into the pixel square
// at (x, y). Outside the world the square is solid ocean, so the map never samples past its edge.
void DrawMapWindow(Renderer& r, const World& w, float x, float y, float size, float cx, float cz, float wide,
                   float winW, float winH) {
    const float S = static_cast<float>(w.size);
    const float half = wide * 0.5f;
    const V3 ocean{0.05f, 0.16f, 0.33f};

    r.BeginUI(static_cast<int>(winW), static_cast<int>(winH));
    r.Rect(x, y, size, size, ocean, 1.0f);
    r.EndUI();

    // The part of the window that is inside the world, in world units.
    const float x0 = std::max(cx - half, -S * 0.5f), x1 = std::min(cx + half, S * 0.5f);
    const float z0 = std::max(cz - half, -S * 0.5f), z1 = std::min(cz + half, S * 0.5f);
    if (x1 <= x0 || z1 <= z0) return;

    // Left is +X and up is +Z, so the window's left and top edges are its largest coordinates.
    const float ppt = size / wide;
    const float sxL = x + (cx + half - x1) * ppt;
    const float sxR = x + (cx + half - x0) * ppt;
    const float syT = y + (cz + half - z1) * ppt;
    const float syB = y + (cz + half - z0) * ppt;
    float u0, v0, u1, v1;
    MapUV(w, x1, z1, u0, v0);
    MapUV(w, x0, z0, u1, v1);

    r.BeginUI(static_cast<int>(winW), static_cast<int>(winH));
    r.DrawMap(sxL, syT, sxR - sxL, syB - syT, u0, v0, u1, v1, 1.0f);
    r.EndUI();
}

// Screen direction of the player's facing: right is -X and down is -Z.
void ScreenForward(const Player& p, float& sx, float& sy) {
    const V3 f = HorizontalForward(p.yaw);
    sx = -f.x;
    sy = -f.z;
}

}  // namespace

MapImage BuildMapImage(const World& w, int maxSide) {
    MapImage img;
    if (w.size <= 0 || w.height.empty()) return img;
    const int S = w.size;
    const int side = std::min(S, maxSide);
    img.side = side;
    img.rgba.assign(static_cast<size_t>(side) * side * 4, 255);
    const float stride = static_cast<float>(S) / side;
    const int d = std::max(1, static_cast<int>(stride + 0.5f));  // neighbor distance, in tiles

    auto tile = [&](int ix, int iz) {
        const int cx = std::clamp(ix, 0, S - 1);
        const int cz = std::clamp(iz, 0, S - 1);
        return static_cast<size_t>(cz) * S + cx;
    };

    for (int r = 0; r < side; ++r) {
        for (int c = 0; c < side; ++c) {
            // Column 0 is the east edge (large x), row 0 the north edge (large z).
            const int ix = S - 1 - static_cast<int>(c * stride + stride * 0.5f);
            const int iz = S - 1 - static_cast<int>(r * stride + stride * 0.5f);
            const size_t i = tile(ix, iz);
            const float h = w.height[i];

            V3 col;
            float shade = 1.0f;
            if (h < w.seaLevel) {
                const float depth = std::clamp((w.seaLevel - h) / 60.0f, 0.0f, 1.0f);
                col = V3{0.10f - 0.05f * depth, 0.28f - 0.12f * depth, 0.48f - 0.15f * depth};
            } else {
                col = BiomeColor(static_cast<Biome>(w.biome[i]));
                if (w.veg.size() == w.height.size()) {
                    // The same canopy tint the terrain mesh uses, so the flora shows on the map.
                    const float v = std::clamp(w.veg[i], 0.0f, 1.0f) * 0.45f;
                    col = V3{col.x + (0.20f - col.x) * v, col.y + (0.34f - col.y) * v, col.z + (0.13f - col.z) * v};
                }
                // Light from the north-west: slopes facing that way are brighter.
                const float hLeft = w.height[tile(ix + d, iz)];
                const float hRight = w.height[tile(ix - d, iz)];
                const float hUp = w.height[tile(ix, iz + d)];
                const float hDown = w.height[tile(ix, iz - d)];
                const float lit = ((hRight - hLeft) + (hDown - hUp)) / (2.0f * d);
                shade = std::clamp(1.0f + 0.25f * lit, 0.65f, 1.3f);
            }
            const size_t o = (static_cast<size_t>(r) * side + c) * 4;
            img.rgba[o + 0] = static_cast<uint8_t>(std::clamp(col.x * shade, 0.0f, 1.0f) * 255.0f);
            img.rgba[o + 1] = static_cast<uint8_t>(std::clamp(col.y * shade, 0.0f, 1.0f) * 255.0f);
            img.rgba[o + 2] = static_cast<uint8_t>(std::clamp(col.z * shade, 0.0f, 1.0f) * 255.0f);
            img.rgba[o + 3] = 255;
        }
    }
    return img;
}

void DrawCompass(Renderer& r, const Player& p, int winW, int winH) {
    static const char* const kNames[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    const float heading = Heading(p.yaw);
    const float W = static_cast<float>(winW);
    const float barW = std::min(1000.0f, W * 0.72f);
    const float barH = 66.0f;
    const float slant = 10.0f;                  // how far the sides lean in at the bottom
    const float x0 = (W - barW) * 0.5f;
    const float y0 = 16.0f;
    const float cx = x0 + barW * 0.5f;
    const float ppd = barW / 120.0f;            // about 120 degrees across the bar
    const float halfDeg = barW * 0.5f / ppd;
    const float cardScale = 0.72f, numScale = 0.5f;
    const float lineH = r.LineHeight();
    const V3 accent = kAccent;
    const V3 white{1.0f, 1.0f, 1.0f};
    const V3 bodyTop{0.05f, 0.14f, 0.30f};
    const V3 bodyBottom{0.02f, 0.07f, 0.16f};

    // The bar is fully opaque across its width; nothing fades at the ends.
    auto fadeAt = [](float) { return 1.0f; };

    r.BeginUI(winW, winH);

    // Body: rows that narrow towards the bottom, with a gradient and glowing sides.
    const int rowStep = 2;
    const int strips = 24;
    for (int row = 0; row < static_cast<int>(barH); row += rowStep) {
        const float t = static_cast<float>(row) / barH;
        const float left = x0 + slant * t;
        const float right = x0 + barW - slant * t;
        const V3 col{bodyTop.x + (bodyBottom.x - bodyTop.x) * t,
                     bodyTop.y + (bodyBottom.y - bodyTop.y) * t,
                     bodyTop.z + (bodyBottom.z - bodyTop.z) * t};
        const float sw = (right - left) / strips;
        for (int i = 0; i < strips; ++i) {
            const float sx = left + sw * static_cast<float>(i);
            const float f = fadeAt(sx + sw * 0.5f);
            if (f > 0.01f) r.Rect(sx, y0 + static_cast<float>(row), sw + 0.5f, static_cast<float>(rowStep), col, 0.85f * f);
        }
        r.Rect(left, y0 + static_cast<float>(row), 2.0f, static_cast<float>(rowStep), accent, 0.9f);
        r.Rect(right - 2.0f, y0 + static_cast<float>(row), 2.0f, static_cast<float>(rowStep), accent, 0.9f);
    }

    // Edges: a faint line across the top, a brighter one along the bottom.
    const int edge = 64;
    for (int i = 0; i < edge; ++i) {
        const float sw = barW / edge;
        const float sx = x0 + sw * static_cast<float>(i);
        r.Rect(sx, y0, sw + 0.5f, 1.5f, accent, 0.45f * fadeAt(sx + sw * 0.5f));
    }
    const float bl = x0 + slant, br = x0 + barW - slant;
    for (int i = 0; i < edge; ++i) {
        const float sw = (br - bl) / edge;
        const float sx = bl + sw * static_cast<float>(i);
        r.Rect(sx, y0 + barH - 2.0f, sw + 0.5f, 2.0f, accent, 0.9f * fadeAt(sx + sw * 0.5f));
    }

    // Ticks hang from the top; numbers and letters sit along the bottom.
    for (int b = 0; b < 360; b += 5) {
        const float d = AngleDiff(static_cast<float>(b), heading);
        if (std::fabs(d) > halfDeg + 5.0f) continue;
        const float x = cx + d * ppd;
        // Only draw inside the bar's own outline, never past its edges.
        if (x < x0 + 2.0f || x > x0 + barW - 2.0f) continue;
        const float f = fadeAt(x);
        if (f < 0.02f) continue;
        const bool major = b % 10 == 0;
        const bool cardinal = b % 45 == 0;
        const float tickH = cardinal ? 14.0f : major ? 11.0f : 6.0f;
        r.Rect(x - 0.5f, y0 + 4.0f, cardinal ? 2.5f : 1.5f, tickH, major ? kBright : kDim, f);
        if (!major) continue;
        const float scale = cardinal ? cardScale : numScale;
        const std::string label = cardinal ? kNames[b / 45] : std::to_string(b);
        const float tw = r.MeasureText(label) * scale;
        // The labels sit along the bottom, where the sides have leaned in, so keep them inside that.
        if (x - tw * 0.5f < x0 + slant + 2.0f || x + tw * 0.5f > x0 + barW - slant - 2.0f) continue;
        r.TextScaled(x - tw * 0.5f, y0 + barH - 4.0f - lineH * scale, label, white, f, scale);
    }

    // Pointer above the bar, pointing at the bar.
    for (int k = 0; k < 10; ++k) {
        const float w = 24.0f * (1.0f - static_cast<float>(k) / 10.0f) + 2.0f;
        r.Rect(cx - w * 0.5f, y0 - 12.0f + static_cast<float>(k), w, 1.0f, accent, 1.0f);
    }

    r.EndUI();
}

void DrawMinimap(Renderer& r, const World& w, const Player& p, int winW, int winH) {
    const float size = 220.0f;
    const float span = 900.0f;  // tiles across the minimap
    const float x0 = static_cast<float>(winW) - size - 16.0f;
    const float y0 = 16.0f;
    const float cx = p.pos.x, cz = p.pos.z;

    r.BeginUI(winW, winH);
    r.Rect(x0 - 2.0f, y0 - 2.0f, size + 4.0f, size + 4.0f, V3{0.0f, 0.0f, 0.0f}, 0.8f);
    r.EndUI();

    DrawMapWindow(r, w, x0, y0, size, cx, cz, span, static_cast<float>(winW), static_cast<float>(winH));

    r.BeginUI(winW, winH);
    const float mx = x0 + size * 0.5f, my = y0 + size * 0.5f;
    float sx, sy;
    ScreenForward(p, sx, sy);
    for (int t = 4; t <= 16; t += 3) r.Rect(mx + sx * t - 1.5f, my + sy * t - 1.5f, 3.0f, 3.0f, kAccent, 1.0f);
    r.Rect(mx - 4.0f, my - 4.0f, 8.0f, 8.0f, kBright, 1.0f);
    r.EndUI();
}

void DrawFullMap(Renderer& r, const World& w, const Player& p, MapView& mv, const Mouse& mouse, int winW, int winH) {
    const float W = static_cast<float>(winW), H = static_cast<float>(winH);
    const float S = static_cast<float>(w.size);
    const float side = std::min(W, H) * 0.86f;
    const float x0 = (W - side) * 0.5f, y0 = (H - side) * 0.5f;
    const float btnW = 210.0f, btnH = 34.0f;
    const float bx = W - btnW - 16.0f, by = 16.0f;
    const bool overButton = Inside(mouse.x, mouse.y, bx, by, btnW, btnH);
    const bool overMap = Inside(mouse.x, mouse.y, x0, y0, side, side);

    // Focus toggle.
    if (mouse.pressed && overButton) {
        mv.focusSelf = !mv.focusSelf;
        mv.cx = p.pos.x;
        mv.cz = p.pos.z;
        mv.dragging = false;
    }

    // Zoom: about the player in self focus, about the cursor in mouse focus.
    const float oldW = S / mv.zoom;
    if (mouse.wheel != 0.0f && overMap) {
        mv.zoom = std::clamp(mv.zoom * std::pow(1.25f, mouse.wheel), 1.0f, 64.0f);
        if (!mv.focusSelf) {
            // Keep the world point under the cursor where it was.
            const float newW = S / mv.zoom;
            const float fx = (mouse.x - x0) / side, fy = (mouse.y - y0) / side;
            const float wx = mv.cx + (0.5f - fx) * oldW;
            const float wz = mv.cz + (0.5f - fy) * oldW;
            mv.cx = wx - (0.5f - fx) * newW;
            mv.cz = wz - (0.5f - fy) * newW;
        }
    }

    // Drag to pan, in mouse focus only. The map follows the cursor.
    const float dx = mouse.x - mv.lastX, dy = mouse.y - mv.lastY;
    mv.lastX = mouse.x;
    mv.lastY = mouse.y;
    if (mouse.pressed && overMap && !overButton && !mv.focusSelf) mv.dragging = true;
    if (!mouse.down) mv.dragging = false;
    const float unitsPerPixel = (S / mv.zoom) / side;
    if (mv.dragging) {
        mv.cx += dx * unitsPerPixel;
        mv.cz += dy * unitsPerPixel;
    }

    // The view center, kept so the window stays on the map.
    float cx = mv.focusSelf ? p.pos.x : mv.cx;
    float cz = mv.focusSelf ? p.pos.z : mv.cz;
    const float wide = S / mv.zoom;
    const float half = wide * 0.5f;
    if (wide < S) {
        cx = std::clamp(cx, -S * 0.5f + half, S * 0.5f - half);
        cz = std::clamp(cz, -S * 0.5f + half, S * 0.5f - half);
    } else {
        cx = 0.0f;
        cz = 0.0f;
    }
    mv.cx = cx;
    mv.cz = cz;

    r.BeginUI(winW, winH);
    r.Rect(0.0f, 0.0f, W, H, V3{0.0f, 0.0f, 0.0f}, 0.6f);
    r.EndUI();

    DrawMapWindow(r, w, x0, y0, side, cx, cz, wide, W, H);

    r.BeginUI(winW, winH);
    // Frame, player marker with a heading line, focus button and hint.
    r.Rect(x0 - 2.0f, y0 - 2.0f, side + 4.0f, 2.0f, kDim, 1.0f);
    r.Rect(x0 - 2.0f, y0 + side, side + 4.0f, 2.0f, kDim, 1.0f);
    r.Rect(x0 - 2.0f, y0, 2.0f, side, kDim, 1.0f);
    r.Rect(x0 + side, y0, 2.0f, side, kDim, 1.0f);

    const float ppt = side / wide;
    const float px = x0 + side * 0.5f - (p.pos.x - cx) * ppt;
    const float py = y0 + side * 0.5f - (p.pos.z - cz) * ppt;
    if (Inside(px, py, x0, y0, side, side)) {
        float sx, sy;
        ScreenForward(p, sx, sy);
        for (int t = 4; t <= 16; t += 3) r.Rect(px + sx * t - 1.5f, py + sy * t - 1.5f, 3.0f, 3.0f, kAccent, 1.0f);
        r.Rect(px - 4.0f, py - 4.0f, 8.0f, 8.0f, kBright, 1.0f);
    }

    r.Rect(bx, by, btnW, btnH, overButton ? V3{0.14f, 0.16f, 0.20f} : V3{0.08f, 0.09f, 0.12f}, 0.9f);
    const std::string label = mv.focusSelf ? "Focus: self" : "Focus: mouse";
    r.Text(bx + (btnW - r.MeasureText(label)) * 0.5f, by + (btnH - r.LineHeight()) * 0.5f, label, kAccent);

    char zoomText[64];
    std::snprintf(zoomText, sizeof zoomText, "%.1fx zoom", mv.zoom);
    r.Text(x0, y0 + side + 10.0f,
           std::string(zoomText) + "    scroll: zoom    drag: move the view (mouse focus)    release M to close",
           kDim);
    r.EndUI();
}

}  // namespace wm
