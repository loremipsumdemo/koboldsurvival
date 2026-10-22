#include "theme/theme.hpp"

#include <algorithm>
#include <cmath>

namespace wm {


namespace {

V3 Rgb(const float* c) { return {c[0], c[1], c[2]}; }

V3 Mix(V3 a, V3 b, float t) { return a + (b - a) * t; }

}  // namespace

SkyState ComputeSky(const Look& l) {
    SkyState s;

    // The sun follows a sine over the day: rising at 6, highest at 12, setting at 18.
    const float angle = (l.clock - 6.0f) / 24.0f * 2.0f * kPi;
    const float elev = l.sunElevation * kDeg2Rad * std::sin(angle);
    const float az = l.sunAzimuth * kDeg2Rad;
    s.sunDir = {std::cos(elev) * std::sin(az), std::sin(elev), std::cos(elev) * std::cos(az)};
    s.sunElevDeg = elev / kDeg2Rad;

    // Smooth blend between night and day around the horizon.
    float t = (std::sin(elev) + 0.10f) / 0.30f;
    t = std::clamp(t, 0.0f, 1.0f);
    s.daylight = t * t * (3.0f - 2.0f * t);

    s.top = Mix(Rgb(l.nightTop), Rgb(l.skyTop), s.daylight);
    s.horizon = Mix(Rgb(l.nightHorizon), Rgb(l.skyHorizon), s.daylight);
    // Sunrise and sunset: the sun turns orange, and the horizon warms, while the sun
    // is low. Full strength when it touches the horizon, gone a little above it.
    const float warm = std::exp(-std::pow(s.sunDir.y / 0.25f, 2.0f)) * std::clamp(l.sunsetGlow, 0.0f, 1.0f);
    const V3 sunTint = Mix(Rgb(l.sunRgb) * ColourOpacity(l, kColSun), Rgb(l.sunGlowRgb) * ColourOpacity(l, kColSunGlow), warm * 0.8f);
    s.horizon = Mix(s.horizon, Rgb(l.horizonWarmRgb), warm * 0.35f * ColourOpacity(l, kColHorizonWarm));

    s.sunColour = sunTint;
    s.sunStrength = l.sunStrength;
    s.light = sunTint * (l.ambient + l.sunStrength * s.daylight);
    s.stars = 1.0f - s.daylight;
    s.fog = l.fogEnabled >= 0.5f ? l.fog : 0.0f;
    s.ambientLight = sunTint * (l.ambient * (0.3f + 0.7f * s.daylight));
    s.sunLight = sunTint * (l.sunStrength * s.daylight);
    s.waterTime = l.time;
    s.rockRgb = V3{l.rockRgb[0], l.rockRgb[1], l.rockRgb[2]};
    s.rockSlope = l.rockSlope;
    s.waterOpacity = l.waterOpacity * ColourOpacity(l, kColWater);
    s.waterPattern = l.waterPattern;
    s.waterSpeed = l.waterSpeed;
    s.starRgb = Rgb(l.starRgb) * ColourOpacity(l, kColStar);
    s.starAmount = l.starAmount;
    s.moonDir = V3{-s.sunDir.x, -s.sunDir.y, -s.sunDir.z};
    s.moonRgb = Rgb(l.moonRgb) * ColourOpacity(l, kColMoon);
    s.moonStrength = l.moonStrength;
    s.moonCos = std::cos(std::clamp(l.moonSize, 0.1f, 10.0f) * kDeg2Rad);
    s.fogColour = Mix(s.horizon, Rgb(l.fogRgb), ColourOpacity(l, kColFog));
    s.underwaterRgb = Rgb(l.underwaterRgb);
    s.waterRgb = Rgb(l.waterRgb);
    return s;
}

void AdvanceClock(Look& l, float dt) {
    l.time += dt;
    if (l.dayCycle < 0.5f) return;
    // Daytime runs from 6 to 18 and takes dayFraction of the cycle; night takes the rest.
    // Each part is 12 hours, so the clock moves faster when its part is shorter.
    const float f = std::clamp(l.dayFraction, 0.1f, 0.9f);
    const float L = std::max(l.dayLength, 1.0f);
    const bool day = l.clock >= 6.0f && l.clock < 18.0f;
    l.clock += 12.0f * dt / (day ? f * L : (1.0f - f) * L);
    while (l.clock >= 24.0f) l.clock -= 24.0f;
}

}  // namespace wm
