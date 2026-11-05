#include "app.h"

using namespace play;

namespace {

const RGBA LIST_WHITE{ 1.0f, 1.0f, 1.0f, 1.0f };
const RGBA LIST_GRAY{ 0.68f, 0.60f, 0.54f, 1.0f };
const RGBA LIST_HOVER{ 0.961f, 0.788f, 0.459f, 1.0f };
const RGBA LIST_HINT{ 0.957f, 0.902f, 0.796f, 1.0f };
const RGBA LIST_MARKED{ 1.0f, 0.62f, 0.62f, 1.0f };   // an autosave's type

// The column headers of the save and map lists. The active sort column shows its direction.
void drawListHeaders(Renderer& r, const PanelGeometry& g, float s, const std::string& timeLabel, bool byName, bool ascending) {
    const float innerX = g.box.x + g.pad, innerW = g.box.w - 2.0f * g.pad;
    const float y = g.headerY + (g.headerH - r.listTextHeight("Ag")) * 0.5f;
    auto label = [&](const std::string& text, bool active) { return active ? text + (ascending ? "  ^" : "  v") : text; };
    r.drawListText(label(lang("UILABEL_NAME"), byName), innerX + 12.0f * s, y, byName ? LIST_WHITE : LIST_GRAY);
    r.drawListText(lang("UILABEL_TYPE"), innerX + COL_TYPE * innerW, y, LIST_GRAY);
    r.drawListText(label(timeLabel, !byName), innerX + COL_TIME * innerW, y, !byName ? LIST_WHITE : LIST_GRAY);
}

// The "..." rows: how many rows of the list are hidden above and below the visible ones
void drawListMoreRows(Renderer& r, const PanelGeometry& g, float s, size_t offset, size_t total, int winH, float mx, float my) {
    const float x = g.box.x + g.pad + 12.0f * s, listH = r.listTextHeight("Ag");
    const size_t first = std::min(offset, total);
    const size_t last = std::min(first + visibleSaveRows(winH), total);
    auto row = [&](const Rect& band, const char* key, size_t count) {
        r.drawListText(langf(key, { std::to_string(count) }), x, band.y + (band.h - listH) * 0.5f,
                       band.contains(mx, my) ? LIST_HOVER : LIST_HINT);
    };
    if (first > 0) row(g.aboveBand, "UILABEL_LIST_MORE_ABOVE", first);
    if (last < total) row(g.belowBand, "UILABEL_LIST_MORE_BELOW", total - last);
}

// One row of the save or map list: its background, then its name, type and time in the columns
void drawListRow(Renderer& r, const Rect& rect, bool hot, float s, const std::string& name, const std::string& type,
                 RGBA typeTint, const std::string& time) {
    r.drawButton(rect, "", true, hot);
    const float y = rect.y + (rect.h - r.listTextHeight("Ag")) * 0.5f;
    const RGBA tint = hot ? LIST_HOVER : LIST_WHITE;
    r.drawListText(name, rect.x + 12.0f * s, y, tint);
    r.drawListText(type, rect.x + COL_TYPE * rect.w, y, typeTint);
    r.drawListText(time, rect.x + COL_TIME * rect.w, y, tint);
}

} // namespace

void App::drawScreenButtons(const std::vector<Button>& buttons) {
    for (const Button& b : buttons) {
        renderer.drawButton(b.rect, b.label, b.enabled, b.rect.contains(mouseX, mouseY));
    }
}

    // The title and pause menus' items. A new screen lights its first usable item, and its items slide in one after
    // another. Moving the mouse onto an item lights it.
void App::drawMenuItems(Screen which, const std::vector<Button>& buttons) {
    const unsigned long long now = SDL_GetTicks();
    if (menuScreen != which) {
        menuScreen = which;
        menuShownAt = now;
        menuSel = -1;
        for (size_t i = 0; i < buttons.size(); ++i) {
            if (buttons[i].enabled) { menuSel = (int)i; break; }
        }
    }
    const bool moved = mouseX != menuMouseX || mouseY != menuMouseY;
    menuMouseX = mouseX;
    menuMouseY = mouseY;
    for (size_t i = 0; i < buttons.size(); ++i) {
        if (moved && buttons[i].enabled && buttons[i].rect.contains(mouseX, mouseY)) menuSel = (int)i;
    }
    const float since = (float)(now - menuShownAt) / 1000.0f;
    for (size_t i = 0; i < buttons.size(); ++i) {
        const Button& b = buttons[i];
        const float appear = std::clamp((since - (0.055f * (float)i + 0.1f)) / 0.34f, 0.0f, 1.0f);
        const std::string sub = b.action == Action::Language ? languageDisplayName() : std::string();
        renderer.drawMenuItem(b.rect, b.label, sub, (int)i == menuSel && b.enabled, b.enabled, appear);
    }
}

void App::dimScreen(float alpha) {
    renderer.drawRect(Rect{ 0.0f, 0.0f, (float)winW, (float)winH }, RGBA{ 0.0f, 0.0f, 0.0f, alpha });
}

void App::drawTitleScreen() {
    renderer.beginUi(winW, winH);
    renderer.drawTitleBackground();
    {
        // The game's name and the notice board, top left; the menu down the right; along the bottom, the build
        // stamp and the keys
        const TitleLayout tl = layoutTitle(winW, winH);
        renderer.drawTitleHeader(tl, lang("GAME_TITLE_FIRST"), lang("GAME_TITLE_SECOND"),
                                 messageOfTheDay());
        drawMenuItems(Screen::Title, layoutButtons(Screen::Title, winW, winH, saves));
        renderer.drawTitleFooter(tl.footer, BUILD_STAMP, lang("MISC_TITLE_KEYS"));
    }
    renderer.endUi();
}

void App::drawSettingsScreen() {
    renderer.beginUi(winW, winH);
    renderer.drawTitleBackground();
    dimScreen(0.45f);
    const std::vector<RowSpec> rows = settingsRows(settingsUi.tab, settings, displayInfo);
    drawSettingsMenu(renderer, layoutSettings(winW, winH, rows, settingsUi), rows, settingsUi, mouseX, mouseY);
    renderer.endUi();
}

void App::drawScenarioScreen() {
    // The picked scenario's name and description at the top, the list of scenarios at the bottom
    const float s = uiScaleFor(winH);
    const ScenarioLayout sl = layoutScenario(winW, winH, (int)ScenarioId::Count, scenarioScroll);
    scenarioScroll = sl.first;
    const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f };
    const RGBA gray{ 0.80f, 0.72f, 0.64f, 1.0f };
    const RGBA accent{ 1.0f, 0.78f, 0.30f, 1.0f };
    renderer.beginUi(winW, winH);
    renderer.drawTitleBackground();
    dimScreen(0.45f);
    renderer.drawThemedPanel(sl.box);
    renderer.drawListText(lang("UILABEL_SCENARIO"), sl.heading.x, sl.heading.y, gray);
    const std::string name = lang(scenarioNameKey(scenarioPick));
    renderer.drawUiTextInBox(name, Rect{ sl.name.x, sl.name.y, sl.name.w, sl.name.h }, accent);
    float ty = sl.desc.y;
    const float lh = renderer.listTextHeight("Ag");
    for (const std::string& ln : renderer.wrapListText(lang(scenarioDescKey(scenarioPick)), sl.desc.w)) {
        if (ty + lh > sl.desc.y + sl.desc.h + 0.5f) break;
        renderer.drawListText(ln, sl.desc.x, ty, white);
        ty += lh;
    }
    renderer.drawRect(Rect{ sl.list.x, sl.list.y - 8.0f * s, sl.list.w, 1.0f * s }, RGBA{ 0.588f, 0.196f, 0.259f, 1.0f });
    for (size_t i = 0; i < sl.rows.size(); ++i) {
        const ScenarioId id = (ScenarioId)(sl.first + (int)i);
        const Rect& r = sl.rows[i];
        renderer.drawButton(r, lang(scenarioNameKey(id)), true, r.contains(mouseX, mouseY));
        if (id == scenarioPick) {
            // The picked one: a bar down its left edge and a frame
            renderer.drawRect(Rect{ r.x, r.y, 5.0f * s, r.h }, accent);
            renderer.drawRect(Rect{ r.x, r.y, r.w, 2.0f * s }, accent);
            renderer.drawRect(Rect{ r.x, r.y + r.h - 2.0f * s, r.w, 2.0f * s }, accent);
            renderer.drawRect(Rect{ r.x + r.w - 2.0f * s, r.y, 2.0f * s, r.h }, accent);
        }
    }
    if (sl.track.h > 0.0f) {
        renderer.drawRect(sl.track, RGBA{ 0.20f, 0.09f, 0.11f, 0.9f });
        renderer.drawRect(sl.thumb, RGBA{ 0.910f, 0.376f, 0.431f, 0.9f });
    }
    renderer.drawButton(sl.back, lang("UILABEL_BACK"), true, sl.back.contains(mouseX, mouseY));
    renderer.drawButton(sl.next, lang("UILABEL_CONTINUE"), true, sl.next.contains(mouseX, mouseY));
    renderer.endUi();
}

void App::drawPartyScreen() {
    const std::vector<DropEntry> drop = dropdownEntries(party, partyUi);
    renderer.beginUi(winW, winH);
    renderer.drawTitleBackground();
    dimScreen(0.45f);
    renderer.drawPartySetup(layoutPartySetup(winW, winH, party, partyUi, drop), party, partyUi,
                            partyPanels(winW, winH), mouseX, mouseY);
    renderer.endUi();
}

void App::drawPauseScreen() {
    // The world and its PAUSED bar as in the game, then the menu over them in the menu fonts
    renderer.setMenuText(false);
    renderer.drawFrame(world, terrain, cam, sel, {}, winW, winH);
    renderer.beginUi(winW, winH);
    dimScreen(0.55f);
    renderer.drawPauseBar(false);
    renderer.setMenuText(true);
    drawMenuItems(Screen::Pause, layoutButtons(Screen::Pause, winW, winH, saves));
    renderer.endUi();
}

// The save and load popout, drawn over whichever screen is showing
void App::drawSavePanel() {
    const float s = uiScaleFor(winH);
    PanelGeometry g = panelGeometry(winW, winH);
    const RGBA white = LIST_WHITE, gray = LIST_GRAY;
    const float cx = g.box.x + g.box.w * 0.5f;

    renderer.beginUi(winW, winH);
    dimScreen(0.25f);
    renderer.drawThemedPanel(g.box);
    renderer.drawUiTextCentered(uiUpper(panelSaveMode ? lang("UILABEL_SAVE_GAME") : lang("UILABEL_LOAD_GAME")), cx, g.box.y + g.pad, white);

    if (ask != Ask::None) {
        DialogGeometry d = questionGeometry(winW, winH);
        renderer.drawThemedPanel(d.box);
        if (ask == Ask::Overwrite) {
            std::string who = pendingOverwrite ? pendingOverwrite->name : std::string();
            renderer.drawUiTextCentered(langf("MISC_OVERWRITE_Q", { who }),
                                        d.box.x + d.box.w * 0.5f, d.box.y + 36.0f * s, white);
        } else if (ask == Ask::LoadUnsaved) {
            std::string who = pendingLoad ? pendingLoad->name : std::string();
            renderer.drawUiTextCentered(langf("MISC_LOAD_Q", { who }),
                                        d.box.x + d.box.w * 0.5f, d.box.y + 30.0f * s, white);
            renderer.drawUiTextCentered(lang("UILABEL_UNSAVED_WARNING"),
                                        d.box.x + d.box.w * 0.5f, d.box.y + 36.0f * s + 48.0f * s, white);
        } else {
            // Delete: the name is shown so it's clear which save goes
            const std::string who = pendingDelete ? pendingDelete->name : std::string();
            renderer.drawUiTextCentered(langf("MISC_DELETE_Q", { who }),
                                        d.box.x + d.box.w * 0.5f, d.box.y + 30.0f * s, white);
            renderer.drawUiTextCentered(lang("UILABEL_HOLD_DELETE"),
                                        d.box.x + d.box.w * 0.5f, d.box.y + 36.0f * s + 48.0f * s, white);
        }
        for (const Button& b : layoutPanel(winW, winH, panelSaveMode, ask, saves, listOffset,
                                           panelFrom == Screen::Title)) {
            if (b.action == Action::DeleteHold) {
                const float fill = deleteHolding
                    ? std::min(1.0f, (float)(SDL_GetTicks() - deleteHoldStart) / (float)DELETE_HOLD_MS)
                    : 0.0f;
                renderer.drawHoldButton(b.rect, b.label, fill);
            } else {
                drawScreenButtons({ b });
            }
        }
    } else {
        if (panelSaveMode) {
            // Name field with a blinking cursor, shown even while the name is empty
            // so it's clear the field has focus as soon as the popout opens
            renderer.drawRect(g.nameBox, RGBA{ 0.14f, 0.06f, 0.075f, 1.0f });
            float textY = g.nameBox.y + g.nameBox.h * 0.22f;
            bool cursorOn = (SDL_GetTicks() / 500) % 2 == 0;
            renderer.drawUiText(saveName + (cursorOn ? "|" : ""), g.nameBox.x + 12.0f, textY, white);
        } else {
            renderer.drawUiTextCentered(lang("UILABEL_CLICK_SAVE"),
                                        cx, g.nameBox.y + g.nameBox.h * 0.22f, gray);
        }

        drawListHeaders(renderer, g, s, lang("UILABEL_TIME"), sortKey == SortKey::Name, sortAsc);

        if (saves.empty()) {
            renderer.drawUiTextCentered(lang("UILABEL_NO_SAVES"), cx, g.listTop + g.nameBox.h * 0.22f, gray);
        }

        drawListMoreRows(renderer, g, s, listOffset, saves.size(), winH, mouseX, mouseY);

        for (const Button& b : layoutPanel(winW, winH, panelSaveMode, Ask::None, saves, listOffset,
                                           panelFrom == Screen::Title)) {
            const bool isHover = b.rect.contains(mouseX, mouseY);
            if (b.action == Action::LoadSlot) {
                const SaveSummary& sv = saves[b.param];
                drawListRow(renderer, b.rect, isHover, s, sv.name, sv.autosave ? lang("UILABEL_AUTO") : lang("UILABEL_MANUAL"),
                            sv.autosave ? LIST_MARKED : LIST_WHITE, timeString(sv.timestamp));
            } else if (b.action == Action::ScrollUp || b.action == Action::ScrollDown
                       || b.action == Action::SortName || b.action == Action::SortTime) {
                // Drawn above as text: the "..." rows and the headers only need their click areas
                continue;
            } else if (b.action == Action::DeleteSave) {
                renderer.drawListButton(b.rect, lang("UILABEL_DELETE"), isHover);
            } else {
                renderer.drawButton(b.rect, b.label, b.enabled, isHover);
            }
        }
    }
    renderer.endUi();
}

// What goes over every screen: the # prompt, notices, the frame rate, and taking a screenshot
void App::drawFrameOverlays() {
    renderer.beginUi(winW, winH);
    // The Behavior tab's # prompt, over the whole game
    if (percentPrompt.open) {
        if (screen != Screen::Playing || !world.has<Behaviors>(percentPrompt.unit)) {
            answerPercentPrompt(false);   // the game it was asked in is gone
        } else {
            const char* title = percentPrompt.kind == BehaviorKind::Eat    ? "UILABEL_BEHAVIOR_PROMPT_EAT"
                                : percentPrompt.kind == BehaviorKind::Heal ? "UILABEL_BEHAVIOR_PROMPT_HEAL"
                                                                           : "UILABEL_BEHAVIOR_PROMPT_REST";
            const PercentRange range = percentRange(percentPrompt.kind);
            renderer.drawPercentPrompt(layoutPercentPrompt(winW, winH), lang(title), percentPrompt.text,
                                       langf("UILABEL_BEHAVIOR_PROMPT_RANGE", { std::to_string(range.lo), std::to_string(range.hi) }),
                                       percentPrompt.error, mouseX, mouseY);
        }
    }
    if (noticeSec > 0.0f) renderer.drawNotice(notice, winW, winH);   // at the top middle of the window
    // The frame rate in the bottom-left corner, white on a black box, over everything else
    ++fpsFrames;
    if (const Uint64 at = SDL_GetTicks(); at - fpsSince >= 500) {
        fpsText = std::to_string((int)std::lround(fpsFrames * 1000.0 / (double)(at - fpsSince))) + " FPS";
        fpsSince = at;
        fpsFrames = 0;
    }
    if (settings.showFps) {
        const float pad = 4.0f, tw = renderer.uiTextWidth(fpsText), th = renderer.uiTextHeight(fpsText);
        const float bx = 6.0f, by = (float)winH - th - 2.0f * pad - 6.0f;
        renderer.drawRect(Rect{ bx, by, tw + 2.0f * pad, th + 2.0f * pad }, RGBA{ 0.0f, 0.0f, 0.0f, 0.85f });
        renderer.drawUiText(fpsText, bx + pad, by + pad, RGBA{ 1.0f, 1.0f, 1.0f, 1.0f });
    }
    renderer.endUi();

    // The frame is complete but not yet shown, so it is the one saved
    if (screenshotPending) {
        screenshotPending = false;
        const std::string saved = saveScreenshot(window);
        showNotice(saved.empty() ? std::string(lang("MISC_SCREENSHOT_FAILED")) : langf("MISC_SCREENSHOT_SAVED", { saved }));
    }
}

void App::render() {
    // ---- Render ----
    renderer.setDayMinutes(timeOfDayMinutes(gs.clockSeconds));
    renderer.setSky(sky);
    if (screen != Screen::Title && screen != Screen::Pause) menuScreen = screen;   // the next menu opens fresh
    renderer.setMenuText(screen != Screen::Playing);
    renderer.hoverFrameStart();
    switch (screen) {
    case Screen::Title: drawTitleScreen(); break;
    case Screen::Settings: drawSettingsScreen(); break;
    case Screen::Playing: drawPlayingScreen(); break;
    case Screen::ScenarioSelect: drawScenarioScreen(); break;
    case Screen::PartySetup: drawPartyScreen(); break;
    case Screen::Pause: drawPauseScreen(); break;
    }
    if (panelOpen) drawSavePanel();
    drawFrameOverlays();
}
