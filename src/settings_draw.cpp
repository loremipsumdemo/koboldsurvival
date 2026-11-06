#include "settings_menu.h"
#include "language.h"

#include <algorithm>
#include "renderer.h"
#include "textcase.h"
#include "ui_icons.h"

namespace {
const RGBA kText = theme::BONE;
const RGBA kSlider = theme::GOLD;
const RGBA kSecondary{ 0.643f, 0.553f, 0.510f, 1.0f };
const RGBA kDisabledText{ 0.52f, 0.46f, 0.43f, 1.0f };
const RGBA kEdge{ 0.588f, 0.196f, 0.259f, 1.0f };
const RGBA kTabIdle{ 0.16f, 0.07f, 0.085f, 1.0f };
const RGBA kTabHover{ 0.26f, 0.16f, 0.07f, 1.0f };
const RGBA kAccent{ 0.353f, 0.235f, 0.110f, 1.0f };
const RGBA kTrack{ 0.20f, 0.09f, 0.11f, 1.0f };
const RGBA kFillOff{ 0.643f, 0.553f, 0.510f, 1.0f };
const RGBA kThumbDrag{ 0.957f, 0.902f, 0.796f, 1.0f };
const RGBA kListBg{ 0.11f, 0.05f, 0.06f, 1.0f };
const RGBA kListHover{ 0.353f, 0.235f, 0.110f, 1.0f };

bool over(const Rect& r, float x, float y) {
    return r.contains(x, y);
}

void drawChevron(Renderer& r, float cx, float cy, float u, bool down, RGBA tint) {
    for (int k = 0; k < 3; ++k) {
        const float w = (10.0f - 4.0f * k) * u;
        const float y = down ? cy - 3.0f * u + 2.0f * k * u
                             : cy + 3.0f * u - 2.0f * (k + 1) * u;
        r.drawRect(Rect{ cx - w * 0.5f, y, w, 2.0f * u }, tint);
    }
}
}

void drawSettingsMenu(Renderer& r, const SettingsLayout& L, const std::vector<RowSpec>& rows,
                      const SettingsMenuState& st, float mx, float my) {
    const float u = L.panel.h / 640.0f;

    r.drawThemedPanel(Rect{ L.panel.x - 2.0f * u, L.panel.y - 2.0f * u, L.panel.w + 4.0f * u, L.panel.h + 4.0f * u });
    r.drawUiTextCentered(uiUpper(lang("UILABEL_SETTINGS")), L.panel.x + L.panel.w * 0.5f, L.panel.y + 18.0f * u, kText);

    for (size_t i = 0; i < L.tabs.size(); ++i) {
        const Rect& t = L.tabs[i];
        const std::string name = settingsTabName(static_cast<SettingsTab>(i));
        RGBA fill = kTabIdle;
        if (static_cast<int>(i) == static_cast<int>(st.tab)) fill = kAccent;
        else if (over(t, mx, my)) fill = kTabHover;
        if (over(t, mx, my)) r.noteHover(t);
        r.drawRect(t, fill);
        if (static_cast<int>(i) == static_cast<int>(st.tab)) r.drawRect(Rect{ t.x, t.y + t.h - 2.0f * u, t.w, 2.0f * u }, theme::GOLD);
        r.drawIconLabel(t, settingsTabIcon(static_cast<int>(i)), name, kText, true);
    }

    for (size_t i = 0; i < rows.size() && i < L.rows.size(); ++i) {
        const RowSpec& row = rows[i];
        const RowLayout& R = L.rows[i];
        const int idx = static_cast<int>(i);

        if (row.kind == RowKind::Text) {
            r.drawUiText(row.label, R.band.x + 14.0f * u,
                         R.band.y + (R.band.h - r.uiTextHeight(row.label)) * 0.5f, kText);
            r.drawUiText(row.text, R.text.x,
                         R.text.y + (R.text.h - r.uiTextHeight(row.text)) * 0.5f, kSecondary);
            continue;
        }

        r.drawUiText(row.label, R.band.x + (row.sub ? 34.0f : 14.0f) * u,
                     R.band.y + (R.band.h - r.uiTextHeight(row.label)) * 0.5f,
                     row.enabled ? kText : kDisabledText);

        switch (row.kind) {
        case RowKind::Dropdown: {
            const bool open = st.openRow == idx;
            r.drawButton(R.control, row.text, row.enabled, (row.enabled && over(R.control, mx, my)) || open);
            drawChevron(r, R.control.x + R.control.w - 18.0f * u, R.control.y + R.control.h * 0.5f, u, !open,
                        row.enabled ? kText : kDisabledText);
            break;
        }
        case RowKind::Action:
            r.drawButton(R.control, row.text, row.enabled, row.enabled && over(R.control, mx, my));
            break;
        case RowKind::Toggle:
            r.drawButton(R.control, row.on ? lang("UILABEL_ON") : lang("UILABEL_OFF"), row.enabled, row.enabled && over(R.control, mx, my));
            break;
        case RowKind::Level:
        case RowKind::Scale: {
            const Rect& tr = R.track;
            const float fillW = tr.w * row.current / 100.0f;
            r.drawRect(tr, kTrack);
            if (fillW > 0.0f) r.drawRect(Rect{ tr.x, tr.y, fillW, tr.h }, row.on ? kSlider : kFillOff);
            const float thumbW = 12.0f * u, thumbH = tr.h + 12.0f * u;
            const bool dragging = st.dragRow == idx;
            r.drawRect(Rect{ tr.x + fillW - thumbW * 0.5f, tr.y + tr.h * 0.5f - thumbH * 0.5f, thumbW, thumbH },
                       dragging ? kThumbDrag : (row.on ? kText : kSecondary));
            r.drawUiText(row.text, R.value.x + R.value.w - r.uiTextWidth(row.text),
                         R.value.y + (R.value.h - r.uiTextHeight(row.text)) * 0.5f, kText);
            if (row.kind == RowKind::Level) r.drawButton(R.toggle, row.on ? lang("UILABEL_ON") : lang("UILABEL_OFF"), true, over(R.toggle, mx, my));
            break;
        }
        case RowKind::Text:
            break;
        }
    }

    r.drawButton(L.defaults, lang("UILABEL_RESTORE_DEFAULTS"), true, over(L.defaults, mx, my));
    r.drawButton(L.back, lang("UILABEL_BACK"), true, over(L.back, mx, my));

    if (L.drop.open && L.drop.row >= 0 && L.drop.row < static_cast<int>(rows.size())) {
        const RowSpec& row = rows[L.drop.row];
        const Rect& box = L.drop.box;
        r.drawRect(Rect{ box.x - 2.0f * u, box.y - 2.0f * u, box.w + 4.0f * u, box.h + 4.0f * u }, kEdge);
        r.drawRect(box, kListBg);
        for (size_t k = 0; k < L.drop.items.size(); ++k) {
            const int index = L.drop.first + static_cast<int>(k);
            const DropOption& opt = row.options[index];
            const Rect& it = L.drop.items[k];
            if (index == row.current) r.drawRect(it, kAccent);
            else if (opt.enabled && over(it, mx, my)) r.drawRect(it, kListHover);
            if (opt.enabled && over(it, mx, my)) r.noteHover(it);
            r.drawUiText(opt.label, it.x + 12.0f * u, it.y + (it.h - r.uiTextHeight(opt.label)) * 0.5f,
                         opt.enabled ? kText : kDisabledText);
        }

        const Rect& first = L.drop.items.front();
        const Rect& last = L.drop.items.back();
        const float markX = box.x + box.w - 18.0f * u;
        if (L.drop.first > 0) drawChevron(r, markX, first.y + first.h * 0.5f, u, false, kText);
        if (L.drop.first + static_cast<int>(L.drop.items.size()) < L.drop.count) {
            drawChevron(r, markX, last.y + last.h * 0.5f, u, true, kText);
        }
    }
}
