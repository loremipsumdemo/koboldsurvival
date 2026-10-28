#pragma once
#include <array>
#include "language.h"

// The sky colors. The sky blends between day and night colors using the
// sun's height, and the dusk colors are used near the horizon at sunrise and sunset.
enum ColorTarget {
    CT_DAY_ZENITH,
    CT_DAY_HORIZON,
    CT_NIGHT_ZENITH,
    CT_NIGHT_HORIZON,
    CT_DUSK_HORIZON,
    CT_DUSK_SUN,
    CT_SUN,
    CT_MOON,
    CT_COUNT
};

inline std::string colorTargetName(int i) {
    static const char* const keys[CT_COUNT] = {
        "MISC_SKY_DAY_ZENITH", "MISC_SKY_DAY_HORIZON", "MISC_SKY_NIGHT_ZENITH", "MISC_SKY_NIGHT_HORIZON",
        "MISC_SKY_DUSK_HORIZON", "MISC_SKY_DUSK_SUN", "MISC_SKY_SUN", "MISC_SKY_MOON"
    };
    return lang(keys[i]);
}

// Atmosphere settings. Colors are RGB, each 0..1.
struct SkyTuning {
    bool clockRunning = true;     // the in-game clock advances during play
    bool stars = true;
    bool moon = true;
    float sunBrightness = 1.0f;   // 0..2, scales the sun's glow and disc
    float starBrightness = 1.0f;  // 0..2
    int colorTarget = 0;          // which color the R/G/B buttons edit, a ColorTarget

    std::array<std::array<float, 3>, CT_COUNT> colors = {{
        {{ 0.22f, 0.45f, 0.85f }},   // CT_DAY_ZENITH
        {{ 0.70f, 0.84f, 1.00f }},   // CT_DAY_HORIZON
        {{ 0.02f, 0.03f, 0.08f }},   // CT_NIGHT_ZENITH
        {{ 0.08f, 0.10f, 0.18f }},   // CT_NIGHT_HORIZON
        {{ 1.00f, 0.55f, 0.30f }},   // CT_DUSK_HORIZON
        {{ 1.00f, 0.60f, 0.35f }},   // CT_DUSK_SUN
        {{ 1.00f, 0.98f, 0.90f }},   // CT_SUN
        {{ 0.80f, 0.86f, 1.00f }},   // CT_MOON
    }};
};
