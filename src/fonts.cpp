#include "fonts.h"
#include "fontlist.h"
#include "pak.h"
#include <algorithm>
#include <cctype>
#include <filesystem>

namespace fs = std::filesystem;

namespace {

// The built-in fonts, in their default order
const char* const BUILT_IN_FONTS[] = {
    "data/font/NotoSans-Regular.ttf",        // Latin, Cyrillic, Greek, Vietnamese
    "data/font/NotoSansArabic-Regular.ttf",  // Arabic
    "data/font/NotoSansJP-Regular.ttf",      // Japanese
    "data/font/NotoSansKR-Regular.ttf",      // Korean
    "data/font/NotoSansSC-Regular.ttf",      // Chinese, Simplified
    "data/font/NotoSansTC-Regular.ttf",      // Chinese, Traditional
    "data/font/NotoSansThai-Regular.ttf",    // Thai
};
constexpr int BUILT_IN_COUNT = 7;

// The index of the built-in font that suits a language best, or -1 to keep the default order
int suitedIndex(const std::string& languageId) {
    if (languageId == "language_ar") return 1;
    if (languageId == "language_ja") return 2;
    if (languageId == "language_ko") return 3;
    if (languageId == "language_zh-CN") return 4;
    if (languageId == "language_zh-TW") return 5;
    if (languageId == "language_th") return 6;
    return -1;
}

// The file name of a path, in lower case
std::string lowerFileName(const std::string& path) {
    std::string name = fs::path(path).filename().string();
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return name;
}

} // namespace

std::vector<std::string> builtInFontOrder(const std::string& languageId) {
    std::vector<std::string> order;
    const int first = suitedIndex(languageId);
    if (first >= 0) order.push_back(BUILT_IN_FONTS[first]);
    for (int i = 0; i < BUILT_IN_COUNT; ++i) {
        if (i != first) order.push_back(BUILT_IN_FONTS[i]);
    }
    return order;
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
    for (const std::string& name : builtInFontOrder(languageId)) add(pakFont(name, size));
    const std::string system = systemFontPath();
    if (!system.empty()) add(TTF_OpenFont(system.c_str(), size));
    return primary;
}

void closeFonts(std::vector<TTF_Font*>& owned) {
    for (TTF_Font* font : owned) TTF_ClearFallbackFonts(font);
    for (TTF_Font* font : owned) TTF_CloseFont(font);
    owned.clear();
}
