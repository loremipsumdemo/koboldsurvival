#pragma once
#include <string>
#include <vector>
#include "props.h"

// The tables in data/: itemdb.txt, factions.txt, and those that say which file each thing uses: sfx.txt, bgm.txt,
// emotes.txt, icons.txt, models.txt, mobs.txt, fonts.txt and languages.txt. mobInfo (mobs.h) and sfxInfo (sounds.h)
// read from them too.
// Every key the game needs must be there, once. Fails with the file and line of the first thing wrong.
bool loadGameTables(std::string& err);

// nullptr: leave what is playing. Empty: silence.
struct Soundtrack {
    const char* music = nullptr;
    const char* ambient = nullptr;
};
Soundtrack soundtrackNamed(const std::string& screenKey);

const std::string& emoteFile(int emote);
const std::string& iconFile(int icon);
const std::string& propModelFile(PropKind kind);
const std::string& unitModelFile(int model);

// The built-in fonts, in the order they are tried, with the language's own first.
std::vector<std::string> fontFilesFor(const std::string& languageId);
