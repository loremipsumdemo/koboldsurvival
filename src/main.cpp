#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <exception>
#include <memory>
#include "app.h"
#include "user_files.h"

int main(int, char**) {
    setGameFolder();
    std::set_terminate(onTerminate);

    // The game is large (the world, the terrain, the renderer), so it lives on the heap
    auto app = std::make_unique<App>();
    if (const int code = app->init()) return code;
    app->run();
    app->shutdown();
    return 0;
}
