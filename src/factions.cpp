#include "patrons.h"
#include "factions.h"
#include "ecs.h"
#include "mobs.h"
#include "party.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include "language.h"

namespace {
const char* const DEFAULT_TEXT =
    "faction\t0\tFACTION_PLAYER\t#c77dff\t0\tcontrol=player\n"
    "faction\t6\tFACTION_FACTIONLESS\t#9a9a9a\t0\ttake=food aid=own\n"
    "faction\t100\tFACTION_EMBER\t#FF4000\t30\tfactionless=hostile trespass=attack downed=cage take=weapons trade=allies markup=25 aid=own build=base_first gates=sleep patron=5\n"
    "faction\t101\tFACTION_SOLAR\t#EFDF00\t30\ttake=weapons trade=not_factionless markup=15 recruit=factionless build=balanced gates=open patron=13\n"
    "faction\t102\tFACTION_AMBER\t#AF8010\t30\ttrespass=warn take=food markup=20 build=balanced gates=sleep patron=6\n"
    "faction\t103\tFACTION_DAWN\t#FFCFAF\t30\tfactionless=friendly downed=carry_out recruit=rescued aid=anyone build=perimeter_first gates=sleep patron=2\n"
    "faction\t104\tFACTION_UMBER\t#704020\t30\ttrespass=attack downed=cage take=weapons trade=allies markup=30 aid=own build=base_first gates=shut patron=9\n"
    "faction\t105\tFACTION_THICKET\t#305000\t30\ttrespass=warn aid=own build=perimeter_first gates=shut patron=1\n"
    "faction\t106\tFACTION_VERDANT\t#00FF40\t30\tfactionless=friendly downed=carry_out recruit=both aid=anyone build=balanced gates=open patron=10\n"
    "faction\t107\tFACTION_JADE\t#509F70\t30\tdowned=carry_out markup=10 build=perimeter_first gates=sleep patron=3\n"
    "faction\t108\tFACTION_TIDAL\t#007080\t30\ttrespass=warn downed=carry_out take=food markup=10 build=perimeter_first gates=shut patron=12\n"
    "faction\t109\tFACTION_GLACIER\t#00EFEF\t30\ttrespass=warn trade=allies markup=15 aid=own build=perimeter_first gates=shut patron=14\n"
    "faction\t110\tFACTION_ZEPHYR\t#40AFFF\t30\trecruit=factionless aid=anyone build=balanced gates=open patron=7\n"
    "faction\t111\tFACTION_COBALT\t#1000EF\t30\ttrespass=attack downed=cage take=weapons markup=20 trade=allies aid=own build=base_first gates=sleep patron=11\n"
    "faction\t112\tFACTION_MYSTIC\t#9F70FF\t30\ttrade=not_factionless markup=25 recruit=rescued build=balanced gates=sleep patron=16\n"
    "faction\t113\tFACTION_VEIL\t#EFCFFF\t30\tfactionless=friendly downed=carry_out aid=anyone build=balanced gates=open patron=4\n"
    "faction\t114\tFACTION_GARNET\t#8F0050\t30\tfactionless=hostile trespass=attack downed=finish_off take=everything trade=nobody aid=own build=base_first gates=sleep patron=8\n"
    "faction\t115\tFACTION_BLOSSOM\t#FF508F\t30\tfactionless=friendly downed=carry_out recruit=both trade=anyone aid=anyone build=balanced gates=open patron=15\n";

// Each rule's words in factions.txt and saves, and the language keys the Faction Rules window shows them by, in the
// order of its enum
struct RuleWords {
    const char* key;
    std::vector<const char*> words;
    std::vector<const char*> labels;
};
const RuleWords& wordsOf(RuleRow row) {
    static const RuleWords table[] = {
        { "factionless", { "friendly", "neutral", "hostile" },
          { "UILABEL_RULE_FRIENDLY", "UILABEL_RULE_NEUTRAL", "UILABEL_RULE_HOSTILE" } },
        { "trespass", { "ignore", "warn", "attack" }, { "UILABEL_RULE_IGNORE", "UILABEL_RULE_WARN", "UILABEL_RULE_ATTACK" } },
        { "downed", { "leave", "cage", "carry_out", "finish_off" },
          { "UILABEL_RULE_LEAVE", "UILABEL_RULE_CAGE", "UILABEL_RULE_CARRY_OUT", "UILABEL_RULE_FINISH_OFF" } },
        { "take", { "nothing", "food", "weapons", "everything", "all" },
          { "UILABEL_RULE_NOTHING", "UILABEL_RULE_FOOD", "UILABEL_RULE_WEAPONS", "UILABEL_RULE_EVERYTHING", "UILABEL_RULE_LOOT_ALL" } },
        { "trade", { "nobody", "allies", "not_factionless", "anyone" },
          { "UILABEL_RULE_NOBODY", "UILABEL_RULE_ALLIES", "UILABEL_RULE_NOT_FACTIONLESS", "UILABEL_RULE_ANYONE" } },
        { "markup", {}, {} },
        { "recruit", { "never", "rescued", "factionless", "both" },
          { "UILABEL_RULE_NEVER", "UILABEL_RULE_RESCUED", "UILABEL_RULE_FACTIONLESS", "UILABEL_RULE_BOTH" } },
        { "aid", { "own", "allies", "anyone" }, { "UILABEL_RULE_OWN", "UILABEL_RULE_ALLIES", "UILABEL_RULE_ANYONE" } },
        { "fortified", {}, {} },
        { "military", {}, {} },
        { "build", { "perimeter_first", "balanced", "base_first" },
          { "UILABEL_RULE_PERIMETER_FIRST", "UILABEL_RULE_BALANCED", "UILABEL_RULE_BASE_FIRST" } },
        { "gates", { "sleep", "shut", "open" }, { "UILABEL_RULE_GATES_SLEEP", "UILABEL_RULE_GATES_SHUT", "UILABEL_RULE_GATES_OPEN" } },
    };
    return table[std::clamp((int)row, 0, (int)RuleRow::Count - 1)];
}

// A row that is a number (a percent) and not one of a few words
bool numberRow(RuleRow row) { return row == RuleRow::Markup || row == RuleRow::Fortified || row == RuleRow::Military; }

int ruleValue(const FactionRules& r, RuleRow row) {
    switch (row) {
    case RuleRow::Factionless: return (int)r.factionless;
    case RuleRow::Trespass: return (int)r.trespass;
    case RuleRow::DownedBody: return (int)r.downedBody;
    case RuleRow::DownedTake: return (int)r.downedTake;
    case RuleRow::Trade: return (int)r.trade;
    case RuleRow::Markup: return r.markup;
    case RuleRow::Recruit: return (int)r.recruit;
    case RuleRow::Aid: return (int)r.aid;
    case RuleRow::Fortified: return r.fortified;
    case RuleRow::Military: return r.military;
    case RuleRow::Build: return (int)r.buildOrder;
    case RuleRow::Gates: return (int)r.gates;
    case RuleRow::Count: break;
    }
    return 0;
}

void setRuleValue(FactionRules& r, RuleRow row, int v) {
    const int most = row == RuleRow::Markup ? TRADE_MARKUP_MAX : numberRow(row) ? 100 : (int)wordsOf(row).words.size() - 1;
    v = std::clamp(v, 0, most);
    switch (row) {
    case RuleRow::Factionless: r.factionless = (StanceRule)v; break;
    case RuleRow::Trespass: r.trespass = (TrespassRule)v; break;
    case RuleRow::DownedBody: r.downedBody = (DownedBodyRule)v; break;
    case RuleRow::DownedTake: r.downedTake = (DownedTakeRule)v; break;
    case RuleRow::Trade: r.trade = (TradeRule)v; break;
    case RuleRow::Markup: r.markup = v; break;
    case RuleRow::Recruit: r.recruit = (RecruitRule)v; break;
    case RuleRow::Aid: r.aid = (AidRule)v; break;
    case RuleRow::Fortified: r.fortified = v; break;
    case RuleRow::Military: r.military = v; break;
    case RuleRow::Build: r.buildOrder = (BuildOrder)v; break;
    case RuleRow::Gates: r.gates = (GateRule)v; break;
    case RuleRow::Count: break;
    }
}
}

std::string factionRulesText(const FactionRules& r) {
    std::string out;
    for (int i = 0; i < (int)RuleRow::Count; ++i) {
        const RuleRow row = (RuleRow)i;
        const RuleWords& rw = wordsOf(row);
        const int v = ruleValue(r, row);
        if (!out.empty()) out += ' ';
        out += rw.key;
        out += '=';
        out += numberRow(row) ? std::to_string(v) : std::string(rw.words[(size_t)std::clamp(v, 0, (int)rw.words.size() - 1)]);
    }
    return out;
}

bool parseFactionRules(const std::string& text, FactionRules& r, std::string& err) {
    std::istringstream in(text);
    std::string word;
    while (in >> word) {
        const size_t eq = word.find('=');
        const std::string key = eq == std::string::npos ? word : word.substr(0, eq);
        const std::string value = eq == std::string::npos ? std::string() : word.substr(eq + 1);
        bool known = false;
        for (int i = 0; i < (int)RuleRow::Count && !known; ++i) {
            const RuleRow row = (RuleRow)i;
            const RuleWords& rw = wordsOf(row);
            if (key != rw.key) continue;
            if (numberRow(row)) {
                char* end = nullptr;
                const long n = std::strtol(value.c_str(), &end, 10);
                if (value.empty() || *end != 0) break;
                setRuleValue(r, row, (int)n);
                known = true;
                break;
            }
            for (size_t k = 0; k < rw.words.size(); ++k) {
                if (value == rw.words[k]) {
                    setRuleValue(r, row, (int)k);
                    known = true;
                }
            }
        }
        if (!known) {
            err = "unknown rule '" + word + "'";
            return false;
        }
    }
    return true;
}

const char* ruleRowKey(RuleRow row) {
    static const char* const KEYS[] = { "UILABEL_RULES_FACTIONLESS", "UILABEL_RULES_TRESPASS", "UILABEL_RULES_DOWNED_BODY",
                                        "UILABEL_RULES_DOWNED_TAKE", "UILABEL_RULES_TRADE", "UILABEL_RULES_MARKUP",
                                        "UILABEL_RULES_RECRUIT", "UILABEL_RULES_AID", "UILABEL_RULES_FORTIFIED", "UILABEL_RULES_MILITARY",
                                        "UILABEL_RULES_BUILD", "UILABEL_RULES_GATES" };
    return KEYS[std::clamp((int)row, 0, (int)RuleRow::Count - 1)];
}

const char* ruleRowWord(RuleRow row) { return wordsOf(row).key; }

const char* ruleRowTipKey(RuleRow row) {
    static const char* const KEYS[] = { "UILABEL_RULES_FACTIONLESS_TIP", "UILABEL_RULES_TRESPASS_TIP", "UILABEL_RULES_DOWNED_BODY_TIP",
                                        "UILABEL_RULES_DOWNED_TAKE_TIP", "UILABEL_RULES_TRADE_TIP", "UILABEL_RULES_MARKUP_TIP",
                                        "UILABEL_RULES_RECRUIT_TIP", "UILABEL_RULES_AID_TIP", "UILABEL_RULES_FORTIFIED_TIP",
                                        "UILABEL_RULES_MILITARY_TIP", "UILABEL_RULES_BUILD_TIP", "UILABEL_RULES_GATES_TIP" };
    return KEYS[std::clamp((int)row, 0, (int)RuleRow::Count - 1)];
}

std::string ruleValueText(const FactionRules& r, RuleRow row) {
    const int v = ruleValue(r, row);
    if (numberRow(row)) return langf("UILABEL_RULE_PERCENT", { std::to_string(v) });
    const RuleWords& rw = wordsOf(row);
    return lang(rw.labels[(size_t)std::clamp(v, 0, (int)rw.labels.size() - 1)]);
}

bool ruleIsNumber(RuleRow row) { return numberRow(row); }

int ruleNumberMax(RuleRow row) { return row == RuleRow::Markup ? TRADE_MARKUP_MAX : 100; }

int ruleCurrent(const FactionRules& r, RuleRow row) { return ruleValue(r, row); }

void setRule(FactionRules& r, RuleRow row, int value) { setRuleValue(r, row, value); }

std::vector<RuleChoice> ruleChoices(RuleRow row) {
    std::vector<RuleChoice> out;
    if (numberRow(row)) {
        const std::vector<int> presets = row == RuleRow::Markup ? std::vector<int>{ 0, 5, 10, 15, 20, 25, 30, 40, 50, 75, 100, 150, 200 }
                                                                : std::vector<int>{ 0, 10, 25, 50, 75, 100 };
        for (int p : presets) out.push_back(RuleChoice{ langf("UILABEL_RULE_PERCENT", { std::to_string(p) }), p });
        return out;
    }
    const RuleWords& rw = wordsOf(row);
    for (size_t k = 0; k < rw.labels.size(); ++k) out.push_back(RuleChoice{ lang(rw.labels[k]), (int)k });
    return out;
}

void stepRule(FactionRules& r, RuleRow row, int delta) {
    const int v = ruleValue(r, row);
    if (numberRow(row)) {
        setRuleValue(r, row, v + delta * 5);
        return;
    }
    const int n = (int)wordsOf(row).words.size();
    setRuleValue(r, row, ((v + delta) % n + n) % n);
}

namespace {
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
            // After the units: control=player or control=npc, and the faction's rules for outsiders
            f.playerControlled = f.id == FACTION_PLAYER;
            std::string word, rules;
            while (ls >> word) {
                if (word == "control=player" || word == "control=npc") f.playerControlled = word == "control=player";
                else if (word.rfind("patron=", 0) == 0) f.patron = std::max(0, std::atoi(word.c_str() + 7));
                else rules += word + " ";
            }
            std::string why;
            if (!parseFactionRules(rules, f.rules, why)) {
                err = "line " + std::to_string(lineNo) + ": " + why;
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
    // No two of the colored factions share a color, so each is told apart on the map and in the lists
    auto colored = [](const FactionDef& f) { return f.id >= PALETTE_FIRST_ID && f.id < PALETTE_FIRST_ID + PALETTE_COUNT; };
    for (size_t i = 0; i < t.list.size(); ++i) {
        if (!colored(t.list[i])) continue;
        for (size_t j = i + 1; j < t.list.size(); ++j) {
            const FactionColor &a = t.list[i].color, &b = t.list[j].color;
            if (colored(t.list[j]) && a.r == b.r && a.g == b.g && a.b == b.b) {
                err = "factions " + std::to_string(t.list[i].id) + " and " + std::to_string(t.list[j].id) + " have the same color";
                return false;
            }
        }
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

int g_playerSlot = PALETTE_DEFAULT_SLOT;
std::string g_playerName;   // what you named your faction; empty takes its color's name
std::vector<FactionDef> g_active;
bool g_activeBuilt = false;

bool isPaletteId(int id) { return id >= PALETTE_FIRST_ID && id < PALETTE_FIRST_ID + PALETTE_COUNT; }

const FactionDef* rawPalette(int slot) {
    if (slot < 0 || slot >= PALETTE_COUNT) return nullptr;
    for (const FactionDef& f : table().list) {
        if (f.id == paletteFactionId(slot)) return &f;
    }
    return nullptr;
}

// The factions in play: the table's, with yours (id 0) taking the slot you picked and that slot's own entry left out
const std::vector<FactionDef>& active() {
    if (g_activeBuilt) return g_active;
    g_active.clear();
    const FactionDef* mine = rawPalette(g_playerSlot);
    for (const FactionDef& f : table().list) {
        if (mine && f.id == mine->id) continue;
        g_active.push_back(f);
        if (mine && f.id == FACTION_PLAYER) {
            g_active.back().nameKey = mine->nameKey;
            g_active.back().color = mine->color;
            g_active.back().patron = mine->patron;   // its patron comes with its color
        }
    }
    g_activeBuilt = true;
    return g_active;
}

// How the Player Faction stands with the others, where it has changed from the start. Part of the world.
struct PlayerStandings {
    std::map<int, int> byFaction;
};

std::map<int, int>& standings(World& w) { return w.resource<PlayerStandings>().byFaction; }

// Rules changed in play, by faction. Part of the world.
struct ChangedRules {
    std::map<int, FactionRules> byFaction;
};

int stanceReputation(StanceRule s) { return s == StanceRule::Hostile ? REP_MIN : s == StanceRule::Friendly ? REP_ALLIED : 0; }
}

bool loadFactions(const std::string& text, std::string& err) {
    Table t;
    if (!parseTable(text, t, err)) return false;
    table() = std::move(t);
    g_activeBuilt = false;
    if (!rawPalette(g_playerSlot)) {
        for (int k = 0; k < PALETTE_COUNT; ++k) {
            if (rawPalette(k)) {
                g_playerSlot = k;
                break;
            }
        }
    }
    return true;
}

int playerSlot() { return g_playerSlot; }

bool setPlayerSlot(int slot) {
    if (!rawPalette(slot)) return false;
    if (slot != g_playerSlot) {
        g_playerSlot = slot;
        g_activeBuilt = false;
    }
    return true;
}

int factionSlot(int faction) {
    if (faction == FACTION_PLAYER) return g_playerSlot;
    return isPaletteId(faction) ? faction - PALETTE_FIRST_ID : -1;
}

const FactionDef* paletteFaction(int slot) { return rawPalette(slot); }

int levelNpcNumber(int code) {
    const int n = code - LEVEL_NPC_CODE;
    return n >= 1 && n <= LEVEL_NPC_FACTIONS ? n : 0;
}

int resolveLevelFaction(int code) {
    const int n = levelNpcNumber(code);
    return n > 0 ? paletteFactionId((g_playerSlot + n) % PALETTE_COUNT) : code;
}

int levelFactionCode(int faction) {
    if (!isPaletteId(faction)) return faction;
    const int n = ((faction - PALETTE_FIRST_ID) - g_playerSlot + PALETTE_COUNT) % PALETTE_COUNT;
    return n > 0 ? LEVEL_NPC_CODE + n : FACTION_PLAYER;
}

int loopReputation(int a, int b) {
    if (a == b) return REP_MAX;
    const int raw = std::abs(a - b);
    const int d = std::min(raw, PALETTE_COUNT - raw);   // steps round the loop: 1 for neighbors
    const int far = PALETTE_COUNT / 2;
    return (int)std::lround(100.0 - (double)(d - 1) * 200.0 / (double)(far - 1));
}

const std::vector<FactionDef>& factions() { return active(); }

const FactionDef* findFaction(int id) {
    for (const FactionDef& f : active()) {
        if (f.id == id) return &f;
    }
    return nullptr;
}

std::string factionName(int id) {
    // A wild animal's faction is its own kind's, but it is shown as one of the Factionless
    if (isMobFaction(id)) return lang("FACTION_FACTIONLESS");
    if (id == FACTION_PLAYER && !g_playerName.empty()) return g_playerName;
    const FactionDef* f = findFaction(id);
    if (!f) return lang("FACTION_PLAYER");
    return lang(f->nameKey);
}

const std::string& playerFactionName() { return g_playerName; }

void setPlayerFactionName(const std::string& name) { g_playerName = cleanName(name); }

FactionColor factionColor(int id) {
    const FactionDef* f = findFaction(id);
    return f ? f->color : FactionColor{};
}

bool factionPlayerControlled(int id) {
    const FactionDef* f = findFaction(id);
    return f ? f->playerControlled : id == FACTION_PLAYER;
}

const FactionRules& factionRules(World& w, int faction) {
    const std::map<int, FactionRules>& changed = w.resource<ChangedRules>().byFaction;
    if (const auto it = changed.find(faction); it != changed.end()) return it->second;
    static const FactionRules none;
    const FactionDef* f = findFaction(faction);
    return f ? f->rules : none;
}

void setFactionRules(World& w, int faction, const FactionRules& r) {
    std::map<int, FactionRules>& changed = w.resource<ChangedRules>().byFaction;
    const FactionDef* f = findFaction(faction);
    if (!f || isMobFaction(faction)) return;
    if (r == f->rules) changed.erase(faction);
    else changed[faction] = r;
}

std::map<int, FactionRules> changedFactionRules(World& w) { return w.resource<ChangedRules>().byFaction; }

void setChangedFactionRules(World& w, const std::map<int, FactionRules>& changed) {
    w.resource<ChangedRules>().byFaction.clear();
    for (const auto& [id, r] : changed) setFactionRules(w, id, r);
}

int defaultReputation(int a, int b) {
    if (a == b) return REP_MAX;
    const auto& r = table().relations;
    const auto it = r.find(keyOf(a, b));
    if (it != r.end()) return it->second;
    const int sa = factionSlot(a), sb = factionSlot(b);
    return sa >= 0 && sb >= 0 ? loopReputation(sa, sb) : 0;
}

int reputation(World& w, int a, int b) {
    if (a == b) return REP_MAX;
    if (isMobFaction(a) || isMobFaction(b)) {
        // Wild animals of kinds that go together (a Gorlicorn and its Rabbits) are allies, as two of one kind are
        if (isMobFaction(a) && isMobFaction(b) && mobKindsFriendly(mobKindOfFaction(a), mobKindOfFaction(b))) return REP_ALLIED;
        const bool aggressive = (isMobFaction(a) && mobTemperament(mobKindOfFaction(a)) == MobTemperament::Aggressive) ||
                                (isMobFaction(b) && mobTemperament(mobKindOfFaction(b)) == MobTemperament::Aggressive);
        return aggressive ? REP_MIN : 0;
    }
    // The Factionless are taken as the other faction's Factionless rule says. Between two Factionless units no faction
    // stands, so they are neutral.
    if (a == FACTION_FACTIONLESS || b == FACTION_FACTIONLESS) {
        const int other = a == FACTION_FACTIONLESS ? b : a;
        return stanceReputation(factionRules(w, other).factionless);
    }
    if (a == FACTION_PLAYER || b == FACTION_PLAYER) {
        const int other = a == FACTION_PLAYER ? b : a;
        const std::map<int, int>& known = standings(w);
        const auto it = known.find(other);
        if (it != known.end()) return it->second;
    }
    return defaultReputation(a, b);
}

bool factionsHostile(World& w, int a, int b) { return a != b && reputation(w, a, b) <= REP_HOSTILE; }
bool factionsAllied(World& w, int a, int b) {
    if (a == b) return a != FACTION_FACTIONLESS;
    return reputation(w, a, b) >= REP_ALLIED;
}

bool factionAids(World& w, int helper, int helped) {
    if (isMobFaction(helper) || isMobFaction(helped)) return helper == helped;
    if (helper == helped && helper != FACTION_FACTIONLESS) return true;
    switch (factionRules(w, helper).aid) {
    case AidRule::Own: return false;
    case AidRule::Allies: return factionsAllied(w, helper, helped);
    case AidRule::Anyone: return !factionsHostile(w, helper, helped);
    case AidRule::Count: break;
    }
    return false;
}

namespace {
// Whether seller's Trade rule lets it trade with buyer
bool tradesWith(World& w, int seller, int buyer) {
    if (isMobFaction(seller) || isMobFaction(buyer)) return false;
    if (seller == buyer) return true;
    switch (factionRules(w, seller).trade) {
    case TradeRule::Nobody: return false;
    case TradeRule::Allies: return factionsAllied(w, seller, buyer);
    case TradeRule::NotFactionless: return buyer != FACTION_FACTIONLESS && !factionsHostile(w, seller, buyer);
    case TradeRule::Anyone: return !factionsHostile(w, seller, buyer);
    case TradeRule::Count: break;
    }
    return false;
}
}

bool factionsTrade(World& w, int a, int b) { return tradesWith(w, a, b) && tradesWith(w, b, a); }

int tradeMarkup(World& w, int faction) { return std::clamp(factionRules(w, faction).markup, 0, TRADE_MARKUP_MAX); }

bool factionCanClaim(int faction) { return faction >= 0 && faction != FACTION_FACTIONLESS && !isMobFaction(faction); }

std::map<int, int> playerReputation(World& w) {
    std::map<int, int> out;
    for (const FactionDef& f : active()) {
        if (f.id != FACTION_PLAYER && f.id != FACTION_FACTIONLESS && !isMobFaction(f.id)) out[f.id] = reputation(w, FACTION_PLAYER, f.id);
    }
    return out;
}

const std::vector<FactionDef>& reputationFactions() {
    static std::vector<FactionDef> out;
    out.clear();
    for (const FactionDef& f : active()) {
        if (!isMobFaction(f.id)) out.push_back(f);
    }
    return out;
}

int reputationFactionCount() {
    int n = 0;
    for (const FactionDef& f : active()) {
        if (!isMobFaction(f.id)) ++n;
    }
    return n;
}

void setPlayerReputation(World& w, const std::map<int, int>& s) {
    standings(w).clear();
    for (const auto& [id, v] : s) {
        if (id != FACTION_PLAYER && id != FACTION_FACTIONLESS && !isMobFaction(id) && findFaction(id)) standings(w)[id] = std::clamp(v, REP_MIN, REP_MAX);
    }
}

int changePlayerReputation(World& w, int faction, int delta) {
    if (faction == FACTION_PLAYER || !findFaction(faction)) return REP_MAX;
    if (isMobFaction(faction) || faction == FACTION_FACTIONLESS) return reputation(w, FACTION_PLAYER, faction);
    const int before = reputation(w, FACTION_PLAYER, faction);
    const int next = std::clamp(before + delta, REP_MIN, REP_MAX);
    standings(w)[faction] = next;
    // Its patron speaks when it turns hostile to you, or allied to you
    if (before > REP_HOSTILE && next <= REP_HOSTILE) patronEvent(w, PatronEvent::Hostile, faction, FACTION_PLAYER);
    else if (before < REP_ALLIED && next >= REP_ALLIED) patronEvent(w, PatronEvent::Allied, faction, FACTION_PLAYER);
    return next;
}

bool factionRunsItself(int faction) { return faction != FACTION_PLAYER && factionCanClaim(faction) && !factionPlayerControlled(faction); }
