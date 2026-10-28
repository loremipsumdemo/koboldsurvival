#pragma once
#include <string>
#include <vector>
#include <SDL3_ttf/SDL_ttf.h>

// The text fonts. Seven built-in fonts ship in data/font, in data.pak. Automatic, the default, shows each character in
// the first of them that has it. The order starts with the font that suits the selected language. A font picked in
// Settings is tried before them, and a basic system font, if one is installed, is tried after them.

// The built-in font names, in the order to try them for a language. The language's own font comes first.
std::vector<std::string> builtInFontOrder(const std::string& languageId);

// A basic system font installed on this computer, or empty if none of the common ones is there
std::string systemFontPath();

// Opens one text style at a size. The first font that opens is the main font, and the others are its fallbacks in
// order: for any character the main font lacks, SDL_ttf tries each fallback in turn. Every font opened is added to
// owned, so close them all with closeFonts. Returns null if no font opened.
TTF_Font* openFontChain(float size, bool bold, const std::string& pickedPath, const std::string& languageId,
                        std::vector<TTF_Font*>& owned);

// Closes the fonts in owned and empties it
void closeFonts(std::vector<TTF_Font*>& owned);
