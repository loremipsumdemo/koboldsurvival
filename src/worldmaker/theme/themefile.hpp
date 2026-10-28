#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "theme/theme.hpp"

namespace wm {


// One editable theme value. Colors have count 3, everything else count 1.
struct LookField {
    const char* name;
    std::size_t offset;   // position of the first float inside Look
    int         count;
    float       lo;
    float       hi;
    const char* help;
};

// Every theme value that the console, the menu and theme files can edit.
const std::vector<LookField>& LookFields();

// Pointer to the first float of a field inside a Look.
float* LookValue(Look& look, const LookField& f);
const float* LookValue(const Look& look, const LookField& f);

// Theme files are JSON: {"format": "worldmaker-look", "version": 1, "clock": 12, "sun_rgb": [1, 0.9, 0.8], ...}
bool SaveLookFile(const Look& look, const std::string& path, std::string& err);
bool LoadLookFile(const std::string& path, Look& look, std::string& err);
// The same, from the text of a look file already read
bool LoadLookText(const std::string& text, Look& look, std::string& err);

// Names (without folder or extension) of every .json file in dir, sorted.
std::vector<std::string> ListLookNames(const std::string& dir);

}  // namespace wm
