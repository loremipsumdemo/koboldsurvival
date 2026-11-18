#pragma once
#include <map>
#include <string>
#include <utility>
#include <vector>
#include "ecs.h"
#include "factions.h"
#include "mobs.h"
#include "props.h"

// A Scenario says how a new game on a made-up map starts: for your own faction, for every faction that runs itself (NPC) and, one by one,
// for each of the colored ones. How many units each begins with (military, fortified, citizens), the animals that go with them, the camp
// it begins with and the props that stand in it, and the faction rules it begins with. The Outpost is the one built in; the others are
// the player's own, a file each in MAP/SCENARIO (made in Add Custom Scenario). A map from a level file places everything itself, and
// takes only your party from the scenario.

inline constexpr const char* SCENARIO_FOLDER = "MAP/SCENARIO";
inline constexpr const char* SCENARIO_EXTENSION = ".scenario";
inline constexpr size_t SCENARIO_NAME_CHARS = 32;
inline constexpr size_t SCENARIO_ABOUT_CHARS = 140;
inline constexpr int SCENARIO_MOST_UNITS = 60;     // of one kind of unit, for a faction that runs itself
inline constexpr int SCENARIO_MOST_MOBS = 20;      // of one kind of mob, or of one kind of prop
inline constexpr int SCENARIO_MOST_CAGED = 12;
inline constexpr int SCENARIO_MOST_EXTRAS = 20;
inline constexpr int SCENARIO_MOST_STOCK = 2000;   // of what a stockpile starts full of
// Where a faction with no Altar puts one, before anything else: the nearest place there is a block this many tiles square of free ground,
// within this many tiles of where its units are; failing that, the nearest free ground tile there is
inline constexpr int ALTAR_SITE_RANGE = 100;
inline constexpr int ALTAR_SITE_BLOCK = 10;

// How much of a camp a faction starts with: none, its Altar alone, or the whole camp (an Altar, a campfire, bedrolls, stockpiles and the
// rest). A faction that runs itself always has at least its Altar: it finds the nearest place with room to put one.
enum class StartCamp : int { None = 0, Altar, Full, Count };

// The numbers a scenario sets for a faction, by the word each has in the file. Stock: how much each stockpile of its camp starts full of.
// Standing: how a faction that runs itself stands with you, -100 to 100 (left alone, it is what the loop of colors says).
enum class StartRow : int { Military = 0, Fortified, Citizens, Caged, Camp, Extras, Stock, Standing, Count };
inline constexpr int STANDING_BY_LOOP = -1000;   // FactionStart::standing when the scenario names none
// How far one step of a row goes in the editor (Shift: five steps)
int startRowStep(StartRow row);
const char* startRowKey(StartRow row);
const char* startRowLabelKey(StartRow row);
const char* startRowTipKey(StartRow row);
// The least and most a row may be, for your faction or for one that runs itself (Camp: yours may be none, theirs at least the Altar). A row
// that does not apply to yours (Fortified) is 0 and 0.
int startRowLeast(StartRow row, bool player);
int startRowMost(StartRow row, bool player);
// The words of Camp: None, Altar Only, Full Camp
const char* startCampKey(int camp);

// What one group of factions is given. A row it does not name is left to the group before it, and in the end to the defaults.
struct ScenarioBlock {
    std::map<int, int> rows;                  // StartRow -> its value
    std::map<std::string, int> mobs;          // a regular mob kind's key -> how many go with the faction
    std::map<std::string, int> props;         // a prop kind's key -> how many of it stand about its camp
    std::map<int, int> rules;                 // RuleRow -> its value, a faction rule it begins with
    bool empty() const { return rows.empty() && mobs.empty() && props.empty() && rules.empty(); }
    bool operator==(const ScenarioBlock& o) const { return rows == o.rows && mobs == o.mobs && props == o.props && rules == o.rules; }
};

struct Scenario {
    std::string name;     // its file's name; empty for the Outpost
    std::string about;    // a line the player wrote
    bool builtin = false;
    ScenarioBlock player;                       // your faction, whichever color you are
    ScenarioBlock npc;                          // every faction that runs itself
    ScenarioBlock factions[PALETTE_COUNT];      // one of those by its slot on the loop, over what the NPC block says of it
    bool operator==(const Scenario& o) const;
};

// What a faction starts with once a scenario is read for it: the Outpost's defaults, then what the scenario's blocks say
struct FactionStart {
    int military = 0, fortified = 0, citizens = 0;
    int caged = 0;                               // wild animals in cages in its camp, to tame
    StartCamp camp = StartCamp::Full;
    int extras = -1;                             // random props about its camp; -1: by chance, three to five, as the camps of NPC factions always had
    int stock = 500;                             // what each stockpile of its camp starts full of (camp_kit.h KIT_STOCK_DEFAULT)
    int standing = STANDING_BY_LOOP;             // how it stands with you at the start, if the scenario says (a faction that runs itself)
    std::vector<std::pair<MobKind, int>> mobs;   // animals that go with it, by kind
    std::vector<std::pair<PropKind, int>> props; // props about its camp, by kind
    std::map<int, int> rules;                    // RuleRow -> value, the faction rules the scenario sets
    int people() const { return military + fortified + citizens; }
    int mobCount() const {
        int n = 0;
        for (const auto& m : mobs) n += m.second;
        return n;
    }
};

// A faction's start: FACTION_PLAYER, or the id of a colored faction. Always valid: rows are held to their ranges, your party to
// PARTY_MAX_UNITS with its animals and with a military unit to command, and a faction of NPC to at least its Altar.
FactionStart scenarioStart(const Scenario& s, int faction);
// The same for a faction's defaults alone: the Outpost's
FactionStart defaultStart(int faction);

// The props a scenario can set a count of, in the order its editor lists them
const std::vector<PropKind>& scenarioPropKinds();

// Sets the rules every faction in the world begins with: those a scenario names, and the shares of Fortified and military its unit counts make
void applyScenarioRules(World& w, const Scenario& s);

const Scenario& outpostScenario();

// The files: MAP/SCENARIO/<name>.scenario, written as one line a setting
std::string scenarioFilePath(const std::string& name);
std::string scenarioToText(const Scenario& s);
bool scenarioFromText(const std::string& text, Scenario& out, std::string& err);
bool writeScenarioFile(const std::string& name, const Scenario& s, std::string& err);
bool readScenarioFile(const std::string& name, Scenario& out, std::string& err);
std::vector<std::string> listScenarioFiles();
bool deleteScenarioFile(const std::string& name);
