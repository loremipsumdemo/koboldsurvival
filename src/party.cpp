#include "party.h"
#include "language.h"
#include <algorithm>
#include "combat.h"
#include "inventory.h"
#include "items.h"
#include "systems.h"
#include "emotes.h"
#include "equip.h"

namespace {

// The ids sorted by item name, so the lists read in order
std::vector<int> sortedByName(std::vector<int> ids) {
    std::stable_sort(ids.begin(), ids.end(), [](int a, int b) { return itemNameOf(a) < itemNameOf(b); });
    return ids;
}

} // namespace

std::string itemNameOf(int item) {
    const ItemDef* d = findItem(item);
    return d ? itemDisplayName(*d) : std::string();
}

std::string cleanName(const std::string& s) {
    const size_t a = s.find_first_not_of(' ');
    if (a == std::string::npos) return "";
    const size_t b = s.find_last_not_of(' ');
    std::string out = s.substr(a, b - a + 1);
    // Keep at most PARTY_NAME_CHARS characters: a character is a whole UTF-8 sequence, so none is split
    size_t chars = 0;
    for (size_t i = 0; i < out.size(); ) {
        if (chars == PARTY_NAME_CHARS) { out.resize(i); break; }
        ++chars;
        ++i;
        while (i < out.size() && (static_cast<unsigned char>(out[i]) & 0xC0) == 0x80) ++i;
    }
    return out;
}

bool partyNamesValid(const Party& p) {
    return std::all_of(p.begin(), p.end(), [](const PartyMember& m) { return !cleanName(m.name).empty(); });
}

void setStatLevel(Stats& s, int stat, int level) {
    if (stat < 0 || stat >= STAT_COUNT) return;
    s.xp[stat] = xpForLevel(std::clamp(level, 1, MAX_LEVEL));
}

Equipment wornBy(const PartyMember& m) {
    Equipment e;
    e.items = m.gear;
    return e;
}

std::vector<int> gearChoices(const PartyMember& m, int slot) {
    const Equipment worn = wornBy(m);
    std::vector<int> out;
    std::string why;
    for (const ItemDef& d : itemDatabase()) {
        if (canEquip(worn, d.id, slot, why)) out.push_back(d.id);
    }
    return sortedByName(out);
}

std::vector<int> allItemChoices() {
    std::vector<int> out;
    for (const ItemDef& d : itemDatabase()) out.push_back(d.id);
    return sortedByName(out);
}

PartyMember randomMember(Rng& rng, const std::vector<std::string>& taken) {
    PartyMember m;
    m.body = randomBodyType(rng);
    m.name = randomUnitName(rng, m.body, taken);
    m.sprite = rng.range(1, UNIT_SPRITE_COUNT);
    m.stats = randomStartingStats(rng);

    // Gear: each slot is filled about half the time, with an item that fits it
    for (int slot = 0; slot < GEAR_SLOTS; ++slot) {
        if (rng.range(0, 1) == 0) continue;
        const std::vector<int> choices = gearChoices(m, slot);
        if (!choices.empty()) m.gear[slot] = choices[rng.range(0, (int)choices.size() - 1)];
    }

    // A few stacks of items to carry, each with a random count of up to five
    const std::vector<int> items = allItemChoices();
    const int stacks = rng.range(2, 5);
    for (int i = 0; i < stacks && !items.empty(); ++i) {
        const int item = items[rng.range(0, (int)items.size() - 1)];
        m.items[i] = ItemStack{ item, rng.range(1, std::min(stackLimit(item), 5)) };
    }
    keepUnderEncumbrance(m.items, m.gear, m.stats);   // never more than it can carry
    return m;
}

void keepUnderEncumbrance(std::array<ItemStack, INVENTORY_SLOTS>& items, std::array<int, GEAR_SLOTS>& gear,
                          const Stats& stats) {
    const float cap = ENCUMBERED_SHARE * carryLimit(stats);
    auto weightOf = [](int item) {
        const ItemDef* d = findItem(item);
        return d ? std::max(0.0f, d->weight) : 0.0f;
    };
    auto total = [&]() {
        Inventory inv;
        inv.slots = items;
        Equipment worn;
        worn.items = gear;
        return carriedWeight(inv) + wornWeight(worn);
    };
    // The last stacks go first, a few at a time, so most of what was rolled stays
    for (int i = INVENTORY_SLOTS - 1; i >= 0 && total() >= cap; --i) {
        ItemStack& st = items[(size_t)i];
        const float each = weightOf(st.item);
        if (st.item == 0 || each <= 0.0f) continue;
        const float over = total() - cap;
        const int drop = std::min(st.count, (int)std::floor(over / each) + 1);
        st.count -= drop;
        if (st.count <= 0) st = ItemStack{};
    }
    // Then the heaviest gear, while it is still too much
    while (total() >= cap) {
        int heaviest = -1;
        for (int g = 0; g < GEAR_SLOTS; ++g) {
            if (gear[(size_t)g] != 0 && weightOf(gear[(size_t)g]) > 0.0f &&
                (heaviest < 0 || weightOf(gear[(size_t)g]) > weightOf(gear[(size_t)heaviest]))) heaviest = g;
        }
        if (heaviest < 0) break;
        gear[(size_t)heaviest] = 0;
    }
}

const char* scenarioNameKey(ScenarioId id) {
    switch (id) {
    case ScenarioId::TravelingCaravan: return "SCENARIO_CARAVAN_NAME";
    case ScenarioId::ShabbyCrew: return "SCENARIO_SHABBY_NAME";
    case ScenarioId::StarvingNobody: return "SCENARIO_NOBODY_NAME";
    case ScenarioId::DungeonDiggers: return "SCENARIO_DIGGERS_NAME";
    default: return "SCENARIO_RANDOM_NAME";
    }
}

const char* scenarioDescKey(ScenarioId id) {
    switch (id) {
    case ScenarioId::TravelingCaravan: return "SCENARIO_CARAVAN_DESC";
    case ScenarioId::ShabbyCrew: return "SCENARIO_SHABBY_DESC";
    case ScenarioId::StarvingNobody: return "SCENARIO_NOBODY_DESC";
    case ScenarioId::DungeonDiggers: return "SCENARIO_DIGGERS_DESC";
    default: return "SCENARIO_RANDOM_DESC";
    }
}

namespace {

constexpr int GOLD_COIN = 4;   // item ids in data/itemdb.txt
constexpr int IRON_ORE = 1;

// Every stat at a random level from lo to hi
Stats statsBetween(Rng& rng, int lo, int hi) {
    Stats s;
    for (int i = 0; i < STAT_COUNT; ++i) setStatLevel(s, i, rng.range(lo, hi));
    return s;
}

// Every gear slot filled where an item fits it and the unit can still carry it, keeping `reserve` weight free for
// what it will carry. The main hand takes a one-handed item, so the off hand can be filled too.
void fullGear(PartyMember& m, Rng& rng, float reserve) {
    const float cap = ENCUMBERED_SHARE * carryLimit(m.stats) - reserve;
    for (int slot = 0; slot < GEAR_SLOTS; ++slot) {
        Equipment worn = wornBy(m);
        const float used = wornWeight(worn);
        std::vector<int> choices;
        for (int item : gearChoices(m, slot)) {
            const ItemDef* d = findItem(item);
            if (!d) continue;
            if (slot == GEAR_MAIN_HAND && d->equipSlot == "two_hand") continue;
            if (used + d->weight >= cap) continue;
            choices.push_back(item);
        }
        if (!choices.empty()) m.gear[(size_t)slot] = choices[(size_t)rng.range(0, (int)choices.size() - 1)];
    }
}

} // namespace

Party scenarioParty(ScenarioId id, Rng& rng, int count) {
    int size = 1;
    switch (id) {
    case ScenarioId::FullRandom: size = count >= 1 ? std::min(count, PARTY_MAX_UNITS) : rng.range(1, PARTY_MAX_UNITS); break;
    case ScenarioId::TravelingCaravan: size = 10; break;
    case ScenarioId::ShabbyCrew: size = 3; break;
    case ScenarioId::StarvingNobody: size = 1; break;
    case ScenarioId::DungeonDiggers: size = 7; break;
    case ScenarioId::Count: break;
    }
    // The names, body types and pictures are rolled as for any party; the scenario then sets the rest
    Party p = randomParty(rng, size);
    const std::vector<int> items = allItemChoices();
    for (PartyMember& m : p) {
        m.gear = {};
        m.items = {};
        switch (id) {
        case ScenarioId::FullRandom: {
            m.stats = statsBetween(rng, 1, MAX_LEVEL);
            for (int slot = 0; slot < GEAR_SLOTS; ++slot) {
                if (rng.range(0, 1) == 0) continue;
                const std::vector<int> choices = gearChoices(m, slot);
                if (!choices.empty()) m.gear[(size_t)slot] = choices[(size_t)rng.range(0, (int)choices.size() - 1)];
            }
            const int stacks = rng.range(2, 5);
            for (int i = 0; i < stacks && !items.empty(); ++i) {
                const int item = items[(size_t)rng.range(0, (int)items.size() - 1)];
                m.items[(size_t)i] = ItemStack{ item, rng.range(1, std::min(stackLimit(item), 5)) };
            }
            break;
        }
        case ScenarioId::TravelingCaravan:
            m.stats = statsBetween(rng, 5, 5);
            m.items[0] = ItemStack{ GOLD_COIN, 10 };
            break;
        case ScenarioId::ShabbyCrew:
            m.stats = statsBetween(rng, 1, 10);
            if (!items.empty()) m.items[0] = ItemStack{ items[(size_t)rng.range(0, (int)items.size() - 1)], 1 };
            break;
        case ScenarioId::StarvingNobody:
            m.stats = statsBetween(rng, 1, 1);
            m.hunger = 0;
            break;
        case ScenarioId::DungeonDiggers: {
            m.stats = statsBetween(rng, 5, 20);
            setStatLevel(m.stats, STAT_MINING, 50);
            const ItemDef* ore = findItem(IRON_ORE);
            fullGear(m, rng, ore ? 3.0f * ore->weight : 0.0f);   // room is kept for the ore
            m.items[0] = ItemStack{ IRON_ORE, 3 };
            break;
        }
        case ScenarioId::Count:
            break;
        }
        keepUnderEncumbrance(m.items, m.gear, m.stats);
    }
    return p;
}

Party randomParty(Rng& rng, int count) {
    Party p(count);
    std::vector<std::string> names;
    std::vector<int> sprites;
    for (PartyMember& m : p) {
        m = randomMember(rng, names);
        while (std::find(sprites.begin(), sprites.end(), m.sprite) != sprites.end()) {
            m.sprite = rng.range(1, UNIT_SPRITE_COUNT);
        }
        names.push_back(m.name);
        sprites.push_back(m.sprite);
    }
    return p;
}

void applyParty(World& w, const Party& p) {
    std::vector<Entity> players = w.view<Unit, Commandable>();
    std::sort(players.begin(), players.end());
    for (size_t i = 0; i < players.size() && i < p.size(); ++i) {
        const Entity u = players[i];
        const PartyMember& m = p[i];
        const std::string name = cleanName(m.name);
        w.add(u, Name{ name.empty() ? std::string(lang("UILABEL_UNNAMED_UNIT")) : name });
        w.add(u, UnitSprite{ m.sprite });
        w.add(u, Body{ m.body });
        w.add(u, m.stats);
        Needs needs;   // a new unit starts at full health for its Vitality, and full unless its scenario says otherwise
        needs.maxHp = needs.hp = maxHealthFor(m.stats);
        if (m.hunger >= 0) needs.hunger = std::clamp(m.hunger, 0, needs.maxHunger);
        w.add(u, needs);
        Inventory inv;
        inv.slots = m.items;
        w.add(u, inv);
        Equipment worn;
        worn.items = m.gear;
        w.add(u, worn);
    }
}

void addRandomMember(Party& p, Rng& rng) {
    std::vector<std::string> names;
    std::vector<int> sprites;
    for (const PartyMember& m : p) {
        names.push_back(m.name);
        sprites.push_back(m.sprite);
    }
    PartyMember m = randomMember(rng, names);
    while (std::find(sprites.begin(), sprites.end(), m.sprite) != sprites.end()) {
        m.sprite = rng.range(1, UNIT_SPRITE_COUNT);
    }
    p.push_back(m);
}
