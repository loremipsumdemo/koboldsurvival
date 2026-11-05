#pragma once
// Text as textures. Each font keeps the pictures of the strings drawn in it, made the first time each is drawn.
// The game has two sets of fonts: the game's, and the menus' (the same fonts at a larger size). One set is in use at a
// time, and each keeps its own pictures, so switching between them is cheap.
#include <map>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <GL/glew.h>

// A texture plus its width/height ratio
struct TextTex {
    GLuint tex = 0;
    float aspect = 1.0f;
};

// One string's picture, white (tinted when drawn), and its size in pixels
struct TextLine {
    std::string text;
    TextTex tex;
    float w = 0.0f, h = 0.0f;
};

// Renders text in the font into a white texture
TextTex makeGlyphTexture(TTF_Font* font, const char* text, SDL_Color color);

// A font with its cache of text pictures. The font is owned by the caller.
struct TextFont {
    TTF_Font* font = nullptr;
    std::map<std::string, TextLine> lines;

    // The string's picture, made on first use. Blank text has no texture but keeps the line height; without a font
    // the line is empty.
    const TextLine& line(const std::string& s);
    float width(const std::string& s) { return line(s).w; }
    float height(const std::string& s) { return line(s).h; }
    // Drops every picture, for a new font or a cache that has grown too large
    void clear();
};

class TextCache {
public:
    // The fonts of the set in use: the menus', the lists', and the Stats popout's two
    TextFont ui, list, statsTitle, statsRow;
    // The hover list's font, and its lines as last drawn
    TTF_Font* hud = nullptr;

    // The hover list's lines for these texts: a line is made again only when its text changed
    const std::vector<TextLine>& hudLines(const std::vector<std::string>& texts);

    // The game's fonts and the menus' fonts. Pictures made with the old fonts are dropped.
    void setGameFonts(TTF_Font* hud, TTF_Font* ui, TTF_Font* list, TTF_Font* statsTitle, TTF_Font* statsRow);
    void setMenuFonts(TTF_Font* hud, TTF_Font* ui, TTF_Font* list, TTF_Font* statsTitle, TTF_Font* statsRow);
    // Which set is in use. Without menu fonts the game's stay in use.
    void setMenuText(bool on);
    bool menuText() const { return menuText_; }
    // Drops the pictures of both sets
    void clear();

private:
    struct Set {
        TTF_Font* hud = nullptr;
        std::vector<TextLine> hudLines;
        TextFont ui, list, statsTitle, statsRow;
    };
    std::vector<TextLine> hudLines_;
    Set parked_;               // the set not in use
    bool menuText_ = false;
    void swapSets();
    void clearActive();
    void replaceActive(TTF_Font* hud, TTF_Font* ui, TTF_Font* list, TTF_Font* statsTitle, TTF_Font* statsRow);
};
