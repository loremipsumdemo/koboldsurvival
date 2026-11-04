#include "language.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>
#include "pak.h"

namespace {

using Table = std::unordered_map<std::string, std::string>;

Table& englishTable() {
    static Table t;
    return t;
}

Table& activeTable() {
    static Table t;
    return t;
}

// A key is not empty, not too long, and has no spaces or control characters. Anything else is allowed, so a key
// can be written in any script; the code only ever finds keys by exact match.
bool validKey(const std::string& key) {
    if (key.empty() || key.size() > 100) return false;
    for (unsigned char c : key) {
        if (c <= 0x20 || c == 0x7F) return false;
    }
    return true;
}

// The byte length of the UTF-8 sequence that starts with this byte, or 0 if it can't start one
int utf8Length(unsigned char lead) {
    if (lead < 0x80) return 1;
    if (lead >= 0xC2 && lead <= 0xDF) return 2;
    if (lead >= 0xE0 && lead <= 0xEF) return 3;
    if (lead >= 0xF0 && lead <= 0xF4) return 4;
    return 0;
}

// Makes a text safe to show: control characters (tabs, newlines and the rest) and broken UTF-8 are removed, the
// ends are trimmed, and it is capped at maxBytes. Anything else, in any script, is kept.
std::string cleanText(const std::string& raw, size_t maxBytes) {
    const size_t MAX_BYTES = maxBytes;
    std::string out;
    for (size_t i = 0; i < raw.size(); ) {
        const unsigned char c = (unsigned char)raw[i];
        if (c < 0x20 || c == 0x7F) {          // control characters: a tab becomes a space, the rest go
            if (c == '\t') out += ' ';
            ++i;
            continue;
        }
        if (c < 0x80) {
            if (out.size() + 1 > MAX_BYTES) break;
            out += (char)c;
            ++i;
            continue;
        }
        const int len = utf8Length(c);
        bool valid = len > 0 && i + len <= raw.size();
        for (int k = 1; valid && k < len; ++k) {
            const unsigned char cont = (unsigned char)raw[i + k];
            valid = (cont & 0xC0) == 0x80;
        }
        if (!valid) {                          // a broken sequence: drop this byte and carry on
            ++i;
            continue;
        }
        if (out.size() + len > MAX_BYTES) break;
        out.append(raw, i, len);
        i += len;
    }
    const size_t first = out.find_first_not_of(' ');
    if (first == std::string::npos) return std::string();
    const size_t last = out.find_last_not_of(' ');
    return out.substr(first, last - first + 1);
}

// Fills a table from the text of a language file. Returns false if no line in it was usable.
bool parseText(const std::string& text0, Table& into) {
    into.clear();
    std::string text = text0;
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);   // a byte order mark, from some editors

    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        start = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;

        const size_t tab = line.find('\t');
        if (tab == std::string::npos) continue;
        std::string key = line.substr(0, tab);
        const size_t kFirst = key.find_first_not_of(' ');
        key = kFirst == std::string::npos ? std::string() : key.substr(kFirst, key.find_last_not_of(' ') - kFirst + 1);
        if (!validKey(key)) continue;
        // A text is capped at 1024 bytes (a long tip in a 3-byte script runs past 500); a name list (NAME_*) holds
        // dozens of names, so it has more room
        const bool nameList = key.compare(0, 5, "NAME_") == 0;
        const std::string value = cleanText(line.substr(tab + 1), nameList ? 8192 : 1024);
        if (value.empty()) continue;           // an empty text falls back to English
        into[key] = value;
    }
    return !into.empty();
}

// Reads a language's file: from the LANGUAGE folder if inFolder, else from the pak. False if it is missing or has
// no usable line.
bool readLanguageFile(const std::string& id, bool inFolder, Table& into) {
    if (inFolder) {
        std::ifstream in(std::filesystem::path(LANGUAGE_FOLDER) / (id + ".txt"), std::ios::binary);
        if (!in) {
            into.clear();
            return false;
        }
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        return parseText(text, into);
    }
    const unsigned char* bytes = nullptr;
    size_t size = 0;
    if (!pakFind("data/language/" + id + ".txt", bytes, size)) {
        into.clear();
        return false;
    }
    return parseText(std::string((const char*)bytes, size), into);
}

// A language's texts: the pak's file, with the LANGUAGE folder's file laid over it key by key. So a key the folder
// file leaves out still has its text from the pak. False if neither file has a usable line.
bool readLanguageMerged(const std::string& id, Table& into) {
    Table packed, folder;
    const bool inPak = readLanguageFile(id, false, packed);
    const bool inFolder = readLanguageFile(id, true, folder);
    into.clear();
    if (!inPak && !inFolder) return false;
    into = packed;
    for (const auto& kv : folder) into[kv.first] = kv.second;
    return true;
}

// The English texts: the base every other language falls back to
bool loadEnglish() {
    if (readLanguageMerged(LANGUAGE_DEFAULT_ID, englishTable())) return true;
    englishTable().clear();
    return false;
}

// The name a table gives its language, or the id when it has no LANGUAGE_NAME line
std::string nameIn(const Table& t, const std::string& id) {
    const auto it = t.find("LANGUAGE_NAME");
    return it != t.end() ? it->second : id;
}

// "[fr] - Français": the language code from the id (language_fr gives fr), then the name
std::string labelFor(const std::string& id, const std::string& name) {
    const std::string prefix = "language_";
    const std::string code = id.compare(0, prefix.size(), prefix) == 0 ? id.substr(prefix.size()) : id;
    return "[" + code + "] - " + name;
}

std::string& activeLabel() {
    static std::string label = labelFor(LANGUAGE_DEFAULT_ID, "English");
    return label;
}

std::vector<LanguageChoice>& choiceList() {
    static std::vector<LanguageChoice> list;
    return list;
}

bool scanned = false;

// Ids from before the web codes. Their files can still sit in an old LANGUAGE folder, so they are never listed.
// Settings maps these names to the new ids.
bool isLegacyId(const std::string& id) {
    return id == "language_english" || id == "language_spanish" || id == "language_french";
}

void scanLanguages() {
    std::vector<LanguageChoice> found;
    std::set<std::string> seen;
    const std::string prefix = "language_";

    // Every candidate id: files in the LANGUAGE folder, then files in the pak
    std::vector<std::string> ids;
    std::error_code ec;
    std::filesystem::directory_iterator it(LANGUAGE_FOLDER, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        const std::string file = it->path().filename().string();
        if (file.size() > 4 && file.compare(file.size() - 4, 4, ".txt") == 0) ids.push_back(file.substr(0, file.size() - 4));
    }
    const std::string pakFolder = "data/language/";
    for (const std::string& name : pakNames(pakFolder + prefix, ".txt")) {
        ids.push_back(name.substr(pakFolder.size(), name.size() - pakFolder.size() - 4));   // the folder and ".txt" removed
    }

    Table t;
    for (const std::string& id : ids) {
        if (!validLanguageId(id) || id.compare(0, prefix.size(), prefix) != 0 || isLegacyId(id) || seen.count(id)) continue;
        if (!readLanguageMerged(id, t)) continue;
        seen.insert(id);
        const std::string name = nameIn(t, id);
        found.push_back(LanguageChoice{ id, name, labelFor(id, name) });
    }

    // Sorted by id, with the default language first
    std::sort(found.begin(), found.end(), [](const LanguageChoice& a, const LanguageChoice& b) {
        return a.id < b.id;
    });
    const auto def = std::find_if(found.begin(), found.end(), [](const LanguageChoice& c) {
        return c.id == LANGUAGE_DEFAULT_ID;
    });
    if (def != found.end() && def != found.begin()) std::rotate(found.begin(), def, def + 1);
    if (found.empty()) found.push_back(LanguageChoice{ LANGUAGE_DEFAULT_ID, "English", labelFor(LANGUAGE_DEFAULT_ID, "English") });
    choiceList() = found;
    scanned = true;
}

} // namespace

bool validLanguageId(const std::string& id) {
    if (id.empty() || id.size() > 64) return false;
    for (char c : id) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) return false;
    }
    return true;
}

const std::vector<LanguageChoice>& languageChoices() {
    if (!scanned) scanLanguages();
    return choiceList();
}

void rescanLanguages() {
    scanLanguages();
}

std::string languageDisplayName() {
    return activeLabel();
}

bool loadLanguage(const std::string& id, std::string& err) {
    err.clear();
    if (!loadEnglish()) std::fprintf(stderr, "language: no usable %s.txt in the LANGUAGE folder or data.pak\n", LANGUAGE_DEFAULT_ID);

    bool ok = true;
    if (id == LANGUAGE_DEFAULT_ID) {
        activeTable() = englishTable();
    } else if (validLanguageId(id) && readLanguageMerged(id, activeTable())) {
        // loaded from the LANGUAGE folder, or else from the pak
    } else {
        activeTable().clear();   // English is used through the fallback
        err = "language file " + id + ".txt was not found";
        ok = false;
    }
    // The label names the language that is actually in use
    const std::string usedId = ok ? id : std::string(LANGUAGE_DEFAULT_ID);
    const Table& t = ok && id != LANGUAGE_DEFAULT_ID ? activeTable() : englishTable();
    activeLabel() = labelFor(usedId, nameIn(t, usedId));
    return ok;
}

std::string lang(const std::string& key) {
    const Table& active = activeTable();
    const auto a = active.find(key);
    if (a != active.end()) return a->second;
    const auto e = englishTable().find(key);
    if (e != englishTable().end()) return e->second;
    return key;
}

// The strings in a list written like ["James","Michael"]. Anything that is not such a list gives no names.
static std::vector<std::string> parseNameArray(const std::string& text) {
    std::vector<std::string> out;
    size_t i = text.find('[');
    if (i == std::string::npos) return out;
    ++i;
    while (i < text.size() && text[i] != ']') {
        if (text[i] != '"') {
            ++i;   // commas and spaces between the strings
            continue;
        }
        std::string s;
        ++i;
        while (i < text.size() && text[i] != '"') {
            if (text[i] == '\\' && i + 1 < text.size()) ++i;   // \" and \\ stand for the character after the backslash
            s += text[i++];
        }
        if (i >= text.size()) return {};   // a string that is never closed: the list is not usable
        ++i;
        if (!s.empty()) out.push_back(s);
    }
    return out;
}

std::vector<std::string> languageNames(NameSet set) {
    const char* key = set == NameSet::Feminine ? "NAME_FEMININE"
                    : set == NameSet::Androgynous ? "NAME_ANDROGYNOUS" : "NAME_MASCULINE";
    std::vector<std::string> out;
    const auto a = activeTable().find(key);
    if (a != activeTable().end()) out = parseNameArray(a->second);
    if (out.empty()) {
        const auto e = englishTable().find(key);
        if (e != englishTable().end()) out = parseNameArray(e->second);
    }
    return out;
}

std::string langf(const std::string& key, std::initializer_list<std::string> args) {
    std::string out = lang(key);
    int index = 0;
    for (const std::string& a : args) {
        const std::string marker = "{" + std::to_string(index++) + "}";
        for (size_t at = out.find(marker); at != std::string::npos; at = out.find(marker, at + a.size())) {
            out.replace(at, marker.size(), a);
        }
    }
    return out;
}
