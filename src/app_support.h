#pragma once
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include "sounds.h"

class Renderer;

void loadingFrame(SDL_Window* window, Renderer& renderer, float progress, const std::string& label = std::string(),
                  const std::string& category = std::string(), bool backdrop = true);
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
