#include "party.h"
#include "language.h"
#include <algorithm>
#include "combat.h"
#include "inventory.h"
#include "items.h"
#include "mobs.h"
#include "systems.h"
#include "emotes.h"
#include "equip.h"

namespace {
std::vector<int> sortedByName(std::vector<int> ids) {
    std::stable_sort(ids.begin(), ids.end(), [](int a, int b) { return itemNameOf(a) < itemNameOf(b); });
    return ids;
}
}

std::string cleanName(const std::string& s) {
    const size_t a = s.find_first_not_of(' ');
    if (a == std::string::npos) return "";
    const size_t b = s.find_last_not_of(' ');
    std::string out = s.substr(a, b - a + 1);
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

void setStatLevel(Stats& s, StatId stat, int level) {
    if (idx(stat) < 0 || idx(stat) >= STAT_COUNT) return;
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

std::string modelLabel(int n) {
    char num[16];
    std::snprintf(num, sizeof(num), "%02d", n);
    return langf("UILABEL_MODEL_N", { num });
}

PartyMember randomMember(Rng& rng, const std::vector<std::string>& taken) {
    PartyMember m;
    m.body = randomBodyType(rng);
    m.name = randomUnitName(rng, m.body, taken);
    m.sprite = modelForBody(m.body);
    m.stats = randomStartingStats(rng);

    for (int slot = 0; slot < GEAR_SLOTS; ++slot) {
        if (rng.range(0, 1) == 0) continue;
        const std::vector<int> choices = gearChoices(m, slot);
        if (!choices.empty()) m.gear[slot] = choices[rng.range(0, (int)choices.size() - 1)];
    }

    const std::vector<int> items = allItemChoices();
    const int stacks = rng.range(2, 5);
    for (int i = 0; i < stacks && !items.empty(); ++i) {
        const int item = items[rng.range(0, (int)items.size() - 1)];
        m.items[i] = ItemStack{ item, rng.range(1, std::min(stackLimit(item), 5)) };
    }
    keepUnderEncumbrance(m.items, m.gear, m.stats);
    return m;
}

void keepUnderEncumbrance(std::array<ItemStack, INVENTORY_SLOTS>& items, GearItems& gear,
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
    for (int i = INVENTORY_SLOTS - 1; i >= 0 && total() >= cap; --i) {
        ItemStack& st = items[(size_t)i];
        const float each = weightOf(st.item);
        if (st.item == 0 || each <= 0.0f) continue;
        const float over = total() - cap;
        const int drop = std::min(st.count, (int)std::floor(over / each) + 1);
        st.count -= drop;
        if (st.count <= 0) st = ItemStack{};
    }
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
    case ScenarioId::Vagabond: return "SCENARIO_VAGABOND_NAME";
    case ScenarioId::PackOfTwo: return "SCENARIO_PACK_NAME";
    case ScenarioId::Beastmaster: return "SCENARIO_BEASTMASTER_NAME";
    case ScenarioId::TravelingCaravan: return "SCENARIO_CARAVAN_NAME";
    case ScenarioId::ShabbyCrew: return "SCENARIO_SHABBY_NAME";
    case ScenarioId::StarvingNobody: return "SCENARIO_NOBODY_NAME";
    case ScenarioId::DungeonDiggers: return "SCENARIO_DIGGERS_NAME";
    case ScenarioId::Custom: return "SCENARIO_CUSTOM_NAME";
    default: return "SCENARIO_RANDOM_NAME";
    }
}

const char* scenarioDescKey(ScenarioId id) {
    switch (id) {
    case ScenarioId::Vagabond: return "SCENARIO_VAGABOND_DESC";
    case ScenarioId::PackOfTwo: return "SCENARIO_PACK_DESC";
    case ScenarioId::Beastmaster: return "SCENARIO_BEASTMASTER_DESC";
    case ScenarioId::TravelingCaravan: return "SCENARIO_CARAVAN_DESC";
    case ScenarioId::ShabbyCrew: return "SCENARIO_SHABBY_DESC";
    case ScenarioId::StarvingNobody: return "SCENARIO_NOBODY_DESC";
    case ScenarioId::DungeonDiggers: return "SCENARIO_DIGGERS_DESC";
    case ScenarioId::Custom: return "SCENARIO_CUSTOM_DESC";
    default: return "SCENARIO_RANDOM_DESC";
    }
}

namespace {
constexpr int BEASTMASTER_MOBS = 20;
constexpr const char* PACK_WOLF_NAME = "Buddy";
constexpr int CAMP_PARTY_PEOPLE = 20;
constexpr int CAMP_PARTY_MOBS_MIN = 3, CAMP_PARTY_MOBS_MAX = 7;

MobKind randomRegularMob(Rng& rng) {
    const std::vector<MobKind> kinds = regularMobKinds();
    return kinds[(size_t)rng.range(0, (int)kinds.size() - 1)];
}

Stats statsBetween(Rng& rng, int lo, int hi) {
    Stats s;
    for (int i = 0; i < STAT_COUNT; ++i) setStatLevel(s, (StatId)i, rng.range(lo, hi));
    return s;
}

void fullGear(PartyMember& m, Rng& rng, float reserve) {
    const float cap = ENCUMBERED_SHARE * carryLimit(m.stats) - reserve;
    for (int slot = 0; slot < GEAR_SLOTS; ++slot) {
        Equipment worn = wornBy(m);
        const float used = wornWeight(worn);
        std::vector<int> choices;
        for (int item : gearChoices(m, slot)) {
            const ItemDef* d = findItem(item);
            if (!d) continue;
            if (slot == idx(GearSlot::MainHand) && d->equipSlot == "two_hand") continue;
            if (used + d->weight >= cap) continue;
            choices.push_back(item);
        }
        if (!choices.empty()) m.gear[(size_t)slot] = choices[(size_t)rng.range(0, (int)choices.size() - 1)];
    }
}
}

PartyMember mobMember(MobKind kind, Rng& rng) {
    PartyMember m;
    m.mob = isRegularMob(kind) ? kind : MobKind::Wolf;
    m.name = lang(mobInfo(m.mob).nameKey);
    m.stats = mobStats(m.mob, rng);
    return m;
}

std::vector<MobKind> regularMobKinds() {
    std::vector<MobKind> out;
    for (int k = 1; k <= mobKindCount(); ++k) {
        if (isRegularMob((MobKind)k)) out.push_back((MobKind)k);
    }
    return out;
}

int partyPeople(const Party& p) {
    return (int)std::count_if(p.begin(), p.end(), [](const PartyMember& m) { return m.mob == MobKind::None; });
}

Party scenarioParty(ScenarioId id, Rng& rng, int count) {
    int size = 1;
    const int campMobs = id == ScenarioId::FullRandom ? rng.range(CAMP_PARTY_MOBS_MIN, CAMP_PARTY_MOBS_MAX) : 0;
    switch (id) {
    case ScenarioId::Vagabond: size = 1; break;
    case ScenarioId::PackOfTwo: size = 1; break;
    case ScenarioId::Beastmaster: size = 1; break;
    case ScenarioId::FullRandom: {
        const int most = PARTY_MAX_UNITS - campMobs;
        size = count >= CAMP_PARTY_PEOPLE ? std::min(count, most) : rng.range(CAMP_PARTY_PEOPLE, most);
        break;
    }
    case ScenarioId::TravelingCaravan: size = 10; break;
    case ScenarioId::ShabbyCrew: size = 3; break;
    case ScenarioId::StarvingNobody: size = 1; break;
    case ScenarioId::DungeonDiggers: size = 7; break;
    case ScenarioId::Custom: size = count >= 1 ? std::min(count, PARTY_MAX_UNITS) : START_UNIT_COUNT; break;
    case ScenarioId::Count: break;
    }
    Party p = randomParty(rng, size);
    if (id == ScenarioId::Custom) {
        for (PartyMember& m : p) keepUnderEncumbrance(m.items, m.gear, m.stats);
        return p;
    }
    const std::vector<int> items = allItemChoices();
    for (PartyMember& m : p) {
        m.gear = {};
        m.items = {};
        switch (id) {
        case ScenarioId::Vagabond:
        case ScenarioId::PackOfTwo:
        case ScenarioId::Beastmaster:
            m.stats = statsBetween(rng, 1, 5);
            break;
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
            m.items[0] = ItemStack{ itemTagged(ITEM_TAG_GOLD_COIN), 10 };
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
            setStatLevel(m.stats, StatId::Mining, 50);
            const ItemDef* ore = findItem(itemTagged(ITEM_TAG_IRON_ORE));
            fullGear(m, rng, ore ? 3.0f * ore->weight : 0.0f);
            m.items[0] = ItemStack{ itemTagged(ITEM_TAG_IRON_ORE), 3 };
            break;
        }
        case ScenarioId::Custom:
        case ScenarioId::Count:
            break;
        }
        keepUnderEncumbrance(m.items, m.gear, m.stats);
    }
    if (id == ScenarioId::PackOfTwo) {
        p.push_back(mobMember(MobKind::Wolf, rng));
        p.back().name = PACK_WOLF_NAME;
    }
    if (id == ScenarioId::Beastmaster) {
        for (int i = 0; i < BEASTMASTER_MOBS; ++i) p.push_back(mobMember(randomRegularMob(rng), rng));
    }
    for (int i = 0; i < campMobs; ++i) p.push_back(mobMember(randomRegularMob(rng), rng));
    return p;
}

Party randomParty(Rng& rng, int count) {
    Party p(count);
    std::vector<std::string> names;
    for (PartyMember& m : p) {
        m = randomMember(rng, names);
        names.push_back(m.name);
    }
    return p;
}

void applyParty(World& w, const Party& p) {
    std::vector<Entity> players, mobs;
    for (Entity u : w.view<Unit, Commandable>()) {
        (w.has<Mob>(u) ? mobs : players).push_back(u);
    }
    std::sort(players.begin(), players.end());
    std::sort(mobs.begin(), mobs.end());
    std::vector<const PartyMember*> people, beasts;
    for (const PartyMember& m : p) (m.mob != MobKind::None ? beasts : people).push_back(&m);
    for (size_t i = 0; i < mobs.size() && i < beasts.size(); ++i) {
        const PartyMember& m = *beasts[i];
        const std::string name = cleanName(m.name);
        w.add(mobs[i], Name{ name.empty() ? std::string(lang(mobInfo(m.mob).nameKey)) : name });
        w.add(mobs[i], m.stats);
        if (Needs* n = w.get<Needs>(mobs[i])) {
            const int hp = mobInfo(m.mob).hp;
            n->maxHp = n->hp = hp > 0 ? hp : maxHealthFor(m.stats);
        }
    }
    for (size_t i = 0; i < players.size() && i < people.size(); ++i) {
        const Entity u = players[i];
        const PartyMember& m = *people[i];
        const std::string name = cleanName(m.name);
        w.add(u, Name{ name.empty() ? std::string(lang("UILABEL_UNNAMED_UNIT")) : name });
        w.add(u, UnitSprite{ m.sprite });
        w.add(u, Body{ m.body });
        w.add(u, m.stats);
        Needs needs;
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
    for (const PartyMember& m : p) names.push_back(m.name);
    p.push_back(randomMember(rng, names));
}
