#pragma once
#include <cstddef>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>

bool pakOpen(const std::string& pakPath);

bool pakFind(const std::string& name, const unsigned char*& bytes, size_t& size);

bool pakHas(const std::string& name);

TTF_Font* pakFont(const std::string& name, float size);

std::vector<std::string> pakNames(const std::string& prefix, const std::string& suffix);
