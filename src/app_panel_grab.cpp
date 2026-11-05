// Moving, resizing, folding and locking the panels that can be moved
#include "app.h"

using namespace play;

PanelPlace& App::placeOf(PanelId which) {
    return which == PanelId::Squad ? squadPlace : which == PanelId::Unit ? unitPlace : logPlace;
}

bool App::panelFolded(PanelId which) {
    return which == PanelId::Squad ? squadCollapsed : which == PanelId::Unit ? !unitOpen : combatCollapsed;
}

Rect App::shownPanel(PanelId which, int W, int H) {
    if (which == PanelId::Squad) return squadPanelLayout(W, H).panel;
    if (which == PanelId::Unit) return sidebarFor(W, H, sidebarScroll).panel;
    return gameLogPanel(W, H).panel;
}

Rect App::panelBarOf(PanelId which, int W, int H) {
    if (which == PanelId::Squad) return squadPanelLayout(W, H).title;
    if (which == PanelId::Unit) return sidebarFor(W, H, sidebarScroll).title;
    return gameLogPanel(W, H).bar;
}

std::pair<std::optional<PanelId>, int> App::grabAt(int W, int H, float x, float y) {
    const float out = 5.0f, in = 4.0f;   // how far outside and inside an edge still grabs it
    for (PanelId which : { PanelId::Info, PanelId::Unit, PanelId::Squad }) {
        if (panelLocked[idx(which)]) continue;
        const Rect r = shownPanel(which, W, H);
        const bool nearX = x >= r.x - out && x < r.x + r.w + out;
        const bool nearY = y >= r.y - out && y < r.y + r.h + out;
        if (!nearX || !nearY) continue;
        int kind = 0;
        if (!panelFolded(which)) {
            if (y < r.y + in) kind |= GRAB_TOP;
            if (y >= r.y + r.h - in) kind |= GRAB_BOTTOM;
            if (x < r.x + in) kind |= GRAB_LEFT;
            if (x >= r.x + r.w - in) kind |= GRAB_RIGHT;
        }
        if (kind != 0) return { which, kind };
        if (panelBarOf(which, W, H).contains(x, y)) return { which, GRAB_MOVE };
    }
    return { std::nullopt, 0 };
}

void App::setGrabCursor(int kind) {
    static SDL_Cursor* cursors[SDL_SYSTEM_CURSOR_COUNT] = {};
    static int shown = -1;
    SDL_SystemCursor want = SDL_SYSTEM_CURSOR_DEFAULT;
    const bool tb = (kind & (GRAB_TOP | GRAB_BOTTOM)) != 0, lr = (kind & (GRAB_LEFT | GRAB_RIGHT)) != 0;
    if (kind == GRAB_MOVE) want = SDL_SYSTEM_CURSOR_MOVE;
    else if (tb && lr) {
        const bool nwse = ((kind & GRAB_TOP) && (kind & GRAB_LEFT)) || ((kind & GRAB_BOTTOM) && (kind & GRAB_RIGHT));
        want = nwse ? SDL_SYSTEM_CURSOR_NWSE_RESIZE : SDL_SYSTEM_CURSOR_NESW_RESIZE;
    } else if (tb) want = SDL_SYSTEM_CURSOR_NS_RESIZE;
    else if (lr) want = SDL_SYSTEM_CURSOR_EW_RESIZE;
    if ((int)want == shown) return;
    shown = (int)want;
    if (want == SDL_SYSTEM_CURSOR_DEFAULT) {
        SDL_SetCursor(SDL_GetDefaultCursor());
        return;
    }
    if (!cursors[want]) cursors[want] = SDL_CreateSystemCursor(want);
    SDL_SetCursor(cursors[want] ? cursors[want] : SDL_GetDefaultCursor());
}

void App::applyGrab(int W, int H, float x, float y) {
    const float dx = x - grabMouseX, dy = y - grabMouseY;
    float mw = 0.0f, mh = 0.0f;
    panelMinSize(*grabWhich, H, mw, mh);
    mw = std::min(mw, (float)W);
    mh = std::min(mh, (float)H);
    float left = grabX, top = grabY, right = grabX + grabW, bottom = grabY + grabH;
    if (grabKind == GRAB_MOVE) {
        // The whole panel moves; only the bar it shows is kept on the screen (below)
        left = grabX + dx;
        top = grabY + dy;
        right = left + grabW;
        bottom = top + grabH;
    } else {
        if (grabKind & GRAB_LEFT) left = std::clamp(grabX + dx, 0.0f, right - mw);
        if (grabKind & GRAB_RIGHT) right = std::clamp(grabX + grabW + dx, left + mw, (float)W);
        if (grabKind & GRAB_TOP) top = std::clamp(grabY + dy, 0.0f, bottom - mh);
        if (grabKind & GRAB_BOTTOM) bottom = std::clamp(grabY + grabH + dy, top + mh, std::max((float)H, grabY + grabH));
    }
    PanelPlace& p = placeOf(*grabWhich);
    p = placeForRect(*grabWhich, Rect{ left, top, right - left, bottom - top }, W, H, p.barBottom);
    p = placeForRect(*grabWhich, panelRect(*grabWhich, p, W, H), W, H, p.barBottom);   // its bar on the screen
}

void App::endGrab(int W, int H) {
    if (grabWhich) placeOf(*grabWhich) = settlePanel(*grabWhich, placeOf(*grabWhich), W, H, panelFolded(*grabWhich));
    grabWhich.reset();
    grabKind = 0;
}

void App::setFolded(PanelId which, bool fold, int W, int H) {
    PanelPlace& p = placeOf(which);
    p = settlePanel(which, p, W, H, !fold);   // the bar on its side before it changes
    if (which == PanelId::Squad) squadCollapsed = fold;
    else if (which == PanelId::Unit) unitOpen = !fold;
    else combatCollapsed = fold;
}

void App::toggleLock(PanelId which) {
    panelLocked[idx(which)] = !panelLocked[idx(which)];
    if (panelLocked[idx(which)]) {
        const PanelPlace& p = placeOf(which);
        settings.panels[(size_t)idx(which)] = { p.dx, p.dy, p.w, p.h };
        saveSettingsFile();
    }
    playSfx(Sfx::UiClick);
}

Rect App::lockRectOf(PanelId which, int W, int H) {
    if (which == PanelId::Squad) return squadPanelLayout(W, H).lock;
    if (which == PanelId::Unit) return sidebarFor(W, H, sidebarScroll).lock;
    return gameLogPanel(W, H).lock;
}

void App::drawPanelLock(PanelId which) {
    const int W = winW, H = winH;
    if (!panelLocked[idx(which)]) {
        const Rect r = shownPanel(which, W, H);
        const float e = std::max(1.0f, 2.0f * H / 720.0f);
        const RGBA g = theme::alpha(theme::GOLD, 0.9f);
        renderer.drawRect(Rect{ r.x - e, r.y - e, r.w + 2.0f * e, e }, g);
        renderer.drawRect(Rect{ r.x - e, r.y + r.h, r.w + 2.0f * e, e }, g);
        renderer.drawRect(Rect{ r.x - e, r.y, e, r.h }, g);
        renderer.drawRect(Rect{ r.x + r.w, r.y, e, r.h }, g);
    }
    renderer.drawLockButton(lockRectOf(which, W, H), panelLocked[idx(which)]);
}
