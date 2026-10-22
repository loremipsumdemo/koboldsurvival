#pragma once

#include <string>

namespace wm {


struct Session;
struct Look;

// The user's desktop folder, as UTF-8. Empty when it cannot be found.
std::string DesktopFolder();

// Writes the loaded world to the desktop as <stem>.wmw. A name already in use gets " (2)",
// " (3)" and so on, so nothing is overwritten. outPath receives the file written.
bool ExportWorldToDesktop(const Session& s, const std::string& stem, std::string& outPath, std::string& err);

// Writes a theme to the desktop as <stem>.json, with the same naming rule.
bool ExportThemeToDesktop(const Look& look, const std::string& stem, std::string& outPath, std::string& err);

}  // namespace wm
