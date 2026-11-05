#pragma once
// Program-level odds and ends: the log file, fatal errors, F12 screenshots, sound effects, the UI fonts and the
// loading bar
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include "sounds.h"

class Renderer;

// Draws one loading frame and keeps the window responsive while things load
void loadingFrame(SDL_Window* window, Renderer& renderer, float progress, const std::string& label = std::string(),
                  const std::string& category = std::string(), bool backdrop = true);
// Current time in milliseconds since 1970
long long nowMs();
// Plays a sound effect now, on the slider it follows (sounds.h lists them all, with their files). volume and pan are
// for a world sound, heard from the camera (sounds.h); one too far off to hear is not played at all.
void playSfx(Sfx s, float volume = 1.0f, float pan = 0.0f);
// The menus' text size in percent, for the game's text size and the window's height
int menuFontPercent(int uiScale, int windowH);

// The text fonts, opened at the Font size setting and for the selected language. Each main font has the other fonts
// as fallbacks (see fonts.h). owned holds every font opened, so they can all be closed.
struct UiFonts {
    TTF_Font* hud = nullptr;
    TTF_Font* ui = nullptr;
    TTF_Font* list = nullptr;
    TTF_Font* statsTitle = nullptr;
    TTF_Font* statsRow = nullptr;
    std::vector<TTF_Font*> owned;
    bool valid() const { return hud && ui && list && statsTitle && statsRow; }
};
UiFonts openUiFonts(int pct, const std::string& fontPath, const std::string& languageId);
void closeUiFonts(UiFonts& f);

// Every SDL_Log line also goes to log.txt beside the game, flushed at once, so a game that closes on its
// own leaves the reason behind. The lines still go to the console or debugger output as before.
void startLogFile();
// Logs why the game has to close and shows it in a message box. Returns the exit code.
int fatalError(SDL_Window* window, const std::string& message);
// An exception nothing caught would end the game without a word: say what it was first
void onTerminate();

// The folder beside the game where F12 screenshots are saved
std::string screenshotsDir();
// Reads back the frame that is about to be shown and saves it as a PNG in the screenshots folder.
// Returns the file's name, or an empty string if it could not be saved.
std::string saveScreenshot(SDL_Window* window);
