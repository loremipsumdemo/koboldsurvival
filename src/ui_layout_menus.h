#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>
#include <vector>
#include "components.h"
#include "items.h"
#include "props.h"
#include "ui_types.h"
#include "ui_layout_panels.h"

// Layout for the title screen, the scenario page and the world map.
// Pure functions of the window size and the state,
// so the drawing code and the mouse handling use the same rectangles.

// The scenario page, between New Game and the party menu: the picked scenario's name and description at the top, and
// a list of the scenarios at the bottom, which scrolls when it has more than fit, with Back and Continue under it
struct ScenarioLayout {
    Rect box;
    Rect heading;   // "Scenario", over everything
    Rect name;      // the picked scenario's name
    Rect desc;      // and its description, wrapped
    Rect list;      // where the rows are
    std::vector<Rect> rows;                   // the rows shown, for scenarios first .. first + rows.size() - 1
    int first = 0;                            // the scroll, kept in range
    int visible = 0;                          // how many rows fit
    Rect track;     // the scroll bar, when the list scrolls
    Rect thumb;
    Rect back;
    Rect next;
};
inline ScenarioLayout layoutScenario(int W, int H, int count, int scroll) {
    const float s = H / 720.0f;
    const float pad = 20.0f * s, rowH = 38.0f * s, rowGap = 6.0f * s, btnH = 36.0f * s, btnW = 170.0f * s;
    const float scrollW = 8.0f * s;
    const float boxW = std::min((float)W - 48.0f * s, 760.0f * s), boxH = (float)H - 64.0f * s;
    const float x0 = (W - boxW) * 0.5f, y0 = 32.0f * s;
    ScenarioLayout L;
    L.box = Rect{ x0, y0, boxW, boxH };
    L.heading = Rect{ x0 + pad, y0 + pad, boxW - 2.0f * pad, 26.0f * s };
    L.name = Rect{ x0 + pad, L.heading.y + L.heading.h + 10.0f * s, boxW - 2.0f * pad, 34.0f * s };
    L.desc = Rect{ x0 + pad, L.name.y + L.name.h + 8.0f * s, boxW - 2.0f * pad, 130.0f * s };
    L.back = Rect{ x0 + pad, y0 + boxH - pad - btnH, btnW, btnH };
    L.next = Rect{ x0 + boxW - pad - btnW, y0 + boxH - pad - btnH, btnW, btnH };
    const float listTop = L.desc.y + L.desc.h + 14.0f * s;
    const float listBottom = L.back.y - 16.0f * s;
    L.list = Rect{ x0 + pad, listTop, boxW - 2.0f * pad, std::max(0.0f, listBottom - listTop) };
    L.visible = std::max(1, (int)std::floor((L.list.h + rowGap) / (rowH + rowGap)));
    L.first = std::clamp(scroll, 0, std::max(0, count - L.visible));
    const bool scrolls = count > L.visible;
    const float rowW = L.list.w - (scrolls ? scrollW + 8.0f * s : 0.0f);
    for (int i = L.first; i < count && i < L.first + L.visible; ++i) {
        L.rows.push_back(Rect{ L.list.x, listTop + (i - L.first) * (rowH + rowGap), rowW, rowH });
    }
    if (scrolls) {
        const float shownH = L.visible * (rowH + rowGap) - rowGap;
        L.track = Rect{ L.list.x + L.list.w - scrollW, listTop, scrollW, shownH };
        const float th = std::max(16.0f * s, shownH * L.visible / count);
        const int maxFirst = count - L.visible;
        L.thumb = Rect{ L.track.x, L.track.y + (shownH - th) * L.first / std::max(1, maxFirst), scrollW, th };
    }
    return L;
}

// The title screen, top left: the game's logo, a pink rule, and the notice board of update notes. Along the bottom:
// the build stamp on the left and the keys on the right.
struct TitleLayout {
    float side = 0.0f;                          // the screen's left and right margin
    Rect name;        // the name's line: its height is the size the name is drawn at
    Rect rule;
    Rect news;        // the notice board, frame included
    Rect newsText;    // inside the board's padding
    Rect footer;      // the line along the bottom
};
inline float titleClamp(float lo, float v, float hi) { return std::max(lo, std::min(v, hi)); }
inline TitleLayout layoutTitle(int W, int H) {
    const float s = H / 720.0f;
    TitleLayout L;
    L.side = titleClamp(24.0f * s, W * 0.05f, 90.0f * s);
    const float top = titleClamp(26.0f * s, H * 0.07f, 80.0f * s);
    const float gap = titleClamp(18.0f * s, H * 0.034f, 38.0f * s);
    // Tall enough for the logo (data/illustration/title.png, 1280x600), which is drawn as large as this line allows
    const float nameH = titleClamp(120.0f * s, std::min(H * 0.36f, (W * 0.56f - L.side) * (600.0f / 1280.0f)), 300.0f * s);
    const float boardW = titleClamp(280.0f * s, W * 0.34f, 470.0f * s);
    const float footerH = 18.0f * s, footerPad = titleClamp(16.0f * s, H * 0.03f, 30.0f * s);
    L.footer = Rect{ L.side, H - footerPad - footerH, W - 2.0f * L.side, footerH };
    L.name = Rect{ L.side, top, W * 0.56f - L.side, nameH };
    L.rule = Rect{ L.side, L.name.y + nameH + gap * 0.45f, boardW, std::max(1.0f, 2.0f * s) };
    const float boardY = L.rule.y + L.rule.h + gap;
    const float boardH = std::max(160.0f * s, L.footer.y - 28.0f * s - boardY);
    L.news = Rect{ L.side, boardY, boardW, boardH };
    const float padX = titleClamp(16.0f * s, W * 0.02f, 26.0f * s), padY = titleClamp(14.0f * s, W * 0.017f, 22.0f * s);
    L.newsText = Rect{ L.news.x + 3.0f * s + padX, L.news.y + 3.0f * s + padY, L.news.w - 6.0f * s - 2.0f * padX,
                       L.news.h - 6.0f * s - 2.0f * padY };
    return L;
}

// The world map window: 800 by 600 pixels, in the middle of the screen until it is dragged by its title bar (less in a
// smaller window, and always kept inside it). The title bar runs across its top with the X that closes it at the right
// end, and the map fills the rest.
constexpr float WORLD_MAP_W = 800.0f;
constexpr float WORLD_MAP_H = 600.0f;
struct WorldMapLayout {
    Rect window;
    Rect bar;     // the title bar
    Rect close;   // the X at the right end of the bar
    Rect map;     // the map itself
};

// dx, dy: how far the window has been dragged from the middle of the screen
inline WorldMapLayout layoutWorldMap(int W, int H, float dx = 0.0f, float dy = 0.0f) {
    const float s = H / 720.0f;
    const float barH = 22.0f * s, pad = 4.0f * s;
    const float w = std::max(0.0f, std::min(WORLD_MAP_W, (float)W));
    const float h = std::max(0.0f, std::min(WORLD_MAP_H, (float)H));
    WorldMapLayout L;
    const float x = std::clamp(std::floor((W - w) * 0.5f + dx), 0.0f, std::max(0.0f, W - w));
    const float y = std::clamp(std::floor((H - h) * 0.5f + dy), 0.0f, std::max(0.0f, H - h));
    L.window = Rect{ x, y, w, h };
    L.bar = Rect{ x, y, w, barH };
    L.close = BarToggle(L.bar, s);
    const float mapTop = L.bar.y + barH + pad;
    L.map = Rect{ x + pad, mapTop, std::max(0.0f, w - 2.0f * pad), std::max(0.0f, y + h - pad - mapTop) };
    return L;
}

// The world map shows `span` tiles from its top to its bottom, and as many more across as its shape gives, so a tile is
// square. Its centre is the ground point cx, cz; the largest x is at its left and the largest z at its top.
inline float mapTilesPerPixel(const Rect& r, float span) { return r.h > 0.0f ? span / r.h : 0.0f; }
// The tiles across the map from left to right
inline float mapSpanAcross(const Rect& r, float span) { return r.h > 0.0f ? span * r.w / r.h : 0.0f; }

// The ground point under a pixel of the map, false off the map or off the level
inline bool mapGround(const Rect& r, float cx, float cz, float span, float levelSize, float px, float py,
                      float& x, float& z) {
    if (!r.contains(px, py) || span <= 0.0f || r.w <= 0.0f || r.h <= 0.0f) return false;
    const float k = mapTilesPerPixel(r, span);
    x = cx + (r.x + r.w * 0.5f - px) * k;
    z = cz + (r.y + r.h * 0.5f - py) * k;
    return x >= 0.0f && z >= 0.0f && x <= levelSize && z <= levelSize;
}

// The pixel on the map over a ground point
inline std::pair<float, float> mapPixel(const Rect& r, float cx, float cz, float span, float x, float z) {
    const float k = mapTilesPerPixel(r, span);
    if (k <= 0.0f) return { r.x, r.y };
    return { r.x + r.w * 0.5f - (x - cx) / k, r.y + r.h * 0.5f - (z - cz) / k };
}

// The widest view of the map: the whole level fits both ways
inline float mapMaxSpan(const Rect& r, float levelSize) {
    if (r.w <= 0.0f || r.h <= 0.0f) return levelSize;
    return levelSize * std::max(1.0f, r.h / r.w);
}

// Keeps the map's view on the level: no closer in than one chunk, no further out than the whole level, and the centre
// placed so the view does not run off an edge (or centred on the level where the view is wider than it)
inline void clampMapView(const Rect& r, float levelSize, float& cx, float& cz, float& span) {
    span = std::clamp(span, std::min(64.0f, levelSize), mapMaxSpan(r, levelSize));
    auto keep = [&](float& c, float across) {
        if (across >= levelSize) c = levelSize * 0.5f;
        else c = std::clamp(c, across * 0.5f, levelSize - across * 0.5f);
    };
    keep(cx, mapSpanAcross(r, span));
    keep(cz, span);
}
