// The world tool (src/worldmaker), hosted by the game's map editor screen. The tool keeps
// its own renderer, menu, console and walker; this header is the only part the game sees.
#pragma once

#include <SDL3/SDL.h>

#include <memory>
#include <string>
#include <vector>

namespace wmhost {

class Tool {
public:
    Tool();
    ~Tool();
    Tool(const Tool&) = delete;
    Tool& operator=(const Tool&) = delete;

    // Sets up the tool on the window's current GL context. font is the whole TrueType file.
    // dataDir holds the tool's saved worlds and themes. mapDir receives maps exported for the game.
    bool Init(SDL_Window* win, const std::vector<unsigned char>& font, const std::string& dataDir,
              const std::string& mapDir, std::string& err);

    // Starts a fresh session each time the editor opens: no world, the walker at the origin.
    void Begin();
    // Releases the mouse and the keyboard text input when the editor closes.
    void End();
    // Frees the GPU objects. Call while the GL context still exists.
    void Shutdown();

    // Passes one SDL event to the tool. Only call while the editor is showing.
    void Event(const SDL_Event& e);
    // Runs one frame of the tool: updates, then draws the world, menu and console.
    void Frame();

    // True after Back to title was chosen, or the console's quit. Begin clears it.
    bool wantsBack() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace wmhost
