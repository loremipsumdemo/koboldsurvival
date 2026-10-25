#pragma once

#include <algorithm>

#include "core/math.hpp"

namespace wm {


// Every value the theme can change. All fields are floats so the console can
// set them by name, and they can be written to a theme file later.
// The colors a theme sets, each with an opacity. The order is the order of Look::colorOpacity.
enum ColorSlot {
    kColSun = 0, kColSunGlow, kColHorizonWarm, kColStar, kColMoon, kColFog, kColUnderwater,
    kColWater, kColRock, kColGridMinor, kColGridMajor, kColSkyTopDay, kColSkyHorizonDay,
    kColSkyTopNight, kColSkyHorizonNight, kColorCount
};

struct Look {
    float clock = 12.0f;            // hour of day, 0 to 24
    float dayCycle = 0.0f;          // 1 = time advances, 0 = frozen (off by default)
    float dayLength = 120.0f;       // seconds for one full day
    float dayFraction = 0.5f;       // share of the cycle that is daytime (0.1 to 0.9)
    float sunsetGlow = 0.6f;        // warm light and sky at sunrise and sunset, 0 to 1
    float sunAzimuth = 135.0f;      // compass direction of the sun at noon (degrees)
    float sunElevation = 60.0f;     // height of the sun at noon (degrees)
    float sunStrength = 0.9f;
    float sunRgb[3] = {1.0f, 0.96f, 0.82f};
    float ambient = 0.35f;          // light in shadow
    float skyTop[3] = {0.22f, 0.42f, 0.80f};
    float skyHorizon[3] = {0.72f, 0.84f, 0.94f};
    float nightTop[3] = {0.02f, 0.03f, 0.07f};
    float nightHorizon[3] = {0.05f, 0.07f, 0.13f};
    float fog = 0.0f;               // 0 = no fog
    float fogEnabled = 1.0f;        // 0 turns all distance fog off, the density is kept
    float rockRgb[3] = {0.46f, 0.43f, 0.39f};   // rock on steep slopes
    float rockSlope = 0.45f;        // steepness where rock starts (0.05 to 0.95)
    float waterOpacity = 1.0f;      // multiplies the water's transparency
    float waterPattern = 1.0f;      // strength of the light bands on the water
    float waterSpeed = 1.0f;        // how fast the water surface moves
    float gridMinor[3] = {0.14f, 0.35f, 0.47f};
    float gridMajor[3] = {0.0f, 0.86f, 1.0f};
    float sunGlowRgb[3] = {1.0f, 0.55f, 0.25f};      // sun color at sunrise and sunset
    float horizonWarmRgb[3] = {0.95f, 0.50f, 0.30f}; // horizon color at sunrise and sunset
    float starRgb[3] = {1.0f, 1.0f, 1.0f};
    float starAmount = 0.8f;        // star brightness, 0 = no stars
    float moonRgb[3] = {0.75f, 0.82f, 0.95f};
    float moonStrength = 0.6f;      // moon brightness, 0 = no moon. The moon sits opposite the sun.
    float moonSize = 1.6f;          // moon disc radius in degrees
    float fogRgb[3] = {0.72f, 0.84f, 0.94f};
    float underwaterRgb[3] = {0.05f, 0.22f, 0.34f};  // the sea floor as seen through the water
    float waterRgb[3] = {0.10f, 0.30f, 0.50f};       // the sea surface
    // Opacity of each color above, 0 to 1, indexed by ColorSlot. Lights use it as brightness,
    // fog and water use it as blend strength. Fog starts at 0 so the default look is unchanged.
    float colorOpacity[kColorCount] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    float time = 0.0f;              // seconds since start, drives the water ripples (not a theme value)
};

// Values derived from a Look for one frame.
// Opacity of a color slot, clamped to 0..1.
inline float ColorOpacity(const Look& l, int slot) { return std::clamp(l.colorOpacity[slot], 0.0f, 1.0f); }

struct SkyState {
    V3 sunDir{0.0f, 1.0f, 0.0f};    // unit vector pointing at the sun
    V3 sunColor{1.0f, 1.0f, 1.0f};
    V3 top{0.0f, 0.0f, 0.0f};       // sky color overhead right now
    V3 horizon{0.0f, 0.0f, 0.0f};   // sky color at the horizon right now, also the fog color
    V3 light{1.0f, 1.0f, 1.0f};     // multiplier for lit world geometry
    float sunStrength = 1.0f;
    float sunElevDeg = 0.0f;
    float daylight = 1.0f;          // 0 at night, 1 in full day
    float stars = 0.0f;             // 0 to 1
    float fog = 1.0f;
    V3 ambientLight{0.0f, 0.0f, 0.0f};   // shadow fill for terrain
    V3 sunLight{0.0f, 0.0f, 0.0f};       // direct sun for terrain, zero at night
    float waterTime = 0.0f;              // ripple phase in seconds
    V3 rockRgb{0.46f, 0.43f, 0.39f};     // rock color on steep slopes
    float rockSlope = 0.45f;             // where rock starts
    float waterOpacity = 1.0f;           // multiplies water alpha
    float waterPattern = 1.0f;           // strength of the water light bands and glints
    float waterSpeed = 1.0f;             // multiplies the water's motion
    V3 starRgb{1.0f, 1.0f, 1.0f};
    float starAmount = 0.8f;
    V3 moonDir{0.0f, 1.0f, 0.0f};        // unit vector pointing at the moon
    V3 moonRgb{0.75f, 0.82f, 0.95f};
    float moonStrength = 0.6f;
    float moonCos = 0.9996f;             // cosine of the moon's angular radius
    V3 fogColor{0.0f, 0.0f, 0.0f};      // color the distance fades to
    V3 underwaterRgb{0.05f, 0.22f, 0.34f};
    V3 waterRgb{0.10f, 0.30f, 0.50f};
};

SkyState ComputeSky(const Look& look);
// The same, with the sun taken from sunDir (a unit vector) instead of the look's clock. The game uses
// this so its ground and sea light the same way as the world maker, with the sun its own sky shows.
SkyState ComputeSkyForSun(const Look& look, V3 sunDir);

// Moves the clock forward when the day cycle is on.
void AdvanceClock(Look& look, float dt);

}  // namespace wm
