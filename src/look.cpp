#include "look.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <vector>

namespace {
Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }

Vec3 rgb(const float* c) { return {c[0], c[1], c[2]}; }
Vec3 mix(Vec3 a, Vec3 b, float t) { return a + (b - a) * t; }

float opacity(const Look& l, LookColor c) { return std::clamp(l.colorOpacity[(int)c], 0.0f, 1.0f); }

struct LookField {
    const char* name;
    std::size_t offset;
    int count;
    float lo, hi;
};

#define LOOK_FIELD(member) offsetof(Look, member)

const std::vector<LookField>& lookFields() {
    static const std::vector<LookField> fields = {
        {"sunset_glow",      LOOK_FIELD(sunsetGlow),     1,  0.0f,   1.0f},
        {"sun_strength",     LOOK_FIELD(sunStrength),    1,  0.0f,   1.5f},
        {"sun_rgb",          LOOK_FIELD(sunRgb),         3,  0.0f,   1.0f},
        {"ambient",          LOOK_FIELD(ambient),        1,  0.0f,   1.0f},
        {"sky_horizon",      LOOK_FIELD(skyHorizon),     3,  0.0f,   1.0f},
        {"night_horizon",    LOOK_FIELD(nightHorizon),   3,  0.0f,   1.0f},
        {"fog",              LOOK_FIELD(fog),            1,  0.0f,   4.0f},
        {"fog_enabled",      LOOK_FIELD(fogEnabled),     1,  0.0f,   1.0f},
        {"rock_rgb",         LOOK_FIELD(rockRgb),        3,  0.0f,   1.0f},
        {"rock_slope",       LOOK_FIELD(rockSlope),      1,  0.05f,  0.95f},
        {"water_opacity",    LOOK_FIELD(waterOpacity),   1,  0.0f,   1.0f},
        {"water_pattern",    LOOK_FIELD(waterPattern),   1,  0.0f,   2.0f},
        {"water_speed",      LOOK_FIELD(waterSpeed),     1,  0.0f,   3.0f},
        {"sun_glow_rgb",     LOOK_FIELD(sunGlowRgb),     3,  0.0f,   1.0f},
        {"horizon_warm_rgb", LOOK_FIELD(horizonWarmRgb), 3,  0.0f,   1.0f},
        {"fog_rgb",          LOOK_FIELD(fogRgb),         3,  0.0f,   1.0f},
        {"underwater_rgb",   LOOK_FIELD(underwaterRgb),  3,  0.0f,   1.0f},
        {"water_rgb",        LOOK_FIELD(waterRgb),       3,  0.0f,   1.0f},
        {"color_opacity",    LOOK_FIELD(colorOpacity),   LOOK_COLOR_COUNT, 0.0f, 1.0f},
    };
    return fields;
}

#undef LOOK_FIELD

float* fieldValue(Look& look, const LookField& f) {
    return reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(&look) + f.offset);
}

std::size_t findValue(const std::string& text, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    std::size_t p = text.find(needle);
    if (p == std::string::npos) return std::string::npos;
    p += needle.size();
    while (p < text.size() && std::isspace((unsigned char)text[p])) ++p;
    if (p >= text.size() || text[p] != ':') return std::string::npos;
    return p + 1;
}

int readValue(const std::string& text, std::size_t p, float* vals, int want) {
    while (p < text.size() && std::isspace((unsigned char)text[p])) ++p;
    if (p >= text.size()) return 0;

    if (text[p] == '[') {
        ++p;
        int n = 0;
        while (n < want && p < text.size()) {
            char* end = nullptr;
            const float v = std::strtof(text.c_str() + p, &end);
            if (end == text.c_str() + p) return 0;
            vals[n++] = v;
            p = (std::size_t)(end - text.c_str());
            while (p < text.size() && (std::isspace((unsigned char)text[p]) || text[p] == ',')) ++p;
        }
        return n == want ? n : 0;
    }

    char* end = nullptr;
    const float v = std::strtof(text.c_str() + p, &end);
    if (end == text.c_str() + p) return 0;
    vals[0] = v;
    return 1;
}
}

SkyLight skyLightFor(const Look& l, Vec3 sunDir) {
    SkyLight s;
    s.sunDir = sunDir;

    const float t = std::clamp((sunDir.y + 0.10f) / 0.30f, 0.0f, 1.0f);
    const float daylight = t * t * (3.0f - 2.0f * t);

    const float warm = std::exp(-std::pow(s.sunDir.y / 0.25f, 2.0f)) * std::clamp(l.sunsetGlow, 0.0f, 1.0f);
    const Vec3 sunTint = mix(rgb(l.sunRgb) * opacity(l, LookColor::Sun),
                             rgb(l.sunGlowRgb) * opacity(l, LookColor::SunGlow), warm * 0.8f);
    const Vec3 horizon = mix(mix(rgb(l.nightHorizon), rgb(l.skyHorizon), daylight), rgb(l.horizonWarmRgb),
                             warm * 0.35f * opacity(l, LookColor::HorizonWarm));

    s.fog = l.fogEnabled >= 0.5f ? l.fog : 0.0f;
    s.ambientLight = sunTint * (l.ambient * (0.3f + 0.7f * daylight));
    s.sunLight = sunTint * (l.sunStrength * daylight);
    s.rockRgb = rgb(l.rockRgb);
    s.rockSlope = l.rockSlope;
    s.waterOpacity = l.waterOpacity * opacity(l, LookColor::Water);
    s.waterPattern = l.waterPattern;
    s.waterSpeed = l.waterSpeed;
    s.fogColor = mix(horizon, rgb(l.fogRgb), opacity(l, LookColor::Fog));
    s.underwaterRgb = rgb(l.underwaterRgb);
    s.waterRgb = rgb(l.waterRgb);
    return s;
}

bool loadLookText(const std::string& text, Look& look, std::string& err) {
    const std::size_t fmt = findValue(text, "format");
    if (fmt == std::string::npos || text.find("worldmaker-look", fmt) == std::string::npos) {
        err = "not a look file";
        return false;
    }

    Look loaded = look;
    for (const LookField& fld : lookFields()) {
        std::size_t p = findValue(text, fld.name);
        if (p == std::string::npos) continue;

        std::vector<float> vals((std::size_t)fld.count, 0.0f);
        if (readValue(text, p, vals.data(), fld.count) != fld.count) {
            err = std::string("bad value for ") + fld.name;
            return false;
        }
        float* dst = fieldValue(loaded, fld);
        for (int i = 0; i < fld.count; ++i) dst[i] = std::clamp(vals[(std::size_t)i], fld.lo, fld.hi);
    }

    look = loaded;
    return true;
}
