#include "app/menu.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <random>
#include <string>

#include "core/debug_options.hpp"
#include "app/desktop.hpp"
#include "core/render_options.hpp"
#include "core/tunables.hpp"
#include "theme/themefile.hpp"
#include "world/world.hpp"
#include "world/worldfile.hpp"

namespace wm {


namespace {

const V3 kPanel{0.06f, 0.07f, 0.10f};
const V3 kButton{0.14f, 0.17f, 0.22f};
const V3 kButtonHot{0.22f, 0.28f, 0.37f};
const V3 kButtonOff{0.10f, 0.10f, 0.12f};
const V3 kAccent{0.0f, 0.86f, 1.0f};
const V3 kText{0.85f, 0.90f, 0.95f};
const V3 kTextDim{0.45f, 0.48f, 0.52f};
const V3 kTrack{0.16f, 0.18f, 0.22f};
const V3 kAccentHot{0.30f, 0.92f, 1.0f};
const V3 kInk{0.02f, 0.04f, 0.06f};            // text on accent-filled buttons
const V3 kHeading{0.0f, 0.66f, 0.78f};         // section headings
const V3 kTabHot{0.66f, 0.72f, 0.78f};         // tab under the pointer
const float kPad = 28.0f;                       // left and right margin inside the panel

enum Tab { kWorld = 0, kGeneration, kThemes, kWalker, kDebug, kFiles, kTabCount };
const char* const kTabNames[kTabCount] = {"World", "Generation", "Themes", "Walker", "Debug", "Files"};

struct Ctx {
    Renderer& r;
    MenuState& m;
    Session& s;
    Look& look;
    const Mouse& mouse;
    float x0, y0, pw, ph;
    float lh;          // font line height
    float rowH;        // vertical step between rows
    float btnH;        // button height
    bool hitField;     // a text field was clicked this frame
    float contentBottom = 0.0f;  // lowest y the tab content may reach
    bool clip = false;           // scrolling tab: rows are drawn only inside [clipTop, clipBottom]
    float clipTop = 0.0f;
    float clipBottom = 0.0f;
    float lowest = 0.0f;         // lowest y any row of the tab reached this frame
};


bool Inside(float px, float py, float x, float y, float w, float h) {
    return px >= x && px < x + w && py >= y && py < y + h;
}

// True when a row is drawn and clickable this frame. A scrolling tab shows only rows that
// fit completely inside its content area, and records how far down its rows go.
bool Visible(Ctx& c, float y, float h) {
    if (!c.clip) return true;
    c.lowest = std::max(c.lowest, y + h);
    return y >= c.clipTop && y + h <= c.clipBottom;
}

// Filled rectangle with centerd label. Returns true on a click.
// Primary buttons are filled with the accent color and are the default action of a panel.
bool Button(Ctx& c, float x, float y, float w, const std::string& label, bool enabled = true, bool primary = false) {
    const float h = c.btnH;
    if (!Visible(c, y, h)) return false;
    const bool hot = enabled && Inside(c.mouse.x, c.mouse.y, x, y, w, h);
    V3 fill = !enabled ? kButtonOff : hot ? kButtonHot : kButton;
    V3 ink = enabled ? kText : kTextDim;
    if (primary) {
        fill = !enabled ? kButtonOff : hot ? kAccentHot : kAccent;
        ink = enabled ? kInk : kTextDim;
    }
    c.r.Rect(x, y, w, h, fill, 1.0f);
    const float tw = c.r.MeasureText(label);
    c.r.Text(x + std::max(0.0f, (w - tw) * 0.5f), y + (h - c.lh) * 0.5f, label, ink);
    return hot && c.mouse.pressed;
}

// Top-level tab: text with an accent underline when selected.
bool TabButton(Ctx& c, float x, float y, float w, float h, const char* label, bool selected) {
    const bool hot = Inside(c.mouse.x, c.mouse.y, x, y, w, h);
    const float tw = c.r.MeasureText(label);
    const V3 ink = selected ? kText : hot ? kTabHot : kTextDim;
    c.r.Text(x + (w - tw) * 0.5f, y + (h - c.lh) * 0.5f, label, ink);
    if (selected) c.r.Rect(x, y + h - 2.0f, w, 2.0f, kAccent, 1.0f);
    return hot && c.mouse.pressed;
}

// Shortens text with an ellipsis so it fits in maxW pixels.
std::string FitText(Renderer& r, std::string text, float maxW) {
    if (r.MeasureText(text) <= maxW) return text;
    while (!text.empty() && r.MeasureText(text + "...") > maxW) text.pop_back();
    return text + "...";
}

// Text field. Clicking it makes it the target of keyboard input.
void TextField(Ctx& c, float x, float y, float w, std::string& text) {
    const float h = c.btnH;
    if (!Visible(c, y, h)) return;
    const bool active = c.m.activeField == &text;
    c.r.Rect(x, y, w, h, active ? kButtonHot : kTrack, 1.0f);
    if (active) c.r.Rect(x, y + h - 3, w, 3, kAccent, 1.0f);

    std::string shown = text;
    if (active && (SDL_GetTicks() / 500) % 2 == 0) shown += "_";
    while (!shown.empty() && c.r.MeasureText(shown) > w - 16) shown.erase(0, 1);
    c.r.Text(x + 8, y + (h - c.lh) * 0.5f, shown, kText);

    if (c.mouse.pressed && Inside(c.mouse.x, c.mouse.y, x, y, w, h)) {
        c.m.activeField = &text;
        c.hitField = true;
    }
}

// Horizontal slider bound directly to a float. Returns true when the value changed.
static const void* SliderKey(const SliderTarget& t) {
    return t.f ? static_cast<const void*>(t.f) : static_cast<const void*>(t.i);
}

static float SliderGet(const SliderTarget& t) {
    return t.f ? *t.f : static_cast<float>(*t.i);
}

static void SliderSet(const SliderTarget& t, float v) {
    if (t.f) *t.f = v;
    else *t.i = static_cast<int>(std::lround(v));
}

static std::string SliderFormat(float v, float step) {
    char buf[32];
    if (step >= 1.0f) std::snprintf(buf, sizeof buf, "%.0f", v);
    else              std::snprintf(buf, sizeof buf, "%.2f", v);
    return buf;
}

// Value box for a number. Click it to type a value; Enter applies it, Escape or a click away cancels.
static void ValueBox(Ctx& c, float x, float y, float w, SliderTarget t, float lo, float hi, float step) {
    const float h = c.btnH;
    const void* key = SliderKey(t);
    if (c.m.activeField == &c.m.sliderEdit.text && c.m.sliderEdit.key == key) {
        c.r.Rect(x, y, w, h, kButtonHot, 1.0f);
        c.r.Rect(x, y + h - 3, w, 3, kAccent, 1.0f);
        std::string shown = c.m.sliderEdit.text;
        if ((SDL_GetTicks() / 500) % 2 == 0) shown += "_";
        c.r.Text(x + 8, y + (h - c.lh) * 0.5f, shown, kText);
        return;
    }
    const bool hot = Inside(c.mouse.x, c.mouse.y, x, y, w, h);
    c.r.Rect(x, y, w, h, hot ? kButtonHot : kTrack, 1.0f);
    c.r.Text(x + 8, y + (h - c.lh) * 0.5f, SliderFormat(SliderGet(t), step), kText);
    if (c.mouse.pressed && hot) {
        SliderEdit& e = c.m.sliderEdit;
        e = SliderEdit{};
        e.key = key;
        e.target = t;
        e.lo = lo;
        e.hi = hi;
        e.step = step;
        c.m.activeField = &e.text;
        c.hitField = true;
    }
}

// Horizontal slider with a value box at its right. Click the box to type a value; Enter applies it.
static bool SliderImpl(Ctx& c, float y, const char* label, SliderTarget t, float lo, float hi, float step) {
    const float tx = c.x0 + c.pw * 0.42f;
    const float tw = c.pw * 0.34f;
    const float trackY = y + c.lh * 0.5f - 4.0f;
    const float boxX = tx + tw + 20.0f;
    const float boxW = 84.0f;
    const void* key = SliderKey(t);
    const bool editing = c.m.activeField == &c.m.sliderEdit.text && c.m.sliderEdit.key == key;

    if (!c.mouse.down) c.m.dragging = nullptr;
    if (!Visible(c, y, c.btnH)) return false;

    c.r.Text(c.x0 + kPad, y, label, kText);

    if (c.mouse.pressed && !editing && Inside(c.mouse.x, c.mouse.y, tx - 12, y, tw + 24, c.lh)) {
        c.m.dragging = key;
    }

    bool changed = false;
    if (c.m.dragging == key) {
        const float pos = std::clamp((c.mouse.x - tx) / tw, 0.0f, 1.0f);
        float nv = lo + pos * (hi - lo);
        if (step > 0.0f) nv = std::round(nv / step) * step;
        nv = std::clamp(nv, lo, hi);
        if (nv != SliderGet(t)) {
            SliderSet(t, nv);
            changed = true;
        }
    }

    const float cur = SliderGet(t);
    const float frac = std::clamp((cur - lo) / (hi - lo), 0.0f, 1.0f);
    c.r.Rect(tx, trackY, tw, 8.0f, kTrack, 1.0f);
    c.r.Rect(tx, trackY, tw * frac, 8.0f, kAccent, 1.0f);
    c.r.Rect(tx + tw * frac - 6.0f, y, 12.0f, c.lh, kAccent, 1.0f);

    ValueBox(c, boxX, y, boxW, t, lo, hi, step);
    return changed;
}

// Slider bound to a float. Returns true when the value changed by dragging.
bool Slider(Ctx& c, float y, const char* label, float* v, float lo, float hi, float step) {
    return SliderImpl(c, y, label, SliderTarget{v, nullptr}, lo, hi, step);
}

// Slider bound to an int, in whole steps. Returns true when the value changed by dragging.
bool SliderInt(Ctx& c, float y, const char* label, int* v, float lo, float hi) {
    return SliderImpl(c, y, label, SliderTarget{nullptr, v}, lo, hi, 1.0f);
}

// One line of body text.
void Label(Ctx& c, float y, const std::string& s, V3 col = kText) {
    if (!Visible(c, y, c.lh)) return;
    c.r.Text(c.x0 + kPad, y, s, col);
}

// Section heading in the accent color.
void Heading(Ctx& c, float y, const char* s) {
    if (!Visible(c, y, c.rowH)) return;
    std::string t = s;
    for (char& ch : t) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    c.r.Text(c.x0 + kPad, y, t, kHeading);
    c.r.Rect(c.x0 + kPad, y + c.lh + 6.0f, c.pw - 2.0f * kPad, 1.0f, kTrack, 1.0f);
}

// Color helpers. Colors are kept as RGB 0 to 1; the picker edits them in HSV.
constexpr float kPi = 3.14159265f;

void HsvToRgb(float h, float s, float v, float* out) {
    h = std::fmod(h, 360.0f);
    if (h < 0.0f) h += 360.0f;
    const float c = v * s;
    const float x = c * (1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
    const float m = v - c;
    float r = 0.0f, g = 0.0f, b = 0.0f;
    if (h < 60.0f)       { r = c; g = x; }
    else if (h < 120.0f) { r = x; g = c; }
    else if (h < 180.0f) { g = c; b = x; }
    else if (h < 240.0f) { g = x; b = c; }
    else if (h < 300.0f) { r = x; b = c; }
    else                 { r = c; b = x; }
    out[0] = r + m;
    out[1] = g + m;
    out[2] = b + m;
}

void RgbToHsv(const float* rgb, float& h, float& s, float& v) {
    const float mx = std::max({rgb[0], rgb[1], rgb[2]});
    const float mn = std::min({rgb[0], rgb[1], rgb[2]});
    const float d = mx - mn;
    v = mx;
    s = mx > 0.0f ? d / mx : 0.0f;
    if (d <= 0.0f) {          // gray: hue has no meaning, keep it at 0
        h = 0.0f;
        return;
    }
    float hh;
    if (mx == rgb[0])      hh = 60.0f * std::fmod((rgb[1] - rgb[2]) / d, 6.0f);
    else if (mx == rgb[1]) hh = 60.0f * ((rgb[2] - rgb[0]) / d + 2.0f);
    else                   hh = 60.0f * ((rgb[0] - rgb[1]) / d + 4.0f);
    if (hh < 0.0f) hh += 360.0f;
    h = hh;
}

std::string FormatHex(const float* rgb) {
    auto byte = [](float f) { return static_cast<int>(std::lround(std::clamp(f, 0.0f, 1.0f) * 255.0f)); };
    char buf[12];
    std::snprintf(buf, sizeof buf, "#%02X%02X%02X", byte(rgb[0]), byte(rgb[1]), byte(rgb[2]));
    return buf;
}

// Reads #RRGGBB or RRGGBB. Returns false when the text is not a color.
bool ParseHex(std::string text, float* rgb) {
    if (!text.empty() && text[0] == '#') text.erase(0, 1);
    if (text.size() != 6) return false;
    for (int i = 0; i < 3; ++i) {
        const std::string part = text.substr(static_cast<std::size_t>(2 * i), 2);
        for (char ch : part) {
            if (!std::isxdigit(static_cast<unsigned char>(ch))) return false;
        }
        rgb[i] = static_cast<float>(std::strtol(part.c_str(), nullptr, 16)) / 255.0f;
    }
    return true;
}

// Pushes the picker's HSV and opacity into the color it edits.
void ApplyPicker(ColorPicker& p) {
    if (!p.rgb) return;
    float col[3];
    HsvToRgb(p.h, p.s, p.v, col);
    p.rgb[0] = col[0];
    p.rgb[1] = col[1];
    p.rgb[2] = col[2];
    if (p.opacity) *p.opacity = p.alpha;
}

void OpenPicker(MenuState& m, const char* label, float* rgb, float* opacity) {
    ColorPicker& p = m.picker;
    p = ColorPicker{};
    p.open = true;
    p.fresh = true;
    p.label = label;
    p.rgb = rgb;
    p.opacity = opacity;
    RgbToHsv(rgb, p.h, p.s, p.v);
    p.alpha = std::clamp(*opacity, 0.0f, 1.0f);
    p.hex = FormatHex(rgb);
    m.activeField = nullptr;
    m.dragging = nullptr;
    m.sliderEdit = SliderEdit{};
}

// One color: swatch, hex code and opacity. Click any of them to open the picker.
void ColorRow(Ctx& c, float y, const char* label, float* rgb, float* opacity) {
    if (!Visible(c, y, c.btnH)) return;
    const float tx = c.x0 + c.pw * 0.42f;
    const float swW = 72.0f, hexX = tx + swW + 12.0f, hexW = 190.0f;
    const float opX = hexX + hexW + 10.0f, opW = 110.0f;
    const float a = std::clamp(*opacity, 0.0f, 1.0f);
    const bool hot = Inside(c.mouse.x, c.mouse.y, tx, y, opX + opW - tx, c.btnH);

    c.r.Text(c.x0 + kPad, y + (c.btnH - c.lh) * 0.5f, label, kText);

    // The swatch shows the color at its opacity over the panel, so the opacity can be judged by eye.
    const V3 shown{kPanel.x * (1.0f - a) + rgb[0] * a,
                   kPanel.y * (1.0f - a) + rgb[1] * a,
                   kPanel.z * (1.0f - a) + rgb[2] * a};
    c.r.Rect(tx, y, swW, c.btnH, kTrack, 1.0f);
    c.r.Rect(tx + 2.0f, y + 2.0f, swW - 4.0f, c.btnH - 4.0f, shown, 1.0f);

    c.r.Rect(hexX, y, hexW, c.btnH, hot ? kButtonHot : kTrack, 1.0f);
    c.r.Text(hexX + 10.0f, y + (c.btnH - c.lh) * 0.5f, FormatHex(rgb), kText);

    c.r.Rect(opX, y, opW, c.btnH, hot ? kButtonHot : kTrack, 1.0f);
    char pct[16];
    std::snprintf(pct, sizeof pct, "%d%%", static_cast<int>(std::lround(a * 100.0f)));
    const float pw = c.r.MeasureText(pct);
    c.r.Text(opX + (opW - pw) * 0.5f, y + (c.btnH - c.lh) * 0.5f, pct, kText);

    if (hot && c.mouse.pressed) OpenPicker(c.m, label, rgb, opacity);
}

// Left edge and width of the column that holds text fields and sliders.
float FieldX(const Ctx& c) { return c.x0 + c.pw * 0.42f; }
float FieldW(const Ctx& c) { return c.pw * 0.34f; }

void ApplyPreset(Look& look, int which) {
    switch (which) {
        case 0:   // clear day
            look.sunRgb[0] = 1.0f;  look.sunRgb[1] = 0.96f; look.sunRgb[2] = 0.82f;
            look.skyTop[0] = 0.22f; look.skyTop[1] = 0.42f; look.skyTop[2] = 0.80f;
            look.skyHorizon[0] = 0.72f; look.skyHorizon[1] = 0.84f; look.skyHorizon[2] = 0.94f;
            look.sunStrength = 0.9f;
            look.fog = 1.0f;
            break;
        case 1:   // sunset
            look.clock = 18.0f;
            look.sunRgb[0] = 1.0f;  look.sunRgb[1] = 0.55f; look.sunRgb[2] = 0.25f;
            look.skyTop[0] = 0.25f; look.skyTop[1] = 0.28f; look.skyTop[2] = 0.55f;
            look.skyHorizon[0] = 0.95f; look.skyHorizon[1] = 0.52f; look.skyHorizon[2] = 0.32f;
            look.sunStrength = 1.0f;
            look.fog = 1.4f;
            break;
        case 2:   // overcast
            look.sunRgb[0] = 0.85f; look.sunRgb[1] = 0.87f; look.sunRgb[2] = 0.90f;
            look.skyTop[0] = 0.45f; look.skyTop[1] = 0.48f; look.skyTop[2] = 0.52f;
            look.skyHorizon[0] = 0.70f; look.skyHorizon[1] = 0.72f; look.skyHorizon[2] = 0.75f;
            look.sunStrength = 0.45f;
            look.fog = 1.6f;
            break;
        case 3:   // night
            look.clock = 23.0f;
            look.moonStrength = 0.9f;
            look.starAmount = 1.2f;
            look.sunRgb[0] = 0.55f; look.sunRgb[1] = 0.62f; look.sunRgb[2] = 0.85f;
            break;
        default:
            look = Look{};
            break;
    }
}

// ---------------------------------------------------------------------------
// Tabs
// ---------------------------------------------------------------------------

void Toggle(Ctx& c, float x, float y, float w, const char* label, bool& value);

// Terrain settings, split over two tabs. Each slider edits the generator's settings;
// the next world generated uses them.
void RandomizeTerrain(GenParams& p) {
    std::mt19937 rng(std::random_device{}());
    auto uni = [&rng](float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(rng); };
    auto coin = [&rng](float chance) { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng) < chance; };
    p.waterShare = uni(0.30f, 0.55f);
    p.relief = uni(0.7f, 1.5f);
    p.rangeCover = uni(0.3f, 0.7f);
    p.plainShare = uni(0.0f, 0.5f);
    p.plainLevel = uni(0.03f, 0.10f);
    p.erosion = uni(0.3f, 0.9f);
    p.passes = static_cast<int>(uni(16.0f, 36.0f));
    p.thermal = uni(0.0f, 0.6f);
    p.rankedClimate = coin(0.7f);
    p.warmth = uni(-0.1f, 0.1f);
    p.wetness = uni(-0.1f, 0.15f);
    p.lowlands = uni(0.1f, 0.6f);
    p.rugged = coin(0.4f);
    p.detail = uni(0.4f, 0.9f);
    p.steep = uni(0.0f, 0.6f);
    p.beach = uni(3.0f, 8.0f);
    p.shelf = uni(6.0f, 14.0f);
    p.cover = uni(0.5f, 0.9f);
}

void LandTab(Ctx& c, float top) {
    GenParams& p = c.m.gen.p;
    float y = top;
    Slider(c, y, "Water share", &p.waterShare, 0.05f, 0.95f, 0.01f);        y += c.rowH;
    Slider(c, y, "Mountain relief", &p.relief, 0.0f, 2.0f, 0.01f);          y += c.rowH;
    Slider(c, y, "Mountain cover", &p.rangeCover, 0.0f, 1.0f, 0.01f);       y += c.rowH;
    Slider(c, y, "Share of plains", &p.plainShare, 0.0f, 1.0f, 0.01f);      y += c.rowH;
    Slider(c, y, "Plains height", &p.plainLevel, 0.0f, 0.5f, 0.01f);        y += c.rowH;
    Slider(c, y, "Water erosion", &p.erosion, 0.0f, 1.0f, 0.01f);           y += c.rowH;
    SliderInt(c, y, "Erosion passes", &p.passes, 8.0f, 80.0f);             y += c.rowH;
    Slider(c, y, "Slope slumping", &p.thermal, 0.0f, 1.0f, 0.01f);          y += c.rowH;
    Slider(c, y, "Lowland bias", &p.lowlands, 0.0f, 1.0f, 0.01f);          y += c.rowH;
}

void GroundTab(Ctx& c, float top) {
    GenParams& p = c.m.gen.p;
    const float gap = 16.0f;
    float y = top;
    const float x = c.x0 + kPad;
    const float colW = (c.pw - 2.0f * kPad - gap) * 0.5f;
    Toggle(c, x, y, colW, "Ranked climate", p.rankedClimate);
    Toggle(c, x + colW + gap, y, colW, "Rugged rock", p.rugged);
    y += c.rowH;
    Slider(c, y, "Fine detail", &p.detail, 0.0f, 1.0f, 0.01f);             y += c.rowH;
    Slider(c, y, "Detail on steep slopes", &p.steep, 0.0f, 1.0f, 0.01f);   y += c.rowH;
    Slider(c, y, "Beach width (tiles)", &p.beach, 0.0f, 20.0f, 0.5f);      y += c.rowH;
    Slider(c, y, "Shelf width (tiles)", &p.shelf, 0.0f, 30.0f, 0.5f);      y += c.rowH;
    Slider(c, y, "Warmth", &p.warmth, -0.3f, 0.3f, 0.01f);                 y += c.rowH;
    Slider(c, y, "Wetness", &p.wetness, -0.3f, 0.3f, 0.01f);               y += c.rowH;
    Slider(c, y, "Vegetation", &p.cover, 0.0f, 1.0f, 0.01f);               y += c.rowH + 8.0f;
    if (Button(c, x, y, colW, "Randomize terrain")) {
        RandomizeTerrain(p);
        c.m.status = "terrain randomized; generate to see it";
    }
    if (Button(c, x + colW + gap, y, colW, "Reset terrain")) {
        p = GenParams{};
        c.m.status = "terrain reset to the defaults";
    }
}

void WorldTab(Ctx& c, float top) {
    float y = top;

    Label(c, y + (c.btnH - c.lh) * 0.5f, "Seed");
    TextField(c, FieldX(c), y, FieldW(c), c.m.seedText);
    if (Button(c, FieldX(c) + FieldW(c) + 16.0f, y, 150.0f, "Random")) {
        c.m.seedText = std::to_string(RandomSeed());
    }
    y += c.rowH;

    {
        // Size presets. The chosen size is in brackets.
        Label(c, y + (c.btnH - c.lh) * 0.5f, "Size (tiles)");
        const int presets[] = {256, 512, 1024, 2048, 4096, 8192};
        const int presetCount = static_cast<int>(sizeof(presets) / sizeof(presets[0]));
        const float gap = 6.0f;
        const float x0 = c.x0 + c.pw * 0.42f;
        const float bw = (c.pw * 0.52f - gap * (presetCount - 1)) / static_cast<float>(presetCount);
        for (int i = 0; i < presetCount; ++i) {
            const bool chosen = std::lround(c.m.sizeTiles) == presets[i];
            const std::string label = chosen ? "[" + std::to_string(presets[i]) + "]" : std::to_string(presets[i]);
            if (Button(c, x0 + static_cast<float>(i) * (bw + gap), y, bw, label)) {
                c.m.sizeTiles = static_cast<float>(presets[i]);
            }
        }
    }
    y += c.rowH;
    Slider(c, y, "Height", &c.m.gen.heightScale, 1.0f, 2000.0f, 1.0f);
    y += c.rowH;


    const bool free = !c.s.generating;
    if (Button(c, c.x0 + kPad, y, c.pw - 2.0f * kPad, "Generate world", free) && free) {
        char* end = nullptr;
        const unsigned long seed = std::strtoul(c.m.seedText.c_str(), &end, 10);
        if (c.m.seedText.empty() || *end != '\0') {
            c.m.status = "seed must be a whole number";
        } else {
            GenSettings g = c.m.gen;
            g.seed = static_cast<uint32_t>(seed);
            int size = static_cast<int>(std::round(c.m.sizeTiles / kChunkTiles)) * kChunkTiles;
            if (size > kWholeWorldLimit) size = static_cast<int>(std::round(size / 1024.0f)) * 1024;
            g.size = std::clamp(size, kMinWorldTiles, MaxWorldTiles());
            StartGenerate(c.s, g);
            c.m.status = "generating seed " + std::to_string(g.seed) + "...";
        }
    }
    y += c.rowH;

    // Current world, or generation progress while it runs.
    if (c.s.generating) {
        const int pct = c.s.progress.load();
        const float barW = c.pw - 2.0f * kPad;
        if (Visible(c, y + 8.0f, 12.0f)) {
            c.r.Rect(c.x0 + kPad, y + 8.0f, barW, 12.0f, kTrack, 1.0f);
            c.r.Rect(c.x0 + kPad, y + 8.0f, barW * pct / 100.0f, 12.0f, kAccent, 1.0f);
        }
        const GenStatus st = SnapshotStatus(c.s);
        const std::string where = st.stage == 0 ? std::string("Starting...")
            : "Stage " + std::to_string(st.stage) + " of " + std::to_string(st.stages) + ": " + st.name;
        Label(c, y + c.rowH, where, kTextDim);
        Label(c, y + 2 * c.rowH, "ETA " + (st.eta < 0 ? std::string("estimating") : FormatDuration(st.eta)) +
                                 "   full log is listed top-left of the screen", kTextDim);
    } else if (c.s.hasWorld) {
        const std::string name = c.s.worldName.empty() ? std::string("not exported") : c.s.worldName;
        Label(c, y, "Loaded: " + name + ", seed " + std::to_string(c.s.world.seed) + ", " +
                    std::to_string(c.s.world.size) + " x " + std::to_string(c.s.world.size) + " tiles");
    } else {
        std::string line;
        {
            std::lock_guard<std::mutex> lock(c.s.lineMutex);
            line = c.s.statusLine;
        }
        Label(c, y, line.empty() ? std::string("No world yet.") : line, kTextDim);
    }
    if (!c.s.generating) {
        float ry = y + c.rowH;
        for (const std::string& line : TimingReport(c.s)) {
            Label(c, ry, line, kTextDim);
            ry += c.rowH;
        }
    }
}

void SkyTab(Ctx& c, float top) {
    Look& l = c.look;
    float y = top;
    Heading(c, y, "Time of day");                                                 y += c.rowH;
    Slider(c, y, "Clock (hour)", &l.clock, 0.0f, 24.0f, 0.0f);                    y += c.rowH;
    if (Button(c, c.x0 + kPad, y, c.pw * 0.42f, l.dayCycle >= 0.5f ? "Day cycle: on" : "Day cycle: off")) {
        l.dayCycle = l.dayCycle >= 0.5f ? 0.0f : 1.0f;
    }
    y += c.rowH;
    Slider(c, y, "Day length (s)", &l.dayLength, 1.0f, 36000.0f, 10.0f);          y += c.rowH;
    Slider(c, y, "Day share of the cycle", &l.dayFraction, 0.1f, 0.9f, 0.01f);    y += c.rowH + 8.0f;

    Heading(c, y, "Sun");                                                         y += c.rowH;
    Slider(c, y, "Sun elevation", &l.sunElevation, -90.0f, 90.0f, 1.0f);          y += c.rowH;
    Slider(c, y, "Sun azimuth", &l.sunAzimuth, 0.0f, 360.0f, 1.0f);               y += c.rowH;
    Slider(c, y, "Sun strength", &l.sunStrength, 0.0f, 10.0f, 0.0f);              y += c.rowH;
    Slider(c, y, "Sunrise and sunset glow", &l.sunsetGlow, 0.0f, 1.0f, 0.01f);    y += c.rowH;
    ColorRow(c, y, "Sun color", l.sunRgb, &l.colorOpacity[kColSun]);                                      y += c.rowH;
    ColorRow(c, y, "Sun color at sunset", l.sunGlowRgb, &l.colorOpacity[kColSunGlow]);                        y += c.rowH;
    ColorRow(c, y, "Horizon at sunset", l.horizonWarmRgb, &l.colorOpacity[kColHorizonWarm]);                       y += c.rowH + 8.0f;

    Heading(c, y, "Moon");                                                        y += c.rowH;
    Slider(c, y, "Moon strength", &l.moonStrength, 0.0f, 3.0f, 0.01f);            y += c.rowH;
    Slider(c, y, "Moon size (degrees)", &l.moonSize, 0.3f, 8.0f, 0.1f);           y += c.rowH;
    ColorRow(c, y, "Moon color", l.moonRgb, &l.colorOpacity[kColMoon]);                                    y += c.rowH + 8.0f;

    Heading(c, y, "Stars");                                                       y += c.rowH;
    Slider(c, y, "Star amount", &l.starAmount, 0.0f, 3.0f, 0.01f);               y += c.rowH;
    ColorRow(c, y, "Star color", l.starRgb, &l.colorOpacity[kColStar]);                                    y += c.rowH + 8.0f;

    Heading(c, y, "Sky and air");                                                 y += c.rowH;
    Slider(c, y, "Ambient light", &l.ambient, 0.0f, 10.0f, 0.0f);                 y += c.rowH;
    if (Button(c, c.x0 + kPad, y, c.pw * 0.42f, l.fogEnabled >= 0.5f ? "Fog: on" : "Fog: off")) {
        l.fogEnabled = l.fogEnabled >= 0.5f ? 0.0f : 1.0f;
    }
    y += c.rowH;
    Slider(c, y, "Fog density", &l.fog, 0.0f, 20.0f, 0.0f);                       y += c.rowH;
    ColorRow(c, y, "Fog color", l.fogRgb, &l.colorOpacity[kColFog]);                                      y += c.rowH;
    ColorRow(c, y, "Sky top, day", l.skyTop, &l.colorOpacity[kColSkyTopDay]);                                    y += c.rowH;
    ColorRow(c, y, "Sky horizon, day", l.skyHorizon, &l.colorOpacity[kColSkyHorizonDay]);                            y += c.rowH;
    ColorRow(c, y, "Sky top, night", l.nightTop, &l.colorOpacity[kColSkyTopNight]);                                y += c.rowH;
    ColorRow(c, y, "Sky horizon, night", l.nightHorizon, &l.colorOpacity[kColSkyHorizonNight]);                        y += c.rowH + 8.0f;

    const char* names[5] = {"Clear", "Sunset", "Overcast", "Night", "Reset"};
    const float gap = 10.0f;
    const float bw = (c.pw - 2.0f * kPad - gap * 4.0f) / 5.0f;
    for (int i = 0; i < 5; ++i) {
        if (Button(c, c.x0 + kPad + i * (bw + gap), y, bw, names[i])) {
            ApplyPreset(c.look, i);
            c.m.status = std::string(names[i]) + " preset applied";
        }
    }
}

// Randomizes the look within ranges that keep it readable: a time of day, a sky
// color family, and sensible light and water strengths. The clock is kept.
void RandomizeLook(Look& l) {
    std::mt19937 rng(std::random_device{}());
    auto uni = [&rng](float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(rng); };
    l.sunElevation = uni(35.0f, 80.0f);
    l.sunAzimuth = uni(0.0f, 360.0f);
    l.sunStrength = uni(0.6f, 1.2f);
    l.ambient = uni(0.2f, 0.5f);
    l.fog = uni(0.0f, 1.5f);
    l.dayFraction = uni(0.35f, 0.65f);
    l.sunsetGlow = uni(0.2f, 1.0f);
    l.sunRgb[0] = 1.0f; l.sunRgb[1] = uni(0.85f, 1.0f); l.sunRgb[2] = uni(0.7f, 0.95f);
    l.skyTop[0] = uni(0.1f, 0.4f); l.skyTop[1] = uni(0.3f, 0.5f); l.skyTop[2] = uni(0.6f, 0.9f);
    l.skyHorizon[0] = uni(0.6f, 0.9f); l.skyHorizon[1] = uni(0.7f, 0.9f); l.skyHorizon[2] = uni(0.8f, 1.0f);
    const float gray = uni(0.3f, 0.6f);
    l.rockRgb[0] = gray * uni(1.0f, 1.1f); l.rockRgb[1] = gray; l.rockRgb[2] = gray * uni(0.85f, 0.95f);
    l.rockSlope = uni(0.3f, 0.7f);
    l.waterOpacity = uni(0.6f, 1.0f);
    l.waterPattern = uni(0.5f, 1.5f);
    l.waterSpeed = uni(0.5f, 2.0f);
}

void SurfaceTab(Ctx& c, float top) {
    Look& l = c.look;
    float y = top;
    Heading(c, y, "Land");                                                        y += c.rowH;
    Slider(c, y, "Rock on steep slopes", &l.rockSlope, 0.05f, 0.95f, 0.01f);      y += c.rowH;
    ColorRow(c, y, "Rock color", l.rockRgb, &l.colorOpacity[kColRock]);                                    y += c.rowH + 8.0f;

    Heading(c, y, "Water");                                                       y += c.rowH;
    ColorRow(c, y, "Sea surface color", l.waterRgb, &l.colorOpacity[kColWater]);                            y += c.rowH;
    ColorRow(c, y, "Sea floor color", l.underwaterRgb, &l.colorOpacity[kColUnderwater]);                         y += c.rowH;
    Slider(c, y, "Water opacity", &l.waterOpacity, 0.0f, 1.0f, 0.01f);            y += c.rowH;
    Slider(c, y, "Water light bands", &l.waterPattern, 0.0f, 2.0f, 0.01f);        y += c.rowH;
    Slider(c, y, "Water surface motion", &l.waterSpeed, 0.0f, 3.0f, 0.01f);       y += c.rowH + 8.0f;

    Heading(c, y, "Grid");                                                        y += c.rowH;
    ColorRow(c, y, "Minor grid lines", l.gridMinor, &l.colorOpacity[kColGridMinor]);                             y += c.rowH;
    ColorRow(c, y, "Major grid lines", l.gridMajor, &l.colorOpacity[kColGridMajor]);                             y += c.rowH + 8.0f;

    if (Button(c, c.x0 + kPad, y, c.pw * 0.42f, "Randomize look")) {
        RandomizeLook(l);
        c.m.status = "look randomized";
    }
}

// Theme file: export writes data/themes/<name>.json, import applies it.
// Beside an Export to Desktop button: where the file will go.
void DesktopNote(Ctx& c, float y) {
    if (!Visible(c, y, c.btnH)) return;
    const std::string where = c.m.desktopDir.empty() ? std::string("desktop folder not found")
                                                     : "to " + c.m.desktopDir;
    const float x = FieldX(c) + 246.0f;
    const float maxW = c.x0 + c.pw - kPad - x;
    c.r.Text(x, y + (c.btnH - c.lh) * 0.5f, FitText(c.r, where, maxW), kTextDim);
}

void ThemeFileSection(Ctx& c, float top) {
    float y = top;
    Heading(c, y, "Theme file");                                                  y += c.rowH;
    Label(c, y + (c.btnH - c.lh) * 0.5f, "Name");
    TextField(c, FieldX(c), y, FieldW(c), c.m.themeName);
    if (Button(c, FieldX(c) + FieldW(c) + 16.0f, y, 150.0f, "Export")) {
        std::string err;
        if (!IsSafeName(c.m.themeName)) {
            c.m.status = "theme name may use letters, digits, space, - and _";
        } else if (SaveLookFile(c.look, c.s.themesDir + "/" + c.m.themeName + ".json", err)) {
            c.m.status = "exported theme " + c.m.themeName;
            c.m.themes = ListLookNames(c.s.themesDir);
        } else {
            c.m.status = "export failed: " + err;
        }
    }
    y += c.rowH;
    if (Button(c, FieldX(c), y, 230.0f, "Export to Desktop")) {
        std::string path, err;
        if (!IsSafeName(c.m.themeName)) {
            c.m.status = "theme name may use letters, digits, space, - and _";
        } else if (ExportThemeToDesktop(c.look, c.m.themeName, path, err)) {
            c.m.status = "saved " + path;
        } else {
            c.m.status = "desktop export failed: " + err;
        }
    }
    DesktopNote(c, y);
    y += c.rowH + 8.0f;
    for (const std::string& name : c.m.themes) {
        if (Button(c, c.x0 + kPad, y, c.pw * 0.5f, "Import " + name)) {
            std::string err;
            if (LoadLookFile(c.s.themesDir + "/" + name + ".json", c.look, err)) c.m.status = "imported theme " + name;
            else                                                             c.m.status = "import failed: " + err;
        }
        y += c.rowH;
    }
}

// Generation: the terrain settings, land then ground.
void GenerationTab(Ctx& c, float top) {
    LandTab(c, top);
    GroundTab(c, c.lowest + 26.0f);
}

// Themes: sky and atmosphere, then surface and water, then the theme file.
void ThemesTab(Ctx& c, float top) {
    SkyTab(c, top);
    SurfaceTab(c, c.lowest + 26.0f);
    ThemeFileSection(c, c.lowest + 26.0f);
}

void WalkerTab(Ctx& c, float top) {
    float y = top;
    Slider(c, y, "Field of view", &T.fov, 1.0f, 179.0f, 1.0f);             y += c.rowH;
    Slider(c, y, "View distance", &T.viewDistance, 1.0f, 10000.0f, 10.0f); y += c.rowH;
    Slider(c, y, "Detail distance", &T.lodDistance, 1.0f, 4000.0f, 10.0f); y += c.rowH;
    Slider(c, y, "Walk speed", &T.walkSpeed, 0.0f, 500.0f, 0.0f);                     y += c.rowH;
    Slider(c, y, "Fly speed", &T.flySpeed, 0.0f, 500.0f, 0.0f);                      y += c.rowH;
    Slider(c, y, "Mouse sensitivity", &T.mouseSens, 0.001f, 5.0f, 0.0f);
}

// One switch shown as a button with its state.
void Toggle(Ctx& c, float x, float y, float w, const char* label, bool& value) {
    char text[96];
    std::snprintf(text, sizeof text, "%s: %s", label, value ? "on" : "off");
    if (Button(c, x, y, w, text)) value = !value;
}

// Performance panels and debug switches. Nothing here is saved.
void DebugTab(Ctx& c, float top) {
    const float gap = 16.0f;
    const float colW = (c.pw - 2.0f * kPad - gap) * 0.5f;
    const float left = c.x0 + kPad;
    const float right = left + colW + gap;
    float y = top;

    Label(c, y, "Performance", kAccent);
    y += c.rowH;
    Toggle(c, left, y, colW, "FPS / UPS counter", gDebug.showFps);
    Toggle(c, right, y, colW, "Performance panel", gDebug.showPerfPanel);
    y += c.rowH + 10.0f;

    Label(c, y, "Debug", kAccent);
    y += c.rowH;
    Toggle(c, left, y, colW, "Wireframe terrain", gDebug.wireframe);
    Toggle(c, right, y, colW, "Color chunks by detail", gDebug.lodTint);
    y += c.rowH;
    Toggle(c, left, y, colW, "Hide terrain", gDebug.hideTerrain);
    Toggle(c, right, y, colW, "Hide sea floor", gDebug.hideFloor);
    y += c.rowH;
    Toggle(c, left, y, colW, "Hide water", gDebug.hideWater);
    Toggle(c, right, y, colW, "Hide sky", gDebug.hideSky);
    y += c.rowH;
    Toggle(c, left, y, colW, "Chunk frustum culling", gOpt.cullChunks);
    Toggle(c, right, y, colW, "Use skybox", gDebug.skybox);
    y += c.rowH;
    Toggle(c, left, y, colW, "Slope view", gDebug.slopeView);
    y += c.rowH;
    Toggle(c, left, y, colW, "Sky after terrain", gOpt.skyLast);
    Toggle(c, right, y, colW, "Dynamic far plane", gOpt.dynamicFar);
    y += c.rowH;
    Toggle(c, left, y, colW, "VSync", gDebug.vsync);
    y += c.rowH + 10.0f;

    Label(c, y, "These change what is drawn or shown. They are not saved.", kTextDim);
}

// The game world list on the Files tab. It works like the game's save list: a name field, a question
// before a name is replaced, sortable columns, and rows that scroll in a window.
constexpr size_t GAME_WORLD_ROWS = 6;          // worlds shown at once
constexpr size_t GAME_WORLD_NAME_LIMIT = 32;   // longest name in bytes, as in the game's save list

// The name without leading or trailing spaces
std::string TrimSpaces(const std::string& s) {
    const size_t a = s.find_first_not_of(' ');
    if (a == std::string::npos) return "";
    const size_t b = s.find_last_not_of(' ');
    return s.substr(a, b - a + 1);
}

// Reads the game worlds again after a change, and otherwise at most once a second
void RefreshGameWorlds(Ctx& c) {
    const unsigned long long now = SDL_GetTicks();
    if (!c.m.gameWorldsStale && now - c.m.gameWorldsAt < 1000) return;
    c.m.gameWorlds = c.s.listGameWorlds ? c.s.listGameWorlds(c.m.gameWorldByName, c.m.gameWorldAsc)
                                        : std::vector<GameWorldEntry>{};
    c.m.gameWorldsStale = false;
    c.m.gameWorldsAt = now;
    const size_t total = c.m.gameWorlds.size();
    c.m.gameWorldOffset = std::min(c.m.gameWorldOffset, total > GAME_WORLD_ROWS ? total - GAME_WORLD_ROWS : 0);
}

// True if a game world is already saved under this name. Reads the maps folder again first.
bool GameWorldNamed(Ctx& c, const std::string& name) {
    if (!c.s.listGameWorlds) return false;
    for (const GameWorldEntry& w : c.s.listGameWorlds(true, true)) {
        if (w.name == name) return true;
    }
    return false;
}

// Writes the loaded world as a game world under this name, replacing one of the same name
void SaveGameWorld(Ctx& c, const std::string& name) {
    std::string path, err;
    c.m.gameWorldAsk = false;
    if (c.s.exportMap && c.s.exportMap(c.s.world, c.look, name, path, err)) {
        c.m.status = "saved game world " + name;
        c.m.gameWorldsStale = true;
    } else {
        c.m.status = "game world save failed: " + err;
    }
}

// One list row: the name on the left, the date on the right. Returns true when it is clicked.
bool ListRow(Ctx& c, float x, float y, float nameW, float dateW, const std::string& name, const std::string& when) {
    const float h = c.btnH;
    if (!Visible(c, y, h)) return false;
    const float w = nameW + dateW;
    const bool hot = Inside(c.mouse.x, c.mouse.y, x, y, w, h);
    c.r.Rect(x, y, w, h, hot ? kButtonHot : kButton, 1.0f);
    const float ty = y + (h - c.lh) * 0.5f;
    c.r.Text(x + 12.0f, ty, FitText(c.r, name, nameW - 24.0f), kText);
    c.r.Text(x + nameW + 12.0f, ty, FitText(c.r, when, dateW - 24.0f), kTextDim);
    return hot && c.mouse.pressed;
}

// The game world section of the Files tab. Returns the y just below it.
float GameWorldSection(Ctx& c, float top) {
    float y = top;
    RefreshGameWorlds(c);

    // Name field and Save. A name another world already has asks before it replaces that world.
    Label(c, y + (c.btnH - c.lh) * 0.5f, "Game world");
    TextField(c, FieldX(c), y, FieldW(c), c.m.gameWorldName);
    if (Button(c, FieldX(c) + FieldW(c) + 16.0f, y, 150.0f, "Save")) {
        const std::string name = TrimSpaces(c.m.gameWorldName);
        c.m.gameWorldAsk = false;
        if (!c.s.hasWorld) {
            c.m.status = "generate a world first";
        } else if (name.empty()) {
            c.m.status = "type a name for the game world";
        } else if (name.size() > GAME_WORLD_NAME_LIMIT) {
            c.m.status = "that name is too long";
        } else if (GameWorldNamed(c, name)) {
            c.m.gameWorldAsk = true;
            c.m.gameWorldAskName = name;
        } else {
            SaveGameWorld(c, name);
        }
    }
    y += c.rowH;

    // The question, when the name is taken
    if (c.m.gameWorldAsk) {
        Label(c, y + (c.btnH - c.lh) * 0.5f, "A game world named \"" + c.m.gameWorldAskName + "\" exists. Replace it?", kAccent);
        y += c.rowH;
        if (Button(c, FieldX(c), y, 150.0f, "Replace", true, true)) SaveGameWorld(c, c.m.gameWorldAskName);
        if (Button(c, FieldX(c) + 166.0f, y, 150.0f, "Cancel")) c.m.gameWorldAsk = false;
        y += c.rowH;
    }
    Label(c, y, "Saved in the maps folder beside the game. New Game lists them.", kTextDim);
    y += c.rowH;

    // Column headers. Clicking the active one flips the direction; a new column starts ascending.
    const float x = FieldX(c), w = FieldW(c) + 166.0f;
    const float nameW = w * 0.6f, dateW = w - nameW;
    auto header = [&](float hx, float hw, const char* label, bool byName) {
        const bool active = c.m.gameWorldByName == byName;
        std::string text = label;
        if (active) text += c.m.gameWorldAsc ? "  ^" : "  v";
        if (Button(c, hx, y, hw, text)) {
            if (active) {
                c.m.gameWorldAsc = !c.m.gameWorldAsc;
            } else {
                c.m.gameWorldByName = byName;
                c.m.gameWorldAsc = true;
            }
            c.m.gameWorldsStale = true;
            c.m.gameWorldOffset = 0;
        }
    };
    header(x, nameW - 4.0f, "Name", true);
    header(x + nameW + 4.0f, dateW - 4.0f, "Created", false);
    y += c.rowH;

    const size_t total = c.m.gameWorlds.size();
    if (total == 0) {
        Label(c, y + (c.btnH - c.lh) * 0.5f, "No game worlds yet. Generate a world and save it here.", kTextDim);
        y += c.rowH;
    }

    // The window the list shows. The wheel over it scrolls the list instead of the tab.
    c.m.gameListX = x;
    c.m.gameListY = y;
    c.m.gameListW = w;
    c.m.gameListH = total > 0 ? (float)(GAME_WORLD_ROWS + 2) * c.rowH : 0.0f;

    const size_t first = std::min(c.m.gameWorldOffset, total);
    const size_t last = std::min(first + GAME_WORLD_ROWS, total);
    if (first > 0) {
        if (Button(c, x, y, w, "...   " + std::to_string(first) + " more above")) {
            c.m.gameWorldOffset = first > GAME_WORLD_ROWS ? first - GAME_WORLD_ROWS : 0;
        }
        y += c.rowH;
    }
    for (size_t i = first; i < last; ++i) {
        const GameWorldEntry& e = c.m.gameWorlds[i];
        if (ListRow(c, x, y, nameW, dateW, e.name, e.when)) {
            // Clicking a world copies its name, so Save replaces it
            c.m.gameWorldName = e.name;
            c.m.gameWorldAsk = false;
            c.m.status = "name copied from the list";
        }
        y += c.rowH;
    }
    if (last < total) {
        if (Button(c, x, y, w, "...   " + std::to_string(total - last) + " more below")) {
            const size_t maxOffset = total > GAME_WORLD_ROWS ? total - GAME_WORLD_ROWS : 0;
            c.m.gameWorldOffset = std::min(first + GAME_WORLD_ROWS, maxOffset);
        }
        y += c.rowH;
    }
    return y;
}

void FilesTab(Ctx& c, float top) {
    float y = top;

    // Editor files: the tool's own save, a .wmw file kept with the editor. Import reads them back.
    Label(c, y + (c.btnH - c.lh) * 0.5f, "Editor file");
    TextField(c, FieldX(c), y, FieldW(c), c.m.exportName);
    if (Button(c, FieldX(c) + FieldW(c) + 16.0f, y, 150.0f, "Save")) {
        std::string err;
        if (!c.s.hasWorld) {
            c.m.status = "generate a world first";
        } else if (SaveWorld(c.s, c.m.exportName, err)) {
            c.m.status = "saved " + c.m.exportName + ".wmw";
            c.m.worlds = ListWorldNames(c.s.worldsDir);
        } else {
            c.m.status = "export failed: " + err;
        }
    }
    y += c.rowH;
    if (Button(c, FieldX(c), y, 230.0f, "Export to Desktop")) {
        std::string path, err;
        if (!IsSafeName(c.m.exportName)) {
            c.m.status = "name may use letters, digits, space, - and _";
        } else if (ExportWorldToDesktop(c.s, c.m.exportName, path, err)) {
            c.m.status = "saved " + path;
        } else {
            c.m.status = "desktop export failed: " + err;
        }
    }
    DesktopNote(c, y);
    y += c.rowH;

    // The same world as a game world, saved in the maps folder and listed under New Game
    y = GameWorldSection(c, y);

    for (const std::string& name : c.m.worlds) {
        if (Button(c, c.x0 + kPad, y, c.pw * 0.5f, "Import " + name)) {
            std::string err;
            if (LoadWorld(c.s, name, err)) c.m.status = "imported " + name;
            else                           c.m.status = "import failed: " + err;
        }
        y += c.rowH;
    }
    y += 6.0f;

}

// The color picker: a hue and saturation wheel, brightness and opacity bars, and a hex field.
void DrawPicker(Ctx& c) {
    ColorPicker& p = c.m.picker;
    MenuState& m = c.m;
    Renderer& r = c.r;
    const Mouse& mouse = c.mouse;

    const float boxW = 440.0f;
    const float boxH = 448.0f + 2.0f * c.btnH;
    const float bx = c.x0 + (c.pw - boxW) * 0.5f;
    const float by = c.y0 + std::max(12.0f, (c.ph - boxH) * 0.5f);
    const bool inBox = Inside(mouse.x, mouse.y, bx, by, boxW, boxH);
    if (p.fresh) {
        p.fresh = false;
    } else if (mouse.pressed && !inBox) {
        p.open = false;
        m.activeField = nullptr;
        m.dragging = nullptr;
        return;
    }

    // Dim the panel, then draw the card.
    r.Rect(c.x0, c.y0, c.pw, c.ph, V3{0.0f, 0.0f, 0.0f}, 0.5f);
    r.Rect(bx - 1.0f, by - 1.0f, boxW + 2.0f, boxH + 2.0f, kTrack, 1.0f);
    r.Rect(bx, by, boxW, boxH, kPanel, 1.0f);
    r.Rect(bx, by, boxW, 2.0f, kAccent, 1.0f);
    r.Text(bx + kPad, by + 18.0f, p.label, kText);
    const std::string esc = "Esc to close";
    r.Text(bx + boxW - kPad - r.MeasureText(esc), by + 18.0f, esc, kTextDim);

    // Wheel: the angle is the hue, the distance from the center is the saturation.
    const float R = 108.0f, cell = 6.0f;
    const float cx = bx + boxW * 0.5f;
    const float cy = by + 46.0f + R;
    for (float dy = -R; dy <= R; dy += cell) {
        for (float dx = -R; dx <= R; dx += cell) {
            const float d = std::sqrt(dx * dx + dy * dy);
            if (d > R) continue;
            float ang = std::atan2(-dy, dx) * 180.0f / kPi;
            if (ang < 0.0f) ang += 360.0f;
            float col[3];
            HsvToRgb(ang, d / R, p.v, col);
            r.Rect(cx + dx - cell * 0.5f, cy + dy - cell * 0.5f, cell, cell, V3{col[0], col[1], col[2]}, 1.0f);
        }
    }
    const float mx = cx + std::cos(p.h * kPi / 180.0f) * p.s * R;
    const float my = cy - std::sin(p.h * kPi / 180.0f) * p.s * R;
    float cur[3];
    HsvToRgb(p.h, p.s, p.v, cur);
    r.Rect(mx - 7.0f, my - 7.0f, 14.0f, 14.0f, kInk, 1.0f);
    r.Rect(mx - 5.0f, my - 5.0f, 10.0f, 10.0f, V3{cur[0], cur[1], cur[2]}, 1.0f);

    const float wdx = mouse.x - cx, wdy = mouse.y - cy;
    if (mouse.pressed && wdx * wdx + wdy * wdy <= R * R) m.dragging = &p.h;
    if (m.dragging == &p.h) {
        if (mouse.down) {
            p.s = std::min(1.0f, std::sqrt(wdx * wdx + wdy * wdy) / R);
            float ang = std::atan2(-wdy, wdx) * 180.0f / kPi;
            if (ang < 0.0f) ang += 360.0f;
            p.h = ang;
            ApplyPicker(p);
        } else {
            m.dragging = nullptr;
        }
    }

    // Brightness bar.
    const float barX = bx + kPad, barW = boxW - 2.0f * kPad;
    const float vY = cy + R + 26.0f;
    r.Text(barX, vY - 22.0f, "Brightness", kTextDim);
    const int steps = 40;
    for (int i = 0; i < steps; ++i) {
        float col[3];
        HsvToRgb(p.h, p.s, (static_cast<float>(i) + 0.5f) / steps, col);
        r.Rect(barX + barW * i / steps, vY, barW / steps + 1.0f, 14.0f, V3{col[0], col[1], col[2]}, 1.0f);
    }
    r.Rect(barX + barW * p.v - 3.0f, vY - 4.0f, 6.0f, 22.0f, kText, 1.0f);
    if (mouse.pressed && Inside(mouse.x, mouse.y, barX - 6.0f, vY - 6.0f, barW + 12.0f, 26.0f)) m.dragging = &p.v;
    if (m.dragging == &p.v) {
        if (mouse.down) {
            p.v = std::clamp((mouse.x - barX) / barW, 0.0f, 1.0f);
            ApplyPicker(p);
        } else {
            m.dragging = nullptr;
        }
    }

    // Opacity bar.
    const float aY = vY + 62.0f;
    char pct[16];
    std::snprintf(pct, sizeof pct, "%d%%", static_cast<int>(std::lround(p.alpha * 100.0f)));
    r.Text(barX, aY - 22.0f, "Opacity", kTextDim);
    r.Text(barX + barW - r.MeasureText(pct), aY - 22.0f, pct, kText);
    r.Rect(barX, aY, barW, 14.0f, kTrack, 1.0f);
    r.Rect(barX, aY, barW * p.alpha, 14.0f, kAccent, 1.0f);
    r.Rect(barX + barW * p.alpha - 3.0f, aY - 4.0f, 6.0f, 22.0f, kText, 1.0f);
    if (mouse.pressed && Inside(mouse.x, mouse.y, barX - 6.0f, aY - 6.0f, barW + 12.0f, 26.0f)) m.dragging = &p.alpha;
    if (m.dragging == &p.alpha) {
        if (mouse.down) {
            p.alpha = std::clamp((mouse.x - barX) / barW, 0.0f, 1.0f);
            ApplyPicker(p);
        } else {
            m.dragging = nullptr;
        }
    }

    // Hex field and a preview of the color at its opacity.
    const float hY = aY + 52.0f;
    if (m.activeField != &p.hex) p.hex = FormatHex(cur);
    r.Text(barX, hY + (c.btnH - c.lh) * 0.5f, "Hex", kTextDim);
    TextField(c, barX + 80.0f, hY, 190.0f, p.hex);
    const V3 shown{kPanel.x * (1.0f - p.alpha) + cur[0] * p.alpha,
                   kPanel.y * (1.0f - p.alpha) + cur[1] * p.alpha,
                   kPanel.z * (1.0f - p.alpha) + cur[2] * p.alpha};
    r.Rect(barX + barW - 110.0f, hY, 110.0f, c.btnH, kTrack, 1.0f);
    r.Rect(barX + barW - 108.0f, hY + 2.0f, 106.0f, c.btnH - 4.0f, shown, 1.0f);

    // Done closes the picker; the color has already been applied as it was changed.
    if (Button(c, bx + boxW - kPad - 140.0f, by + boxH - c.btnH - 18.0f, 140.0f, "Done", true, true)) {
        p.open = false;
        m.activeField = nullptr;
        m.dragging = nullptr;
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------

void MenuOpen(MenuState& m, const Session& s) {
    m.desktopDir = DesktopFolder();
    m.open = true;
    m.worlds = ListWorldNames(s.worldsDir);
    m.themes = ListLookNames(s.themesDir);
    m.activeField = nullptr;
    m.dragging = nullptr;
}

void MenuTextInput(MenuState& m, const std::string& text) {
    if (!m.activeField) return;
    if (m.activeField == &m.sliderEdit.text) {
        // A typed slider value takes number characters only.
        static const std::string kNumberChars = "0123456789.-+eE";
        for (char ch : text) {
            if (m.sliderEdit.text.size() < 16 && ch != '\0' && kNumberChars.find(ch) != std::string::npos) {
                m.sliderEdit.text += ch;
            }
        }
        return;
    }
    if (m.activeField->size() + text.size() > 60) return;
    *m.activeField += text;
}

void MenuScrollBy(MenuState& m, float px) {
    m.scroll = std::clamp(m.scroll + px, 0.0f, m.scrollMax);
}

void MenuScrollTo(MenuState& m, bool end) {
    m.scroll = end ? m.scrollMax : 0.0f;
}

void MenuBackspace(MenuState& m) {
    if (m.activeField && !m.activeField->empty()) m.activeField->pop_back();
}

void MenuStopEditing(MenuState& m) {
    m.activeField = nullptr;
    m.sliderEdit = SliderEdit{};   // cancels a typed slider value that was not applied
}

void MenuCommitSlider(MenuState& m) {
    if (m.picker.open && m.activeField == &m.picker.hex) {
        float rgb[3];
        if (ParseHex(m.picker.hex, rgb)) {
            ColorPicker& p = m.picker;
            p.rgb[0] = rgb[0];
            p.rgb[1] = rgb[1];
            p.rgb[2] = rgb[2];
            RgbToHsv(rgb, p.h, p.s, p.v);
            ApplyPicker(p);
            m.status.clear();
        } else {
            m.status = "a hex color looks like #RRGGBB";
        }
        return;
    }
    SliderEdit& e = m.sliderEdit;
    if (!e.key || m.activeField != &e.text || e.text.empty()) return;
    char* end = nullptr;
    const float typed = std::strtof(e.text.c_str(), &end);
    if (end == e.text.c_str()) {
        m.status = "not a number";
        return;
    }
    float v = std::clamp(typed, e.lo, e.hi);
    if (e.step > 0.0f) v = std::round(v / e.step) * e.step;
    SliderSet(e.target, v);
    m.status = v == typed ? std::string() : "limited to " + SliderFormat(v, e.step);
}

MenuAction DrawMenu(Renderer& r, MenuState& m, Session& s, Look& look, const Mouse& mouse, int w, int h) {
    // The export name follows the world's seed until the user types over it.
    if (s.hasWorld) {
        const std::string wanted = "world_" + std::to_string(s.world.seed);
        if (wanted != m.autoName) {
            m.autoName = wanted;
            m.exportName = wanted;
        }
    }

    const float fw = static_cast<float>(w);
    const float fh = static_cast<float>(h);

    Ctx c{r, m, s, look, mouse, 0, 0, 0, 0, r.LineHeight(), 0, 0, false};
    c.btnH = c.lh + 12.0f;
    c.rowH = c.lh + 24.0f;
    c.pw = std::min(1200.0f, fw - 48.0f);
    c.ph = std::min(fh - 48.0f, 940.0f);
    c.x0 = (fw - c.pw) * 0.5f;
    c.y0 = (fh - c.ph) * 0.5f;
    const float right = c.x0 + c.pw;
    const bool picking = m.picker.open;

    // Backdrop, then the panel with a hairline border and an accent edge.
    r.Rect(0.0f, 0.0f, fw, fh, V3{0.0f, 0.0f, 0.0f}, 0.6f);
    r.Rect(c.x0 - 1.0f, c.y0 - 1.0f, c.pw + 2.0f, c.ph + 2.0f, kTrack, 1.0f);
    r.Rect(c.x0, c.y0, c.pw, c.ph, kPanel, 1.0f);
    r.Rect(c.x0, c.y0, c.pw, 2.0f, kAccent, 1.0f);

    // Header: title on the left, how to use the panel on the right.
    const float headerH = c.lh + 36.0f;
    const float titleY = c.y0 + (headerH - c.lh) * 0.5f;
    r.Text(c.x0 + kPad, titleY, "Menu", kText);
    const std::string hint = "Esc close   wheel, arrows, Page keys scroll";
    r.Text(right - kPad - r.MeasureText(hint), titleY, hint, kTextDim);
    r.Rect(c.x0, c.y0 + headerH, c.pw, 1.0f, kTrack, 1.0f);

    // Tab bar.
    const float tabTop = c.y0 + headerH + 6.0f;
    const float tabPad = 18.0f;
    float tx = c.x0 + kPad;
    for (int i = 0; i < kTabCount; ++i) {
        const float tw = r.MeasureText(kTabNames[i]) + 2.0f * tabPad;
        const bool sel = m.tab == i;
        const bool hit = TabButton(c, tx, tabTop, tw, c.btnH, kTabNames[i], sel);
        if (hit && !picking && !sel) {
            m.tab = i;
            m.scroll = 0.0f;
            m.activeField = nullptr;
            if (i == kFiles) m.worlds = ListWorldNames(s.worldsDir);
            if (i == kThemes) m.themes = ListLookNames(s.themesDir);
        }
        tx += tw + 4.0f;
    }
    const float tabRule = tabTop + c.btnH + 6.0f;
    r.Rect(c.x0, tabRule, c.pw, 1.0f, kTrack, 1.0f);

    // Footer: a rule, the status on the left and the buttons on the right.
    const float footH = c.btnH + 32.0f;
    const float footTop = c.y0 + c.ph - footH;
    const float footY = footTop + (footH - c.btnH) * 0.5f;
    const float contentTop = tabRule + 14.0f;
    const float contentBottom = footTop - 12.0f;
    const float viewH = contentBottom - contentTop;
    c.contentBottom = contentBottom;
    r.Rect(c.x0, footTop, c.pw, 1.0f, kTrack, 1.0f);

    // Scrollable content, with a scrollbar at the right edge when it overflows.
    if (!picking) {
        c.clip = true;
        c.clipTop = contentTop;
        c.clipBottom = contentBottom;
        c.lowest = 0.0f;
        if (mouse.wheel != 0.0f && Inside(mouse.x, mouse.y, c.x0, contentTop, c.pw, viewH)) {
            if (m.tab == kFiles && m.gameListH > 0.0f &&
                Inside(mouse.x, mouse.y, m.gameListX, m.gameListY, m.gameListW, m.gameListH)) {
                // Over the game world list: the wheel scrolls the list a row at a time
                const size_t total = m.gameWorlds.size();
                const int maxOffset = total > GAME_WORLD_ROWS ? (int)(total - GAME_WORLD_ROWS) : 0;
                const int next = (int)m.gameWorldOffset - (int)mouse.wheel;
                m.gameWorldOffset = (size_t)std::clamp(next, 0, maxOffset);
            } else {
                m.scroll = std::clamp(m.scroll - mouse.wheel * c.rowH * 1.5f, 0.0f, m.scrollMax);
                MenuStopEditing(m);
            }
        }
        if (m.scrollMax > 0.0f) {
            const float thumbH = std::max(44.0f, viewH * viewH / (viewH + m.scrollMax));
            const float travel = viewH - thumbH;
            const float barX = right - 14.0f;
            const bool onBar = Inside(mouse.x, mouse.y, barX - 8.0f, contentTop, 22.0f, viewH);
            if (mouse.pressed && onBar) m.dragging = &m.scroll;
            if (m.dragging == &m.scroll) {
                if (mouse.down) {
                    const float f = std::clamp((mouse.y - contentTop - thumbH * 0.5f) / travel, 0.0f, 1.0f);
                    m.scroll = f * m.scrollMax;
                } else {
                    m.dragging = nullptr;
                }
            }
            const float thumbY = contentTop + travel * (m.scroll / m.scrollMax);
            r.Rect(barX, contentTop, 6.0f, viewH, kTrack, 0.7f);
            r.Rect(barX, thumbY, 6.0f, thumbH, (m.dragging == &m.scroll || onBar) ? kAccent : kTextDim, 1.0f);
        }

        const float scrollTop = contentTop - m.scroll;
        switch (m.tab) {
            case kWorld:      WorldTab(c, scrollTop); break;
            case kGeneration: GenerationTab(c, scrollTop); break;
            case kThemes:     ThemesTab(c, scrollTop); break;
            case kWalker:     WalkerTab(c, scrollTop); break;
            case kDebug:      DebugTab(c, scrollTop); break;
            case kFiles:      FilesTab(c, scrollTop); break;
            default: break;
        }

        // How far the content can scroll, for the next frame's wheel and bar.
        const float extent = c.lowest - contentTop + m.scroll;   // content height before scrolling
        m.scrollMax = std::max(0.0f, extent - viewH);
        m.scroll = std::min(m.scroll, m.scrollMax);
        c.clip = false;
    }

    // Footer buttons and status. Resume is the primary action.
    MenuAction action = MenuAction::None;
    // The last button is as wide as its label, with room either side, so the label always fits
    const float bw1 = 170.0f, bw2 = 170.0f, gap = 12.0f;
    const float bw3 = std::max(140.0f, r.MeasureText("Return to Title") + 32.0f);
    const float bx = right - kPad - (bw1 + bw2 + bw3 + gap * 2.0f);
    if (Button(c, bx, footY, bw1, "Resume", !picking, true)) action = MenuAction::Resume;
    if (Button(c, bx + bw1 + gap, footY, bw2, "Console", !picking)) action = MenuAction::OpenConsole;
    if (Button(c, bx + bw1 + bw2 + gap * 2.0f, footY, bw3, "Return to Title", !picking)) action = MenuAction::Quit;
    if (!m.status.empty()) {
        const float avail = bx - 20.0f - (c.x0 + kPad);
        r.Text(c.x0 + kPad, footY + (c.btnH - c.lh) * 0.5f, FitText(r, m.status, avail), kAccent);
    }

    if (picking) DrawPicker(c);

    // Clicking outside a text field stops typing into it.
    if (mouse.pressed && !c.hitField) MenuStopEditing(m);

    return action;
}

}  // namespace wm
