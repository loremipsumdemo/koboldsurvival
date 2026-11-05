#pragma once
#include <array>
#include "enum_index.h"
#include "language.h"

// The sky colors. The sky blends between day and night colors using the
// sun's height, and the dusk colors are used near the horizon at sunrise and sunset.
enum class ColorTarget : int {
    DayZenith,
    DayHorizon,
    NightZenith,
    NightHorizon,
    DuskHorizon,
    DuskSun,
    Sun,
    Moon,
    Count
};
constexpr int COLOR_TARGET_COUNT = idx(ColorTarget::Count);

// Atmosphere settings. Colors are RGB, each 0..1.
struct SkyTuning {
    bool clockRunning = true;     // the in-game clock advances during play
    bool stars = true;
    bool moon = true;
    float sunBrightness = 1.0f;   // 0..2, scales the sun's glow and disc
    float starBrightness = 1.0f;  // 0..2
    int colorTarget = 0;          // which color the R/G/B buttons edit, a ColorTarget

    std::array<std::array<float, 3>, COLOR_TARGET_COUNT> colors = {{
        {{ 0.22f, 0.45f, 0.85f }},   // ColorTarget::DayZenith
        {{ 0.70f, 0.84f, 1.00f }},   // ColorTarget::DayHorizon
        {{ 0.02f, 0.03f, 0.08f }},   // ColorTarget::NightZenith
        {{ 0.08f, 0.10f, 0.18f }},   // ColorTarget::NightHorizon
        {{ 1.00f, 0.55f, 0.30f }},   // ColorTarget::DuskHorizon
        {{ 1.00f, 0.60f, 0.35f }},   // ColorTarget::DuskSun
        {{ 1.00f, 0.98f, 0.90f }},   // ColorTarget::Sun
        {{ 0.80f, 0.86f, 1.00f }},   // ColorTarget::Moon
    }};
};
