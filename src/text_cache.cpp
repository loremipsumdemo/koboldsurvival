#include "text_cache.h"
#include "gl_util.h"
#include <cstring>
#include <utility>

TextTex makeGlyphTexture(TTF_Font* font, const char* text, SDL_Color color) {
    SDL_Surface* s = TTF_RenderText_Blended(font, text, strlen(text), color);
    if (!s) {
        SDL_Log("TTF_RenderText failed: %s", SDL_GetError());
        return {};
    }
    SDL_Surface* conv = SDL_ConvertSurface(s, SDL_PIXELFORMAT_ABGR8888);
    SDL_DestroySurface(s);

    const GLuint t = makeTexture2D(conv->w, conv->h, GL_RGBA, GL_RGBA, conv->pixels, GL_LINEAR, GL_CLAMP_TO_EDGE);

    TextTex result{ t, (float)conv->w / (float)conv->h };
    SDL_DestroySurface(conv);
    return result;
}

const TextLine& TextFont::line(const std::string& s) {
    auto it = lines.find(s);
    if (it != lines.end()) return it->second;

    TextLine hl;
    hl.text = s;
    int tw = 0, th = 0;
    if (!font) return lines[s] = hl;
    if (s.find_first_not_of(' ') == std::string::npos) {
        // SDL_ttf fails on text with no width, so blank text gets no texture but keeps its line height
        TTF_GetStringSize(font, "Ag", 2, &tw, &th);
        hl.h = (float)th;
        return lines[s] = hl;
    }
    TTF_GetStringSize(font, s.c_str(), s.size(), &tw, &th);
    hl.tex = makeGlyphTexture(font, s.c_str(), SDL_Color{ 255, 255, 255, 255 });
    hl.w = (float)tw;
    hl.h = (float)th;
    return lines[s] = hl;
}

void TextFont::clear() {
    for (auto& entry : lines) {
        if (entry.second.tex.tex) glDeleteTextures(1, &entry.second.tex.tex);
    }
    lines.clear();
}

const std::vector<TextLine>& TextCache::hudLines(const std::vector<std::string>& texts) {
    if (!hud) return hudLines_;
    for (size_t i = 0; i < texts.size(); ++i) {
        if (i == hudLines_.size()) hudLines_.emplace_back();
        TextLine& hl = hudLines_[i];
        const std::string& text = texts[i];
        if (hl.tex.tex && hl.text == text) continue;
        if (hl.tex.tex) glDeleteTextures(1, &hl.tex.tex);

        int tw = 0, th = 0;
        TTF_GetStringSize(hud, text.c_str(), text.size(), &tw, &th);
        hl.text = text;
        hl.tex = makeGlyphTexture(hud, text.c_str(), SDL_Color{ 255, 255, 255, 255 });
        hl.w = (float)tw;
        hl.h = (float)th;
    }
    // Drop lines that are no longer shown
    while (hudLines_.size() > texts.size()) {
        if (hudLines_.back().tex.tex) glDeleteTextures(1, &hudLines_.back().tex.tex);
        hudLines_.pop_back();
    }
    return hudLines_;
}

void TextCache::clearActive() {
    for (TextLine& hl : hudLines_) {
        if (hl.tex.tex) glDeleteTextures(1, &hl.tex.tex);
    }
    hudLines_.clear();
    for (TextFont* f : { &ui, &list, &statsTitle, &statsRow }) f->clear();
}

void TextCache::replaceActive(TTF_Font* hudFont, TTF_Font* uiFont, TTF_Font* listFont, TTF_Font* statsTitleFont,
                              TTF_Font* statsRowFont) {
    clearActive();
    hud = hudFont;
    ui.font = uiFont;
    list.font = listFont;
    statsTitle.font = statsTitleFont;
    statsRow.font = statsRowFont;
}

void TextCache::swapSets() {
    std::swap(hud, parked_.hud);
    std::swap(hudLines_, parked_.hudLines);
    std::swap(ui, parked_.ui);
    std::swap(list, parked_.list);
    std::swap(statsTitle, parked_.statsTitle);
    std::swap(statsRow, parked_.statsRow);
}

void TextCache::setGameFonts(TTF_Font* hudFont, TTF_Font* uiFont, TTF_Font* listFont, TTF_Font* statsTitleFont,
                             TTF_Font* statsRowFont) {
    if (menuText_) swapSets();
    replaceActive(hudFont, uiFont, listFont, statsTitleFont, statsRowFont);
    if (menuText_) swapSets();
}

void TextCache::setMenuFonts(TTF_Font* hudFont, TTF_Font* uiFont, TTF_Font* listFont, TTF_Font* statsTitleFont,
                             TTF_Font* statsRowFont) {
    if (!menuText_) swapSets();
    replaceActive(hudFont, uiFont, listFont, statsTitleFont, statsRowFont);
    if (!menuText_) swapSets();
}

void TextCache::setMenuText(bool on) {
    if (on == menuText_) return;
    // Without menu fonts there is nothing to swap to
    if (on && !parked_.ui.font) return;
    swapSets();
    menuText_ = on;
}

void TextCache::clear() {
    // Both sets: the one in use, then the parked one
    clearActive();
    swapSets();
    clearActive();
    swapSets();
}
