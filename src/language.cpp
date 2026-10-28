#include "language.h"
#include <cstdio>
#include <unordered_map>
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
// ends are trimmed, and it is capped in length. Anything else, in any script, is kept.
std::string cleanText(const std::string& raw) {
    constexpr size_t MAX_BYTES = 500;
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

// Reads one language file from the pak into a table. False if the file is missing.
bool readFile(const char* file, Table& into, std::string& err) {
    into.clear();
    const unsigned char* bytes = nullptr;
    size_t size = 0;
    if (!pakFind(file, bytes, size)) {
        err = std::string(file) + " is not in the pak";
        return false;
    }
    std::string text((const char*)bytes, size);
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
        const std::string value = cleanText(line.substr(tab + 1));
        if (value.empty()) continue;           // an empty text falls back to English
        into[key] = value;
    }
    return true;
}

} // namespace

const char* languageName(int language) {
    static const char* const names[] = { "English", "Spanish" };
    return names[language >= 0 && language < LANGUAGE_COUNT ? language : 0];
}

const char* languageFile(int language) {
    static const char* const files[] = {
        "data/language_english.txt", "data/language_spanish.txt"
    };
    return files[language >= 0 && language < LANGUAGE_COUNT ? language : 0];
}

bool loadLanguage(int language, std::string& err) {
    err.clear();
    std::string englishErr;
    if (!readFile(languageFile(0), englishTable(), englishErr)) {
        std::fprintf(stderr, "language: %s\n", englishErr.c_str());
    }
    if (language == 0) {
        activeTable() = englishTable();
        return true;
    }
    if (!readFile(languageFile(language), activeTable(), err)) {
        activeTable().clear();   // English is used through the fallback
        return false;
    }
    return true;
}

std::string lang(const std::string& key) {
    const Table& active = activeTable();
    const auto a = active.find(key);
    if (a != active.end()) return a->second;
    const auto e = englishTable().find(key);
    if (e != englishTable().end()) return e->second;
    return key;
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
