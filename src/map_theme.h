#pragma once
#include <cstdint>
#include <string>
#include "look.h"
#include "sky.h"

// How a generated map looks: its sky, sun, moon, day and night, light, fog, water and ground color. It changes nothing about
// the ground itself, so it can be edited at any time. All whole numbers, so a map file stays short and exact.

enum class SkyKind : int { Gradient = 0, Solid, Cave, Galaxy, Count };

using ThemeColor = uint32_t;   // 0xRRGGBB

struct MapTheme {
    int skyKind = (int)SkyKind::Gradient;
    ThemeColor skyDayTop = 0x3873D9, skyDayHorizon = 0xB3D6FF, skyNightTop = 0x050814, skyNightHorizon = 0x141A2E;
    ThemeColor skyDusk = 0xFF8C4D;
    int stars = 100;

    int sunShown = 1;
    ThemeColor sunColor = 0xFFFAE6;
    int moonShown = 1;
    ThemeColor moonColor = 0xCCDBFF;
    int cloudsShown = 1;  // two drifting layers of cloud overhead, and their shadows on the ground
    int dayCycle = 1;    // 0: always the fixed hour, though the game's clock and calendar still run
    int fixedHour = 12;

    ThemeColor lightColor = 0xFFF5D1;
    int sunStrength = 90;
    int ambient = 35;
    int shadows = 50;     // how much darker the side away from the light is. 50 is the game's usual.
    int modelLight = 0;   // how much the light reaches props and units. At 0 they always show at full brightness.

    int fog = 0;
    ThemeColor fogColor = 0xB8D6F0;

    int waterShown = 1;   // 0: no water and no sea bed, and ground far below sea level isn't drawn: a void
    ThemeColor waterColor = 0x1A4D80, waterDeepColor = 0x0D3857;
    int waterOpacity = 100;
    int waterSpeed = 100;
    int waterRipples = 50;
    int waterGlitter = 0;
    int waterGlow = 0;

    ThemeColor groundTint = 0xFFFFFF;
    int groundTintAmount = 0;
    ThemeColor rockColor = 0x756E63;
};

struct ThemePreset {
    const char* fileName;
    const char* labelKey;
    MapTheme theme;
};
const ThemePreset* themePresets();
int themePresetCount();
// The first preset if the name is unknown
int themePresetNamed(const std::string& fileName);

// The rows of the Theme tab, and the keys of the map file: adding a setting is one more MapTheme member and one more row
enum class ThemeRowKind { Heading, Choice, Toggle, Number, Color };
struct ThemeRow {
    ThemeRowKind kind;
    const char* key;
    const char* labelKey;
    int MapTheme::* number;
    ThemeColor MapTheme::* color;
    int low, high;
};
const ThemeRow* themeRows();
int themeRowCount();
const char* skyKindLabelKey(int kind);

// Only what differs from the preset is written
std::string themeFileText(const MapTheme& theme, int preset);
// True if this line of a map file was part of the theme. Read the preset line first.
bool themeLineFromText(const std::string& key, const std::string& value, MapTheme& theme, int& preset);

// What the theme means to the renderer
Look themeLook(const MapTheme& theme);
SkyColors themeSkyColors(const MapTheme& theme);
void themeRgb(ThemeColor c, float out[3]);
