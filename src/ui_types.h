#pragma once
#include <algorithm>
#include <string>

struct PanelPlace {
    float dx = 0.0f, dy = 0.0f, w = 0.0f, h = 0.0f;
    bool barBottom = false;
};

enum class Screen { Title, Settings, Playing, Pause, MapSelect, ScenarioSelect, PartySetup, WorldGen, DefaultWorldAsk };

enum class Action {
    Continue, NewGame, LoadGame, Settings, Language, Quit, WorldGeneration, BuildingBlueprints,
    Resume, Save, Load, LoadSlot, LoadPicked, ReturnToTitle, Back,
    ConfirmName,
    CancelName,
    OverwriteYes, OverwriteNo,
    LoadYes, LoadNo,
    LeaveYes, LeaveNo,
    DefaultWorldYes, DefaultWorldNo,
    SortName, SortTime,
    ScrollUp, ScrollDown,
    DeleteSave,
    DeleteHold,
    DeleteNo,
    PartyPick,
    PartyTabPick,
    PartyNameField,
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
    PartyPatron,
    PartyFaction,
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

// A scrollable area: content taller than the room it has. The bar shows only when the content no longer fits, and takes its width
// from the view (view is what the content may use). Offset is how far down the content is scrolled, in pixels.
// To use it: layoutScrollArea(full, contentH, offset, s), lay the content out from view.y - offset (width view.w), clip drawing to
// view, draw track and thumb when needed, and scroll by wheel (scrollWheelOffset) or by dragging the thumb (scrollOffsetAt).
struct ScrollArea {
    Rect view, track, thumb;
    float content = 0.0f, offset = 0.0f, maxOffset = 0.0f;
    bool needed = false;
};

inline ScrollArea layoutScrollArea(const Rect& full, float contentH, float offset, float s) {
    ScrollArea a;
    a.view = full;
    a.content = contentH;
    a.maxOffset = std::max(0.0f, contentH - full.h);
    a.needed = a.maxOffset > 0.5f && full.h > 0.0f;
    a.offset = a.needed ? std::clamp(offset, 0.0f, a.maxOffset) : 0.0f;
    if (a.needed) {
        const float barW = 8.0f * s;
        a.view.w = std::max(0.0f, full.w - barW - 6.0f * s);
        a.track = Rect{ full.x + full.w - barW, full.y, barW, full.h };
        a.thumb = scrollThumb(a.track, full.h / contentH, std::min(24.0f * s, a.track.h), a.offset / a.maxOffset);
    }
    return a;
}

inline float scrollOffsetAt(const ScrollArea& a, float thumbTop) { return scrollFracAt(a.track, a.thumb, thumbTop) * a.maxOffset; }

// A wheel notch scrolls by this many pixels (positive wheel is up)
inline float scrollWheelOffset(const ScrollArea& a, float wheel, float notchPx) {
    return std::clamp(a.offset - wheel * notchPx, 0.0f, a.maxOffset);
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
