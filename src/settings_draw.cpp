#include "settings_menu.h"
#include "language.h"

#include <algorithm>
#include "renderer.h"
#include "textcase.h"

namespace {

const RGBA kText = theme::BONE;
const RGBA kSlider = theme::GREEN;   // a slider's filled part, while its sound is on
const RGBA kSecondary{ 0.553f, 0.576f, 0.631f, 1.0f };
const RGBA kDisabledText{ 0.50f, 0.52f, 0.56f, 1.0f };
const RGBA kPanel{ 0.05f, 0.035f, 0.08f, 0.96f };
const RGBA kEdge{ 0.478f, 0.043f, 0.278f, 1.0f };
const RGBA kTabIdle{ 0.10f, 0.07f, 0.13f, 1.0f };
const RGBA kTabHover{ 0.14f, 0.21f, 0.0f, 1.0f };
const RGBA kAccent{ 0.176f, 0.259f, 0.0f, 1.0f };
const RGBA kTrack{ 0.13f, 0.08f, 0.17f, 1.0f };
const RGBA kFillOff{ 0.553f, 0.576f, 0.631f, 1.0f };
const RGBA kThumbDrag{ 0.91f, 0.925f, 0.949f, 1.0f };
const RGBA kListBg{ 0.07f, 0.05f, 0.10f, 1.0f };
const RGBA kListHover{ 0.176f, 0.259f, 0.0f, 1.0f };

bool over(const Rect& r, float x, float y) {
    return r.contains(x, y);
}

// A small triangle, pointing down or up, drawn as three stacked strips so it needs no font glyph
void drawChevron(Renderer& r, float cx, float cy, float u, bool down, RGBA tint) {
    for (int k = 0; k < 3; ++k) {
        const float w = (10.0f - 4.0f * k) * u;
        const float y = down ? cy - 3.0f * u + 2.0f * k * u
                             : cy + 3.0f * u - 2.0f * (k + 1) * u;
        r.drawRect(Rect{ cx - w * 0.5f, y, w, 2.0f * u }, tint);
    }
}

} // namespace

void drawSettingsMenu(Renderer& r, const SettingsLayout& L, const std::vector<RowSpec>& rows,
                      const SettingsMenuState& st, float mx, float my) {
    const float u = L.panel.h / 640.0f;

    // The pink-framed panel, with its title in capitals
    r.drawThemedPanel(Rect{ L.panel.x - 2.0f * u, L.panel.y - 2.0f * u, L.panel.w + 4.0f * u, L.panel.h + 4.0f * u });
    r.drawUiTextCentered(uiUpper(lang("UILABEL_SETTINGS")), L.panel.x + L.panel.w * 0.5f, L.panel.y + 18.0f * u, kText);

    // Tabs: the one showing is filled in dark green
    for (size_t i = 0; i < L.tabs.size(); ++i) {
        const Rect& t = L.tabs[i];
        const std::string name = settingsTabName(static_cast<SettingsTab>(i));
        RGBA fill = kTabIdle;
        if (static_cast<int>(i) == static_cast<int>(st.tab)) fill = kAccent;
        else if (over(t, mx, my)) fill = kTabHover;
        if (over(t, mx, my)) r.noteHover(t);
        r.drawRect(t, fill);
        // The open tab has a green edge along its foot
        if (static_cast<int>(i) == static_cast<int>(st.tab)) r.drawRect(Rect{ t.x, t.y + t.h - 2.0f * u, t.w, 2.0f * u }, theme::GREEN);
        r.drawIconLabel(t, settingsTabIcon(static_cast<int>(i)), name, kText, true);
    }

    for (size_t i = 0; i < rows.size() && i < L.rows.size(); ++i) {
        const RowSpec& row = rows[i];
        const RowLayout& R = L.rows[i];
        const int idx = static_cast<int>(i);

        if (row.kind == RowKind::Text) {
            // Reference line: the key on the left, what it does on the right
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
            // Track filled to the level, with a thumb at the level; the percentage sits to its right
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

    // The open dropdown is drawn last, over the rows and the footer
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

        // Marks at the ends of a list that scrolls, so it is clear there is more above or below
        const Rect& first = L.drop.items.front();
        const Rect& last = L.drop.items.back();
        const float markX = box.x + box.w - 18.0f * u;
        if (L.drop.first > 0) drawChevron(r, markX, first.y + first.h * 0.5f, u, false, kText);
        if (L.drop.first + static_cast<int>(L.drop.items.size()) < L.drop.count) {
            drawChevron(r, markX, last.y + last.h * 0.5f, u, true, kText);
        }
    }
}
