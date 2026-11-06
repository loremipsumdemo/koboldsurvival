#pragma once
#include <string>
#include "geometry.h"

// The colors a look sets, each with an opacity. The order is the order of Look::colorOpacity, and of the
// color_opacity list in look.json, so it must not change.
enum class LookColor : int {
    Sun = 0, SunGlow, HorizonWarm, Star, Moon, Fog, Underwater, Water, Rock, GridMinor, GridMajor,
    SkyTopDay, SkyHorizonDay, SkyTopNight, SkyHorizonNight, Count
};
constexpr int LOOK_COLOR_COUNT = (int)LookColor::Count;

struct Look {
    float clock = 12.0f;
    float dayCycle = 0.0f;
    float dayLength = 120.0f;
    float dayFraction = 0.5f;
    float sunsetGlow = 0.6f;
    float sunAzimuth = 135.0f;
    float sunElevation = 60.0f;
    float sunStrength = 0.9f;
    float sunRgb[3] = {1.0f, 0.96f, 0.82f};
    float ambient = 0.35f;
    float skyTop[3] = {0.22f, 0.42f, 0.80f};
    float skyHorizon[3] = {0.72f, 0.84f, 0.94f};
    float nightTop[3] = {0.02f, 0.03f, 0.07f};
    float nightHorizon[3] = {0.05f, 0.07f, 0.13f};
    float fog = 0.0f;
    float fogEnabled = 1.0f;
    float rockRgb[3] = {0.46f, 0.43f, 0.39f};
    float rockSlope = 0.45f;
    float waterOpacity = 1.0f;
    float waterPattern = 1.0f;
    float waterSpeed = 1.0f;
    float gridMinor[3] = {0.14f, 0.35f, 0.47f};
    float gridMajor[3] = {0.0f, 0.86f, 1.0f};
    float sunGlowRgb[3] = {1.0f, 0.55f, 0.25f};
    float horizonWarmRgb[3] = {0.95f, 0.50f, 0.30f};
    float starRgb[3] = {1.0f, 1.0f, 1.0f};
    float starAmount = 0.8f;
    float moonRgb[3] = {0.75f, 0.82f, 0.95f};
    float moonStrength = 0.6f;
    float moonSize = 1.6f;
    float fogRgb[3] = {0.72f, 0.84f, 0.94f};
    float underwaterRgb[3] = {0.05f, 0.22f, 0.34f};
    float waterRgb[3] = {0.10f, 0.30f, 0.50f};
    float colorOpacity[LOOK_COLOR_COUNT] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    float time = 0.0f;
};

struct SkyLight {
    Vec3 sunDir{0.0f, 1.0f, 0.0f};
    Vec3 sunColor{1.0f, 1.0f, 1.0f};
    Vec3 top{0.0f, 0.0f, 0.0f};
    Vec3 horizon{0.0f, 0.0f, 0.0f};
    Vec3 light{1.0f, 1.0f, 1.0f};
    float sunStrength = 1.0f;
    float daylight = 1.0f;
    float stars = 0.0f;
    float fog = 1.0f;
    Vec3 ambientLight{0.0f, 0.0f, 0.0f};
    Vec3 sunLight{0.0f, 0.0f, 0.0f};
    float waterTime = 0.0f;
    Vec3 rockRgb{0.46f, 0.43f, 0.39f};
    float rockSlope = 0.45f;
    float waterOpacity = 1.0f;
    float waterPattern = 1.0f;
    float waterSpeed = 1.0f;
    Vec3 starRgb{1.0f, 1.0f, 1.0f};
    float starAmount = 0.8f;
    Vec3 moonDir{0.0f, 1.0f, 0.0f};
    Vec3 moonRgb{0.75f, 0.82f, 0.95f};
    float moonStrength = 0.6f;
    float moonCos = 0.9996f;
    Vec3 fogColor{0.0f, 0.0f, 0.0f};
    Vec3 underwaterRgb{0.05f, 0.22f, 0.34f};
    Vec3 waterRgb{0.10f, 0.30f, 0.50f};
};

SkyLight skyLightFor(const Look& look, Vec3 sunDir);

bool saveLookFile(const Look& look, const std::string& path, std::string& err);
bool loadLookText(const std::string& text, Look& look, std::string& err);
