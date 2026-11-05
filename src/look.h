#pragma once
#include <string>
#include "geometry.h"

// A world's look: the sky, sun, moon, fog, rock and water colors and the water's motion. Each world folder keeps its
// look in look.json (world_format.h). The terrain renderer shades the ground and the sea with it.

// The colors a look sets, each with an opacity. The order is the order of Look::colorOpacity, and of the
// color_opacity list in look.json, so it must not change.
enum class LookColor : int {
    Sun = 0, SunGlow, HorizonWarm, Star, Moon, Fog, Underwater, Water, Rock, GridMinor, GridMajor,
    SkyTopDay, SkyHorizonDay, SkyTopNight, SkyHorizonNight, Count
};
constexpr int LOOK_COLOR_COUNT = (int)LookColor::Count;

// Every value a look file holds. All are floats, so the file reader can treat them alike. A few (the clock, the day
// cycle, the grid colors) are not used by the game, but are kept so a look file is written back as it was read.
struct Look {
    float clock = 12.0f;            // hour of day, 0 to 24
    float dayCycle = 0.0f;          // 1 = time advances, 0 = frozen
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
    // Opacity of each color above, 0 to 1, indexed by LookColor. Lights use it as brightness, fog and water use it
    // as blend strength. Fog starts at 0 so the default look is unchanged.
    float colorOpacity[LOOK_COLOR_COUNT] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    float time = 0.0f;              // seconds since start, drives the water ripples (not saved)
};

// The light and colors the terrain is shaded with for one frame, worked out from a Look and the sun
struct SkyLight {
    Vec3 sunDir{0.0f, 1.0f, 0.0f};       // unit vector pointing at the sun
    Vec3 sunColor{1.0f, 1.0f, 1.0f};
    Vec3 top{0.0f, 0.0f, 0.0f};          // sky color overhead right now
    Vec3 horizon{0.0f, 0.0f, 0.0f};      // sky color at the horizon right now
    Vec3 light{1.0f, 1.0f, 1.0f};        // multiplier for lit world geometry
    float sunStrength = 1.0f;
    float daylight = 1.0f;               // 0 at night, 1 in full day
    float stars = 0.0f;                  // 0 to 1
    float fog = 1.0f;
    Vec3 ambientLight{0.0f, 0.0f, 0.0f}; // shadow fill for terrain
    Vec3 sunLight{0.0f, 0.0f, 0.0f};     // direct sun for terrain, zero at night
    float waterTime = 0.0f;              // ripple phase in seconds
    Vec3 rockRgb{0.46f, 0.43f, 0.39f};   // rock color on steep slopes
    float rockSlope = 0.45f;             // where rock starts
    float waterOpacity = 1.0f;           // multiplies water alpha
    float waterPattern = 1.0f;           // strength of the water light bands and glints
    float waterSpeed = 1.0f;             // multiplies the water's motion
    Vec3 starRgb{1.0f, 1.0f, 1.0f};
    float starAmount = 0.8f;
    Vec3 moonDir{0.0f, 1.0f, 0.0f};      // unit vector pointing at the moon
    Vec3 moonRgb{0.75f, 0.82f, 0.95f};
    float moonStrength = 0.6f;
    float moonCos = 0.9996f;             // cosine of the moon's angular radius
    Vec3 fogColor{0.0f, 0.0f, 0.0f};     // color the distance fades to
    Vec3 underwaterRgb{0.05f, 0.22f, 0.34f};
    Vec3 waterRgb{0.10f, 0.30f, 0.50f};
};

// The light for a look, with the sun at sunDir (a unit vector), the sun the game's sky shows
SkyLight skyLightFor(const Look& look, Vec3 sunDir);

// look.json is JSON: {"format": "worldmaker-look", "version": 1, "clock": 12, "sun_rgb": [1, 0.9, 0.8], ...}. The
// format name is kept as it is so the look files of existing worlds still load.
bool saveLookFile(const Look& look, const std::string& path, std::string& err);
// Reads the text of a look file. Missing keys keep look's value; a bad file changes nothing and returns false.
bool loadLookText(const std::string& text, Look& look, std::string& err);
