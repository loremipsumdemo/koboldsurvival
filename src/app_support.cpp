#include "app_support.h"
#include <chrono>
#include <cmath>
#include "audio.h"
#include "fonts.h"
#include "renderer.h"
#include <cstdlib>
#include <ctime>
#include <exception>
#include <filesystem>
#include <GL/glew.h>
#include <SDL3/SDL_opengl.h>
#include <SDL3_image/SDL_image.h>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <vector>
#include "user_files.h"

std::string screenshotsDir() {
    const char* base = SDL_GetBasePath();
    return std::string(base ? base : "") + "screenshots";
}

std::string saveScreenshot(SDL_Window* window) {
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    if (w <= 0 || h <= 0) return "";

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    std::vector<unsigned char> pixels((size_t)w * h * 4);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    const size_t rowBytes = (size_t)w * 4;
    std::vector<unsigned char> flipped(pixels.size());
    for (int y = 0; y < h; ++y) {
        std::copy_n(pixels.data() + (size_t)(h - 1 - y) * rowBytes, rowBytes, flipped.data() + (size_t)y * rowBytes);
    }
    for (size_t i = 3; i < flipped.size(); i += 4) flipped[i] = 255;

    std::error_code ec;
    const std::filesystem::path dir = screenshotsDir();
    std::filesystem::create_directories(dir, ec);
    if (ec) return "";

    const std::time_t now = std::time(nullptr);
    const std::tm* tm = std::localtime(&now);
    char stamp[32] = "screenshot";
    if (tm) std::strftime(stamp, sizeof(stamp), "screenshot_%Y%m%d_%H%M%S", tm);
    std::string name = std::string(stamp) + ".png";
    for (int n = 2; std::filesystem::exists(dir / name, ec); ++n) {
        name = std::string(stamp) + "_" + std::to_string(n) + ".png";
    }

    SDL_Surface* surface = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_ABGR8888, flipped.data(), (int)rowBytes);
    if (!surface) return "";
    const bool ok = IMG_SavePNG(surface, (dir / name).string().c_str());
    SDL_DestroySurface(surface);
    return ok ? name : "";
}

int fatalError(SDL_Window* window, const std::string& message) {
    SDL_Log("FATAL: %s", message.c_str());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Kobold Survival", message.c_str(), window);
    return 1;
}

void onTerminate() {
    std::string what = "an unknown error";
    if (std::exception_ptr ex = std::current_exception()) {
        try { std::rethrow_exception(ex); }
        catch (const std::exception& e) { what = e.what(); }
        catch (...) {}
    }
    fatalError(nullptr, "The game stopped on an error: " + what);
    std::abort();
}

void loadingFrame(SDL_Window* window, Renderer& renderer, float progress, const std::string& label,
                  const std::string& category, bool backdrop) {
    SDL_PumpEvents();
    int winW, winH;
    SDL_GetWindowSizeInPixels(window, &winW, &winH);
    renderer.drawLoadingScreen(progress, winW, winH, label, category, backdrop);
    SDL_GL_SwapWindow(window);
}

long long nowMs() {
    return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

constexpr Uint64 SFX_REPEAT_GAP_MS = 50;
void playSfx(Sfx s, float volume, float pan) {
    static Uint64 lastStart[(int)Sfx::Count] = {};
    static bool started[(int)Sfx::Count] = {};
    if (volume <= 0.0f) return;
    const int i = (int)s;
    if (i >= 0 && i < (int)Sfx::Count) {
        const Uint64 now = SDL_GetTicks();
        if (started[i] && now - lastStart[i] < SFX_REPEAT_GAP_MS) return;
        started[i] = true;
        lastStart[i] = now;
    }
    const SfxInfo& info = sfxInfo(s);
    audio::playEffect(info.file, info.ui ? audio::Bus::Interface : audio::Bus::Effects, volume, pan);
}

constexpr float MENU_FONT_SCALE = 1.5f;
int menuFontPercent(int uiScale, int windowH) {
    const float grow = std::clamp(windowH / 720.0f, 1.0f, MENU_FONT_SCALE);
    return (int)std::lround(uiScale * grow);
}

UiFonts openUiFonts(int pct, const std::string& fontPath, const std::string& languageId) {
    const float size = 20.0f * pct / 100.0f;
    UiFonts f;
    f.hud = openFontChain(size, true, fontPath, languageId, f.owned);
    f.ui = openFontChain(size, true, fontPath, languageId, f.owned);
    f.list = openFontChain(size, true, fontPath, languageId, f.owned);
    return f;
}

void closeUiFonts(UiFonts& f) {
    closeFonts(f.owned);
    f.hud = f.ui = f.list = nullptr;
}
