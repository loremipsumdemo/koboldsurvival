#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>   // the WinMain entry point for a Windows GUI program (no console window)
#include <exception>
#include <filesystem>
#include <memory>
#include "app.h"

int main(int, char**) {
    startLogFile();
    std::set_terminate(onTerminate);
    SDL_Log("Kobold Game starting. Working folder: %s", std::filesystem::current_path().string().c_str());

    // The game is large (the world, the terrain, the renderer), so it lives on the heap
    auto app = std::make_unique<App>();
    if (const int code = app->init()) return code;
    app->run();
    app->shutdown();
    return 0;
}
