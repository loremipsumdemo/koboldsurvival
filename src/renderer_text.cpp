// Renderer: fonts and text. Each font keeps a cache of its text textures; fitting, shrinking and wrapping text to a box.
#include "renderer_internal.h"

using namespace render_detail;

namespace {

// Automatic text fit. Text is drawn no smaller than this share of its size, and anything still too long is then
// shortened with "...".
const float kMinTextFit = 0.6f;

} // namespace

void Renderer::setTitleFont(TTF_Font* font) {
    for (auto& entry : titleWords_) {
        if (entry.second.tex) glDeleteTextures(1, &entry.second.tex);
    }
    titleWords_.clear();
    titleFont_ = font;
}

const TextTex& Renderer::titleWord(const std::string& text) {
    auto it = titleWords_.find(text);
    if (it != titleWords_.end()) return it->second;
    TextTex t;
    if (titleFont_ && !text.empty()) {
        t = makeGlyphTexture(titleFont_, text.c_str(), SDL_Color{ 255, 255, 255, 255 });
        if (t.tex) {
            // Mipmaps, so the large picture shrinks smoothly to whatever size the name is drawn at
            glBindTexture(GL_TEXTURE_2D, t.tex);
            glGenerateMipmap(GL_TEXTURE_2D);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        }
    }
    return titleWords_.emplace(text, t).first->second;
}

float Renderer::uiTextWidth(const std::string& s) {
    return uiLine(s).w;
}

float Renderer::uiTextHeight(const std::string& s) {
    return uiLine(s).h;
}

void Renderer::drawUiText(const std::string& s, float x, float y, RGBA tint) {
    drawFontText(text_.ui, s, x, y, tint);
}

void Renderer::drawUiTextCentered(const std::string& s, float cx, float y, RGBA tint) {
    drawUiText(s, cx - uiTextWidth(s) * 0.5f, y, tint);
}

// The text as it will be drawn in a box boxW wide: shortened only if it cannot fit even at the smallest size
std::string Renderer::fitShrinkText(const std::string& s, float boxW) {
    return fitText(s, boxW / kMinTextFit, true);
}

// The factor a line of ui text is drawn at to fit a box: 1 when it fits, otherwise smaller, never below kMinTextFit
float Renderer::fitShrinkScale(const std::string& shown, float boxW, float boxH) {
    const TextLine& hl = uiLine(shown);
    if (hl.w <= 0.0f || hl.h <= 0.0f) return 1.0f;
    float k = 1.0f;
    if (hl.w > boxW) k = std::min(k, boxW / hl.w);
    if (hl.h > boxH) k = std::min(k, boxH / hl.h);
    return std::max(k, kMinTextFit);
}

// Draws ui text with its top-left corner at (x, y), at k times its size
void Renderer::drawUiTextScaled(const std::string& s, float x, float y, float k, RGBA tint) {
    const TextLine& hl = uiLine(s);
    if (!hl.tex.tex) return;
    const float w = hl.w * k, h = hl.h * k;
    drawTexture(hl.tex.tex, x, y, w, h, tint);
}

// Draws ui text centred in a box, shrunk to fit it when automatic text fit is on
void Renderer::drawUiTextInBox(const std::string& s, const Rect& box, RGBA tint) {
    const std::string shown = fitShrinkText(s, box.w);
    if (shown != s) noteTip(s, box);
    const float k = fitShrinkScale(shown, box.w, box.h);
    const float w = uiTextWidth(shown) * k, h = uiTextHeight(shown) * k;
    drawUiTextScaled(shown, box.x + (box.w - w) * 0.5f, box.y + (box.h - h) * 0.5f, k, tint);
}

// Sets the tooltip to the full text if the mouse is over the area the text was drawn in
void Renderer::noteTip(const std::string& full, const Rect& area) {
    if (area.contains(mouseX_, mouseY_)) tip_ = full;
}

void Renderer::setListFont(TTF_Font* font) {
    // Textures made with the old font would be stale, so drop them
    text_.list.clear();
    text_.list.font = font;
}

// Swaps every text font at once, for the Font size setting. Textures made with the old fonts are dropped.
void Renderer::setFonts(TTF_Font* hud, TTF_Font* ui, TTF_Font* list) {
    text_.setGameFonts(hud, ui, list);
}

void Renderer::setMenuFonts(TTF_Font* hud, TTF_Font* ui, TTF_Font* list) {
    text_.setMenuFonts(hud, ui, list);
}

void Renderer::drawBigText(const std::string& text, float cx, float cy, float h, RGBA tint) {
    const TextTex& t = titleWord(text);
    if (!t.tex) {
        drawUiTextLarge(text, cx, cy, h / std::max(1.0f, uiTextHeight("Ag")), tint);
        return;
    }
    const float w = t.aspect * h, x = cx - w * 0.5f, y = cy - h * 0.5f;
    drawTexture(t.tex, x, y, w, h, tint);
}

void Renderer::drawFontText(TextFont& f, const std::string& s, float x, float y, RGBA tint) {
    const TextLine& hl = f.line(s);
    if (!hl.tex.tex) return;
    drawTexture(hl.tex.tex, x, y, hl.w, hl.h, tint);
}

float Renderer::listTextHeight(const std::string& s) {
    return listLine(s).h;
}

float Renderer::listTextWidth(const std::string& s) {
    return listLine(s).w;
}

void Renderer::drawListText(const std::string& s, float x, float y, RGBA tint) {
    drawFontText(text_.list, s, x, y, tint);
}

// Shortens text to fit maxW. Whole words are kept where possible, so a name is cut at a word and
// "..." is added. Text with no room for one word is cut by character.
std::string Renderer::fitText(const std::string& text, float maxW, bool ui) {
    auto width = [&](const std::string& t) { return ui ? uiTextWidth(t) : listTextWidth(t); };
    if (width(text) <= maxW) return text;

    std::vector<std::string> words;
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && text[i] == ' ') ++i;
        const size_t start = i;
        while (i < text.size() && text[i] != ' ') ++i;
        if (i > start) words.push_back(text.substr(start, i - start));
    }
    for (size_t n = words.size(); n > 0; --n) {
        std::string joined;
        for (size_t k = 0; k < n; ++k) joined += (k ? " " : "") + words[k];
        if (width(joined + "...") <= maxW) return joined + "...";
    }

    std::string t = text;
    while (!t.empty() && width(t + "...") > maxW) {
        t.pop_back();
        while (!t.empty() && (static_cast<unsigned char>(t.back()) & 0xC0) == 0x80) t.pop_back();
    }
    return t + "...";
}

std::string Renderer::fitTextTip(const std::string& text, float maxW, const Rect& tipArea) {
    std::string shown = fitText(text, maxW, false);
    if (shown != text) noteTip(text, tipArea);
    return shown;
}

// Every digit takes the width of the widest digit, so numbers that change don't shift sideways.
// Other characters keep their own width.
float Renderer::digitCellWidth() {
    float cell = 0.0f;
    for (char ch = '0'; ch <= '9'; ++ch) cell = std::max(cell, uiTextWidth(std::string(1, ch)));
    return cell;
}

float Renderer::clockTextWidth(const std::string& s) {
    const float cell = digitCellWidth();
    float total = 0.0f;
    for (char ch : s) {
        total += (ch >= '0' && ch <= '9') ? cell : uiTextWidth(std::string(1, ch));
    }
    return total;
}

void Renderer::drawClockText(const std::string& s, float x, float y, RGBA tint) {
    const float cell = digitCellWidth();
    for (char ch : s) {
        const std::string one(1, ch);
        const float cw = uiTextWidth(one);
        if (ch >= '0' && ch <= '9') {
            drawUiText(one, x + (cell - cw) * 0.5f, y, tint);
            x += cell;
        } else {
            drawUiText(one, x, y, tint);
            x += cw;
        }
    }
}

std::vector<std::string> Renderer::wrapListText(const std::string& text, float maxW) {
    std::vector<std::string> out;
    if (!text_.list.font) return out;
    // Measured without making a texture for every trial line
    const auto width = [this](const std::string& s) {
        int tw = 0, th = 0;
        if (!s.empty()) TTF_GetStringSize(text_.list.font, s.c_str(), s.size(), &tw, &th);
        return (float)tw;
    };
    std::string cur;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(' ', start);
        if (end == std::string::npos) end = text.size();
        const std::string word = text.substr(start, end - start);
        if (!word.empty()) {
            const std::string trial = cur.empty() ? word : cur + " " + word;
            if (width(trial) <= maxW) {
                cur = trial;
            } else if (width(word) <= maxW) {
                out.push_back(cur);
                cur = word;
            } else {
                // A word wider than the line (or text with no spaces, as in Japanese, Chinese and Thai) breaks
                // between its characters
                std::string piece = cur.empty() ? std::string() : cur + " ";
                for (size_t i = 0; i < word.size();) {
                    const unsigned char c = (unsigned char)word[i];
                    const size_t n = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
                    const std::string ch = word.substr(i, n);
                    i += n;
                    if (!piece.empty() && width(piece + ch) > maxW) {
                        if (piece.back() == ' ') piece.pop_back();
                        if (!piece.empty()) out.push_back(piece);
                        piece = ch;
                    } else {
                        piece += ch;
                    }
                }
                cur = piece;
            }
        }
        start = end + 1;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

void Renderer::drawUiTextLarge(const std::string& s, float cx, float cy, float scale, RGBA tint) {
    const TextLine& hl = uiLine(s);
    if (!hl.tex.tex) return;
    const float w = hl.w * scale, h = hl.h * scale;
    const float x = cx - w * 0.5f, y = cy - h * 0.5f;
    drawTexture(hl.tex.tex, x, y, w, h, tint);
}
