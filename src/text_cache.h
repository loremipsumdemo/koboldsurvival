#pragma once
#include <map>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <GL/glew.h>

struct TextTex {
    GLuint tex = 0;
    float aspect = 1.0f;
};

struct TextLine {
    std::string text;
    TextTex tex;
    float w = 0.0f, h = 0.0f;
    unsigned long long used = 0;   // when it was last asked for (SDL ticks, as of the last trim)
};

TextTex makeGlyphTexture(TTF_Font* font, const char* text, SDL_Color color);

struct TextFont {
    TTF_Font* font = nullptr;
    std::map<std::string, TextLine> lines;
    unsigned long long now = 0;

    const TextLine& line(const std::string& s);
    // Past limit lines, lets go of the ones asked for least lately. A line asked for in the last half second is kept
    // whatever the limit, so text that stays on screen is never made again however much of it there is.
    void trim(size_t limit);
    float width(const std::string& s) { return line(s).w; }
    float height(const std::string& s) { return line(s).h; }
    void clear();
};

class TextCache {
public:
    TextFont ui, list;
    TTF_Font* hud = nullptr;

    const std::vector<TextLine>& hudLines(const std::vector<std::string>& texts);

    void setGameFonts(TTF_Font* hud, TTF_Font* ui, TTF_Font* list);
    void setMenuFonts(TTF_Font* hud, TTF_Font* ui, TTF_Font* list);
    void setMenuText(bool on);
    bool menuText() const { return menuText_; }
    void clear();

private:
    struct Set {
        TTF_Font* hud = nullptr;
        std::vector<TextLine> hudLines;
        TextFont ui, list;
    };
    std::vector<TextLine> hudLines_;
    Set parked_;
    bool menuText_ = false;
    void swapSets();
    void clearActive();
    void replaceActive(TTF_Font* hud, TTF_Font* ui, TTF_Font* list);
};
