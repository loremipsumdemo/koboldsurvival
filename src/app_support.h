#pragma once
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include "sounds.h"

class Renderer;

void loadingFrame(SDL_Window* window, Renderer& renderer, float progress, const std::string& label = std::string(),
                  const std::string& category = std::string(), bool backdrop = false, float black = 0.0f);

// How long a fade to black, or one from black, takes
inline constexpr Uint64 FADE_NS = 100'000'000;

// A loading screen from its first frame to its last: it comes up out of black, and goes back to black when done
class LoadingScreen {
public:
    LoadingScreen(SDL_Window* window, Renderer& renderer) : window_(window), renderer_(renderer) {}
    // Draws the bar this full, with what is being loaded named under it. The first call fades the screen in.
    void show(float progress, const std::string& what);
    // The full bar fades to black. Nothing, if the screen was never shown.
    void fadeOut();

private:
    void fade(float progress, bool toBlack);
    SDL_Window* window_;
    Renderer& renderer_;
    bool shown_ = false;
    std::string what_;
};
long long nowMs();
void playSfx(Sfx s, float volume = 1.0f, float pan = 0.0f);
int menuFontPercent(int uiScale, int windowH);

struct UiFonts {
    TTF_Font* hud = nullptr;
    TTF_Font* ui = nullptr;
    TTF_Font* list = nullptr;
    std::vector<TTF_Font*> owned;
    bool valid() const { return hud && ui && list; }
};
UiFonts openUiFonts(int pct, const std::string& fontPath, const std::string& languageId);
void closeUiFonts(UiFonts& f);

int fatalError(SDL_Window* window, const std::string& message);
void onTerminate();

std::string screenshotsDir();
std::string saveScreenshot(SDL_Window* window);
