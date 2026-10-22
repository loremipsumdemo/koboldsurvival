#pragma once

#include <deque>
#include <string>
#include <vector>

#include "app/session.hpp"
#include "render/renderer.hpp"
#include "theme/theme.hpp"

namespace wm {


// The F1 console: a scrolling log, one input line and command history.
struct Console {
    bool open = false;
    std::string input;
    std::deque<std::string> lines;
    std::vector<std::string> history;
    int histPos = -1;

    void print(const std::string& s);
};

// Runs one typed line. Sets running to false on quit.
void RunConsoleLine(const std::string& line, Console& con, bool& running, Look& look, Session& s);

// Lists the commands and a few current values.
void PrintConsoleHelp(Console& con);

// Draws the console panel across the top of the window.
void DrawConsole(Renderer& r, const Console& con, int w, int h);

// Sets every walker variable back to its default.
void ResetWalkerVars();

}  // namespace wm
