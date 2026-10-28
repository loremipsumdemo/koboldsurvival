#pragma once
#include <array>
#include <cstddef>
#include <string>
#include <vector>
#include "components.h"
#include "ecs.h"
#include "equip.h"
#include "grid.h"
#include "resources.h"
#include "stats.h"

// The starting party: the units a new game begins with, as set up in the party menu. Plain
// functions over the data, so they can be tested without the game running.

// Unit pictures are data/unit/01.png to 71.png
inline constexpr int UNIT_SPRITE_COUNT = 71;

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

// Most units a party can have. The starting party is START_UNIT_COUNT, and the menu adds more.
inline constexpr int PARTY_MAX_UNITS = 24;

// One unit of the starting party
struct PartyMember {
    std::string name;
    int body = BODY_MASCULINE;                         // the body type, which the name is drawn from
    int sprite = 1;                                    // 1..UNIT_SPRITE_COUNT
    Stats stats;                                       // total XP in each stat
    std::array<int, GEAR_SLOTS> gear{};               // item id in each gear slot, 0 for empty
    std::array<ItemStack, INVENTORY_SLOTS> items{};   // what the unit carries at the start
};

// The units a new game starts with: START_UNIT_COUNT or more, up to PARTY_MAX_UNITS
using Party = std::vector<PartyMember>;

// The display name of an item, or an empty string for id 0 or an unknown id
std::string itemNameOf(int item);

// The name with spaces removed from both ends, cut to PARTY_NAME_CHARS characters
std::string cleanName(const std::string& s);

// True if every member has a name once cleaned
bool partyNamesValid(const Party& p);

// Sets one stat to a level from 1 to 99, by giving exactly the XP that level needs
void setStatLevel(Stats& s, int stat, int level);

// The gear a member wears, as the gear rules read it
Equipment wornBy(const PartyMember& m);

// Item ids that may go into a gear slot of this member, sorted by name. Empty is not included.
std::vector<int> gearChoices(const PartyMember& m, int slot);

// Every item in the database, sorted by name
std::vector<int> allItemChoices();

// A random member: a name not in taken, a random sprite, random starting stats, some gear that fits
// and a few stacks of items
PartyMember randomMember(Rng& rng, const std::vector<std::string>& taken);

// A random party of distinct names and distinct sprites
Party randomParty(Rng& rng, int count = START_UNIT_COUNT);

// Adds a random member to the end of the party, with a name and sprite that no member has yet
void addRandomMember(Party& p, Rng& rng);

// Puts the party into the player units (the Commandable ones), in the order they were created. Other
// units are left as they are.
void applyParty(World& w, const Party& p);
