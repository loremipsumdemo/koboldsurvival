#pragma once
#include <cstdint>
#include <vector>

class World;
using Entity = std::uint32_t;   // as ecs.h

// Every sound effect the game plays, the file it plays from the SFX folder, and which volume slider it follows:
// Interface sounds (menus, buttons, notices) or Sound effects (the world). data/sfxinfo.txt lists the same table for
// whoever makes the sounds; tests check the two agree. A missing file is simply silent.
// Files 01 to 99 are free to use: to give a sound its own file, change its number here and in sfxinfo.txt.
enum class Sfx : int {
    UiClick,         // a button or control clicked
    Hit,             // an attack lands
    Miss,            // an attack misses or is dodged
    ItemMove,        // an item moved inside an inventory, or the inventory sorted
    UiHover,         // the pointer moves onto a button, a menu item or a tab
    UiBack,          // Cancel, Close, Back, or a menu closed
    UiError,         // something refused: locked, taken, no room, out of reach, skill too low, the wrong item
    UiTab,           // a tab changed
    UiToggle,        // a switch or tick box flipped
    UiSlider,        // a slider let go
    UiMenuOpen,      // a right-click menu, an item menu or a dialog opens
    UiSaved,         // the game saved (or autosaved)
    UiLoaded,        // a save loaded
    UiPause,         // the game paused
    UiResume,        // the game resumed
    UiSelect,        // units selected
    UiOrder,         // units sent somewhere
    Equip,           // gear put on
    Unequip,         // gear taken off
    Drop,            // an item dropped on the ground
    Pickup,          // an item picked up off the ground
    Eat,             // food eaten
    Drink,           // a potion or elixir drunk
    Poison,          // something eaten that hurt
    WorkMining,      // a unit working ore, now and then while it works
    WorkWoodcutting, // a unit working a tree
    WorkHerbalism,   // a unit gathering at a mushroom
    WorkCooking,     // a unit cooking at a campfire
    WorkPump,        // a unit working the water pump
    WorkTraining,    // a unit hitting the training dummy
    WorkLockpick,    // a unit picking a lock
    ForgeBurning,    // a forge burning its fuel, now and then
    DoneOre,         // a piece of Iron Ore mined
    DoneTree,        // a Wood Log cut
    DoneMushroom,    // a mushroom gathered
    DoneCooking,     // something cooked
    DoneWater,       // Water pumped
    DoneTraining,    // a training session finished
    DoneForge,       // the forge made an Iron Ingot
    Unlocked,        // a lock picked open
    LockpickFailed,  // a lockpick attempt that failed
    TradeDone,       // a trade made
    TradeFailed,     // a trade that fell through
    Store,           // items put into a storage or crafting prop
    Take,            // items taken out of a storage or crafting prop
    TurnHostile,     // a unit turned hostile
    LockedIn,        // a cage locked behind a unit
    Occupy,          // a unit sat or lay down on a prop
    GearChanged,     // Optimize, Remove All or Optimize All changed gear
    Downed,          // a unit dropped to no health
    LevelUp,         // a unit's skill or stat went up a level
    TalkOpen,        // a talk began
    CarryUp,         // a unit picked up an Unconscious unit to carry it
    PutDown,         // a carried unit put down on the ground
    Dies,            // an Unconscious unit's Doom reached 100: it is dead
    Recovers,        // an Unconscious unit woke up in a bed or bedroll
    Count
};

// The gathering and crafting sounds: working ore, trees, mushrooms and the water pump, cooking at a campfire and the
// forge, and the sound of each piece made. The Crafting and gathering setting switches them all off together.
inline bool isCraftGatherSfx(Sfx s) {
    switch (s) {
    case Sfx::WorkMining: case Sfx::WorkWoodcutting: case Sfx::WorkHerbalism: case Sfx::WorkCooking:
    case Sfx::WorkPump: case Sfx::ForgeBurning:
    case Sfx::DoneOre: case Sfx::DoneTree: case Sfx::DoneMushroom: case Sfx::DoneCooking:
    case Sfx::DoneWater: case Sfx::DoneForge:
        return true;
    default:
        return false;
    }
}

struct SfxInfo {
    const char* file;
    bool ui;            // follows the Interface sounds slider, else Sound effects
    const char* what;   // for sfxinfo.txt
};

inline const SfxInfo& sfxInfo(Sfx s) {
    static const SfxInfo table[(int)Sfx::Count] = {
        { "sfx_01.mp3", true,  "Interface: a button or control clicked" },
        { "sfx_02.mp3", false, "Combat: an attack lands" },
        { "sfx_03.mp3", false, "Combat: an attack misses or is dodged" },
        { "sfx_04.mp3", false, "Items: an item moved inside an inventory, or the inventory sorted" },
        { "sfx_05.mp3", true,  "Interface: the pointer moves onto a button, menu item or tab" },
        { "sfx_06.mp3", true,  "Interface: Cancel, Close, Back, or a menu closed" },
        { "sfx_07.mp3", true,  "Interface: refused (locked, already in use, no room, unreachable, skill too low, wrong item)" },
        { "sfx_08.mp3", true,  "Interface: a tab changed" },
        { "sfx_09.mp3", true,  "Interface: a switch or tick box flipped" },
        { "sfx_10.mp3", true,  "Interface: a slider let go" },
        { "sfx_11.mp3", true,  "Interface: a right-click menu, item menu or dialog opens" },
        { "sfx_12.mp3", true,  "Interface: the game saved or autosaved" },
        { "sfx_13.mp3", true,  "Interface: a save loaded" },
        { "sfx_14.mp3", true,  "Interface: the game paused" },
        { "sfx_15.mp3", true,  "Interface: the game resumed" },
        { "sfx_16.mp3", true,  "Interface: units selected" },
        { "sfx_17.mp3", true,  "Interface: units sent somewhere (a move order)" },
        { "sfx_18.mp3", false, "Items: gear put on" },
        { "sfx_19.mp3", false, "Items: gear taken off" },
        { "sfx_20.mp3", false, "Items: an item dropped on the ground" },
        { "sfx_21.mp3", false, "Items: an item picked up off the ground" },
        { "sfx_22.mp3", false, "Items: food eaten" },
        { "sfx_23.mp3", false, "Items: a potion or elixir drunk" },
        { "sfx_24.mp3", false, "Items: something eaten that hurt (a Poison Mushroom)" },
        { "sfx_25.mp3", false, "Work: mining Ore (plays now and then while a unit works it)" },
        { "sfx_26.mp3", false, "Work: chopping a Tree" },
        { "sfx_27.mp3", false, "Work: gathering at a Mushroom" },
        { "sfx_28.mp3", false, "Work: cooking at a Campfire" },
        { "sfx_29.mp3", false, "Work: working the Water Pump" },
        { "sfx_30.mp3", false, "Work: hitting the Training Dummy" },
        { "sfx_31.mp3", false, "Work: picking a lock" },
        { "sfx_32.mp3", false, "Work: a Forge burning its fuel (now and then)" },
        { "sfx_33.mp3", false, "Done: a piece of Iron Ore mined" },
        { "sfx_34.mp3", false, "Done: a Wood Log cut" },
        { "sfx_35.mp3", false, "Done: a Mushroom gathered" },
        { "sfx_36.mp3", false, "Done: something cooked" },
        { "sfx_37.mp3", false, "Done: Water pumped" },
        { "sfx_38.mp3", false, "Done: a training session finished" },
        { "sfx_39.mp3", false, "Done: the Forge made an Iron Ingot" },
        { "sfx_40.mp3", false, "Locks: a lock picked open" },
        { "sfx_41.mp3", false, "Locks: a lockpick attempt failed" },
        { "sfx_42.mp3", false, "Trade: a trade made" },
        { "sfx_43.mp3", false, "Trade: a trade fell through" },
        { "sfx_44.mp3", false, "Storage: items put into a storage or crafting prop" },
        { "sfx_45.mp3", false, "Storage: items taken out of a storage or crafting prop" },
        { "sfx_46.mp3", false, "Units: a unit turned hostile" },
        { "sfx_47.mp3", false, "Units: a cage locked behind a unit" },
        { "sfx_48.mp3", false, "Units: a unit sat or lay down on a prop" },
        { "sfx_49.mp3", false, "Items: Optimize, Remove All or Optimize All changed gear" },
        { "sfx_50.mp3", false, "Units: a unit dropped to no health and fell Unconscious" },
        { "sfx_51.mp3", false, "Units: a skill or stat went up a level" },
        { "sfx_52.mp3", true,  "Interface: a talk began" },
        { "sfx_53.mp3", false, "Units: an Unconscious unit picked up to be carried" },
        { "sfx_54.mp3", false, "Units: a carried unit put down on the ground" },
        { "sfx_55.mp3", false, "Units: an Unconscious unit died (its Doom reached 100)" },
        { "sfx_56.mp3", false, "Units: an Unconscious unit woke up in a bed or bedroll" },
    };
    const int i = (int)s;
    return table[(i >= 0 && i < (int)Sfx::Count) ? i : 0];
}

// For systems that have no audio of their own: sounds asked for this frame, played by the game after its systems run.
// A sound queued with a place (queueSoundAt, or queueSoundFrom for a unit or prop) is a world sound: the
// game plays it quieter the farther it is from the camera, and to the side of the screen it is on (soundGain and
// soundPan below). One queued without a place, such as a sound for something you did in a panel, is heard in full.
// The same sound asked for several times in a frame plays once, as loud as its nearest one.
struct QueuedSound {
    Sfx sfx = Sfx::UiClick;
    bool placed = false;
    float x = 0.0f, y = 0.0f, z = 0.0f;
};
void queueSound(Sfx s);
void queueSoundAt(Sfx s, float x, float y, float z);
// A world sound from where a unit or prop stands; heard in full when it has no place
void queueSoundFrom(World& w, Sfx s, Entity e);
std::vector<QueuedSound> takeQueuedSounds();
void discardQueuedSounds();

// World sounds, by their distance from the camera's eye: full volume within SOUND_FULL_DISTANCE, falling away to
// silence at SOUND_SILENT_DISTANCE. The fall is squared, so a sound some way off is quiet well before it is gone.
// At the default zoom (the eye 20 tiles from what it looks at) the units in the middle of the view are heard at
// about four fifths.
constexpr float SOUND_FULL_DISTANCE = 15.0f;
constexpr float SOUND_SILENT_DISTANCE = 70.0f;
// How far a sound at the very edge of the screen is panned toward that side: 0 is centred, 1 wholly to one side
constexpr float SOUND_PAN_EDGE = 0.6f;
inline float soundGain(float distance) {
    if (distance <= SOUND_FULL_DISTANCE) return 1.0f;
    if (distance >= SOUND_SILENT_DISTANCE) return 0.0f;
    const float f = (SOUND_SILENT_DISTANCE - distance) / (SOUND_SILENT_DISTANCE - SOUND_FULL_DISTANCE);
    return f * f;
}
// The pan for a sound at clip-space x (-1 the left edge of the screen, 1 the right), kept within the edge's pan
inline float soundPan(float clipX) {
    const float x = clipX < -1.0f ? -1.0f : clipX > 1.0f ? 1.0f : clipX;
    return x * SOUND_PAN_EDGE;
}
