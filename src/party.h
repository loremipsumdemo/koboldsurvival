#pragma once
#include <array>
#include <cstddef>
#include <string>
#include <vector>
#include "components.h"
#include "ecs.h"
#include "equip.h"
#include "grid.h"
#include "model.h"
#include "rng.h"
#include "stats.h"

inline int modelForBody(BodyType body) {
    if (body == BodyType::Feminine) return 1;
    if (body == BodyType::Androgynous) return 3;
    return 2;
}

std::string modelLabel(int n);

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
    BodyType body = BodyType::Masculine;
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

enum class ScenarioId : int {
    Vagabond = 0,   // one unit, every stat at level 1, nothing carried
    Custom,         // Custom Squad: build your own
    Count
};
const char* scenarioNameKey(ScenarioId id);
const char* scenarioDescKey(ScenarioId id);
inline bool scenarioResizable(ScenarioId id) { return id == ScenarioId::Custom; }
inline bool scenarioEditable(ScenarioId id) { return id == ScenarioId::Custom; }
inline bool scenarioMobKindEditable(ScenarioId id) { return id == ScenarioId::Custom; }
Party scenarioParty(ScenarioId id, Rng& rng, int count = 0);

void addRandomMember(Party& p, Rng& rng);

void applyParty(World& w, const Party& p);
