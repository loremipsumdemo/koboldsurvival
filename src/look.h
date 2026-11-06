#pragma once
#include <string>
#include "geometry.h"

// The colors a look sets, each with an opacity. The order is the order of Look::colorOpacity, and of the
// color_opacity list in look.json, so it must not change, even for colors the game does not use.
enum class LookColor : int {
    Sun = 0, SunGlow, HorizonWarm, Star, Moon, Fog, Underwater, Water, Rock, GridMinor, GridMajor,
    SkyTopDay, SkyHorizonDay, SkyTopNight, SkyHorizonNight, Count
};
constexpr int LOOK_COLOR_COUNT = (int)LookColor::Count;

struct Look {
    float sunsetGlow = 0.6f;
    float sunStrength = 0.9f;
    float sunRgb[3] = {1.0f, 0.96f, 0.82f};
    float ambient = 0.35f;
    float skyHorizon[3] = {0.72f, 0.84f, 0.94f};
    float nightHorizon[3] = {0.05f, 0.07f, 0.13f};
    float fog = 0.0f;
    float fogEnabled = 1.0f;
    float rockRgb[3] = {0.46f, 0.43f, 0.39f};
    float rockSlope = 0.45f;
    float waterOpacity = 1.0f;
    float waterPattern = 1.0f;
    float waterSpeed = 1.0f;
    float sunGlowRgb[3] = {1.0f, 0.55f, 0.25f};
    float horizonWarmRgb[3] = {0.95f, 0.50f, 0.30f};
    float fogRgb[3] = {0.72f, 0.84f, 0.94f};
    float underwaterRgb[3] = {0.05f, 0.22f, 0.34f};
    float waterRgb[3] = {0.10f, 0.30f, 0.50f};
    float colorOpacity[LOOK_COLOR_COUNT] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
};

struct SkyLight {
    Vec3 sunDir{0.0f, 1.0f, 0.0f};
    float fog = 1.0f;
    Vec3 ambientLight{0.0f, 0.0f, 0.0f};
    Vec3 sunLight{0.0f, 0.0f, 0.0f};
    Vec3 rockRgb{0.46f, 0.43f, 0.39f};
    float rockSlope = 0.45f;
    float waterOpacity = 1.0f;
    float waterPattern = 1.0f;
    float waterSpeed = 1.0f;
    Vec3 fogColor{0.0f, 0.0f, 0.0f};
    Vec3 underwaterRgb{0.05f, 0.22f, 0.34f};
    Vec3 waterRgb{0.10f, 0.30f, 0.50f};
};

SkyLight skyLightFor(const Look& look, Vec3 sunDir);

bool loadLookText(const std::string& text, Look& look, std::string& err);
