#include "factions.h"
#include "ecs.h"
#include "mobs.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include "language.h"

namespace {
const char* const DEFAULT_TEXT =
    "faction\t0\tFACTION_PLAYER\t#c77dff\t0\tcontrol=player\n"
    "faction\t2\tFACTION_RED\t#ff4d4d\t30\tfactionless=hostile trespass=attack downed=cage take=weapons trade=allies markup=25 aid=own\n"
    "faction\t3\tFACTION_GREEN\t#5ccf5c\t10\tfactionless=friendly downed=carry_out recruit=rescued aid=anyone\n"
    "faction\t4\tFACTION_BLUE\t#4d7dff\t10\ttrespass=warn downed=carry_out take=food markup=10\n"
    "faction\t5\tFACTION_YELLOW\t#f5d43a\t10\ttake=weapons trade=not_factionless markup=15 recruit=factionless\n"
    "faction\t6\tFACTION_FACTIONLESS\t#9a9a9a\t0\ttake=food aid=own\n"
    "\n"
    "relation\t0\t2\t-100\n"
    "relation\t2\t3\t-100\n"
    "relation\t2\t4\t-100\n"
    "relation\t2\t5\t-100\n"
    "relation\t4\t3\t75\n"
    "relation\t4\t5\t-40\n"
    "relation\t3\t5\t0\n";

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
        { "take", { "nothing", "food", "weapons", "everything" },
          { "UILABEL_RULE_NOTHING", "UILABEL_RULE_FOOD", "UILABEL_RULE_WEAPONS", "UILABEL_RULE_EVERYTHING" } },
        { "trade", { "nobody", "allies", "not_factionless", "anyone" },
          { "UILABEL_RULE_NOBODY", "UILABEL_RULE_ALLIES", "UILABEL_RULE_NOT_FACTIONLESS", "UILABEL_RULE_ANYONE" } },
        { "markup", {}, {} },
        { "recruit", { "never", "rescued", "factionless", "both" },
          { "UILABEL_RULE_NEVER", "UILABEL_RULE_RESCUED", "UILABEL_RULE_FACTIONLESS", "UILABEL_RULE_BOTH" } },
        { "aid", { "own", "allies", "anyone" }, { "UILABEL_RULE_OWN", "UILABEL_RULE_ALLIES", "UILABEL_RULE_ANYONE" } },
    };
    return table[std::clamp((int)row, 0, (int)RuleRow::Count - 1)];
}

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
    case RuleRow::Count: break;
    }
    return 0;
}

void setRuleValue(FactionRules& r, RuleRow row, int v) {
    const int most = row == RuleRow::Markup ? TRADE_MARKUP_MAX : (int)wordsOf(row).words.size() - 1;
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
        out += row == RuleRow::Markup ? std::to_string(v) : std::string(rw.words[(size_t)std::clamp(v, 0, (int)rw.words.size() - 1)]);
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
            if (row == RuleRow::Markup) {
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
                                        "UILABEL_RULES_RECRUIT", "UILABEL_RULES_AID" };
    return KEYS[std::clamp((int)row, 0, (int)RuleRow::Count - 1)];
}

const char* ruleRowTipKey(RuleRow row) {
    static const char* const KEYS[] = { "UILABEL_RULES_FACTIONLESS_TIP", "UILABEL_RULES_TRESPASS_TIP", "UILABEL_RULES_DOWNED_BODY_TIP",
                                        "UILABEL_RULES_DOWNED_TAKE_TIP", "UILABEL_RULES_TRADE_TIP", "UILABEL_RULES_MARKUP_TIP",
                                        "UILABEL_RULES_RECRUIT_TIP", "UILABEL_RULES_AID_TIP" };
    return KEYS[std::clamp((int)row, 0, (int)RuleRow::Count - 1)];
}

std::string ruleValueText(const FactionRules& r, RuleRow row) {
    const int v = ruleValue(r, row);
    if (row == RuleRow::Markup) return langf("UILABEL_RULE_PERCENT", { std::to_string(v) });
    const RuleWords& rw = wordsOf(row);
    return lang(rw.labels[(size_t)std::clamp(v, 0, (int)rw.labels.size() - 1)]);
}

void stepRule(FactionRules& r, RuleRow row, int delta) {
    const int v = ruleValue(r, row);
    if (row == RuleRow::Markup) {
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
    // A wild animal's faction is its own kind's, but it is shown as one of the Factionless
    if (isMobFaction(id)) return lang("FACTION_FACTIONLESS");
    const FactionDef* f = findFaction(id);
    if (!f) return lang("FACTION_PLAYER");
    return lang(f->nameKey);
}

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
    return it == r.end() ? 0 : it->second;
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
    for (const FactionDef& f : table().list) {
        if (f.id != FACTION_PLAYER && f.id != FACTION_FACTIONLESS && !isMobFaction(f.id)) out[f.id] = reputation(w, FACTION_PLAYER, f.id);
    }
    return out;
}

const std::vector<FactionDef>& reputationFactions() {
    static std::vector<FactionDef> out;
    out.clear();
    for (const FactionDef& f : table().list) {
        if (!isMobFaction(f.id)) out.push_back(f);
    }
    return out;
}

int reputationFactionCount() {
    int n = 0;
    for (const FactionDef& f : table().list) {
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
    const int next = std::clamp(reputation(w, FACTION_PLAYER, faction) + delta, REP_MIN, REP_MAX);
    standings(w)[faction] = next;
    return next;
}
