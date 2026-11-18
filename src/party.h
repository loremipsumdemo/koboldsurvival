#pragma once
#include <array>
#include <cstddef>
#include <string>
#include <vector>
#include "components.h"
#include "ecs.h"
#include "equip.h"
#include "game_tables.h"
#include "grid.h"
#include "model.h"
#include "rng.h"
#include "stats.h"

// A unit's model is its body's: f1 for a female, m1 for a male.
inline int modelForBody(BodyType body) { return body == BodyType::Female ? 1 : 2; }

inline constexpr size_t PARTY_NAME_CHARS = 32;

inline size_t utf8Length(const std::string& s) {
    size_t n = 0;
    for (unsigned char c : s) if ((c & 0xC0) != 0x80) ++n;
    return n;
}

inline void appendUtf8Limited(std::string& s, const std::string& add, size_t max) {
    for (size_t i = 0; i < add.size(); ) {
        size_t end = i + 1;
        while (end < add.size() && (static_cast<unsigned char>(add[end]) & 0xC0) == 0x80) ++end;
        if (utf8Length(s) >= max) return;
        s.append(add, i, end - i);
        i = end;
    }
}

inline constexpr int PARTY_MAX_UNITS = 32;

struct PartyMember {
    std::string name;
    BodyType body = BodyType::Male;
    int sprite = 1;
    Stats stats;
    GearItems gear{};
    std::array<ItemStack, INVENTORY_SLOTS> items{};
    int hunger = -1;
    MobKind mob = MobKind::None;
};

using Party = std::vector<PartyMember>;

PartyMember mobMember(MobKind kind, Rng& rng);
std::vector<MobKind> regularMobKinds();
int partyPeople(const Party& p);

std::string cleanName(const std::string& s);

bool partyNamesValid(const Party& p);

void setStatLevel(Stats& s, StatId stat, int level);

Equipment wornBy(const PartyMember& m);

std::vector<int> gearChoices(const PartyMember& m, int slot);

std::vector<int> allItemChoices();

PartyMember randomMember(Rng& rng, const std::vector<std::string>& taken);

Party randomParty(Rng& rng, int count = START_UNIT_COUNT);

// Drops items, then gear, until the load is under share of what the unit can carry (ENCUMBERED_SHARE by default)
void keepUnderEncumbrance(std::array<ItemStack, INVENTORY_SLOTS>& items, GearItems& gear,
                          const Stats& stats, float share = -1.0f);

// Your party as a scenario (scenario.h) starts it: as many people as it gives your faction, each with random stats and gear that they can
// carry, and the mobs it gives them after the people. Their names, stats and gear can be changed in the party menu, but not how many.
struct Scenario;
Party scenarioParty(const Scenario& scenario, Rng& rng);

void addRandomMember(Party& p, Rng& rng);

void applyParty(World& w, const Party& p);
