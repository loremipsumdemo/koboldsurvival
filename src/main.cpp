#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>   // the WinMain entry point for a Windows GUI program (no console window)
#include <exception>
#include <filesystem>
#include <memory>
#include "app.h"
#include "user_files.h"

int main(int, char**) {
    setGameFolder();   // everything of the game's is found relative to its own folder, however it was started
    startLogFile();
    std::set_terminate(onTerminate);
    SDL_Log("Kobold Game starting. Game folder: %s. Player's files: %s", std::filesystem::current_path().u8string().c_str(), userFilesDir().c_str());
    copyOldUserFiles();

    // The game is large (the world, the terrain, the renderer), so it lives on the heap
    auto app = std::make_unique<App>();
    if (const int code = app->init()) return code;
    app->run();
    app->shutdown();
    return 0;
}
