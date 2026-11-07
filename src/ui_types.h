#pragma once
#include <algorithm>
#include <string>

struct PanelPlace {
    float dx = 0.0f, dy = 0.0f, w = 0.0f, h = 0.0f;
    bool barBottom = false;
};

enum class Screen { Title, Settings, Playing, Pause, MapSelect, ScenarioSelect, PartySetup, WorldGen };

enum class Action {
    Continue, NewGame, LoadGame, Settings, Language, Quit, WorldGeneration,
    Resume, Save, Load, LoadSlot, ReturnToTitle, Back,
    ConfirmName,
    CancelName,
    OverwriteYes, OverwriteNo,
    LoadYes, LoadNo,
    LeaveYes, LeaveNo,
    SortName, SortTime,
    ScrollUp, ScrollDown,
    DeleteSave,
    DeleteHold,
    DeleteNo,
    PartyPick,
    PartyTabPick,
    PartyNameField,
    PartySpriteBox,
    PartyBodyBox,
    PartyRandomUnit,
    PartyAddUnit,
    PartyRemoveUnit,
    PartyAddMob,
    PartyMobBox,
    PartyGearSlot,
    PartyStatLabel,
    PartyStep,
    PartyHeading,
    PartyNote,
    PartyInvSlot,
    PartyCount,
    PartyDropPick,
    PartyDropScroll,
    PartyDropBack,
    PartyRandomAll,
    PartyConfirm,
    PartyMessage
};

struct Selection {
    bool dragging = false;
    float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
};

struct Rect {
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    bool contains(float px, float py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
};

inline float uiScaleFor(float H) { return H / 720.0f; }

inline Rect scrollThumb(const Rect& track, float shownShare, float minH, float frac) {
    const float h = std::max(minH, track.h * shownShare);
    return Rect{ track.x, track.y + (track.h - h) * frac, track.w, h };
}

inline float scrollFracAt(const Rect& track, const Rect& thumb, float thumbTop) {
    const float room = track.h - thumb.h;
    return room > 0.0f ? std::clamp((thumbTop - track.y) / room, 0.0f, 1.0f) : 0.0f;
}

constexpr float ITEM_PICTURE_PX = 64.0f;
constexpr float ITEM_CELL_PX = 72.0f;

struct Button {
    std::string label;
    Rect rect;
    Action action;
    bool enabled = true;
    int param = 0;
};
