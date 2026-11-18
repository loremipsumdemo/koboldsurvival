#pragma once
#include <algorithm>
#include <cmath>
#include <string>

struct PanelPlace {
    float dx = 0.0f, dy = 0.0f, w = 0.0f, h = 0.0f;
    bool barBottom = false;
};

enum class Screen { Title, Settings, Playing, Pause, MapSelect, ScenarioSelect, PartySetup, WorldGen, DefaultWorldAsk, ScenarioEdit };

enum class Action {
    Continue, NewGame, LoadGame, Settings, Language, Quit, WorldGeneration, AddCustomScenario, BuildingBlueprints,
    Resume, Save, Load, LoadSlot, LoadPicked, ReturnToTitle, Back,
    ConfirmName,
    CancelName,
    OverwriteYes, OverwriteNo,
    LoadYes, LoadNo,
    LeaveYes, LeaveNo,
    DefaultWorldYes, DefaultWorldNo,
    SortName, SortTime,
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
    PartyDropBar,
    PartyDropBack,
    PartyRandomAll,
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

// A window's title line along the top of its box, with the close button at its right end. Every window lays these out the same way.
inline void layoutTitleClose(const Rect& box, float pad, float line, Rect& title, Rect& close) {
    title = Rect{ box.x + pad, box.y + pad, box.w - 2.0f * pad - line, line };
    close = Rect{ box.x + box.w - pad - line, box.y + pad, line, line };
}

// A click that dismisses a window: on its close button, or anywhere outside it
inline bool dismissAt(const Rect& box, const Rect& close, float x, float y) { return close.contains(x, y) || !box.contains(x, y); }

// A window that is moved by holding its title bar. A window's state derives from this, keeping x and y (-1: not placed yet).
// grab() when the title is pressed; follow() every frame, with whether the left button is still down and where the pointer is.
struct WindowDrag {
    float x = -1.0f, y = -1.0f;
    bool moving = false;
    float grabX = 0.0f, grabY = 0.0f;
    void grab(const Rect& panel, float px, float py) {
        moving = true;
        x = panel.x;
        y = panel.y;
        grabX = px - panel.x;
        grabY = py - panel.y;
    }
    // True while it is being moved
    bool follow(bool leftDown, float mx, float my) {
        if (!moving) return false;
        if (!leftDown) {
            moving = false;
            return false;
        }
        x = mx - grabX;
        y = my - grabY;
        return true;
    }
};

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

// A scrollbar for a list that scrolls a row at a time: the first row shown is what moves. Empty (on() false) when every row fits.
struct RowBar {
    Rect track, thumb;
    bool on() const { return track.h > 0.0f; }
};

inline RowBar layoutRowBar(float x, float y, float h, float s, int total, int shown, int first) {
    RowBar b;
    if (total <= shown || shown <= 0 || h <= 0.0f) return b;
    b.track = Rect{ x, y, 8.0f * s, h };
    b.thumb = scrollThumb(b.track, (float)shown / (float)total, std::min(16.0f * s, h), (float)first / (float)(total - shown));
    return b;
}

// The first row to show with the thumb's top at thumbTop
inline int rowBarFirst(const RowBar& b, int total, int shown, float thumbTop) {
    return (int)std::lround(scrollFracAt(b.track, b.thumb, thumbTop) * (float)std::max(0, total - shown));
}

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
    int extra = 0;   // a second number for the few buttons that need one
};
