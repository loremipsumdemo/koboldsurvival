#pragma once
#include "stats.h"

// The UI icons: 32 x 32 pixel PNGs in data/icon/, named icon_<id>.png with no leading zeros, e.g. icon_7.png
// (data/iconinfo.txt lists them). An id whose picture is missing is drawn as a pink square with the id as
// its number. ICON_NONE (0) asks for no icon at all.
// The ids below are the ones the game asks for. They are file numbers, not an enum: each _FIRST one starts a range,
// and a stat names its own icon (StatInfo::icon, 6 to 33).

constexpr int ICON_NONE = 0;
constexpr int ICON_EQUIPMENT = 1; // the Equipment heading of the unit sidebar
constexpr int ICON_INVENTORY = 2; // the Inventory heading
constexpr int ICON_BEHAVIOR = 3; // the Behavior heading
constexpr int ICON_GAME_LOG = 4; // the bar above the game log
constexpr int ICON_SQUAD_PANEL = 5; // the bar above the squad panel
constexpr int ICON_PARTY_TAB_FIRST = 34; // 34 to 37: Identity, Stats, Gear, Items
constexpr int ICON_SETTINGS_TAB_FIRST = 38; // 38 to 41: Graphics, Interface, Controls, Audio (Language is 43)
constexpr int ICON_UNIT_PANEL = 42; // the Unit Panel's title bar
constexpr int ICON_SETTINGS_LANGUAGE = 43; // the Language tab of the Settings screen

inline int statIcon(StatId stat) { return statInfo(stat).icon; }
inline int partyTabIcon(int tab) { return ICON_PARTY_TAB_FIRST + tab; }
// The Settings tabs in order: Graphics, Interface, Language, Controls, Audio
inline int settingsTabIcon(int tab) {
    if (tab == 2) return ICON_SETTINGS_LANGUAGE;
    return ICON_SETTINGS_TAB_FIRST + (tab < 2 ? tab : tab - 1);
}
