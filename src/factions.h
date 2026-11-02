#pragma once
#include <map>
#include <string>
#include <vector>

// Factions and reputation.
//
// Every unit belongs to a faction (the Faction component). The factions are listed in data/factioninfo.txt: the Player
// Faction (always id 0, your units), then the others. Each pair of factions has a reputation from -100 to 100, the same
// both ways; 0 is neutral, and a faction is always at 100 with itself. At REP_HOSTILE or below, their units attack each
// other on sight; at REP_ALLIED or above, they are allies, and help each other when one is attacked.
//
// The reputations between two other factions never change. The Player Faction's reputation with each faction starts at
// the file's value and changes in play: it falls by REP_KO_PENALTY for each unit of that faction your units knock
// unconscious, and by REP_DEATH_PENALTY more if that unit then dies without waking. It is kept in the save.

constexpr int FACTION_PLAYER = 0;
constexpr int FACTION_RETIRED_GRAY = 1;   // the old Gray Faction: units saved with it load into FACTION_GREEN
constexpr int FACTION_RED = 2;
constexpr int FACTION_GREEN = 3;

constexpr int REP_MIN = -100;
constexpr int REP_MAX = 100;
constexpr int REP_HOSTILE = -25;     // at or below: attack on sight
constexpr int REP_ALLIED = 50;       // at or above: allies
constexpr int REP_KO_PENALTY = 5;    // the Player Faction's loss with a faction for each of its units knocked out
constexpr int REP_DEATH_PENALTY = 5; // and again if that unit dies before it wakes

struct FactionColor {
    float r = 1.0f, g = 1.0f, b = 1.0f;
};

struct FactionDef {
    int id = 0;
    std::string nameKey;   // its name's language key; a key the language files lack is shown as written
    FactionColor color;
    int units = 0;         // how many of its units a new level places (none for the Player Faction)
};

// Reads a factioninfo.txt text and makes it the game's faction list. A bad or empty text leaves the built-in list (the
// same as the shipped file). Returns false, with err set, when the text could not be used.
bool loadFactions(const std::string& text, std::string& err);
// The text of the built-in list, as factioninfo.txt is shipped
const std::string& defaultFactionText();

// Every faction, the Player Faction first, then by id
const std::vector<FactionDef>& factions();
const FactionDef* findFaction(int id);
// The faction a saved id stands for now: a retired id maps to the faction that replaced it, an unknown one to the
// first faction other than the Player Faction
int knownFactionId(int id);
// A faction's name in the current language
std::string factionName(int id);
FactionColor factionColor(int id);

// The starting reputation between two factions, from the file (0 if the pair is not listed, 100 with itself)
int defaultReputation(int a, int b);
// The reputation now: the Player Faction's as it stands in play, the rest as in the file
int reputation(int a, int b);
bool factionsHostile(int a, int b);   // at or below REP_HOSTILE (never a faction with itself)
bool factionsAllied(int a, int b);    // at or above REP_ALLIED, or the same faction

// The Player Faction's standing with each faction, for the save and the Reputation tab
std::map<int, int> playerReputation();
// Starts every standing at its default (a new game)
void resetPlayerReputation();
// Sets the standings from a save; factions the save does not name keep their defaults
void setPlayerReputation(const std::map<int, int>& standings);
// Adds delta (negative to lower it) to the Player Faction's standing with a faction, kept within -100..100. Returns
// the new standing.
int changePlayerReputation(int faction, int delta);
