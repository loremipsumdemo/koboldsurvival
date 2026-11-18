#include "scenario.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "game_tables.h"
#include "party.h"

namespace fs = std::filesystem;

namespace {
struct RowInfo {
    const char* key;
    const char* label;
    const char* tip;
};

const RowInfo& rowInfo(StartRow row) {
    static const RowInfo table[] = {
        { "military", "UILABEL_SCENARIO_MILITARY", "UILABEL_SCENARIO_MILITARY_TIP" },
        { "fortified", "UILABEL_SCENARIO_FORTIFIED", "UILABEL_SCENARIO_FORTIFIED_TIP" },
        { "citizens", "UILABEL_SCENARIO_CITIZENS", "UILABEL_SCENARIO_CITIZENS_TIP" },
        { "caged", "UILABEL_SCENARIO_CAGED", "UILABEL_SCENARIO_CAGED_TIP" },
        { "camp", "UILABEL_SCENARIO_CAMP", "UILABEL_SCENARIO_CAMP_TIP" },
        { "extras", "UILABEL_SCENARIO_EXTRAS", "UILABEL_SCENARIO_EXTRAS_TIP" },
        { "stock", "UILABEL_SCENARIO_STOCK", "UILABEL_SCENARIO_STOCK_TIP" },
        { "standing", "UILABEL_SCENARIO_STANDING", "UILABEL_SCENARIO_STANDING_TIP" },
    };
    return table[std::clamp((int)row, 0, (int)StartRow::Count - 1)];
}

template <typename K>
void setCount(std::vector<std::pair<K, int>>& list, K kind, int n) {
    for (auto& e : list) {
        if (e.first == kind) {
            e.second = n;
            return;
        }
    }
    list.push_back({ kind, n });
}

template <typename K>
void tidy(std::vector<std::pair<K, int>>& list) {
    list.erase(std::remove_if(list.begin(), list.end(), [](const std::pair<K, int>& e) { return e.second <= 0; }), list.end());
    std::sort(list.begin(), list.end(), [](const std::pair<K, int>& a, const std::pair<K, int>& b) { return (int)a.first < (int)b.first; });
}

bool ruleRowOf(const std::string& word, int& row) {
    for (int i = 0; i < (int)RuleRow::Count; ++i) {
        if (word == ruleRowWord((RuleRow)i)) {
            row = i;
            return true;
        }
    }
    return false;
}

int startRowOf(const std::string& word) {
    for (int i = 0; i < (int)StartRow::Count; ++i) {
        if (word == startRowKey((StartRow)i)) return i;
    }
    return -1;
}

// Whether a rule row is one a faction of this kind has: yours, the rows for outsiders; an NPC faction's, all but the two shares its unit
// counts make
bool ruleRowApplies(int row, bool player) {
    if (row < 0 || row >= (int)RuleRow::Count) return false;
    if (player) return row < PLAYER_RULE_ROWS;
    return row != (int)RuleRow::Fortified && row != (int)RuleRow::Military;
}

std::string clipped(const std::string& s, size_t chars) {
    std::string out;
    appendUtf8Limited(out, s, chars);
    return out;
}
}

const char* startRowKey(StartRow row) { return rowInfo(row).key; }
const char* startRowLabelKey(StartRow row) { return rowInfo(row).label; }
const char* startRowTipKey(StartRow row) { return rowInfo(row).tip; }

// Yours has a unit to command; a faction that runs itself has at least its Altar
int startRowLeast(StartRow row, bool player) {
    if (player) return row == StartRow::Military ? 1 : 0;
    if (row == StartRow::Standing) return REP_MIN;
    return row == StartRow::Camp ? (int)StartCamp::Altar : 0;
}

int startRowMost(StartRow row, bool player) {
    switch (row) {
    case StartRow::Military:
    case StartRow::Citizens: return player ? PARTY_MAX_UNITS : SCENARIO_MOST_UNITS;
    case StartRow::Fortified: return player ? PARTY_MAX_UNITS : SCENARIO_MOST_UNITS;
    case StartRow::Caged: return SCENARIO_MOST_CAGED;
    case StartRow::Camp: return (int)StartCamp::Full;
    case StartRow::Extras: return SCENARIO_MOST_EXTRAS;
    case StartRow::Stock: return SCENARIO_MOST_STOCK;
    case StartRow::Standing: return player ? 0 : REP_MAX;
    case StartRow::Count: break;
    }
    return 0;
}

int startRowStep(StartRow row) {
    switch (row) {
    case StartRow::Stock: return 50;
    case StartRow::Standing: return 5;
    default: return 1;
    }
}

const char* startCampKey(int camp) {
    switch (std::clamp(camp, 0, (int)StartCamp::Count - 1)) {
    case (int)StartCamp::None: return "UILABEL_SCENARIO_CAMP_NONE";
    case (int)StartCamp::Altar: return "UILABEL_SCENARIO_CAMP_ALTAR";
    default: return "UILABEL_SCENARIO_CAMP_FULL";
    }
}

bool Scenario::operator==(const Scenario& o) const {
    if (name != o.name || about != o.about || !(player == o.player) || !(npc == o.npc)) return false;
    for (int i = 0; i < PALETTE_COUNT; ++i) {
        if (!(factions[i] == o.factions[i])) return false;
    }
    return true;
}

const Scenario& outpostScenario() {
    static const Scenario outpost = [] {
        Scenario s;
        s.builtin = true;
        return s;
    }();
    return outpost;
}

const std::vector<PropKind>& scenarioPropKinds() {
    static const std::vector<PropKind> kinds = {
        PropKind::Campfire, PropKind::Bedroll, PropKind::Barrel, PropKind::WoodStockpile, PropKind::StoneStockpile, PropKind::IronStockpile,
        PropKind::OreStockpile, PropKind::Stool, PropKind::Chair, PropKind::Bench, PropKind::Table, PropKind::EndTable, PropKind::RoundTable,
        PropKind::GameTable, PropKind::StandingTorch, PropKind::TrainingDummy, PropKind::WeaponRack, PropKind::ArmorStand,
        PropKind::RawFoodStorage, PropKind::FoodStorage, PropKind::LockedChest, PropKind::Stall, PropKind::WaterTank, PropKind::WaterPump,
        PropKind::Forge, PropKind::Wagon, PropKind::Boneheap, PropKind::Tree, PropKind::Boulder, PropKind::Ore, PropKind::Mushroom,
    };
    return kinds;
}

FactionStart defaultStart(int faction) {
    FactionStart st;
    if (faction == FACTION_PLAYER) {
        // The Outpost: the same as every other faction with an Altar: 3 military to command, 7 Fortified and 20 citizens
        st.military = 3;
        st.fortified = 7;
        st.citizens = 20;
        st.camp = StartCamp::Full;
        st.extras = 0;
        st.caged = 0;
        return st;
    }
    // A faction that runs itself: its table's units, by the shares its rules say are Fortified and military (settleFactionHomes). A colored
    // one is read from the table's own row: the color you play has one too, though it is left out of the factions in play.
    const bool colored = faction >= PALETTE_FIRST_ID && faction < PALETTE_FIRST_ID + PALETTE_COUNT;
    const FactionDef* f = colored ? paletteFaction(faction - PALETTE_FIRST_ID) : findFaction(faction);
    const int total = f ? f->units : 0;
    const FactionRules rules = f ? f->rules : FactionRules{};
    st.fortified = std::min(total, (int)std::lround((double)rules.fortified * 0.01 * (double)total));
    st.military = std::min((int)std::lround((double)rules.military * 0.01 * (double)total), std::max(0, total - st.fortified - 1));
    st.citizens = std::max(0, total - st.fortified - st.military);
    st.caged = CAMP_MOBS;
    st.camp = StartCamp::Full;
    st.extras = -1;
    return st;
}

FactionStart scenarioStart(const Scenario& s, int faction) {
    const bool player = faction == FACTION_PLAYER;
    FactionStart st = defaultStart(faction);
    const auto apply = [&](const ScenarioBlock& b) {
        for (const auto& [row, value] : b.rows) {
            if (row < 0 || row >= (int)StartRow::Count) continue;
            const StartRow r = (StartRow)row;
            const int v = std::clamp(value, startRowLeast(r, player), startRowMost(r, player));
            switch (r) {
            case StartRow::Military: st.military = v; break;
            case StartRow::Fortified: st.fortified = v; break;
            case StartRow::Citizens: st.citizens = v; break;
            case StartRow::Caged: st.caged = v; break;
            case StartRow::Camp: st.camp = (StartCamp)v; break;
            case StartRow::Extras: st.extras = v; break;
            case StartRow::Stock: st.stock = v; break;
            case StartRow::Standing: st.standing = player ? STANDING_BY_LOOP : v; break;
            case StartRow::Count: break;
            }
        }
        for (const auto& [key, n] : b.mobs) {
            MobKind kind = MobKind::None;
            if (mobKindNamed(key, kind) && isRegularMob(kind)) setCount(st.mobs, kind, std::clamp(n, 0, SCENARIO_MOST_MOBS));
        }
        for (const auto& [key, n] : b.props) {
            PropKind kind = PropKind::Wagon;
            if (propKindNamed(key, kind)) setCount(st.props, kind, std::clamp(n, 0, SCENARIO_MOST_MOBS));
        }
        for (const auto& [row, v] : b.rules) {
            if (ruleRowApplies(row, player)) st.rules[row] = v;
        }
    };
    apply(player ? s.player : s.npc);
    const int slot = faction - PALETTE_FIRST_ID;
    if (!player && slot >= 0 && slot < PALETTE_COUNT) apply(s.factions[slot]);
    tidy(st.mobs);
    tidy(st.props);
    if (player) {
        // Your party is held to what its screen holds, with its animals, and has a military unit to command
        st.military = std::clamp(st.military, 1, PARTY_MAX_UNITS);
        st.fortified = std::clamp(st.fortified, 0, PARTY_MAX_UNITS - st.military);
        st.citizens = std::clamp(st.citizens, 0, PARTY_MAX_UNITS - st.military - st.fortified);
        int room = std::max(0, PARTY_MAX_UNITS - st.people());
        for (auto& m : st.mobs) {
            m.second = std::min(m.second, room);
            room -= m.second;
        }
        tidy(st.mobs);
    } else if (st.camp == StartCamp::None) {
        st.camp = StartCamp::Altar;
    }
    return st;
}

void applyScenarioRules(World& w, const Scenario& s) {
    std::map<int, int> standings;   // how the factions stand with you, where the scenario says
    for (const FactionDef& f : factions()) {
        const bool player = f.id == FACTION_PLAYER;
        const bool colored = f.id >= PALETTE_FIRST_ID && f.id < PALETTE_FIRST_ID + PALETTE_COUNT;
        if (!player && !colored) continue;
        const FactionStart st = scenarioStart(s, f.id);
        if (!player && st.standing != STANDING_BY_LOOP) standings[f.id] = st.standing;
        FactionRules r = factionRules(w, f.id);
        for (const auto& [row, value] : st.rules) setRule(r, (RuleRow)row, value);
        // The shares of Fortified and military follow from the counts, when the scenario changed them from the table's
        if (!player && st.people() > 0) {
            const FactionStart def = defaultStart(f.id);
            if (st.military != def.military || st.fortified != def.fortified || st.citizens != def.citizens) {
                r.fortified = (int)std::lround(100.0 * (double)st.fortified / (double)st.people());
                r.military = (int)std::lround(100.0 * (double)st.military / (double)st.people());
            }
        }
        setFactionRules(w, f.id, r);
    }
    if (!standings.empty()) setPlayerReputation(w, standings);
}

// ---- The files

std::string scenarioFilePath(const std::string& name) { return std::string(SCENARIO_FOLDER) + "/" + name + SCENARIO_EXTENSION; }

namespace {
void writeBlock(std::ostringstream& out, const std::string& scope, const ScenarioBlock& b) {
    for (const auto& [row, v] : b.rows) {
        if (row >= 0 && row < (int)StartRow::Count) out << scope << ' ' << startRowKey((StartRow)row) << ' ' << v << '\n';
    }
    for (const auto& [key, n] : b.mobs) out << scope << " mob " << key << ' ' << n << '\n';
    for (const auto& [key, n] : b.props) out << scope << " prop " << key << ' ' << n << '\n';
    for (const auto& [row, v] : b.rules) {
        if (row < 0 || row >= (int)RuleRow::Count) continue;
        // factionRulesText lists every row, a word each, in the order of the rows
        FactionRules r;
        setRule(r, (RuleRow)row, v);
        std::istringstream words(factionRulesText(r));
        std::string word;
        for (int i = 0; words >> word; ++i) {
            if (i == row) out << scope << " rule " << word << '\n';
        }
    }
}
}

std::string scenarioToText(const Scenario& s) {
    std::ostringstream out;
    out << "format 1\n";
    if (!s.about.empty()) out << "about " << s.about << '\n';
    writeBlock(out, "player", s.player);
    writeBlock(out, "npc", s.npc);
    for (int slot = 0; slot < PALETTE_COUNT; ++slot) writeBlock(out, "faction " + std::to_string(paletteFactionId(slot)), s.factions[slot]);
    return out.str();
}

bool scenarioFromText(const std::string& text, Scenario& out, std::string& err) {
    Scenario s;
    std::istringstream in(text);
    std::string line;
    int number = 0, format = 0;
    const auto bad = [&] {
        err = "line " + std::to_string(number) + ": " + line;
        return false;
    };
    while (std::getline(in, line)) {
        ++number;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream ls(line);
        std::string key;
        if (!(ls >> key) || key[0] == '#') continue;
        if (key == "format") {
            if (!(ls >> format)) return bad();
            continue;
        }
        if (key == "about") {
            std::string about;
            std::getline(ls >> std::ws, about);
            s.about = clipped(about, SCENARIO_ABOUT_CHARS);
            continue;
        }
        ScenarioBlock* block = nullptr;
        if (key == "player") {
            block = &s.player;
        } else if (key == "npc") {
            block = &s.npc;
        } else if (key == "faction") {
            int id = 0;
            if (!(ls >> id)) return bad();
            const int slot = id - PALETTE_FIRST_ID;
            if (slot < 0 || slot >= PALETTE_COUNT) continue;   // a faction this game's table does not have: left out
            block = &s.factions[slot];
        } else {
            continue;   // a line this game does not know is skipped, so a newer file opens as far as it can
        }
        std::string what;
        if (!(ls >> what)) return bad();
        if (what == "mob") {
            std::string name;
            int n = 0;
            if (!(ls >> name >> n)) return bad();
            MobKind kind = MobKind::None;
            if (mobKindNamed(name, kind) && isRegularMob(kind)) block->mobs[mobInfo(kind).key] = std::clamp(n, 0, SCENARIO_MOST_MOBS);
        } else if (what == "prop") {
            std::string name;
            int n = 0;
            if (!(ls >> name >> n)) return bad();
            PropKind kind = PropKind::Wagon;
            if (propKindNamed(currentPropKey(name), kind)) block->props[propInfo(kind).key] = std::clamp(n, 0, SCENARIO_MOST_MOBS);
        } else if (what == "rule") {
            std::string word;
            if (!(ls >> word)) return bad();
            FactionRules parsed;
            std::string why;
            if (!parseFactionRules(word, parsed, why)) return bad();
            int row = 0;
            if (ruleRowOf(word.substr(0, word.find('=')), row)) block->rules[row] = ruleCurrent(parsed, (RuleRow)row);
        } else {
            const int row = startRowOf(what);
            int value = 0;
            if (row < 0) continue;
            if (!(ls >> value)) return bad();
            const StartRow r = (StartRow)row;
            block->rows[row] = std::clamp(value, std::min(startRowLeast(r, true), startRowLeast(r, false)),
                                          std::max(startRowMost(r, true), startRowMost(r, false)));
        }
    }
    if (format != 1) {
        err = "not a version 1 scenario";
        return false;
    }
    out = std::move(s);
    return true;
}

bool writeScenarioFile(const std::string& name, const Scenario& s, std::string& err) {
    const std::string path = scenarioFilePath(name);
    std::error_code ec;
    fs::create_directories(fs::u8path(path).parent_path(), ec);
    std::ofstream out(fs::u8path(path), std::ios::binary);
    out << scenarioToText(s);
    out.close();
    if (!out) {
        err = "could not write " + path;
        return false;
    }
    return true;
}

bool readScenarioFile(const std::string& name, Scenario& out, std::string& err) {
    const std::string path = scenarioFilePath(name);
    std::ifstream in(fs::u8path(path), std::ios::binary);
    if (!in) {
        err = "could not read " + path;
        return false;
    }
    std::ostringstream all;
    all << in.rdbuf();
    if (!scenarioFromText(all.str(), out, err)) return false;
    out.name = name;
    out.builtin = false;
    return true;
}

std::vector<std::string> listScenarioFiles() {
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(fs::u8path(SCENARIO_FOLDER), ec)) {
        if (entry.path().extension() == SCENARIO_EXTENSION) out.push_back(entry.path().stem().u8string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool deleteScenarioFile(const std::string& name) {
    std::error_code ec;
    return fs::remove(fs::u8path(scenarioFilePath(name)), ec);
}
