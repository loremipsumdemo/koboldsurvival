#include "look.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr float DEG_TO_RAD = 3.14159265358979f / 180.0f;

Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }

Vec3 rgb(const float* c) { return {c[0], c[1], c[2]}; }
Vec3 mix(Vec3 a, Vec3 b, float t) { return a + (b - a) * t; }

// Opacity of a color, clamped to 0..1
float opacity(const Look& l, LookColor c) { return std::clamp(l.colorOpacity[(int)c], 0.0f, 1.0f); }

// One value a look file holds. Colors have count 3, color_opacity one per LookColor, everything else 1.
struct LookField {
    const char* name;
    std::size_t offset;   // position of the first float inside Look
    int count;
    float lo, hi;         // a value read from a file is clamped to this range
};

#define LOOK_FIELD(member) offsetof(Look, member)

const std::vector<LookField>& lookFields() {
    static const std::vector<LookField> fields = {
        {"clock",            LOOK_FIELD(clock),          1,  0.0f,  24.0f},
        {"cycle",            LOOK_FIELD(dayCycle),       1,  0.0f,   1.0f},
        {"day_length",       LOOK_FIELD(dayLength),      1, 10.0f, 3600.0f},
        {"day_fraction",     LOOK_FIELD(dayFraction),    1,  0.1f,   0.9f},
        {"sunset_glow",      LOOK_FIELD(sunsetGlow),     1,  0.0f,   1.0f},
        {"sun_azimuth",      LOOK_FIELD(sunAzimuth),     1,  0.0f, 360.0f},
        {"sun_elevation",    LOOK_FIELD(sunElevation),   1,  5.0f,  85.0f},
        {"sun_strength",     LOOK_FIELD(sunStrength),    1,  0.0f,   1.5f},
        {"sun_rgb",          LOOK_FIELD(sunRgb),         3,  0.0f,   1.0f},
        {"ambient",          LOOK_FIELD(ambient),        1,  0.0f,   1.0f},
        {"sky_top",          LOOK_FIELD(skyTop),         3,  0.0f,   1.0f},
        {"sky_horizon",      LOOK_FIELD(skyHorizon),     3,  0.0f,   1.0f},
        {"night_top",        LOOK_FIELD(nightTop),       3,  0.0f,   1.0f},
        {"night_horizon",    LOOK_FIELD(nightHorizon),   3,  0.0f,   1.0f},
        {"fog",              LOOK_FIELD(fog),            1,  0.0f,   4.0f},
        {"fog_enabled",      LOOK_FIELD(fogEnabled),     1,  0.0f,   1.0f},
        {"rock_rgb",         LOOK_FIELD(rockRgb),        3,  0.0f,   1.0f},
        {"rock_slope",       LOOK_FIELD(rockSlope),      1,  0.05f,  0.95f},
        {"water_opacity",    LOOK_FIELD(waterOpacity),   1,  0.0f,   1.0f},
        {"water_pattern",    LOOK_FIELD(waterPattern),   1,  0.0f,   2.0f},
        {"water_speed",      LOOK_FIELD(waterSpeed),     1,  0.0f,   3.0f},
        {"grid_minor",       LOOK_FIELD(gridMinor),      3,  0.0f,   1.0f},
        {"grid_major",       LOOK_FIELD(gridMajor),      3,  0.0f,   1.0f},
        {"sun_glow_rgb",     LOOK_FIELD(sunGlowRgb),     3,  0.0f,   1.0f},
        {"horizon_warm_rgb", LOOK_FIELD(horizonWarmRgb), 3,  0.0f,   1.0f},
        {"star_rgb",         LOOK_FIELD(starRgb),        3,  0.0f,   1.0f},
        {"star_amount",      LOOK_FIELD(starAmount),     1,  0.0f,   3.0f},
        {"moon_rgb",         LOOK_FIELD(moonRgb),        3,  0.0f,   1.0f},
        {"moon_strength",    LOOK_FIELD(moonStrength),   1,  0.0f,   3.0f},
        {"moon_size",        LOOK_FIELD(moonSize),       1,  0.3f,   8.0f},
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
const float* fieldValue(const Look& look, const LookField& f) {
    return reinterpret_cast<const float*>(reinterpret_cast<const unsigned char*>(&look) + f.offset);
}

// Finds "key": and returns the position just after the colon, or npos
std::size_t findValue(const std::string& text, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    std::size_t p = text.find(needle);
    if (p == std::string::npos) return std::string::npos;
    p += needle.size();
    while (p < text.size() && std::isspace((unsigned char)text[p])) ++p;
    if (p >= text.size() || text[p] != ':') return std::string::npos;
    return p + 1;
}

// Reads a number, or [a, b, c] for a list, into vals. Returns the count read, or 0.
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

} // namespace

SkyLight skyLightFor(const Look& l, Vec3 sunDir) {
    SkyLight s;
    s.sunDir = sunDir;

    // Smooth blend between night and day around the horizon
    const float t = std::clamp((sunDir.y + 0.10f) / 0.30f, 0.0f, 1.0f);
    s.daylight = t * t * (3.0f - 2.0f * t);

    s.top = mix(rgb(l.nightTop), rgb(l.skyTop), s.daylight);
    s.horizon = mix(rgb(l.nightHorizon), rgb(l.skyHorizon), s.daylight);
    // Sunrise and sunset: the sun turns orange, and the horizon warms, while the sun is low. Full strength when it
    // touches the horizon, gone a little above it.
    const float warm = std::exp(-std::pow(s.sunDir.y / 0.25f, 2.0f)) * std::clamp(l.sunsetGlow, 0.0f, 1.0f);
    const Vec3 sunTint = mix(rgb(l.sunRgb) * opacity(l, LookColor::Sun),
                             rgb(l.sunGlowRgb) * opacity(l, LookColor::SunGlow), warm * 0.8f);
    s.horizon = mix(s.horizon, rgb(l.horizonWarmRgb), warm * 0.35f * opacity(l, LookColor::HorizonWarm));

    s.sunColor = sunTint;
    s.sunStrength = l.sunStrength;
    s.light = sunTint * (l.ambient + l.sunStrength * s.daylight);
    s.stars = 1.0f - s.daylight;
    s.fog = l.fogEnabled >= 0.5f ? l.fog : 0.0f;
    s.ambientLight = sunTint * (l.ambient * (0.3f + 0.7f * s.daylight));
    s.sunLight = sunTint * (l.sunStrength * s.daylight);
    s.waterTime = l.time;
    s.rockRgb = rgb(l.rockRgb);
    s.rockSlope = l.rockSlope;
    s.waterOpacity = l.waterOpacity * opacity(l, LookColor::Water);
    s.waterPattern = l.waterPattern;
    s.waterSpeed = l.waterSpeed;
    s.starRgb = rgb(l.starRgb) * opacity(l, LookColor::Star);
    s.starAmount = l.starAmount;
    s.moonDir = Vec3{-s.sunDir.x, -s.sunDir.y, -s.sunDir.z};
    s.moonRgb = rgb(l.moonRgb) * opacity(l, LookColor::Moon);
    s.moonStrength = l.moonStrength;
    s.moonCos = std::cos(std::clamp(l.moonSize, 0.1f, 10.0f) * DEG_TO_RAD);
    s.fogColor = mix(s.horizon, rgb(l.fogRgb), opacity(l, LookColor::Fog));
    s.underwaterRgb = rgb(l.underwaterRgb);
    s.waterRgb = rgb(l.waterRgb);
    return s;
}

bool saveLookFile(const Look& look, const std::string& path, std::string& err) {
    std::ostringstream out;
    out << "{\n  \"format\": \"worldmaker-look\",\n  \"version\": 1";
    for (const LookField& f : lookFields()) {
        const float* p = fieldValue(look, f);
        out << ",\n  \"" << f.name << "\": ";
        char buf[32];
        if (f.count == 1) {
            std::snprintf(buf, sizeof buf, "%.6g", p[0]);
            out << buf;
        } else {
            out << "[";
            for (int i = 0; i < f.count; ++i) {
                std::snprintf(buf, sizeof buf, "%s%.6g", i ? ", " : "", p[i]);
                out << buf;
            }
            out << "]";
        }
    }
    out << "\n}\n";

    const fs::path target = fs::u8path(path);
    std::error_code ec;
    if (target.has_parent_path()) fs::create_directories(target.parent_path(), ec);

    // Written to a temporary file first, so a failed write leaves the old file whole
    const fs::path tmp = fs::u8path(path + ".tmp");
    {
        std::ofstream f(tmp, std::ios::trunc);
        if (!f) {
            err = "could not open for writing: " + tmp.string();
            return false;
        }
        f << out.str();
        if (!f) {
            err = "write failed: " + tmp.string();
            return false;
        }
    }
    fs::rename(tmp, target, ec);
    if (ec) {
        err = "could not replace " + path + ": " + ec.message();
        return false;
    }
    return true;
}

bool loadLookText(const std::string& text, Look& look, std::string& err) {
    const std::size_t fmt = findValue(text, "format");
    if (fmt == std::string::npos || text.find("worldmaker-look", fmt) == std::string::npos) {
        err = "not a look file";
        return false;
    }

    // Read into a copy so a bad file changes nothing
    Look loaded = look;
    for (const LookField& fld : lookFields()) {
        std::size_t p = findValue(text, fld.name);
        // Older world files spell it colour_opacity
        if (p == std::string::npos && std::string(fld.name) == "color_opacity") p = findValue(text, "colour_opacity");
        if (p == std::string::npos) continue;   // a missing key keeps its current value

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
