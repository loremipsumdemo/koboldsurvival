#include "textcase.h"
#include "fontlist.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

namespace {
void addFontsIn(const fs::path& dir, std::vector<std::string>& out) {
    std::error_code ec;
    if (dir.empty() || !fs::is_directory(dir, ec)) return;
    for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        std::error_code fileEc;
        if (!it->is_regular_file(fileEc)) continue;
        std::string ext = it->path().extension().u8string();
        ext = toLower(ext);
        if (ext == ".ttf" || ext == ".otf") out.push_back(it->path().u8string());
        if (out.size() >= 2000) return;
    }
}

std::vector<std::string> scan() {
    std::vector<fs::path> dirs;
#if defined(_WIN32)
    if (const wchar_t* win = _wgetenv(L"WINDIR")) dirs.push_back(fs::path(win) / "Fonts");
    if (const wchar_t* local = _wgetenv(L"LOCALAPPDATA")) dirs.push_back(fs::path(local) / "Microsoft" / "Windows" / "Fonts");
#elif defined(__APPLE__)
    dirs.push_back("/System/Library/Fonts");
    dirs.push_back("/Library/Fonts");
    if (const char* home = std::getenv("HOME")) dirs.push_back(fs::path(home) / "Library" / "Fonts");
#else
    dirs.push_back("/usr/share/fonts");
    dirs.push_back("/usr/local/share/fonts");
    if (const char* home = std::getenv("HOME")) {
        dirs.push_back(fs::path(home) / ".fonts");
        dirs.push_back(fs::path(home) / ".local" / "share" / "fonts");
    }
#endif
    std::vector<std::string> found;
    for (const fs::path& d : dirs) addFontsIn(d, found);
    std::sort(found.begin(), found.end());
    found.erase(std::unique(found.begin(), found.end()), found.end());
    std::stable_sort(found.begin(), found.end(), [](const std::string& a, const std::string& b) {
        return fontDisplayName(a) < fontDisplayName(b);
    });
    return found;
}
}

const std::vector<std::string>& installedFontPaths() {
    static const std::vector<std::string> fonts = scan();
    return fonts;
}

std::string fontDisplayName(const std::string& path) {
    return fs::u8path(path).stem().u8string();
}
