#pragma once
#include <string>
#include <vector>
#include <SDL3_ttf/SDL_ttf.h>


std::string systemFontPath();

TTF_Font* openFontChain(float size, bool bold, const std::string& pickedPath, const std::string& languageId,
                        std::vector<TTF_Font*>& owned);

void closeFonts(std::vector<TTF_Font*>& owned);
