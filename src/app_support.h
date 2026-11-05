#pragma once
// Program-level odds and ends: the log file, fatal errors, and F12 screenshots
#include <string>
#include <SDL3/SDL.h>

// Every SDL_Log line also goes to log.txt beside the game, flushed at once, so a game that closes on its
// own leaves the reason behind. The lines still go to the console or debugger output as before.
void startLogFile();
// Logs why the game has to close and shows it in a message box. Returns the exit code.
int fatalError(SDL_Window* window, const std::string& message);
// An exception nothing caught would end the game without a word: say what it was first
void onTerminate();

// The folder beside the game where F12 screenshots are saved
std::string screenshotsDir();
// Reads back the frame that is about to be shown and saves it as a PNG in the screenshots folder.
// Returns the file's name, or an empty string if it could not be saved.
std::string saveScreenshot(SDL_Window* window);
