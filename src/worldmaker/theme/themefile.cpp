#include "theme/themefile.hpp"

#include <algorithm>
#include "core/tunables.hpp"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace wm {


namespace fs = std::filesystem;

#define LOOK_FIELD(member) offsetof(Look, member)

const std::vector<LookField>& LookFields() {
    static const std::vector<LookField> fields = {
        {"clock",          LOOK_FIELD(clock),         1,  0.0f,  24.0f,   "hour of day (0-24)"},
        {"cycle",          LOOK_FIELD(dayCycle),      1,  0.0f,   1.0f,   "run the day/night cycle (0 or 1)"},
        {"day_length",     LOOK_FIELD(dayLength),     1, 10.0f, 3600.0f,  "seconds for a full day"},
        {"day_fraction",   LOOK_FIELD(dayFraction),   1,  0.1f,  0.9f,    "share of the cycle that is daytime"},
        {"sunset_glow",    LOOK_FIELD(sunsetGlow),    1,  0.0f,  1.0f,    "warm light at sunrise and sunset"},
        {"sun_azimuth",    LOOK_FIELD(sunAzimuth),    1,  0.0f, 360.0f,   "sun compass direction (degrees)"},
        {"sun_elevation",  LOOK_FIELD(sunElevation),  1,  5.0f,  85.0f,   "sun height at noon (degrees)"},
        {"sun_strength",   LOOK_FIELD(sunStrength),   1,  0.0f,   1.5f,   "sun brightness"},
        {"sun_rgb",        LOOK_FIELD(sunRgb),        3,  0.0f,   1.0f,   "sun color"},
        {"ambient",        LOOK_FIELD(ambient),       1,  0.0f,   1.0f,   "light in shadow"},
        {"sky_top",        LOOK_FIELD(skyTop),        3,  0.0f,   1.0f,   "day sky overhead"},
        {"sky_horizon",    LOOK_FIELD(skyHorizon),    3,  0.0f,   1.0f,   "day sky at the horizon"},
        {"night_top",      LOOK_FIELD(nightTop),      3,  0.0f,   1.0f,   "night sky overhead"},
        {"night_horizon",  LOOK_FIELD(nightHorizon),  3,  0.0f,   1.0f,   "night sky at the horizon"},
        {"fog",            LOOK_FIELD(fog),           1,  0.0f,   4.0f,   "fog density (0 = off)"},
        {"fog_enabled",    LOOK_FIELD(fogEnabled),    1,  0.0f,   1.0f,   "fog switch: 1 = on, 0 = all fog off"},
        {"rock_rgb",       LOOK_FIELD(rockRgb),       3,  0.0f,   1.0f,   "rock color on steep slopes"},
        {"rock_slope",     LOOK_FIELD(rockSlope),     1,  0.05f,  0.95f,  "steepness where rock starts"},
        {"water_opacity",  LOOK_FIELD(waterOpacity),  1,  0.0f,   1.0f,   "water transparency multiplier"},
        {"water_pattern",  LOOK_FIELD(waterPattern),  1,  0.0f,   2.0f,   "strength of the light bands on the water"},
        {"water_speed",    LOOK_FIELD(waterSpeed),    1,  0.0f,   3.0f,   "how fast the water surface moves"},
        {"grid_minor",     LOOK_FIELD(gridMinor),     3,  0.0f,   1.0f,   "minor grid line color"},
        {"grid_major",     LOOK_FIELD(gridMajor),     3,  0.0f,   1.0f,   "major grid line color"},
        {"sun_glow_rgb",   LOOK_FIELD(sunGlowRgb),    3,  0.0f,   1.0f,   "sun color at sunrise and sunset"},
        {"horizon_warm_rgb", LOOK_FIELD(horizonWarmRgb), 3, 0.0f, 1.0f,   "horizon color at sunrise and sunset"},
        {"star_rgb",       LOOK_FIELD(starRgb),       3,  0.0f,   1.0f,   "star color"},
        {"star_amount",    LOOK_FIELD(starAmount),    1,  0.0f,   3.0f,   "star brightness (0 = no stars)"},
        {"moon_rgb",       LOOK_FIELD(moonRgb),       3,  0.0f,   1.0f,   "moon color"},
        {"moon_strength",  LOOK_FIELD(moonStrength),  1,  0.0f,   3.0f,   "moon brightness (0 = no moon)"},
        {"moon_size",      LOOK_FIELD(moonSize),      1,  0.3f,   8.0f,   "moon radius (degrees)"},
        {"fog_rgb",        LOOK_FIELD(fogRgb),        3,  0.0f,   1.0f,   "fog color, blended in by the opacity in color_opacity"},
        {"underwater_rgb", LOOK_FIELD(underwaterRgb), 3,  0.0f,   1.0f,   "sea floor color seen through the water"},
        {"water_rgb",      LOOK_FIELD(waterRgb),      3,  0.0f,   1.0f,   "sea surface color"},
        {"color_opacity", LOOK_FIELD(colorOpacity), kColorCount, 0.0f, 1.0f, "opacity of each color, in slot order: sun, sun glow, horizon warm, star, moon, fog, underwater, water, rock, grid minor, grid major, sky top day, sky horizon day, sky top night, sky horizon night"},
    };
    return fields;
}

float* LookValue(Look& look, const LookField& f) {
    return reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(&look) + f.offset);
}

const float* LookValue(const Look& look, const LookField& f) {
    return reinterpret_cast<const float*>(reinterpret_cast<const unsigned char*>(&look) + f.offset);
}

bool SaveLookFile(const Look& look, const std::string& path, std::string& err) {
    std::ostringstream out;
    out << "{\n  \"format\": \"worldmaker-look\",\n  \"version\": 1";
    for (const LookField& f : LookFields()) {
        const float* p = LookValue(look, f);
        out << ",\n  \"" << f.name << "\": ";
        if (f.count == 1) {
            char buf[32];
            std::snprintf(buf, sizeof buf, "%.6g", p[0]);
            out << buf;
        } else {
            char buf[96];
            std::snprintf(buf, sizeof buf, "[%.6g, %.6g, %.6g]", p[0], p[1], p[2]);
            out << buf;
        }
    }
    out << "\n}\n";

    const fs::path target = fs::u8path(path);
    std::error_code ec;
    if (target.has_parent_path()) fs::create_directories(target.parent_path(), ec);

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

namespace {

// Finds "key": and returns the position just after the colon, or npos.
std::size_t FindValue(const std::string& text, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    std::size_t p = text.find(needle);
    if (p == std::string::npos) return std::string::npos;
    p += needle.size();
    while (p < text.size() && std::isspace(static_cast<unsigned char>(text[p]))) ++p;
    if (p >= text.size() || text[p] != ':') return std::string::npos;
    return p + 1;
}

// Reads a number, or [a, b, c] for colors, into vals. Returns the count read or 0.
int ReadValue(const std::string& text, std::size_t p, float* vals, int want) {
    while (p < text.size() && std::isspace(static_cast<unsigned char>(text[p]))) ++p;
    if (p >= text.size()) return 0;

    if (text[p] == '[') {
        ++p;
        int n = 0;
        while (n < want && p < text.size()) {
            char* end = nullptr;
            const float v = std::strtof(text.c_str() + p, &end);
            if (end == text.c_str() + p) return 0;
            vals[n++] = v;
            p = static_cast<std::size_t>(end - text.c_str());
            while (p < text.size() && (std::isspace(static_cast<unsigned char>(text[p])) || text[p] == ',')) ++p;
        }
        return n == want ? n : 0;
    }

    char* end = nullptr;
    const float v = std::strtof(text.c_str() + p, &end);
    if (end == text.c_str() + p) return 0;
    vals[0] = v;
    return 1;
}

}  // namespace

bool LoadLookFile(const std::string& path, Look& look, std::string& err) {
    std::ifstream f(fs::u8path(path));
    if (!f) {
        err = "could not open " + path;
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string text = ss.str();

    const std::size_t fmt = FindValue(text, "format");
    if (fmt == std::string::npos || text.find("worldmaker-look", fmt) == std::string::npos) {
        err = "not a worldmaker theme file";
        return false;
    }

    // Read into a copy so a bad file changes nothing.
    Look loaded = look;
    for (const LookField& fld : LookFields()) {
        const std::size_t p = FindValue(text, fld.name);
        if (p == std::string::npos) continue;   // missing keys keep their current value

        float vals[3] = {0.0f, 0.0f, 0.0f};
        if (ReadValue(text, p, vals, fld.count) != fld.count) {
            err = std::string("bad value for ") + fld.name;
            return false;
        }
        float* dst = LookValue(loaded, fld);
        for (int i = 0; i < fld.count; ++i) dst[i] = Limit(vals[i], fld.lo, fld.hi);
    }

    look = loaded;
    return true;
}

std::vector<std::string> ListLookNames(const std::string& dir) {
    std::vector<std::string> names;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return names;

    for (const fs::directory_entry& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file(ec)) continue;
        if (e.path().extension() == ".json") names.push_back(e.path().stem().string());
    }
    std::sort(names.begin(), names.end());
    return names;
}

}  // namespace wm
