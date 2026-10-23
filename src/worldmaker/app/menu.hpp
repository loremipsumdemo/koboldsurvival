#pragma once

#include <string>
#include <vector>

#include "app/session.hpp"
#include "render/renderer.hpp"
#include "theme/theme.hpp"
#include "world/world.hpp"

namespace wm {


// Mouse state in window pixels, updated once per event by main.
struct Mouse {
    float x = 0.0f;
    float y = 0.0f;
    bool down = false;       // left button held
    bool pressed = false;    // left button went down this frame
    float wheel = 0.0f;      // scroll notches this frame, positive is up
};

// What the menu asks the main loop to do.
enum class MenuAction { None, Resume, OpenConsole, Quit };

// What a slider writes to: a float or an int. The pointers point at persistent settings.
struct SliderTarget {
    float* f = nullptr;
    int* i = nullptr;
};

// A slider value being typed in. Applied on Enter, canceled on Escape or a click elsewhere.
struct SliderEdit {
    const void* key = nullptr;   // identifies the slider being edited
    SliderTarget target;
    float lo = 0.0f, hi = 0.0f, step = 0.0f;
    std::string text;            // what has been typed so far
};

// The color picker. Changes apply live to the color it was opened for.
struct ColorPicker {
    bool open = false;
    bool fresh = false;                 // opened this frame, so the click that opened it does not close it
    std::string label;
    float* rgb = nullptr;               // the color being edited
    float* opacity = nullptr;           // its opacity
    float h = 0.0f, s = 0.0f, v = 1.0f; // the color in HSV, kept so the wheel does not lose its place
    float alpha = 1.0f;
    std::string hex;                    // "#RRGGBB" as shown in the hex field
};

struct MenuState {
    bool open = false;
    int tab = 0;             // 0 World, 1 Generation, 2 Themes, 3 Walker, 4 Debug, 5 Files

    std::string seedText = std::to_string(RandomSeed());   // a random seed to start with
    std::string exportName = "world_1";   // file name for export, world_<seed> by default
    std::string autoName;                 // the default name last applied, so edits survive until the seed changes
    std::string themeName = "theme";
    std::string* activeField = nullptr;   // the text field receiving keys, if any
    SliderEdit sliderEdit;                // a slider value being typed in

    float sizeTiles = 1024.0f;   // world size, one of the presets 256 to 8192; the default is 1024
    GenSettings gen;             // sea level and height scale, edited by sliders

    const void* dragging = nullptr;   // slider being dragged
    float scroll = 0.0f;              // how far the Sky and Surface tabs are scrolled, in pixels
    float scrollMax = 0.0f;           // the furthest those tabs can scroll, from the last frame
    ColorPicker picker;
    std::string desktopDir;           // where Export to Desktop writes, found when the menu opens
    std::string status;

    std::vector<std::string> worlds;  // exported worlds available to import

    // Game worlds on the Files tab. The list works like the game's save list.
    std::string gameWorldName;                // the name typed for the next game world
    std::string gameWorldAskName;             // the name a replace question is about
    bool gameWorldAsk = false;                // that name is taken: ask before replacing it
    bool gameWorldByName = false;             // sorted by name, else by created date
    bool gameWorldAsc = false;                // ascending order
    size_t gameWorldOffset = 0;               // first world shown in the list
    std::vector<GameWorldEntry> gameWorlds;   // the list, read from the host
    bool gameWorldsStale = true;              // the list must be read again
    unsigned long long gameWorldsAt = 0;      // when it was last read, in ms
    // Where the list was drawn last frame, for the wheel. gameListH is 0 when it was not shown.
    float gameListX = 0.0f, gameListY = 0.0f, gameListW = 0.0f, gameListH = 0.0f;
    std::vector<std::string> themes;  // exported themes available to import
};

// Opens the menu and refreshes the file lists.
void MenuOpen(MenuState& m, const Session& s);

// Draws the whole menu for one frame and reports what the user clicked.
MenuAction DrawMenu(Renderer& r, MenuState& m, Session& s, Look& look, const Mouse& mouse, int w, int h);

// Scrolls the open panel's content by pixels, or to its top or bottom.
void MenuScrollBy(MenuState& m, float px);
void MenuScrollTo(MenuState& m, bool end);

// Keyboard input for the active text field.
void MenuTextInput(MenuState& m, const std::string& text);
void MenuBackspace(MenuState& m);
void MenuStopEditing(MenuState& m);
// Applies the typed slider value, clamped to the slider's range. Does nothing if none is being typed.
void MenuCommitSlider(MenuState& m);

}  // namespace wm
