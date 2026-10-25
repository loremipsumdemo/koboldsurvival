#pragma once

#include <string>
#include <vector>

#include "world/world.hpp"

namespace wm {


// Binary world file (.wmw). Layout, all little-endian:
//   char[4]  magic "WMW1"
//   u32      format version (1)
//   i32      size in tiles
//   u32      seed
//   f32      sea level in world units
//   f32[]    height, size*size
//   u8[]     biome, size*size
//   f32[]    veg, size*size
// Files are written to a temporary name and renamed into place, so a crash
// during save never leaves a half-written world behind.
bool SaveWorldFile(const World& w, const std::string& path, std::string& err);
bool LoadWorldFile(const std::string& path, World& out, std::string& err);

// Names (without folder or extension) of every .wmw file in dir, sorted.
std::vector<std::string> ListWorldNames(const std::string& dir);

}  // namespace wm
