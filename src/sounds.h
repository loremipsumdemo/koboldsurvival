#pragma once
#include <cstdint>
#include <string>
#include <vector>

class World;
using Entity = std::uint32_t;

enum class Sfx : int {
    UiClick,
    Hit,
    Miss,
    ItemMove,
    UiHover,
    UiBack,
    UiError,
    UiTab,
    UiToggle,
    UiSlider,
    UiMenuOpen,
    UiSaved,
    UiLoaded,
    UiPause,
    UiResume,
    UiSelect,
    UiOrder,
    Equip,
    Unequip,
    Drop,
    Pickup,
    Eat,
    Drink,
    Poison,
    WorkMining,
    WorkWoodcutting,
    WorkHerbalism,
    WorkCooking,
    WorkPump,
    WorkTraining,
    WorkLockpick,
    ForgeBurning,
    DoneOre,
    DoneTree,
    DoneMushroom,
    DoneCooking,
    DoneWater,
    DoneTraining,
    DoneForge,
    Unlocked,
    LockpickFailed,
    TradeDone,
    TradeFailed,
    Store,
    Take,
    TurnHostile,
    LockedIn,
    Occupy,
    GearChanged,
    Downed,
    LevelUp,
    TalkOpen,
    CarryUp,
    PutDown,
    Dies,
    Recovers,
    Count
};

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
    std::string file;
    bool ui = false;
};

// From data/sfx.txt
const SfxInfo& sfxInfo(Sfx s);

struct QueuedSound {
    Sfx sfx = Sfx::UiClick;
    bool placed = false;
    float x = 0.0f, y = 0.0f, z = 0.0f;
};
void queueSound(World& w, Sfx s);
void queueSoundAt(World& w, Sfx s, float x, float y, float z);
void queueSoundFrom(World& w, Sfx s, Entity e);

constexpr float SOUND_FULL_DISTANCE = 15.0f;
constexpr float SOUND_SILENT_DISTANCE = 70.0f;
constexpr float SOUND_PAN_EDGE = 0.6f;
inline float soundGain(float distance) {
    if (distance <= SOUND_FULL_DISTANCE) return 1.0f;
    if (distance >= SOUND_SILENT_DISTANCE) return 0.0f;
    const float f = (SOUND_SILENT_DISTANCE - distance) / (SOUND_SILENT_DISTANCE - SOUND_FULL_DISTANCE);
    return f * f;
}
inline float soundPan(float clipX) {
    const float x = clipX < -1.0f ? -1.0f : clipX > 1.0f ? 1.0f : clipX;
    return x * SOUND_PAN_EDGE;
}
