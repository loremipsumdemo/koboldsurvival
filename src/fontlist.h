#pragma once
#include <string>
#include <vector>

// The fonts installed on this computer (.ttf and .otf files in the system font folders), sorted by file name.
// Read once, the first time it is asked for.
const std::vector<std::string>& installedFontPaths();

// The name shown for a font file: its file name without the folder or the extension
std::string fontDisplayName(const std::string& path);
