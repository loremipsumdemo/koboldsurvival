#include "app.h"
#include "debug_log.h"
#include "game_tables.h"
#include "game_data_check.h"
#include "user_files.h"
#include "profile.h"

using namespace play;

int App::init() {
    if (!SDL_Init(SDL_INIT_VIDEO)) return fatalError(nullptr, std::string("SDL_Init failed: ") + SDL_GetError());
    if (!TTF_Init()) return fatalError(nullptr, std::string("TTF_Init failed: ") + SDL_GetError());
    if (!pakOpen("data.pak")) return fatalError(nullptr, "Could not open data.pak. It must sit next to the game executable.");
    {
        std::string err;
        if (!loadGameTables(err)) return fatalError(nullptr, "The game's data has a mistake: " + err);
        const std::vector<std::string> problems = gameDataProblems();
        if (!problems.empty()) return fatalError(nullptr, "The game's data has a mistake: " + problems.front());
    }
    settings = loadSettings(settingsFilePath());
    fonts = openUiFonts(settings.fontScale, settings.fontPath, settings.language);
    appliedFontScale = settings.fontScale;
    appliedFontPath = settings.fontPath;
    appliedLanguage = settings.language;
    {
        std::string langErr;
        if (!loadLanguage(settings.language, langErr)) {
            SDL_Log("Language: %s. Using the default language.", langErr.c_str());
            settings.language = LANGUAGE_DEFAULT_ID;
            loadLanguage(settings.language, langErr);
            saveSettings(settings, settingsFilePath());
        }
    }
    if (!fonts.valid()) return fatalError(nullptr, std::string("Could not open a font from data.pak: ") + SDL_GetError());

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    // 24 bits: the depth every graphics card offers for a window. Asking for 32 made Windows fall back to its
    // software renderer ("GDI Generic", OpenGL 1.1) on cards without it, and the game could not start.
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    // One window, made once. Destroying an OpenGL window and making another left the new one black on Windows.
    window = SDL_CreateWindow("Kobold Survival", 1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!window) return fatalError(nullptr, std::string("Could not create the game window: ") + SDL_GetError());
    ctx = SDL_GL_CreateContext(window);
    if (!ctx || !SDL_GL_MakeCurrent(window, ctx))
        return fatalError(window, std::string("Could not start OpenGL 3.3: ") + SDL_GetError() +
                                  "\nUpdating the graphics driver may help.");
    {
        // The game needs OpenGL 3.3 on the graphics card. Windows' own software renderer only has 1.1.
        const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
        const char* card = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
        int major = 0, minor = 0;
        if (version) std::sscanf(version, "%d.%d", &major, &minor);
        const bool software = card && std::string(card).find("GDI Generic") != std::string::npos;
        if (major < 3 || (major == 3 && minor < 3) || software)
            return fatalError(window, std::string("The game needs OpenGL 3.3 on the graphics card, and got OpenGL ") +
                                      (version ? version : "?") + " on " + (card ? card : "?") +
                                      ".\nUpdating the graphics driver may help.");
    }
    {
        // No render scale chosen yet, on a card that shares the processor's memory or draws in software: start at 75%, which
        // costs little in looks and a good deal of the ground pass. Chosen once; the settings menu changes it as before.
        const std::string card = reinterpret_cast<const char*>(glGetString(GL_RENDERER) ? glGetString(GL_RENDERER) : (const GLubyte*)"");
        const bool weak = (card.find("Intel") != std::string::npos && card.find("Arc") == std::string::npos) ||
                          card.find("llvmpipe") != std::string::npos || card.find("SwiftShader") != std::string::npos ||
                          card.find("Radeon(TM) Graphics") != std::string::npos || card.find("Radeon Graphics") != std::string::npos;
        if (!settings.renderScaleSet) {
            if (weak) settings.renderScale = 75;
            settings.renderScaleSet = true;
            saveSettings(settings, settingsFilePath());
        }
    }
    glewExperimental = GL_TRUE;
    GLenum glewErr = glewInit();
    if (glewErr != GLEW_OK)
        return fatalError(window, std::string("GLEW init failed: ") + reinterpret_cast<const char*>(glewGetErrorString(glewErr)));
    glGetError();

    displayInfo = queryDisplays(settings.monitor);
    applyDisplay(window, settings, displayInfo);
    SDL_GL_SetSwapInterval(settings.vsync ? 1 : 0);

    // The pictures, models and shaders fill the bar up to here; the sounds fill the rest
    constexpr float SOUNDS_FROM = 0.9f;
    LoadingScreen loading(window, renderer);
    if (!renderer.init(fonts.hud, fonts.ui, [&](float p, const std::string& what) { loading.show(p * SOUNDS_FROM, what); }))
        return fatalError(window, "Loading the game's textures, models or shaders failed.");
    renderer.setListFont(fonts.list);
    renderer.setTitleFont(openFontChain(TITLE_NAME_POINTS, true, settings.fontPath, settings.language, nameFontOwned));

    auto placeFromSettings = [&](PanelId which) {
        const std::array<float, 4>& v = settings.panels[(size_t)idx(which)];
        PanelPlace p = defaultPanelPlace(which);
        p.dx = v[0];
        p.dy = v[1];
        p.w = v[2];
        p.h = v[3];
        return p;
    };
    squadPlace = placeFromSettings(PanelId::Squad);
    unitPlace = placeFromSettings(PanelId::Unit);
    logPlace = placeFromSettings(PanelId::Info);

    partyRng.gen.seed((unsigned)std::time(nullptr));

    saves = listSaves();
    sortSaves(saves, sortKey, sortAsc);

    lastTicks = SDL_GetTicksNS();
    fpsSince = SDL_GetTicks();

    // Every sound effect, and the first screen's music opened ready to start, so nothing is still loading once the
    // title screen shows
    LoadStages stages([&](float p, const std::string& what) { loading.show(p, what); });
    const int soundCount = (int)Sfx::Count + 1;
    stages.stage(SOUNDS_FROM, "LOADING_SOUNDS", soundCount);
    audio::init();
    applyAudio();
    for (int i = 0; i < (int)Sfx::Count; ++i) {
        const SfxInfo& info = sfxInfo((Sfx)i);
        audio::preloadEffect(info.file, info.ui ? audio::Bus::Interface : audio::Bus::Effects);
        stages.step(SOUNDS_FROM + (1.0f - SOUNDS_FROM) * (float)(i + 1) / (float)soundCount, i + 1);
    }
    const Soundtrack first = soundtrackFor(screen);
    if (first.music) audio::preloadTrack(audio::Bus::Music, first.music);
    if (first.ambient) audio::preloadTrack(audio::Bus::Ambient, first.ambient);

    // The full bar goes to black, and the title screen comes up out of black (see run)
    loading.fadeOut();

    lastTicks = SDL_GetTicksNS();
    return 0;
}

void App::beginFrame() {
    frameStart = std::chrono::steady_clock::now();

    const Soundtrack sound = soundtrackFor(screen);
    if (sound.music) audio::playTrack(audio::Bus::Music, sound.music);
    if (sound.ambient) audio::playTrack(audio::Bus::Ambient, sound.ambient);

    SDL_GetWindowSizeInPixels(window, &winW, &winH);
    viewH = winH;

    if (const int pct = menuFontPercent(settings.fontScale, winH); pct != appliedMenuPercent) {
        UiFonts next = openUiFonts(pct, settings.fontPath, settings.language);
        if (next.valid()) {
            renderer.setMenuFonts(next.hud, next.ui, next.list);
            closeUiFonts(menuFonts);
            menuFonts = next;
        } else {
            closeUiFonts(next);
        }
        appliedMenuPercent = pct;
    }
}

void App::run() {
    benchStart();
    while (running) {
        beginFrame();

        renderer.setMenuText(screen != Screen::Playing);

        // While the screen goes to black for what comes next, nothing else can be clicked
        while (SDL_PollEvent(&e)) {
            if (!afterFadeOut || e.type == SDL_EVENT_QUIT) handleEvent();
        }

        PROFILED("frame: update", update());
        PROFILED("frame: updateHover", updateHover());
        benchFrame();
        debugLogFrame();
        PROFILED("frame: render", render());
        putUnitsBack();

        // The first screen, and the game once entered, come up out of black. The first frame is wholly black and the
        // fade is timed from when it shows, since that frame is a slow one (fonts opened, the ground first drawn).
        bool wentBlack = false;
        if (afterFadeOut) {
            const float black = (float)(SDL_GetTicksNS() - fadeOutFrom) / (float)FADE_NS;
            renderer.drawBlackOver(black, winW, winH);
            wentBlack = black >= 1.0f;
        } else if (fadingIn) {
            const float black = fadeInFrom ? 1.0f - (float)(SDL_GetTicksNS() - fadeInFrom) / (float)FADE_NS : 1.0f;
            if (black <= 0.0f) fadingIn = false;
            else renderer.drawBlackOver(black, winW, winH);
        }

        if (renderer.hoverFrameEnd()) playSfx(Sfx::UiHover);
        PROFILED("frame: swap", SDL_GL_SwapWindow(window));
        if (fadingIn && !fadeInFrom) fadeInFrom = SDL_GetTicksNS();
        if (wentBlack) {
            // The screen is black: now the slow part, and whatever it leaves showing comes up out of black
            const std::function<void()> then = std::move(afterFadeOut);
            afterFadeOut = nullptr;
            then();
            fadingIn = true;
            fadeInFrom = 0;
            lastTicks = SDL_GetTicksNS();
        }
        PROFILE_FRAME_END();
        if (bench.quit) running = false;
        if (!bench.on) limitFrameRate(frameStart, settings.fpsLimit);
    }
}

void App::shutdown() {
    debuglog::line("session", "end");
    debuglog::setEnabled(false);
    audio::shutdown();
    renderer.shutdown();
    renderer.setTitleFont(nullptr);
    closeFonts(nameFontOwned);
    closeUiFonts(fonts);
    closeUiFonts(menuFonts);
    TTF_Quit();
    SDL_GL_DestroyContext(ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
}
