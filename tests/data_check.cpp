// Checks the game's data: the tables agree with each other, a mistake in them is caught, and the language files use only
// English's keys. Runs beside data.pak. Text not yet translated is listed but is not a failure: it shows in English.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <cstdio>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include "game_data_check.h"
#include "game_tables.h"
#include "items.h"
#include "mobs.h"
#include "pak.h"
#include "props.h"

namespace {
int failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

std::string pakText(const std::string& name) {
    const unsigned char* bytes = nullptr;
    size_t size = 0;
    return pakFind(name, bytes, size) ? std::string((const char*)bytes, size) : std::string();
}

std::set<std::string> languageKeys(const std::string& file) {
    std::set<std::string> keys;
    std::istringstream in(pakText(file));
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("\xEF\xBB\xBF", 0) == 0) line.erase(0, 3);
        const size_t tab = line.find('\t');
        if (!line.empty() && line[0] != '#' && tab != std::string::npos) keys.insert(line.substr(0, tab));
    }
    return keys;
}
}

int main(int, char**) {
    const char* base = SDL_GetBasePath();
    if (!pakOpen(std::string(base ? base : "") + "data.pak")) {
        std::printf("FAIL: data.pak not found\n");
        return 1;
    }
    std::string err;
    const bool loaded = loadGameTables(err);
    check(loaded, "the tables load " + err);
    if (!loaded) return 1;
    const std::vector<std::string> problems = gameDataProblems();
    for (const std::string& p : problems) std::printf("     %s\n", p.c_str());
    check(problems.empty(), "the tables agree with each other");

    // A mistake is caught
    const std::string real = pakText("data/itemdb.txt");
    check(!loadItemDbText("[1]\nnot a key and value", err), "a broken item table is refused");
    check(loadItemDbText("[1]\ntype = item\nname = Thing\ncooks_into = 9999\n", err) && !gameDataProblems().empty(), "a missing item is caught");
    check(loadItemDbText("[1]\ntype = consumable\nname = Thing\non_use = luck +1\n", err) && !gameDataProblems().empty(), "a bad use script is caught");
    check(loadItemDbText(real, err) && gameDataProblems().empty(), "the real table loads again");

    // A new prop needs only a row
    const std::string realProps = pakText("data/props.txt");
    const int propsBefore = propKindCount();
    const std::string extra = realProps + "Birdbath\tPROP_BIRDBATH\tmodel_99.glb\t1\toccupy\tnone\t0\t0\t0\t0\tany\t1\t0\t0\t1\t5\t5\tnone\tItemMove\tnone\tseat\t0\t0\t12\t0\n";
    check(loadPropTableText(extra, err), "a new prop loads " + err);
    const PropKind added = (PropKind)propsBefore;
    check(propKindCount() == propsBefore + 1 && propInfo(added).key == "Birdbath" && isSeat(added) && isBed(added), "a new prop is what its row says");
    check(propInfo(PropKind::Stove).key == "Stove" && propInfo(PropKind::Bedroll).key == "Bedroll", "the named props keep their numbers");
    check(!loadPropTableText(realProps + "Birdbath2\tX\n", err), "a short row is refused");
    check(propKindCount() == propsBefore + 1, "a refused table changes nothing");
    check(loadPropTableText(realProps, err) && propKindCount() == propsBefore, "the real props load again");
    check(buildRowPick(1).kind == PropKind::Bedroll && buildRowPick(3).kind == PropKind::Altar, "the Build tab follows the table");
    int factionsOk = 0;
    for (int i = 1; i <= mobKindCount(); ++i) factionsOk += mobKindOfFaction(mobFaction((MobKind)i)) == (MobKind)i ? 1 : 0;
    check(factionsOk == mobKindCount(), "each mob's faction leads back to it");
    check(mobKindsFriendly(MobKind::Gorlicorn, MobKind::Rabbit) && !mobKindsFriendly(MobKind::Wolf, MobKind::Rabbit), "an escort does not fight the kind it comes with");

    // Language files
    const std::set<std::string> english = languageKeys("data/language/language_en.txt");
    check(!english.empty(), "English has text");
    for (int i = 1; i <= mobKindCount(); ++i) {
        const std::string& key = mobInfo((MobKind)i).nameKey;
        if (!english.count(key)) check(false, "English has no " + key);
    }
    for (int i = 0; i < propKindCount(); ++i) {
        const std::string key = propInfo((PropKind)i).nameKey;
        if (!english.count(key)) check(false, "English has no " + key);
    }
    std::set<std::string> untranslated;
    int languages = 0;
    for (const std::string& file : pakNames("data/language/language_", ".txt")) {
        const std::set<std::string> keys = languageKeys(file);
        ++languages;
        for (const std::string& k : keys) {
            if (!english.count(k)) check(false, file + " has a key English lacks: " + k);
        }
        for (const std::string& k : english) {
            if (!keys.count(k)) untranslated.insert(k);
        }
    }
    check(languages > 1, "language files are in the pak");
    std::printf("note: %d key(s) are not translated in every language:", (int)untranslated.size());
    for (const std::string& k : untranslated) std::printf(" %s", k.c_str());
    std::printf("\n%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
