#pragma once
#include <cstddef>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>

// Packed game data. The game reads everything under data/ from data.pak and never from loose
// files. Names are the paths the game uses, such as "data/unit/unit_1.png".

// Opens data.pak and loads its index. Returns false if it's missing or damaged. Call once at startup.
bool pakOpen(const std::string& pakPath);

// Finds a file in the pak. The bytes stay valid until the program exits.
bool pakFind(const std::string& name, const unsigned char*& bytes, size_t& size);

// Whether a file is in the pak
bool pakHas(const std::string& name);

// Opens a font from the pak. Returns null if it's missing. Close it with TTF_CloseFont.
TTF_Font* pakFont(const std::string& name, float size);

// Names of every file in the pak that starts with prefix and ends with suffix, sorted.
std::vector<std::string> pakNames(const std::string& prefix, const std::string& suffix);
