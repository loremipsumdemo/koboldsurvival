#pragma once
#include <initializer_list>
#include <string>

// The game's display text, looked up by key. Each language is a file in the pak with one line per text:
//   KEY<TAB>text
// A key has no spaces or control characters, e.g. UILABEL_STRENGTH_STAT. The text is anything else, in any language,
// and is shown as it is written: it is never read as code. Lines starting with # are comments, and blank lines are
// skipped. A line that can't be read is skipped.
//
// Lookup: the chosen language first, then the English file, then the key itself. So a text missing from a language
// shows its English, and a key missing everywhere shows its key name.
//
// Ids are grouped by prefix: UILABEL_ for interface text, NPC_ for dialogue and what NPCs say, MISC_ for anything else.

inline constexpr int LANGUAGE_COUNT = 2;   // 0 English, 1 Spanish. Chinese comes later.
const char* languageName(int language);
const char* languageFile(int language);     // the file in the pak, e.g. "data/language_spanish.txt"

// Reads the English file, and the file for the language. False, with err set, if the chosen language's file can't be
// read (English is still in use then). The English file is always read.
bool loadLanguage(int language, std::string& err);

// The text for a key, in the language in use, else in English, else the key itself
std::string lang(const std::string& key);

// lang, with {0}, {1}, ... in the text replaced by the arguments in order
std::string langf(const std::string& key, std::initializer_list<std::string> args);
