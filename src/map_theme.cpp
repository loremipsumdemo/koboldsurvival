#include "map_theme.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace {
MapTheme overworldTheme() {
    return MapTheme{};
}

MapTheme cavernTheme() {
    MapTheme t;
    t.skyKind = (int)SkyKind::Cave;
    t.skyDayTop = t.skyNightTop = 0x6A5E54;        // the rock of the roof and the stalactites
    t.skyDayHorizon = t.skyNightHorizon = 0x12261A;   // the dark far back, lit a little by the pools
    t.stars = 0;
    t.sunShown = 0;
    t.moonShown = 0;
    t.cloudsShown = 0;
    t.dayCycle = 0;
    t.fixedHour = 11;
    t.lightColor = 0xDDE2D6;
    t.sunStrength = 35;
    t.ambient = 42;
    t.shadows = 62;
    t.modelLight = 45;
    t.fog = 22;
    t.fogColor = 0x0E1A14;
    t.waterColor = 0x7CFF1E;
    t.waterDeepColor = 0x2E7A0A;
    t.waterOpacity = 150;
    t.waterSpeed = 45;
    t.waterRipples = 70;
    t.waterGlow = 60;
    t.groundTint = 0x8E8A80;
    t.groundTintAmount = 55;
    t.rockColor = 0x4A4642;
    return t;
}

MapTheme galaxyTheme() {
    MapTheme t;
    t.skyKind = (int)SkyKind::Galaxy;
    t.skyDayTop = t.skyNightTop = 0x7A28C8;        // one color of the nebula
    t.skyDayHorizon = t.skyNightHorizon = 0x1BA6C4;   // the other
    t.sunShown = 0;
    t.moonShown = 0;
    t.cloudsShown = 0;
    t.dayCycle = 0;
    t.fixedHour = 10;
    t.lightColor = 0xE4DAFF;
    t.sunStrength = 95;
    t.ambient = 40;
    t.shadows = 80;
    t.modelLight = 30;
    t.fogColor = 0x05030C;
    t.waterColor = 0xC13BFF;
    t.waterDeepColor = 0x3A0A70;
    t.waterOpacity = 170;
    t.waterSpeed = 70;
    t.waterRipples = 80;
    t.waterGlitter = 60;
    t.waterGlow = 65;
    t.groundTint = 0x7E70A0;
    t.groundTintAmount = 75;
    t.rockColor = 0x40365A;
    return t;
}

MapTheme voidTheme() {
    MapTheme t = galaxyTheme();
    t.waterShown = 0;
    t.stars = 70;
    t.shadows = 92;
    t.groundTint = 0x5A5068;
    t.rockColor = 0x1E1A28;
    return t;
}

MapTheme emberTheme() {
    MapTheme t;
    t.skyKind = (int)SkyKind::Solid;
    t.skyDayTop = t.skyNightTop = 0x3A1712;
    t.skyDayHorizon = t.skyNightHorizon = 0x3A1712;
    t.stars = 0;
    t.sunShown = 0;
    t.moonShown = 0;
    t.cloudsShown = 0;
    t.dayCycle = 0;
    t.fixedHour = 15;
    t.lightColor = 0xFFB070;
    t.sunStrength = 70;
    t.ambient = 34;
    t.shadows = 70;
    t.modelLight = 25;
    t.fog = 16;
    t.fogColor = 0x3A1712;
    t.waterColor = 0xFF5A0A;
    t.waterDeepColor = 0xA01E00;
    t.waterOpacity = 200;
    t.waterSpeed = 25;
    t.waterRipples = 100;
    t.waterGlow = 90;
    t.groundTint = 0x5E4A42;
    t.groundTintAmount = 70;
    t.rockColor = 0x2A2220;
    return t;
}

const ThemePreset PRESETS[] = {
    { "overworld", "UILABEL_THEME_PRESET_OVERWORLD", overworldTheme() },
    { "cavern", "UILABEL_THEME_PRESET_CAVERN", cavernTheme() },
    { "galaxy", "UILABEL_THEME_PRESET_GALAXY", galaxyTheme() },
    { "void", "UILABEL_THEME_PRESET_VOID", voidTheme() },
    { "ember", "UILABEL_THEME_PRESET_EMBER", emberTheme() },
};

constexpr ThemeRow heading(const char* labelKey) { return { ThemeRowKind::Heading, "", labelKey, nullptr, nullptr, 0, 0 }; }
constexpr ThemeRow number(const char* key, const char* labelKey, int MapTheme::* m, int low, int high) {
    return { ThemeRowKind::Number, key, labelKey, m, nullptr, low, high };
}
constexpr ThemeRow toggle(const char* key, const char* labelKey, int MapTheme::* m) {
    return { ThemeRowKind::Toggle, key, labelKey, m, nullptr, 0, 1 };
}
constexpr ThemeRow color(const char* key, const char* labelKey, ThemeColor MapTheme::* m) {
    return { ThemeRowKind::Color, key, labelKey, nullptr, m, 0, 255 };
}

const ThemeRow ROWS[] = {
    heading("UILABEL_THEME_SKY"),
    { ThemeRowKind::Choice, "sky_kind", "UILABEL_THEME_SKY_KIND", &MapTheme::skyKind, nullptr, 0, (int)SkyKind::Count - 1 },
    color("sky_day_top", "UILABEL_THEME_SKY_DAY_TOP", &MapTheme::skyDayTop),
    color("sky_day_horizon", "UILABEL_THEME_SKY_DAY_HORIZON", &MapTheme::skyDayHorizon),
    color("sky_night_top", "UILABEL_THEME_SKY_NIGHT_TOP", &MapTheme::skyNightTop),
    color("sky_night_horizon", "UILABEL_THEME_SKY_NIGHT_HORIZON", &MapTheme::skyNightHorizon),
    color("sky_dusk", "UILABEL_THEME_SKY_DUSK", &MapTheme::skyDusk),
    number("stars", "UILABEL_THEME_STARS", &MapTheme::stars, 0, 100),

    heading("UILABEL_THEME_SUN_MOON"),
    toggle("sun_shown", "UILABEL_THEME_SUN_SHOWN", &MapTheme::sunShown),
    color("sun_color", "UILABEL_THEME_SUN_COLOR", &MapTheme::sunColor),
    toggle("moon_shown", "UILABEL_THEME_MOON_SHOWN", &MapTheme::moonShown),
    color("moon_color", "UILABEL_THEME_MOON_COLOR", &MapTheme::moonColor),
    toggle("clouds_shown", "UILABEL_THEME_CLOUDS_SHOWN", &MapTheme::cloudsShown),
    toggle("day_cycle", "UILABEL_THEME_DAY_CYCLE", &MapTheme::dayCycle),
    number("fixed_hour", "UILABEL_THEME_FIXED_HOUR", &MapTheme::fixedHour, 0, 23),

    heading("UILABEL_THEME_LIGHT"),
    color("light_color", "UILABEL_THEME_LIGHT_COLOR", &MapTheme::lightColor),
    number("sun_strength", "UILABEL_THEME_SUN_STRENGTH", &MapTheme::sunStrength, 0, 150),
    number("ambient", "UILABEL_THEME_AMBIENT", &MapTheme::ambient, 0, 100),
    number("shadows", "UILABEL_THEME_SHADOWS", &MapTheme::shadows, 0, 100),
    number("model_light", "UILABEL_THEME_MODEL_LIGHT", &MapTheme::modelLight, 0, 100),
    number("fog", "UILABEL_THEME_FOG", &MapTheme::fog, 0, 100),
    color("fog_color", "UILABEL_THEME_FOG_COLOR", &MapTheme::fogColor),

    heading("UILABEL_THEME_WATER"),
    toggle("water_shown", "UILABEL_THEME_WATER_SHOWN", &MapTheme::waterShown),
    color("water_color", "UILABEL_THEME_WATER_COLOR", &MapTheme::waterColor),
    color("water_deep", "UILABEL_THEME_WATER_DEEP", &MapTheme::waterDeepColor),
    number("water_opacity", "UILABEL_THEME_WATER_OPACITY", &MapTheme::waterOpacity, 0, 200),
    number("water_speed", "UILABEL_THEME_WATER_SPEED", &MapTheme::waterSpeed, 0, 300),
    number("water_ripples", "UILABEL_THEME_WATER_RIPPLES", &MapTheme::waterRipples, 0, 100),
    number("water_glitter", "UILABEL_THEME_WATER_GLITTER", &MapTheme::waterGlitter, 0, 100),
    number("water_glow", "UILABEL_THEME_WATER_GLOW", &MapTheme::waterGlow, 0, 100),

    heading("UILABEL_THEME_GROUND"),
    color("ground_tint", "UILABEL_THEME_GROUND_TINT", &MapTheme::groundTint),
    number("ground_tint_amount", "UILABEL_THEME_GROUND_TINT_AMOUNT", &MapTheme::groundTintAmount, 0, 100),
    color("rock_color", "UILABEL_THEME_ROCK_COLOR", &MapTheme::rockColor),
};

void setRgb(float out[3], ThemeColor c) {
    themeRgb(c, out);
}
}

void themeRgb(ThemeColor c, float out[3]) {
    out[0] = (float)((c >> 16) & 255u) / 255.0f;
    out[1] = (float)((c >> 8) & 255u) / 255.0f;
    out[2] = (float)(c & 255u) / 255.0f;
}

const ThemePreset* themePresets() { return PRESETS; }
int themePresetCount() { return (int)(sizeof(PRESETS) / sizeof(PRESETS[0])); }
const ThemeRow* themeRows() { return ROWS; }
int themeRowCount() { return (int)(sizeof(ROWS) / sizeof(ROWS[0])); }

int themePresetNamed(const std::string& fileName) {
    for (int i = 0; i < themePresetCount(); ++i) {
        if (fileName == PRESETS[i].fileName) return i;
    }
    return 0;
}

const char* skyKindLabelKey(int kind) {
    switch ((SkyKind)kind) {
    case SkyKind::Solid: return "UILABEL_THEME_SKY_SOLID";
    case SkyKind::Cave: return "UILABEL_THEME_SKY_CAVE";
    case SkyKind::Galaxy: return "UILABEL_THEME_SKY_GALAXY";
    default: return "UILABEL_THEME_SKY_GRADIENT";
    }
}

std::string themeFileText(const MapTheme& theme, int preset) {
    preset = std::clamp(preset, 0, themePresetCount() - 1);
    const MapTheme& base = PRESETS[preset].theme;
    std::ostringstream out;
    out << "theme " << PRESETS[preset].fileName << "\n";
    for (const ThemeRow& row : ROWS) {
        if (row.number && theme.*row.number != base.*row.number) {
            out << row.key << " " << theme.*row.number << "\n";
        } else if (row.color && theme.*row.color != base.*row.color) {
            char hex[8];
            std::snprintf(hex, sizeof hex, "%06x", (unsigned)(theme.*row.color));
            out << row.key << " " << hex << "\n";
        }
    }
    return out.str();
}

bool themeLineFromText(const std::string& key, const std::string& value, MapTheme& theme, int& preset) {
    if (key == "theme") {
        preset = themePresetNamed(value);
        theme = PRESETS[preset].theme;
        return true;
    }
    for (const ThemeRow& row : ROWS) {
        if (key != row.key || row.kind == ThemeRowKind::Heading) continue;
        if (row.number) theme.*row.number = std::clamp(std::atoi(value.c_str()), row.low, row.high);
        else theme.*row.color = (ThemeColor)std::strtoul(value.c_str(), nullptr, 16) & 0xFFFFFFu;
        return true;
    }
    return false;
}

Look themeLook(const MapTheme& t) {
    Look l;
    // At 50 the light is as it always was. Harsher takes light from the shade and adds a little to the lit side.
    const float harsh = ((float)t.shadows - 50.0f) / 50.0f;
    const float shade = harsh >= 0.0f ? 1.0f - 0.8f * harsh : 1.0f - 1.2f * harsh;
    const float lit = harsh >= 0.0f ? 1.0f + 0.25f * harsh : 1.0f + 0.5f * harsh;
    setRgb(l.sunRgb, t.lightColor);
    l.sunStrength = (float)t.sunStrength / 100.0f * lit;
    l.ambient = (float)t.ambient / 100.0f * shade;
    setRgb(l.skyHorizon, t.skyDayHorizon);
    setRgb(l.nightHorizon, t.skyNightHorizon);
    setRgb(l.horizonWarmRgb, t.skyDusk);
    l.fog = (float)t.fog / 25.0f;
    setRgb(l.fogRgb, t.fogColor);
    l.colorOpacity[(int)LookColor::Fog] = t.fog > 0 || t.skyKind != (int)SkyKind::Gradient ? 1.0f : 0.0f;
    setRgb(l.rockRgb, t.rockColor);
    l.waterOpacity = (float)t.waterOpacity / 100.0f;
    l.waterPattern = (float)t.waterRipples / 50.0f;
    l.waterSpeed = (float)t.waterSpeed / 100.0f;
    setRgb(l.waterRgb, t.waterColor);
    setRgb(l.underwaterRgb, t.waterDeepColor);
    return l;
}

SkyColors themeSkyColors(const MapTheme& t) {
    SkyColors c;
    const auto set = [&](ColorTarget target, ThemeColor color) { themeRgb(color, c.colors[idx(target)].data()); };
    set(ColorTarget::DayZenith, t.skyDayTop);
    set(ColorTarget::DayHorizon, t.skyDayHorizon);
    set(ColorTarget::NightZenith, t.skyNightTop);
    set(ColorTarget::NightHorizon, t.skyNightHorizon);
    set(ColorTarget::DuskHorizon, t.skyDusk);
    set(ColorTarget::Sun, t.sunColor);
    set(ColorTarget::Moon, t.moonColor);
    return c;
}
