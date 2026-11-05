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
#include <vector>

// The folder beside the game where F12 screenshots are saved
std::string screenshotsDir() {
    const char* base = SDL_GetBasePath();
    return std::string(base ? base : "") + "screenshots";
}

// Reads back the frame that is about to be shown and saves it as a PNG in the screenshots folder.
// Returns the file's name, or an empty string if it could not be saved.
std::string saveScreenshot(SDL_Window* window) {
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    if (w <= 0 || h <= 0) return "";

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    std::vector<unsigned char> pixels((size_t)w * h * 4);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    // OpenGL counts rows from the bottom, the picture from the top. The window's alpha is not part of the picture.
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

    // Named by the time it was taken. A number is added if that name is already used.
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

// Every SDL_Log line also goes to log.txt beside the game, flushed at once, so a game that closes on its
// own leaves the reason behind. The lines still go to the console or debugger output as before.
std::FILE* gLogFile = nullptr;
SDL_LogOutputFunction gDefaultLog = nullptr;
void* gDefaultLogData = nullptr;

void SDLCALL logToFile(void*, int category, SDL_LogPriority priority, const char* message) {
    if (gLogFile) {
        std::fprintf(gLogFile, "%s\n", message);
        std::fflush(gLogFile);
    }
    if (gDefaultLog) gDefaultLog(gDefaultLogData, category, priority, message);
}

// log.txt goes next to the exe, whatever folder the game was started from
void startLogFile() {
    const char* base = SDL_GetBasePath();   // owned by SDL; works before SDL_Init
    const std::string path = std::string(base ? base : "") + "log.txt";
    gLogFile = std::fopen(path.c_str(), "w");
    if (!gLogFile) gLogFile = std::fopen("log.txt", "w");
    SDL_GetLogOutputFunction(&gDefaultLog, &gDefaultLogData);
    SDL_SetLogOutputFunction(logToFile, nullptr);
}

// Logs why the game has to close and shows it in a message box. Returns the exit code.
int fatalError(SDL_Window* window, const std::string& message) {
    SDL_Log("FATAL: %s", message.c_str());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Kobold Survival", message.c_str(), window);
    return 1;
}

// An exception nothing caught would end the game without a word: say what it was first
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

// Draws one loading frame and keeps the window responsive while things load
void loadingFrame(SDL_Window* window, Renderer& renderer, float progress, const std::string& label,
                  const std::string& category, bool backdrop) {
    SDL_PumpEvents();
    int winW, winH;
    SDL_GetWindowSizeInPixels(window, &winW, &winH);
    renderer.drawLoadingScreen(progress, winW, winH, label, category, backdrop);
    SDL_GL_SwapWindow(window);
}

// Current time in milliseconds since 1970
long long nowMs() {
    return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

// Plays a sound effect now, on the slider it follows (sounds.h lists them all, with their files)
// A sound effect is not played again within this long of its last start: a click that lands on several things at once,
// or an order given to many units, plays it once
// volume and pan are for a world sound, heard from the camera (sounds.h); one too far off to hear is not played at all.
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

// The menus' text (the title, New Game, the scenarios, the party menu, Settings and the Esc menu) is this much larger
// than the game's. The stat rows of the party menu's Stats tab stay at the game's size. The menus are laid out for a
// 720 pixel high window and grow with it, so in a smaller window the text grows less, to keep fitting its boxes:
// the full 1.5 times from 1080 pixels high, and the game's own size at 720.
constexpr float MENU_FONT_SCALE = 1.5f;
int menuFontPercent(int uiScale, int windowH) {
    const float grow = std::clamp(windowH / 720.0f, 1.0f, MENU_FONT_SCALE);
    return (int)std::lround(uiScale * grow);
}

UiFonts openUiFonts(int pct, const std::string& fontPath, const std::string& languageId) {
    // 100% is the default size: 20 points at 100%
    const float size = 20.0f * pct / 100.0f;
    UiFonts f;
    // The HUD, the UI, the lists and the title are bold
    f.hud = openFontChain(size, true, fontPath, languageId, f.owned);
    f.ui = openFontChain(size, true, fontPath, languageId, f.owned);
    f.list = openFontChain(size, true, fontPath, languageId, f.owned);
    return f;
}

void closeUiFonts(UiFonts& f) {
    closeFonts(f.owned);
    f.hud = f.ui = f.list = nullptr;
}
