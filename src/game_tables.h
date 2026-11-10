#pragma once
#include <string>
#include <vector>
#include "mobs.h"
#include "props.h"

inline constexpr int UNIT_SPRITE_COUNT = 9;

// The tables in data/: itemdb.txt, factions.txt, and those that say which file each thing uses: sfx.txt, bgm.txt,
// emotes.txt, icons.txt, models.txt, mobs.txt, fonts.txt and languages.txt. mobInfo (mobs.h) and sfxInfo (sounds.h)
// read from them too.
// Every key the game needs must be there, once. Fails with the file and line of the first thing wrong.
bool loadGameTables(std::string& err);
// Replaces the props with this text in the format of data/props.txt. On a mistake they are left as they were
bool loadPropTableText(const std::string& text, std::string& err);

// nullptr: leave what is playing. Empty: silence.
struct Soundtrack {
    const char* music = nullptr;
    const char* ambient = nullptr;
};
Soundtrack soundtrackNamed(const std::string& screenKey);

// The kind with this key in data/props.txt or data/mobs.txt. False if there is none.
bool propKindNamed(const std::string& key, PropKind& out);
bool mobKindNamed(const std::string& key, MobKind& out);

const std::string& emoteFile(int emote);
const std::string& iconFile(int icon);
const std::string& propModelFile(PropKind kind);
const std::string& unitModelFile(int model);
// A unit model's name, such as "f1": the key in models.txt, its Model name in the party menu and its profile picture.
std::string unitModelKey(int model);

// The built-in fonts, in the order they are tried, with the language's own first.
std::vector<std::string> fontFilesFor(const std::string& languageId);
