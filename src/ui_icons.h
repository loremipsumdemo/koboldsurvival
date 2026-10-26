#pragma once

// The UI icons: 32 x 32 pixel PNGs in data/ui/, named icon_NN.png, where NN is the icon's id with two
// digits. Until a picture is packed for an id, the game draws a pink square with the id as its number.
// The ids below are the ones the game asks for; a stat's icon is its StatId plus ICON_STAT_FIRST.

enum UiIcon : int {
    ICON_NONE = 0,
    ICON_EQUIPMENT = 1,        // the Equipment heading of the unit sidebar
    ICON_INVENTORY = 2,        // the Inventory heading
    ICON_BEHAVIOR = 3,         // the Behavior heading
    ICON_COMBAT_LOG = 4,       // the bar above the combat log
    ICON_SQUAD_PANEL = 5,      // the bar above the squad panel
    ICON_STAT_FIRST = 6,       // 6 to 33: one per stat, in StatId order (Strength is 06, Medical is 33)
    ICON_PARTY_TAB_FIRST = 34, // 34 to 37: Identity, Stats, Gear, Items
    ICON_SETTINGS_TAB_FIRST = 38, // 38 to 41: Graphics, Interface, Controls, Audio
    ICON_UNIT_PANEL = 42,      // the Unit Panel's title bar
    ICON_COUNT = 43
};

inline int statIcon(int stat) { return ICON_STAT_FIRST + stat; }
inline int partyTabIcon(int tab) { return ICON_PARTY_TAB_FIRST + tab; }
inline int settingsTabIcon(int tab) { return ICON_SETTINGS_TAB_FIRST + tab; }
