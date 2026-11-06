#include "factions.h"
#include "mobs.h"
#include <algorithm>
#include <cstdio>
#include <sstream>
#include "language.h"

namespace {
const char* const DEFAULT_TEXT =
    "# Factions. Tab-separated lines; blank lines and lines starting with # are skipped.\n"
    "#\n"
    "# faction <id> <name key> <color> <units>\n"
    "#   id: a whole number that never changes or gets reused, since saves keep it. 0 is the Player Faction (your\n"
    "#       units) and must be listed.\n"
    "#   name key: the language key of its name (FACTION_... in data/language). A key the language files lack is\n"
    "#       shown as written.\n"
    "#   color: #rrggbb, for its dot in the Reputation tab\n"
    "#   units: how many of its units a new level places, together in one camp. 0 for the Player Faction.\n"
    "#\n"
    "# relation <id> <id> <reputation>\n"
    "#   The starting reputation between two factions, the same both ways, from -100 to 100. Pairs not listed start\n"
    "#   at 0 (neutral); a faction is always at 100 with itself. At -25 or below their units attack each other on\n"
    "#   sight; at 50 or above they are allies. Reputations between two other factions never change; the Player\n"
    "#   Faction's change in play.\n"
    "\n"
    "faction\t0\tFACTION_PLAYER\t#c77dff\t0\n"
    "faction\t2\tFACTION_RED\t#ff4d4d\t30\n"
    "faction\t3\tFACTION_GREEN\t#5ccf5c\t10\n"
    "faction\t4\tFACTION_BLUE\t#4d7dff\t10\n"
    "faction\t5\tFACTION_YELLOW\t#f5d43a\t10\n"
    "faction\t6\tFACTION_EXILE\t#9a9a9a\t0\n"
    "\n"
    "relation\t0\t2\t-100\n"
    "relation\t2\t3\t-100\n"
    "relation\t2\t4\t-100\n"
    "relation\t2\t5\t-100\n"
    "relation\t4\t3\t75\n"
    "relation\t4\t5\t-40\n"
    "relation\t3\t5\t0\n";

struct Table {
    std::vector<FactionDef> list;
    std::map<std::pair<int, int>, int> relations;
};

std::pair<int, int> keyOf(int a, int b) { return a < b ? std::make_pair(a, b) : std::make_pair(b, a); }

bool parseColor(const std::string& s, FactionColor& c) {
    unsigned v = 0;
    if (s.size() != 7 || s[0] != '#' || std::sscanf(s.c_str() + 1, "%6x", &v) != 1) return false;
    c.r = ((v >> 16) & 0xff) / 255.0f;
    c.g = ((v >> 8) & 0xff) / 255.0f;
    c.b = (v & 0xff) / 255.0f;
    return true;
}

bool parseTable(const std::string& text, Table& out, std::string& err) {
    Table t;
    std::istringstream in(text);
    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos || line[start] == '#') continue;
        std::istringstream ls(line);
        std::string kind;
        ls >> kind;
        if (kind == "faction") {
            FactionDef f;
            std::string color;
            ls >> f.id >> f.nameKey >> color >> f.units;
            if (ls.fail() || f.id < 0 || f.nameKey.empty() || !parseColor(color, f.color)) {
                err = "line " + std::to_string(lineNo) + ": bad faction line";
                return false;
            }
            for (const FactionDef& o : t.list) {
                if (o.id == f.id) {
                    err = "line " + std::to_string(lineNo) + ": faction " + std::to_string(f.id) + " is listed twice";
                    return false;
                }
            }
            f.units = std::clamp(f.units, 0, 500);
            if (f.id == FACTION_PLAYER) f.units = 0;
            t.list.push_back(f);
        } else if (kind == "relation") {
            int a = 0, b = 0, v = 0;
            ls >> a >> b >> v;
            if (ls.fail() || a == b) {
                err = "line " + std::to_string(lineNo) + ": bad relation line";
                return false;
            }
            t.relations[keyOf(a, b)] = std::clamp(v, REP_MIN, REP_MAX);
        } else {
            err = "line " + std::to_string(lineNo) + ": unknown line '" + kind + "'";
            return false;
        }
    }
    const bool hasPlayer = std::any_of(t.list.begin(), t.list.end(), [](const FactionDef& f) { return f.id == FACTION_PLAYER; });
    if (!hasPlayer || t.list.size() < 2) {
        err = "the list needs the Player Faction (id 0) and at least one other";
        return false;
    }
    std::sort(t.list.begin(), t.list.end(), [](const FactionDef& a, const FactionDef& b) { return a.id < b.id; });
    out = std::move(t);
    return true;
}

Table& table() {
    static Table t = [] {
        Table d;
        std::string err;
        parseTable(DEFAULT_TEXT, d, err);
        return d;
    }();
    return t;
}

std::map<int, int>& standings() {
    static std::map<int, int> s;
    return s;
}
}

bool loadFactions(const std::string& text, std::string& err) {
    Table t;
    if (!parseTable(text, t, err)) return false;
    table() = std::move(t);
    resetPlayerReputation();
    return true;
}

const std::vector<FactionDef>& factions() { return table().list; }

const FactionDef* findFaction(int id) {
    for (const FactionDef& f : table().list) {
        if (f.id == id) return &f;
    }
    return nullptr;
}

std::string factionName(int id) {
    const FactionDef* f = findFaction(id);
    if (!f) return lang("FACTION_PLAYER");
    return lang(f->nameKey);
}

FactionColor factionColor(int id) {
    const FactionDef* f = findFaction(id);
    return f ? f->color : FactionColor{};
}

int defaultReputation(int a, int b) {
    if (a == b) return REP_MAX;
    const auto& r = table().relations;
    const auto it = r.find(keyOf(a, b));
    return it == r.end() ? 0 : it->second;
}

int reputation(int a, int b) {
    if (a == b) return REP_MAX;
    if (a == FACTION_EXILE || b == FACTION_EXILE) return 0;
    if (isMobFaction(a) || isMobFaction(b)) {
        if (isMobFaction(a) && isMobFaction(b) && mobKindsFriendly(mobKindOfFaction(a), mobKindOfFaction(b))) return false;
        const bool aggressive = (isMobFaction(a) && mobTemperament(mobKindOfFaction(a)) == MobTemperament::Aggressive) ||
                                (isMobFaction(b) && mobTemperament(mobKindOfFaction(b)) == MobTemperament::Aggressive);
        return aggressive ? REP_MIN : 0;
    }
    if (a == FACTION_PLAYER || b == FACTION_PLAYER) {
        const int other = a == FACTION_PLAYER ? b : a;
        const auto it = standings().find(other);
        if (it != standings().end()) return it->second;
    }
    return defaultReputation(a, b);
}

bool factionsHostile(int a, int b) { return a != b && reputation(a, b) <= REP_HOSTILE; }
bool factionsAllied(int a, int b) { return a == b || reputation(a, b) >= REP_ALLIED; }

std::map<int, int> playerReputation() {
    std::map<int, int> out;
    for (const FactionDef& f : table().list) {
        if (f.id != FACTION_PLAYER && f.id != FACTION_EXILE && !isMobFaction(f.id)) out[f.id] = reputation(FACTION_PLAYER, f.id);
    }
    return out;
}

const std::vector<FactionDef>& reputationFactions() {
    static std::vector<FactionDef> out;
    out.clear();
    for (const FactionDef& f : table().list) {
        if (!isMobFaction(f.id) && f.id != FACTION_EXILE) out.push_back(f);
    }
    return out;
}

void resetPlayerReputation() { standings().clear(); }

void setPlayerReputation(const std::map<int, int>& s) {
    standings().clear();
    for (const auto& [id, v] : s) {
        if (id != FACTION_PLAYER && id != FACTION_EXILE && !isMobFaction(id) && findFaction(id)) standings()[id] = std::clamp(v, REP_MIN, REP_MAX);
    }
}

int changePlayerReputation(int faction, int delta) {
    if (faction == FACTION_PLAYER || !findFaction(faction)) return REP_MAX;
    if (isMobFaction(faction) || faction == FACTION_EXILE) return reputation(FACTION_PLAYER, faction);
    const int next = std::clamp(reputation(FACTION_PLAYER, faction) + delta, REP_MIN, REP_MAX);
    standings()[faction] = next;
    return next;
}
