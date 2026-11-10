#pragma once
#include <map>
#include <string>
#include <vector>

class World;

constexpr int FACTION_PLAYER = 0;
// Units of no faction: disbanded units, the Wandering Trader, a unit spawned with none, and the wild animals (each kind
// kept in its own mob faction, shown as Factionless). They may not hold an Altar. Factionless Kobolds are not one
// another's allies, only neutral: whether one helps another is its Aid rule. Wild animals of one kind, or of kinds that
// go together (mobKindsFriendly), are allies and come to one another's aid.
constexpr int FACTION_FACTIONLESS = 6;

constexpr int REP_MIN = -100;
constexpr int REP_MAX = 100;
constexpr int REP_HOSTILE = -25;
constexpr int REP_ALLIED = 50;
constexpr int REP_KO_PENALTY = 5;
constexpr int REP_DEATH_PENALTY = 5;
constexpr int TRADE_MARKUP_MAX = 200;

struct FactionColor {
    float r = 1.0f, g = 1.0f, b = 1.0f;
};

// ---- A faction's rules for those outside it (data/factions.txt; yours can be changed in play, in Faction Rules) ----
// How it takes to the Factionless: as friends (allies), as anyone else (neutral), or as enemies
enum class StanceRule : int { Friendly = 0, Neutral, Hostile, Count };
// An outsider (neither ally nor enemy) standing in its claim: let be, warned and attacked if it stays, or attacked
enum class TrespassRule : int { Ignore = 0, Warn, Attack, Count };
// A hostile unit it has knocked out: left lying, locked in a Prisoner Cage of its own, carried out of its claim, or killed
enum class DownedBodyRule : int { Leave = 0, Cage, CarryOut, FinishOff, Count };
// What it takes from that unit first, each taking what the one before it does: nothing, its food, its weapons too (main
// hand and off hand), or its whole pack too. What the taker can't hold is dropped beside it.
enum class DownedTakeRule : int { Nothing = 0, Food, Weapons, Everything, Count };
// Whom it trades with: no one, its allies, anyone not hostile but the Factionless, or anyone not hostile
enum class TradeRule : int { Nobody = 0, Allies, NotFactionless, Anyone, Count };
// Whom it takes in: no one, an outsider it rescued that wakes in its bed, a Factionless unit that lingers in its claim, or both
enum class RecruitRule : int { Never = 0, Rescued, Factionless, Both, Count };
// Whom its units rescue and share food with: their own faction only, its allies too, or anyone not hostile
enum class AidRule : int { Own = 0, Allies, Anyone, Count };

struct FactionRules {
    StanceRule factionless = StanceRule::Neutral;
    TrespassRule trespass = TrespassRule::Ignore;
    DownedBodyRule downedBody = DownedBodyRule::Leave;
    DownedTakeRule downedTake = DownedTakeRule::Nothing;
    TradeRule trade = TradeRule::Anyone;
    int markup = 0;   // percent added to what it asks in trade
    RecruitRule recruit = RecruitRule::Never;
    AidRule aid = AidRule::Allies;
    bool operator==(const FactionRules& o) const {
        return factionless == o.factionless && trespass == o.trespass && downedBody == o.downedBody && downedTake == o.downedTake &&
               trade == o.trade && markup == o.markup && recruit == o.recruit && aid == o.aid;
    }
    bool operator!=(const FactionRules& o) const { return !(*this == o); }
};

// The rules as key=value words (factionless=hostile trespass=attack ...), as factions.txt and saves write them
std::string factionRulesText(const FactionRules& r);
// Reads such words into r, leaving the rules not named as they are. False on a word it does not know.
bool parseFactionRules(const std::string& text, FactionRules& r, std::string& err);

// The rows of the Faction Rules window, and the language keys of each one's values
enum class RuleRow : int { Factionless = 0, Trespass, DownedBody, DownedTake, Trade, Markup, Recruit, Aid, Count };
const char* ruleRowKey(RuleRow row);
const char* ruleRowTipKey(RuleRow row);
// The value of one row of r as text for the window
std::string ruleValueText(const FactionRules& r, RuleRow row);
// Steps one row's value up or down (the markup by 5 percent), wrapping round
void stepRule(FactionRules& r, RuleRow row, int delta);

struct FactionDef {
    int id = 0;
    std::string nameKey;
    FactionColor color;
    int units = 0;
    bool playerControlled = false;   // "control=player": its units are yours to order. The rest run themselves.
    FactionRules rules;
};

bool loadFactions(const std::string& text, std::string& err);

const std::vector<FactionDef>& factions();
const FactionDef* findFaction(int id);
std::string factionName(int id);
FactionColor factionColor(int id);
bool factionPlayerControlled(int id);

// A faction's rules in this world: the table's, or what was set for it in play
const FactionRules& factionRules(World& w, int faction);
void setFactionRules(World& w, int faction, const FactionRules& r);
// The factions whose rules were changed in play, for the save
std::map<int, FactionRules> changedFactionRules(World& w);
void setChangedFactionRules(World& w, const std::map<int, FactionRules>& changed);

int defaultReputation(int a, int b);
int reputation(World& w, int a, int b);
bool factionsHostile(World& w, int a, int b);
// Two factions are allies, or one and the same (but two Factionless units are not)
bool factionsAllied(World& w, int a, int b);
// Whether a unit of helper's faction rescues and shares food with one of helped's, by helper's Aid rule
bool factionAids(World& w, int helper, int helped);
// Whether these two factions trade, both ways, by each one's Trade rule
bool factionsTrade(World& w, int a, int b);
int tradeMarkup(World& w, int faction);
// Whether a faction may hold an Altar and the claim around it: all but the Factionless and the mobs (boss and wild alike)
bool factionCanClaim(int faction);

std::map<int, int> playerReputation(World& w);
const std::vector<FactionDef>& reputationFactions();
int reputationFactionCount();
void setPlayerReputation(World& w, const std::map<int, int>& standings);
int changePlayerReputation(World& w, int faction, int delta);
