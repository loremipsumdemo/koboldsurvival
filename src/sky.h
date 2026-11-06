#pragma once
#include <array>
#include "enum_index.h"
#include "language.h"

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

struct SkyColors {
    std::array<std::array<float, 3>, COLOR_TARGET_COUNT> colors = {{
        {{ 0.22f, 0.45f, 0.85f }},
        {{ 0.70f, 0.84f, 1.00f }},
        {{ 0.02f, 0.03f, 0.08f }},
        {{ 0.08f, 0.10f, 0.18f }},
        {{ 1.00f, 0.55f, 0.30f }},
        {{ 1.00f, 0.60f, 0.35f }},
        {{ 1.00f, 0.98f, 0.90f }},
        {{ 0.80f, 0.86f, 1.00f }},
    }};
};
