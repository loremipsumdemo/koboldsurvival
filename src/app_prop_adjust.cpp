#include "app.h"
#include "build.h"
#include "prop_visual.h"

using namespace play;

// The Build tab's Adjust tool: how a prop is drawn apart from where it stands. Click a prop, then drag the widget on
// it: Move slides it up to half a tile each way, Rotate turns it about its middle, Scale stretches it from half to one
// and a half times its size. Four buttons at the prop's top right switch between Move, Rotate and Scale, and Commit
// keeps the change; Escape, a right-click or picking another prop drops what was not committed. Only the look changes:
// the prop stands, blocks and is walked to on its own tile, but it is clicked where it is drawn.

namespace {
constexpr float HANDLE_REACH = 11.0f;   // pixels from a handle that still take hold of it
constexpr float RING_REACH = 8.0f;
constexpr int RING_STEPS = 40;
constexpr float BUTTON = 30.0f;

const RGBA AXIS_COLORS[3] = { RGBA{ 1.0f, 0.30f, 0.30f, 1.0f }, RGBA{ 0.30f, 1.0f, 0.30f, 1.0f }, RGBA{ 0.35f, 0.45f, 1.0f, 1.0f } };
const RGBA HOT{ 1.0f, 0.92f, 0.35f, 1.0f };

struct V3f {
    float x, y, z;
};
V3f axisOf(int a) { return V3f{ a == 0 ? 1.0f : 0.0f, a == 1 ? 1.0f : 0.0f, a == 2 ? 1.0f : 0.0f }; }

float segmentGap(float px, float py, float ax, float ay, float bx, float by) {
    const float dx = bx - ax, dy = by - ay;
    const float len2 = dx * dx + dy * dy;
    const float t = len2 > 0.0f ? std::clamp(((px - ax) * dx + (py - ay) * dy) / len2, 0.0f, 1.0f) : 0.0f;
    return std::hypot(px - (ax + dx * t), py - (ay + dy * t));
}

// A prop the tool may adjust: one that stays put on its own tile and is drawn by its own model
bool adjustable(World& w, ObjectId e) {
    const Prop* p = w.get<Prop>(e);
    if (!p || w.has<Blueprint>(e)) return false;
    const PropInfo& info = propInfo(p->kind);
    return !info.foundation && !info.onFoundation && !info.moves && !info.wallRule;
}
}

struct App::AdjustView {
    bool ok = false;
    Mat4 vp{};
    V3f mid{};            // the widget's middle, in the world
    float reach = 1.0f;   // how long its arms are, in the world
    float cx = 0.0f, cy = 0.0f;   // its middle on the screen
    float ax[3] = {}, ay[3] = {}; // the end of each arm on the screen
    Rect buttons[4];
    Rect values;
};

App::AdjustView App::adjustView() {
    AdjustView v;
    const ObjectId e = propAdjust.prop;
    const Prop* p = world.get<Prop>(e);
    const Transform* tr = world.get<Transform>(e);
    if (!p || !tr) return v;
    const PropInfo& info = propInfo(p->kind);
    const PropVisual pv = world.has<PropVisual>(e) ? *world.get<PropVisual>(e) : PropVisual{};
    v.vp = cam.viewProj((float)winW / (float)winH);
    v.mid = V3f{ tr->x + pv.dx, tr->y + pv.dy + propVisualMiddle(info.size), tr->z + pv.dz };
    v.reach = std::clamp(info.size * 0.7f, 0.7f, 2.0f);
    if (!worldToScreen(v.vp, v.mid.x, v.mid.y, v.mid.z, winW, winH, v.cx, v.cy)) return v;
    for (int a = 0; a < 3; ++a) {
        const V3f d = axisOf(a);
        if (!worldToScreen(v.vp, v.mid.x + d.x * v.reach, v.mid.y + d.y * v.reach, v.mid.z + d.z * v.reach, winW, winH, v.ax[a], v.ay[a])) return v;
    }
    // The buttons: up and to the right of the prop, kept on the screen
    const float s = uiScaleFor((float)winH);
    const float side = BUTTON * s, gap = 4.0f * s;
    float topX = v.cx, topY = v.cy;
    worldToScreen(v.vp, v.mid.x, v.mid.y + std::max(info.size * 0.7f, v.reach), v.mid.z, winW, winH, topX, topY);
    float bx = std::max(v.cx, topX) + 46.0f * s, by = std::min(topY, v.cy - 60.0f * s) - side;
    const float rowW = 4.0f * side + 3.0f * gap;
    bx = std::clamp(bx, 4.0f, std::max(4.0f, (float)winW - rowW - 4.0f));
    by = std::clamp(by, 4.0f, std::max(4.0f, (float)winH - side - 60.0f * s));
    for (int i = 0; i < 4; ++i) v.buttons[i] = Rect{ bx + i * (side + gap), by, side, side };
    v.values = Rect{ bx, by + side + gap, rowW, 3.0f * 18.0f * s + 6.0f * s };
    v.ok = true;
    return v;
}

void App::endPropAdjust(bool keep) {
    const ObjectId e = propAdjust.prop;
    if (e != NO_OBJECT && world.has<Prop>(e)) {
        if (!keep) {
            if (propAdjust.before.plain()) world.remove<PropVisual>(e);
            else world.add(e, propAdjust.before);
        } else if (const PropVisual* pv = world.get<PropVisual>(e); pv && pv->plain()) {
            world.remove<PropVisual>(e);
        }
    }
    propAdjust = PropAdjustUi{};
}

// Which handle of the widget is under the mouse: 0 to 2 an arm or ring, 3 the middle, -1 none
int App::adjustHandleAt(const AdjustView& v, float mx, float my) {
    if (!v.ok) return -1;
    if (propAdjust.mode == PropAdjustUi::Mode::Rotate) {
        int best = -1;
        float bestGap = RING_REACH;
        for (int a = 0; a < 3; ++a) {
            const V3f u = axisOf((a + 1) % 3), w2 = axisOf((a + 2) % 3);
            float px = 0.0f, py = 0.0f;
            for (int i = 0; i <= RING_STEPS; ++i) {
                const float t = 6.2831853f * (float)i / (float)RING_STEPS;
                const float c = std::cos(t) * v.reach, sn = std::sin(t) * v.reach;
                float sx = 0.0f, sy = 0.0f;
                if (!worldToScreen(v.vp, v.mid.x + u.x * c + w2.x * sn, v.mid.y + u.y * c + w2.y * sn, v.mid.z + u.z * c + w2.z * sn, winW, winH, sx, sy)) continue;
                if (i > 0) {
                    const float gapHere = segmentGap(mx, my, px, py, sx, sy);
                    if (gapHere < bestGap) {
                        bestGap = gapHere;
                        best = a;
                    }
                }
                px = sx;
                py = sy;
            }
        }
        return best;
    }
    if (std::hypot(mx - v.cx, my - v.cy) <= HANDLE_REACH) return 3;
    int best = -1;
    float bestGap = HANDLE_REACH;
    for (int a = 0; a < 3; ++a) {
        const float gapHere = std::min(segmentGap(mx, my, v.cx, v.cy, v.ax[a], v.ay[a]), std::hypot(mx - v.ax[a], my - v.ay[a]) - 4.0f);
        if (gapHere < bestGap) {
            bestGap = gapHere;
            best = a;
        }
    }
    return best;
}

bool App::adjustPress(float mx, float my) {
    if (propAdjust.prop != NO_OBJECT && !world.has<Prop>(propAdjust.prop)) propAdjust = PropAdjustUi{};
    if (propAdjust.prop != NO_OBJECT) {
        const AdjustView v = adjustView();
        for (int i = 0; v.ok && i < 4; ++i) {
            if (!v.buttons[i].contains(mx, my)) continue;
            if (i == 3) {
                endPropAdjust(true);
                showNotice(lang("MISC_ADJUST_COMMITTED"), 2.0f);
                playSfx(Sfx::Drop);
            } else {
                propAdjust.mode = (PropAdjustUi::Mode)i;
                playSfx(Sfx::UiClick);
            }
            return true;
        }
        const int handle = adjustHandleAt(v, mx, my);
        if (handle >= 0) {
            propAdjust.drag = handle;
            propAdjust.lastX = mx;
            propAdjust.lastY = my;
            return true;
        }
    }
    // Not on the widget: the nearest prop under the mouse is taken up (what was not committed on the last one is dropped)
    ObjectId target = NO_OBJECT;
    float ex = 0.0f, ey = 0.0f, ez = 0.0f, best = 0.0f;
    cam.eye(ex, ey, ez);
    for (ObjectId e : renderer.propsAtPixel(world, cam, mx, my, winW, winH)) {
        const Transform* pt = world.get<Transform>(e);
        const float d2 = (pt->x - ex) * (pt->x - ex) + (pt->y - ey) * (pt->y - ey) + (pt->z - ez) * (pt->z - ez);
        if (target == NO_OBJECT || d2 < best) {
            target = e;
            best = d2;
        }
    }
    if (target == NO_OBJECT) return true;
    if (target == propAdjust.prop) return true;
    if (!adjustable(world, target)) {
        refuse(lang("MISC_ADJUST_CANT"));
        return true;
    }
    const PropAdjustUi::Mode mode = propAdjust.mode;
    endPropAdjust(false);
    propAdjust.prop = target;
    propAdjust.mode = mode;
    propAdjust.before = world.has<PropVisual>(target) ? *world.get<PropVisual>(target) : PropVisual{};
    playSfx(Sfx::UiSelect);
    return true;
}

bool App::pressOnAdjustWidget() {
    if (screen != Screen::Playing || !session.buildPick.adjusting() || e.button.button != SDL_BUTTON_LEFT) return false;
    if (propAdjust.prop == NO_OBJECT || !world.has<Prop>(propAdjust.prop)) return false;
    const float mx = e.button.x, my = e.button.y;
    const AdjustView v = adjustView();
    if (!v.ok) return false;
    bool on = v.values.contains(mx, my) || adjustHandleAt(v, mx, my) >= 0;
    for (int i = 0; i < 4; ++i) on = on || v.buttons[i].contains(mx, my);
    if (!on) return false;
    adjustPress(mx, my);
    return true;
}

void App::adjustMotion(float mx, float my) {
    if (propAdjust.drag < 0 || propAdjust.prop == NO_OBJECT || !world.has<Prop>(propAdjust.prop)) return;
    const AdjustView v = adjustView();
    if (!v.ok) return;
    const ObjectId e = propAdjust.prop;
    PropVisual pv = world.has<PropVisual>(e) ? *world.get<PropVisual>(e) : PropVisual{};
    const float dx = mx - propAdjust.lastX, dy = my - propAdjust.lastY;
    const int a = propAdjust.drag;
    // How far along an arm the mouse moved, in the world: its move on the screen measured against the arm's
    auto along = [&](int axis) {
        const float sx = v.ax[axis] - v.cx, sy = v.ay[axis] - v.cy;
        const float len2 = sx * sx + sy * sy;
        return len2 > 1.0f ? (dx * sx + dy * sy) / len2 * v.reach : 0.0f;
    };
    float* moves[3] = { &pv.dx, &pv.dy, &pv.dz };
    float* turns[3] = { &pv.rx, &pv.ry, &pv.rz };
    float* sizes[3] = { &pv.sx, &pv.sy, &pv.sz };
    switch (propAdjust.mode) {
    case PropAdjustUi::Mode::Move:
        if (a < 3) {
            *moves[a] += along(a);
        } else {
            // The middle: slid over the ground, along x and z
            pv.dx += along(0);
            pv.dz += along(2);
        }
        break;
    case PropAdjustUi::Mode::Rotate:
        if (a < 3) {
            // Round the ring: the angle the mouse sweeps about the widget's middle, the way the ring faces the camera
            const float before = std::atan2(propAdjust.lastY - v.cy, propAdjust.lastX - v.cx);
            const float now = std::atan2(my - v.cy, mx - v.cx);
            float sweep = now - before;
            if (sweep > 3.14159265f) sweep -= 6.2831853f;
            if (sweep < -3.14159265f) sweep += 6.2831853f;
            float ex = 0.0f, ey = 0.0f, ez = 0.0f;
            cam.eye(ex, ey, ez);
            const V3f d = axisOf(a);
            const float facing = d.x * (ex - v.mid.x) + d.y * (ey - v.mid.y) + d.z * (ez - v.mid.z);
            *turns[a] += (facing >= 0.0f ? -1.0f : 1.0f) * sweep * 57.29578f;
        }
        break;
    case PropAdjustUi::Mode::Scale:
        if (a < 3) {
            *sizes[a] += along(a) / v.reach;
        } else {
            // The middle: every way at once, by dragging up or to the right
            const float by = (dx - dy) / 120.0f;
            pv.sx += by;
            pv.sy += by;
            pv.sz += by;
        }
        break;
    }
    world.add(e, clampPropVisual(pv));
    propAdjust.lastX = mx;
    propAdjust.lastY = my;
}

bool App::adjustRelease() {
    if (propAdjust.drag < 0) return false;
    propAdjust.drag = -1;
    return true;
}

void App::drawPropAdjust() {
    if (screen != Screen::Playing) return;
    // The tool put away, or its prop gone: what was not committed is dropped
    if (!session.buildPick.adjusting() || (propAdjust.prop != NO_OBJECT && !world.has<Prop>(propAdjust.prop))) {
        if (propAdjust.prop != NO_OBJECT) endPropAdjust(false);
        return;
    }
    if (propAdjust.prop == NO_OBJECT) return;
    const AdjustView v = adjustView();
    if (!v.ok) return;
    const float s = uiScaleFor((float)winH);
    const float thick = 2.5f * s;
    const int hot = propAdjust.drag >= 0 ? propAdjust.drag : adjustHandleAt(v, (float)mouseX, (float)mouseY);
    const PropVisual pv = world.has<PropVisual>(propAdjust.prop) ? *world.get<PropVisual>(propAdjust.prop) : PropVisual{};
    const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f }, dark{ 0.0f, 0.0f, 0.0f, 0.55f };

    if (propAdjust.mode == PropAdjustUi::Mode::Rotate) {
        for (int a = 0; a < 3; ++a) {
            const V3f u = axisOf((a + 1) % 3), w2 = axisOf((a + 2) % 3);
            float px = 0.0f, py = 0.0f;
            bool have = false;
            for (int i = 0; i <= RING_STEPS; ++i) {
                const float t = 6.2831853f * (float)i / (float)RING_STEPS;
                const float c = std::cos(t) * v.reach, sn = std::sin(t) * v.reach;
                float sx = 0.0f, sy = 0.0f;
                const bool seen = worldToScreen(v.vp, v.mid.x + u.x * c + w2.x * sn, v.mid.y + u.y * c + w2.y * sn, v.mid.z + u.z * c + w2.z * sn,
                                                winW, winH, sx, sy);
                if (seen && have) renderer.drawLine2D(px, py, sx, sy, hot == a ? thick * 1.6f : thick, hot == a ? HOT : AXIS_COLORS[a]);
                px = sx;
                py = sy;
                have = seen;
            }
        }
        renderer.drawDisc(v.cx, v.cy, 5.0f * s, white);
    } else {
        const bool scale = propAdjust.mode == PropAdjustUi::Mode::Scale;
        for (int a = 0; a < 3; ++a) {
            const RGBA c = hot == a ? HOT : AXIS_COLORS[a];
            renderer.drawLine2D(v.cx, v.cy, v.ax[a], v.ay[a], hot == a ? thick * 1.6f : thick, c);
            if (scale) {
                // A block on the end of each arm
                const float h = 6.0f * s;
                renderer.drawRect(Rect{ v.ax[a] - h, v.ay[a] - h, 2.0f * h, 2.0f * h }, c);
            } else {
                // An arrowhead
                const float dx = v.ax[a] - v.cx, dy = v.ay[a] - v.cy;
                const float len = std::max(1.0f, std::hypot(dx, dy));
                const float ux = dx / len, uy = dy / len, h = 11.0f * s;
                renderer.drawLine2D(v.ax[a], v.ay[a], v.ax[a] - ux * h - uy * h * 0.5f, v.ay[a] - uy * h + ux * h * 0.5f, thick * 1.4f, c);
                renderer.drawLine2D(v.ax[a], v.ay[a], v.ax[a] - ux * h + uy * h * 0.5f, v.ay[a] - uy * h - ux * h * 0.5f, thick * 1.4f, c);
            }
        }
        if (scale) {
            const float h = 7.0f * s;
            renderer.drawRect(Rect{ v.cx - h, v.cy - h, 2.0f * h, 2.0f * h }, hot == 3 ? HOT : white);
        } else {
            renderer.drawDisc(v.cx, v.cy, 7.0f * s, hot == 3 ? HOT : white);
        }
    }

    // The four buttons: Move, Rotate, Scale, Commit, each a small drawing
    static const char* const TIPS[4] = { "UILABEL_ADJUST_MOVE", "UILABEL_ADJUST_ROTATE", "UILABEL_ADJUST_SCALE", "UILABEL_ADJUST_COMMIT" };
    for (int i = 0; i < 4; ++i) {
        const Rect& b = v.buttons[i];
        const bool on = i < 3 && (int)propAdjust.mode == i;
        const bool over = b.contains((float)mouseX, (float)mouseY);
        renderer.drawRect(b, on ? RGBA{ 0.55f, 0.38f, 0.14f, 0.95f } : RGBA{ 0.16f, 0.10f, 0.09f, over ? 0.95f : 0.8f });
        const float mx = b.x + b.w * 0.5f, my = b.y + b.h * 0.5f, r = b.w * 0.30f;
        const RGBA ink = i == 3 ? RGBA{ 0.45f, 1.0f, 0.45f, 1.0f } : (on || over ? HOT : white);
        const float t = 2.0f * s;
        if (i == 0) {
            // Move: a cross of arrows
            renderer.drawLine2D(mx - r, my, mx + r, my, t, ink);
            renderer.drawLine2D(mx, my - r, mx, my + r, t, ink);
            const float h = r * 0.4f;
            renderer.drawLine2D(mx + r, my, mx + r - h, my - h, t, ink);
            renderer.drawLine2D(mx + r, my, mx + r - h, my + h, t, ink);
            renderer.drawLine2D(mx - r, my, mx - r + h, my - h, t, ink);
            renderer.drawLine2D(mx - r, my, mx - r + h, my + h, t, ink);
            renderer.drawLine2D(mx, my - r, mx - h, my - r + h, t, ink);
            renderer.drawLine2D(mx, my - r, mx + h, my - r + h, t, ink);
            renderer.drawLine2D(mx, my + r, mx - h, my + r - h, t, ink);
            renderer.drawLine2D(mx, my + r, mx + h, my + r - h, t, ink);
        } else if (i == 1) {
            // Rotate: most of a circle, with an arrowhead
            const int steps = 14;
            float px = 0.0f, py = 0.0f;
            for (int k = 0; k <= steps; ++k) {
                const float a = 0.6f + 4.9f * (float)k / (float)steps;
                const float x = mx + std::cos(a) * r, y = my + std::sin(a) * r;
                if (k > 0) renderer.drawLine2D(px, py, x, y, t, ink);
                px = x;
                py = y;
            }
            renderer.drawLine2D(px, py, px - r * 0.55f, py - r * 0.1f, t, ink);
            renderer.drawLine2D(px, py, px + r * 0.1f, py + r * 0.55f, t, ink);
        } else if (i == 2) {
            // Scale: a small square growing into a large one
            const float q = r * 0.45f;
            renderer.drawRect(Rect{ mx - r, my + r - 2.0f * q, 2.0f * q, 2.0f * q }, ink);
            renderer.drawLine2D(mx - r, my - r, mx + r, my - r, t, ink);
            renderer.drawLine2D(mx + r, my - r, mx + r, my + r, t, ink);
            renderer.drawLine2D(mx - r + 2.0f * q, my + r - 2.0f * q, mx + r, my - r, t, ink);
        } else {
            // Commit: a check mark
            renderer.drawLine2D(mx - r, my, mx - r * 0.25f, my + r * 0.75f, t * 1.3f, ink);
            renderer.drawLine2D(mx - r * 0.25f, my + r * 0.75f, mx + r, my - r * 0.7f, t * 1.3f, ink);
        }
        if (over) renderer.noteTip(lang(TIPS[i]), b);
    }

    // The three sets of values, the one being changed in gold
    renderer.drawRect(v.values, dark);
    const float textH = renderer.listTextHeight("Ag");
    char text[96];
    float y = v.values.y + 3.0f * s;
    auto row = [&](PropAdjustUi::Mode mode, const char* key, const char* format, float a, float b, float c) {
        std::snprintf(text, sizeof text, format, a, b, c);
        const std::string line = renderer.fitListText(lang(key) + "  " + text, v.values.w - 8.0f * s);
        renderer.drawListText(line, v.values.x + 4.0f * s, y, propAdjust.mode == mode ? HOT : white);
        y += textH + 2.0f;
    };
    row(PropAdjustUi::Mode::Move, "UILABEL_ADJUST_MOVE_SHORT", "%+.2f %+.2f %+.2f", pv.dx, pv.dy, pv.dz);
    row(PropAdjustUi::Mode::Rotate, "UILABEL_ADJUST_ROTATE_SHORT", "%.0f %.0f %.0f", pv.rx, pv.ry, pv.rz);
    row(PropAdjustUi::Mode::Scale, "UILABEL_ADJUST_SCALE_SHORT", "%.2f %.2f %.2f", pv.sx, pv.sy, pv.sz);
}
