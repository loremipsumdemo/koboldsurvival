#include "level.h"
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>
#include "language.h"
#include "world_format.h"
#include "world_gen.h"

namespace {
namespace fs = std::filesystem;

std::string nicknamesPath() {
    return std::string(MAP_FOLDER) + "/nicknames.txt";
}

// Writes the nicknames down, a line a map. False if they could not be written.
bool writeNicknames(const std::map<std::string, std::string>& list) {
    std::error_code ec;
    fs::create_directories(fs::u8path(MAP_FOLDER), ec);
    std::ofstream out(fs::u8path(nicknamesPath()), std::ios::binary);
    for (const auto& [name, nick] : list) out << name << '\t' << nick << '\n';
    out.close();
    return (bool)out;
}

// The nicknames by map name, read from the file the first time one is asked for. A nickname whose map is no longer there (it was deleted
// outside the game) is taken out, and its line with it.
std::map<std::string, std::string>& nicknames() {
    static std::map<std::string, std::string> list;
    static bool read = false;
    if (read) return list;
    read = true;
    std::ifstream in(fs::u8path(nicknamesPath()), std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t tab = line.find('\t');
        if (tab == std::string::npos || tab == 0 || tab + 1 >= line.size()) continue;
        list[line.substr(0, tab)] = line.substr(tab + 1);
    }
    bool gone = false;
    for (auto it = list.begin(); it != list.end();) {
        std::error_code ec;
        const bool there = fs::exists(fs::u8path(mapFilePath(it->first)), ec);
        if (!ec && !there) {   // only a map that is known not to be there; one that could not be looked for keeps its nickname
            it = list.erase(it);
            gone = true;
        } else {
            ++it;
        }
    }
    if (gone) writeNicknames(list);
    return list;
}

// A tab or a line break would split its line, and spaces at the ends are only noise
std::string cleanedNickname(std::string s) {
    for (char& c : s) {
        if ((unsigned char)c < 0x20) c = ' ';
    }
    const size_t first = s.find_first_not_of(' ');
    if (first == std::string::npos) return "";
    return s.substr(first, s.find_last_not_of(' ') - first + 1);
}
}

std::string mapNickname(const std::string& stem) {
    const std::map<std::string, std::string>& list = nicknames();
    const auto it = list.find(stem);
    return it == list.end() ? std::string() : it->second;
}

bool setMapNickname(const std::string& stem, const std::string& nickname) {
    std::map<std::string, std::string> next = nicknames();
    const std::string clean = cleanedNickname(nickname);
    if (clean.empty()) next.erase(stem);
    else next[stem] = clean;
    if (!writeNicknames(next)) return false;
    nicknames() = std::move(next);
    return true;
}

std::string mapDisplayName(const std::string& stem) {
    const std::string nick = mapNickname(stem);
    return nick.empty() ? stem : langf("UILABEL_MAP_NICKNAMED", { nick, stem });
}

std::vector<MapChoice> listMaps() {
    std::vector<MapChoice> out;
    for (const std::string& name : listMapFiles()) out.push_back(MapChoice{ MAP_SOURCE_FILE, name, mapDisplayName(name), mapFilePath(name) });
    return out;
}
