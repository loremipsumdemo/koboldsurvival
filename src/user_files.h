#pragma once
#include <string>

// Where the game's files are. The game's own files (data.pak, BGM, SFX, LANGUAGE) sit beside the executable, and are read
// through relative paths once setGameFolder has run, whatever folder the game was started from. What the player makes
// (saves, settings) lives in the player's own files, since the game's folder may not be writable. All paths here are
// UTF-8: open them through std::filesystem::u8path.

// Makes the folder holding the executable the working folder. Runs before anything else opens a file.
void setGameFolder();
// The player's own folder for this game, ending in a separator (on Windows, %APPDATA%\KoboldSurvival\). If there is no
// such folder, this is empty, so paths are relative to the game's folder.
std::string userFilesDir();
// The settings file, in the player's own folder
std::string settingsFilePath();
// Copies saves and settings left beside the game by an earlier version into the player's folder, the first time only.
// The old files stay where they were.
void copyOldUserFiles();
