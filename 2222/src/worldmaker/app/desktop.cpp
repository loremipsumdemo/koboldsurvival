#include "app/desktop.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "app/session.hpp"
#include "theme/themefile.hpp"
#include "world/worldfile.hpp"

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#endif

namespace wm {

namespace fs = std::filesystem;

std::string DesktopFolder() {
#ifdef _WIN32
    // The shell knows where the desktop really is, including when OneDrive has moved it.
    PWSTR wide = nullptr;
    std::string out;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &wide))) {
        const int n = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
        if (n > 1) {
            out.resize(static_cast<std::size_t>(n - 1));
            WideCharToMultiByte(CP_UTF8, 0, wide, -1, out.data(), n, nullptr, nullptr);
        }
        CoTaskMemFree(wide);
    }
    return out;
#else
    const char* home = std::getenv("HOME");
    const std::string h = home ? home : "";
    if (h.empty()) return "";
    // The desktop folder can be moved; user-dirs.dirs says where it is.
    const std::string key = "XDG_DESKTOP_DIR=\"";
    std::ifstream f(h + "/.config/user-dirs.dirs");
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind(key, 0) == 0 && !line.empty() && line.back() == '"') {
            std::string v = line.substr(key.size(), line.size() - key.size() - 1);
            if (v.rfind("$HOME", 0) == 0) v = h + v.substr(5);
            return v;
        }
    }
    return h + "/Desktop";
#endif
}

namespace {

// The desktop folder, created if it is missing.
bool DesktopDir(fs::path& dir, std::string& err) {
    const std::string where = DesktopFolder();
    if (where.empty()) {
        err = "could not find the desktop folder";
        return false;
    }
    dir = fs::u8path(where);
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (!fs::is_directory(dir, ec)) {
        err = "desktop folder is not available: " + where;
        return false;
    }
    return true;
}

// dir/stem.ext, or dir/stem (2).ext, (3).ext... whichever is free.
fs::path UniqueFile(const fs::path& dir, const std::string& stem, const std::string& ext) {
    std::error_code ec;
    fs::path p = dir / fs::u8path(stem + ext);
    for (int n = 2; fs::exists(p, ec); ++n) {
        p = dir / fs::u8path(stem + " (" + std::to_string(n) + ")" + ext);
    }
    return p;
}

}  // namespace

bool ExportWorldToDesktop(const Session& s, const std::string& stem, std::string& outPath, std::string& err) {
    if (!s.hasWorld) {
        err = "generate or load a world first";
        return false;
    }
    fs::path dir;
    if (!DesktopDir(dir, err)) return false;
    const fs::path file = UniqueFile(dir, stem, ".wmw");
    if (!SaveWorldFile(s.world, file.u8string(), err)) return false;
    outPath = file.u8string();
    return true;
}

bool ExportThemeToDesktop(const Look& look, const std::string& stem, std::string& outPath, std::string& err) {
    fs::path dir;
    if (!DesktopDir(dir, err)) return false;
    const fs::path file = UniqueFile(dir, stem, ".json");
    if (!SaveLookFile(look, file.u8string(), err)) return false;
    outPath = file.u8string();
    return true;
}

}  // namespace wm
