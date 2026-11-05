#pragma once
#include <array>
#include <cstddef>
#include <string>
#include <vector>
#include "components.h"
#include "ecs.h"
#include "equip.h"
#include "grid.h"
#include "rng.h"
#include "stats.h"

// The starting party: the units a new game begins with, as set up in the party menu. Plain
// functions over the data, so they can be tested without the game running.

// Unit models are data/model/unit/unit_01.glb to unit_03.glb; a unit whose model is missing is drawn as a pink square.
// A unit's sprite number picks its model, and a number past the last (from an older save) wraps round to one of them.
inline constexpr int UNIT_SPRITE_COUNT = 3;

// The model a body type starts with: Feminine unit_01.glb, Masculine unit_02.glb, Androgynous unit_03.glb. A new party
// member and every NPC get theirs; in the party menu the model can then be changed on its own.
inline int modelForBody(BodyType body) {
    if (body == BodyType::Feminine) return 1;
    if (body == BodyType::Androgynous) return 3;
    return 2;
}

// A model's name in the lists: "Model 01" for unit_01.glb, from the one language key UILABEL_MODEL_N
std::string modelLabel(int n);

// Longest name a unit can have, in characters. A character is one UTF-8 code point, so a name can be typed in any
// language, and a character takes up to 4 bytes, so the bytes are capped at PARTY_NAME_LIMIT.
inline constexpr size_t PARTY_NAME_CHARS = 32;
inline constexpr size_t PARTY_NAME_LIMIT = 4 * PARTY_NAME_CHARS;

// The number of UTF-8 characters in a string
inline size_t utf8Length(const std::string& s) {
    size_t n = 0;
    for (unsigned char c : s) if ((c & 0xC0) != 0x80) ++n;
    return n;
}

// Appends the characters of add to s, one at a time, until s has max characters
inline void appendUtf8Limited(std::string& s, const std::string& add, size_t max) {
    for (size_t i = 0; i < add.size(); ) {
        size_t end = i + 1;
        while (end < add.size() && (static_cast<unsigned char>(add[end]) & 0xC0) == 0x80) ++end;
        if (utf8Length(s) >= max) return;
        s.append(add, i, end - i);
        i = end;
    }
}

// Most units a party can have. A scenario sets how many it starts with, and the menu adds or removes them.
inline constexpr int PARTY_MAX_UNITS = 32;

// One unit of the starting party
struct PartyMember {
    std::string name;
    BodyType body = BodyType::Masculine;                      // the body type, which the name is drawn from
    int sprite = 1;                                    // 1..UNIT_SPRITE_COUNT
    Stats stats;                                       // total XP in each stat
    GearItems gear{};                                 // item id in each gear slot, 0 for empty
    std::array<ItemStack, INVENTORY_SLOTS> items{};   // what the unit carries at the start
    int hunger = -1;                                   // hunger at the start, 0 to 100; -1 for full
    MobKind mob = MobKind::None;                       // a mob's kind (mobs.h), a regular one; 0 for a person. A mob
                                                       // has its kind's stats, no gear, and only its meat and bones.
};

// The units a new game starts with: START_UNIT_COUNT or more, up to PARTY_MAX_UNITS, people and mobs
using Party = std::vector<PartyMember>;

// A party member that is a mob of a kind: named for its kind, with that kind's rolled stats
PartyMember mobMember(MobKind kind, Rng& rng);
// The regular mob kinds (no bosses), in kind order
std::vector<MobKind> regularMobKinds();
// How many of a party's members are people, not mobs
int partyPeople(const Party& p);

// The display name of an item, or an empty string for id 0 or an unknown id

// The name with spaces removed from both ends, cut to PARTY_NAME_CHARS characters
std::string cleanName(const std::string& s);

// True if every member has a name once cleaned
bool partyNamesValid(const Party& p);

// Sets one stat to a level from 1 to 99, by giving exactly the XP that level needs
void setStatLevel(Stats& s, StatId stat, int level);

// The gear a member wears, as the gear rules read it
Equipment wornBy(const PartyMember& m);

// Item ids that may go into a gear slot of this member, sorted by name. Empty is not included.
std::vector<int> gearChoices(const PartyMember& m, int slot);

// Every item in the database, sorted by name
std::vector<int> allItemChoices();

// A random member: a name not in taken, a random sprite, random starting stats, some gear that fits
// and a few stacks of items
PartyMember randomMember(Rng& rng, const std::vector<std::string>& taken);

// A random party of distinct names, each with its body type's model
Party randomParty(Rng& rng, int count = START_UNIT_COUNT);

// Takes items off a unit until it carries less than ENCUMBERED_SHARE of its carry limit (emotes.h): the last stacks
// of its inventory first, then its heaviest gear. Items that weigh nothing stay. Used on every unit a game generates.
void keepUnderEncumbrance(std::array<ItemStack, INVENTORY_SLOTS>& items, GearItems& gear,
                          const Stats& stats);

// ---- Scenarios: the start a new game is set up from, picked on the page before the party menu ----
// Listed in this order, Vagabond at the top and Custom at the bottom. Only Custom lets the party menu add or remove units.
// Only Full Random Camp starts with your camp (scenarioHasCamp); the rest start with no camp, and no mobs but their own.
enum class ScenarioId : int {
    Vagabond = 0,       // 1 unit, every stat 1 to 5, nothing carried or worn
    PackOfTwo,          // a Vagabond and a Wolf named Buddy
    Beastmaster,        // a Vagabond and 20 regular mobs of random kinds; each mob's kind can be changed, not their number
    FullRandom,         // Full Random Camp: your camp, 20 or more units with every stat 1 to 99 and random items and gear
                        // they can carry, and 3 to 7 regular mobs
    TravelingCaravan,   // 10 units, every stat 5, 10 Gold Coins each
    ShabbyCrew,         // 3 units, one random item each, every stat 1 to 10
    StarvingNobody,     // 1 unit, nothing carried or worn, every stat 1, hunger 0
    DungeonDiggers,     // 7 units in full gear, every stat 5 to 20 but Mining 50, 3 Iron Ore each
    Custom,             // 3 units rolled as new units are (randomMember), and units can be added and removed
    Count
};
// The language keys for a scenario's name and description
const char* scenarioNameKey(ScenarioId id);
const char* scenarioDescKey(ScenarioId id);
// Whether the party menu may add and remove units for this scenario, and change their stats, gear and items: only
// Custom. The other scenarios keep what they roll; their units' names, pictures and body types can still be changed.
inline bool scenarioResizable(ScenarioId id) { return id == ScenarioId::Custom; }
inline bool scenarioEditable(ScenarioId id) { return id == ScenarioId::Custom; }
// Whether the party menu may change a mob member's kind: Custom, and Beastmaster, whose mobs can each be made any
// regular kind though their number is fixed
inline bool scenarioMobKindEditable(ScenarioId id) { return id == ScenarioId::Custom || id == ScenarioId::Beastmaster; }
// Whether a new game of this scenario starts with your camp: only Full Random Camp
inline bool scenarioHasCamp(ScenarioId id) { return id == ScenarioId::FullRandom; }
// A party for a scenario, with distinct names and sprites. count, when 1 or more, sets the size of a Full Random or
// Custom party (the other scenarios have their own sizes).
Party scenarioParty(ScenarioId id, Rng& rng, int count = 0);

// Adds a random member to the end of the party, with a name and sprite that no member has yet
void addRandomMember(Party& p, Rng& rng);

// Puts the party into the player units (the Commandable ones), in the order they were created: its people into your
// people, its mobs into your mobs. Other units are left as they are.
void applyParty(World& w, const Party& p);
