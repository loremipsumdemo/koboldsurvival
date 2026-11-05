#include "app.h"

using namespace play;

int App::init() {
    if (!SDL_Init(SDL_INIT_VIDEO)) return fatalError(nullptr, std::string("SDL_Init failed: ") + SDL_GetError());
    if (!TTF_Init()) return fatalError(nullptr, std::string("TTF_Init failed: ") + SDL_GetError());
    // Every asset comes from data.pak next to the executable, except the music and sound effects: those are loose
    // files in the BGM and SFX folders beside it, read by the audio module.
    if (!pakOpen("data.pak")) return fatalError(nullptr, "Could not open data.pak. It must sit next to the game executable.");
    SDL_Log("data.pak open");
    // The factions and their reputations, from data/factioninfo.txt. Missing or bad, the built-in list is used.
    {
        const unsigned char* bytes = nullptr;
        size_t size = 0;
        std::string err;
        if (!pakFind("data/factioninfo.txt", bytes, size)) {
            SDL_Log("data/factioninfo.txt is missing; using the built-in factions");
        } else if (!loadFactions(std::string((const char*)bytes, size), err)) {
            SDL_Log("data/factioninfo.txt: %s; using the built-in factions", err.c_str());
        }
    }
    // Settings come first: the saved text size decides the fonts
    settings = loadSettings(SETTINGS_PATH);
    fonts = openUiFonts(settings.fontScale, settings.fontPath, settings.language);
    appliedFontScale = settings.fontScale;
    appliedFontPath = settings.fontPath;
    appliedLanguage = settings.language;
    {
        // A saved language whose file can't be found goes back to the default one, and the setting is saved
        std::string langErr;
        if (!loadLanguage(settings.language, langErr)) {
            SDL_Log("Language: %s. Using the default language.", langErr.c_str());
            settings.language = LANGUAGE_DEFAULT_ID;
            loadLanguage(settings.language, langErr);
            saveSettings(settings, SETTINGS_PATH);
        }
    }
    // The large title font, for the selected language, with the same fallbacks. It keeps the language it started with.
    font = openFontChain(64.0f, true, settings.fontPath, settings.language, titleFontOwned);
    if (!font || !fonts.valid()) return fatalError(nullptr, std::string("Could not open a font from data.pak: ") + SDL_GetError());
    SDL_Log("Settings, language and fonts loaded (language %s, UI scale %d%%)", settings.language.c_str(), settings.fontScale);

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
        int gotDepth = 0;
        SDL_GL_GetAttribute(SDL_GL_DEPTH_SIZE, &gotDepth);
        SDL_Log("Depth buffer: %d bits", gotDepth);
    }

    glewExperimental = GL_TRUE;
    GLenum glewErr = glewInit();
    if (glewErr != GLEW_OK)
        return fatalError(window, std::string("GLEW init failed: ") + reinterpret_cast<const char*>(glewGetErrorString(glewErr)));
    glGetError();
    SDL_Log("OpenGL %s on %s", reinterpret_cast<const char*>(glGetString(GL_VERSION)),
            reinterpret_cast<const char*>(glGetString(GL_RENDERER)));

    moveOldMapsFolder();
    displayInfo = queryDisplays(settings.monitor);
    applyDisplay(window, settings, displayInfo);
    SDL_GL_SetSwapInterval(settings.vsync ? 1 : 0);

    // ---- Loading phase: textures and models load before the title screen appears ----
    // The first load has no backdrop behind the bar, and names what is loading under it
    if (!renderer.init(font, fonts.hud, fonts.ui,
                       [&](float p, const std::string& what) { loadingFrame(window, renderer, p, std::string(), what, false); }))
        return fatalError(window, "Loading the game's textures, models or shaders failed. log.txt beside the game has the details.");
    SDL_Log("Renderer loaded");
    renderer.setListFont(fonts.list);
    // The game's name on the title screen: a very large font, drawn shrunk so it stays sharp
    renderer.setTitleFont(openFontChain(TITLE_NAME_POINTS, true, settings.fontPath, settings.language, nameFontOwned));

    // Where the Squad Panel, the Unit Panel and the Information Panel are, as last locked (kept in the settings). 0 is
    // the default size and place.
    auto placeFromSettings = [&](PanelId which) {
        const std::array<float, 4>& v = settings.panels[(size_t)idx(which)];
        PanelPlace p = defaultPanelPlace(which);   // its bar's side, until the first frame settles it from its place
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

    lastTicks = SDL_GetTicks();
    fpsSince = lastTicks;

    // Without an audio output the game runs silent, so this result is not checked
    audio::init();
    applyAudio();

    SDL_Log("Start-up done, showing the title screen");
    return 0;
}

void App::beginFrame() {
    // Taken at the top of the frame, for the frame rate limit at its end
    frameStart = std::chrono::steady_clock::now();

    // Music and ambient sound follow the screen. A screen that names a track starts it, nullptr keeps what is
    // playing, and "" is silence. Each request is tried once, so a missing track is not reloaded every frame.
    const Soundtrack sound = soundtrackFor(screen);
    if (sound.music) audio::playTrack(audio::Bus::Music, sound.music);
    if (sound.ambient) audio::playTrack(audio::Bus::Ambient, sound.ambient);

    SDL_GetWindowSizeInPixels(window, &winW, &winH);
    viewH = winH;

    // The menu fonts, opened again when the window's height or the text settings call for another size
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
    while (running) {
        beginFrame();

        // The menus measure and draw their text in the larger menu fonts, the game in its own: set before the input,
        // so a click is matched against the same layout that was drawn
        renderer.setMenuText(screen != Screen::Playing);

        while (SDL_PollEvent(&e)) handleEvent();

        update();
        updateHover();
        render();

        if (renderer.hoverFrameEnd()) playSfx(Sfx::UiHover);   // the pointer came onto another button
        SDL_GL_SwapWindow(window);
        // Holds the frame to the limit, counted from the frame's start
        limitFrameRate(frameStart, settings.fpsLimit);
    }
}

void App::shutdown() {
    audio::shutdown();
    renderer.shutdown();
    renderer.setTitleFont(nullptr);
    closeFonts(nameFontOwned);
    closeUiFonts(fonts);
    closeUiFonts(menuFonts);
    closeFonts(titleFontOwned);
    TTF_Quit();
    SDL_GL_DestroyContext(ctx);
    SDL_DestroyWindow(window);
    SDL_Log("Closed normally");
    SDL_Quit();
}
