#pragma once
#include <initializer_list>
#include <string>
#include <vector>

// The game's display text, looked up by key. Each language is a text file with one line per text:
//   KEY<TAB>text
// A key has no spaces or control characters, e.g. UILABEL_STRENGTH_STAT. The text is anything else, in any language,
// and is shown as it is written: it is never read as code. Lines starting with # are comments, and blank lines are
// skipped. A line that can't be read is skipped.
//
// A language's id is its file name without ".txt", named by its language code, e.g. "language_fr" or "language_pt-BR". Its file is looked for first in the
// LANGUAGE folder next to the game, then in data.pak as data/<id>.txt. The first line of text is
//   LANGUAGE_NAME<TAB>Español
// which is the name shown in the menus.
//
// Lookup: the chosen language first, then the English file, then the key itself. So a text missing from a language
// shows its English, and a key missing everywhere shows its key name.
//
// Ids are grouped by prefix: UILABEL_ for interface text, NPC_ for dialogue and what NPCs say, MISC_ for messages and
// anything else, CONTROL_ for the Controls tab, ITEM_<id>_NAME and ITEM_<id>_DESC for each item by its id.

inline constexpr const char* LANGUAGE_DEFAULT_ID = "language_en";
inline constexpr const char* LANGUAGE_FOLDER = "LANGUAGE";

// True for an id that can be used as a file name: letters, digits, '_' and '-'
bool validLanguageId(const std::string& id);

struct LanguageChoice {
    std::string id;     // e.g. "language_fr"
    std::string name;   // from the file's LANGUAGE_NAME line, or the id if it has none
    std::string label;  // what the menus show: "[fr] - Français"
};

// Every language that can be read, the default first. The list is scanned when first asked for and again after
// rescanLanguages(), so it is cheap to ask for every frame.
const std::vector<LanguageChoice>& languageChoices();
void rescanLanguages();

// The label of the language in use, e.g. "[en] - English"
std::string languageDisplayName();

// Reads the language with this id, and the English file (the LANGUAGE folder's copy if it is usable, else the pak's).
// False, with err set, if the chosen language can't be read; English is then in use. The caller decides what to do
// about it (the game falls back to the default language and saves that).
bool loadLanguage(const std::string& id, std::string& err);

// The three sets of unit names. A language gives each set as one key holding a list of names, e.g.
//   NAME_MASCULINE<TAB>["James","Michael","David"]
// with NAME_FEMININE and NAME_ANDROGYNOUS for the others. A list can have any number of names.
enum class NameSet { Masculine, Feminine, Androgynous };

// The names of one set in the language in use, in key order, else the English ones if it has none. Empty if neither
// has any.
std::vector<std::string> languageNames(NameSet set);

// The text for a key, in the language in use, else in English, else the key itself
std::string lang(const std::string& key);

// lang, with {0}, {1}, ... in the text replaced by the arguments in order
std::string langf(const std::string& key, std::initializer_list<std::string> args);
