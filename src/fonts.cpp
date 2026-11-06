#include "textcase.h"
#include "fonts.h"
#include "fontlist.h"
#include "game_tables.h"
#include "pak.h"
#include <algorithm>
#include <cctype>
#include <filesystem>

static TTF_Font* pakFont(const std::string& name, float size) {
    const unsigned char* bytes = nullptr;
    size_t byteCount = 0;
    if (!pakFind(name, bytes, byteCount)) {
        SDL_Log("Not in data.pak: %s", name.c_str());
        return nullptr;
    }
    return TTF_OpenFontIO(SDL_IOFromConstMem(bytes, byteCount), true, size);
}

namespace fs = std::filesystem;

namespace {
std::string lowerFileName(const std::string& path) {
    std::string name = fs::u8path(path).filename().u8string();
    name = toLower(name);
    return name;
}
}

std::string systemFontPath() {
    static const char* const common[] = {
        "segoeui.ttf", "arial.ttf", "helvetica.ttf", "dejavusans.ttf", "liberationsans-regular.ttf",
    };
    const std::vector<std::string>& installed = installedFontPaths();
    for (const char* want : common) {
        for (const std::string& path : installed) {
            if (lowerFileName(path) == want) return path;
        }
    }
    return std::string();
}

TTF_Font* openFontChain(float size, bool bold, const std::string& pickedPath, const std::string& languageId,
                        std::vector<TTF_Font*>& owned) {
    TTF_Font* primary = nullptr;
    auto add = [&](TTF_Font* font) {
        if (!font) return;
        TTF_SetFontStyle(font, bold ? TTF_STYLE_BOLD : TTF_STYLE_NORMAL);
        owned.push_back(font);
        if (!primary) primary = font;
        else TTF_AddFallbackFont(primary, font);
    };
    if (!pickedPath.empty()) add(TTF_OpenFont(pickedPath.c_str(), size));
    for (const std::string& file : fontFilesFor(languageId)) add(pakFont("data/font/" + file, size));
    const std::string system = systemFontPath();
    if (!system.empty()) add(TTF_OpenFont(system.c_str(), size));
    return primary;
}

void closeFonts(std::vector<TTF_Font*>& owned) {
    for (TTF_Font* font : owned) TTF_ClearFallbackFonts(font);
    for (TTF_Font* font : owned) TTF_CloseFont(font);
    owned.clear();
}
