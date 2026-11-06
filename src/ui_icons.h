#pragma once
#include "stats.h"

constexpr int ICON_NONE = 0;
constexpr int ICON_EQUIPMENT = 1;
constexpr int ICON_INVENTORY = 2;
constexpr int ICON_BEHAVIOR = 3;
constexpr int ICON_GAME_LOG = 4;
constexpr int ICON_SQUAD_PANEL = 5;
constexpr int ICON_PARTY_TAB_FIRST = 34;
constexpr int ICON_SETTINGS_TAB_FIRST = 38;
constexpr int ICON_UNIT_PANEL = 42;
constexpr int ICON_SETTINGS_LANGUAGE = 43;

inline int statIcon(StatId stat) { return statInfo(stat).icon; }
inline int partyTabIcon(int tab) { return ICON_PARTY_TAB_FIRST + tab; }
inline int settingsTabIcon(int tab) {
    if (tab == 2) return ICON_SETTINGS_LANGUAGE;
    return ICON_SETTINGS_TAB_FIRST + (tab < 2 ? tab : tab - 1);
}
