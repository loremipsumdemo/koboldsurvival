#include "renderer_internal.h"
#include "gl_util.h"
#include <SDL3_image/SDL_image.h>

namespace {
// The size a save's picture is kept at, 16:9
constexpr int GRAB_W = 384, GRAB_H = 216;
}

// Copies the world just drawn (before the selection rings and the UI) down to GRAB_W x GRAB_H, in two halvings so it
// isn't sparkly, and reads it back
void Renderer::grabScene() {
    if (!grabWanted_) return;
    grabWanted_ = false;
    GLint vp[4] = { 0, 0, 0, 0 };
    glGetIntegerv(GL_VIEWPORT, vp);
    const int W = vp[2], H = vp[3];
    if (W <= 0 || H <= 0) return;
    int cw = W, ch = H;
    if (W * 9 > H * 16) cw = H * 16 / 9;
    else ch = W * 9 / 16;
    const int cx = vp[0] + (W - cw) / 2, cy = vp[1] + (H - ch) / 2;

    // Where the world was drawn (the window), asked before anything here binds another framebuffer, so the rest of the
    // frame goes back to it
    GLint drawn = 0, texture = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawn);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);

    const int sizes[2][2] = { { GRAB_W * 2, GRAB_H * 2 }, { GRAB_W, GRAB_H } };
    for (int i = 0; i < 2; ++i) {
        if (grabFbo_[i]) continue;
        grabTex_[i] = makeTexture2D(sizes[i][0], sizes[i][1], GL_RGBA8, GL_RGBA, nullptr, GL_LINEAR, GL_CLAMP_TO_EDGE);
        glGenFramebuffers(1, &grabFbo_[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, grabFbo_[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, grabTex_[i], 0);
    }
    glBindTexture(GL_TEXTURE_2D, (GLuint)texture);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)drawn);
    if (drawn == 0) glReadBuffer(GL_BACK);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, grabFbo_[0]);
    glBlitFramebuffer(cx, cy, cx + cw, cy + ch, 0, 0, sizes[0][0], sizes[0][1], GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, grabFbo_[0]);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, grabFbo_[1]);
    glBlitFramebuffer(0, 0, sizes[0][0], sizes[0][1], 0, 0, GRAB_W, GRAB_H, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, grabFbo_[1]);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    std::vector<uint8_t> up((size_t)GRAB_W * GRAB_H * 4);
    glReadPixels(0, 0, GRAB_W, GRAB_H, GL_RGBA, GL_UNSIGNED_BYTE, up.data());
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)drawn);
    if (drawn == 0) glReadBuffer(GL_BACK);

    // GL reads bottom row first; pictures are kept top row first
    grab_.resize(up.size());
    const size_t row = (size_t)GRAB_W * 4;
    for (int y = 0; y < GRAB_H; ++y) {
        std::copy_n(up.data() + (size_t)(GRAB_H - 1 - y) * row, row, grab_.data() + (size_t)y * row);
    }
    for (size_t i = 3; i < grab_.size(); i += 4) grab_[i] = 255;
    grabW_ = GRAB_W;
    grabH_ = GRAB_H;
    ++grabCount_;
}

const std::vector<uint8_t>* Renderer::sceneGrab(int& w, int& h) const {
    if (grab_.empty()) return nullptr;
    w = grabW_;
    h = grabH_;
    return &grab_;
}

bool Renderer::previewFromFile(const std::string& path) {
    const std::string key = "file:" + path;
    if (key == previewKey_) return previewShown_;
    previewKey_ = key;
    previewShown_ = false;
    SDL_Surface* s = IMG_Load(path.c_str());
    if (!s) return false;
    SDL_Surface* conv = SDL_ConvertSurface(s, SDL_PIXELFORMAT_ABGR8888);
    SDL_DestroySurface(s);
    if (!conv) return false;
    std::vector<uint8_t> px((size_t)conv->w * conv->h * 4);
    for (int y = 0; y < conv->h; ++y) {
        std::copy_n((const uint8_t*)conv->pixels + (size_t)y * conv->pitch, (size_t)conv->w * 4, px.data() + (size_t)y * conv->w * 4);
    }
    glDeleteTextures(1, &previewTex_);
    previewTex_ = makeTexture2D(conv->w, conv->h, GL_RGBA8, GL_RGBA, px.data(), GL_LINEAR, GL_CLAMP_TO_EDGE);
    SDL_DestroySurface(conv);
    previewShown_ = true;
    return true;
}

bool Renderer::previewFromGrab() {
    const std::string key = "grab:" + std::to_string(grabCount_);
    if (key == previewKey_) return previewShown_;
    previewKey_ = key;
    previewShown_ = !grab_.empty();
    if (!previewShown_) return false;
    glDeleteTextures(1, &previewTex_);
    previewTex_ = makeTexture2D(grabW_, grabH_, GL_RGBA8, GL_RGBA, grab_.data(), GL_LINEAR, GL_CLAMP_TO_EDGE);
    return true;
}

void Renderer::drawSavePreview(const Rect& r) {
    if (previewShown_ && previewTex_) drawTexture(previewTex_, r.x, r.y, r.w, r.h, RGBA{ 1.0f, 1.0f, 1.0f, 1.0f });
}

void Renderer::releasePreviewGl() {
    glDeleteTextures(1, &previewTex_);
    previewTex_ = 0;
    previewKey_.clear();
    previewShown_ = false;
    glDeleteFramebuffers(2, grabFbo_);
    glDeleteTextures(2, grabTex_);
    grabFbo_[0] = grabFbo_[1] = 0;
    grabTex_[0] = grabTex_[1] = 0;
}
