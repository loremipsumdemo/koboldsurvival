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
    return langf("UILABEL_MODEL_N", { unitModelKey(n) });
}

PartyMember randomMember(Rng& rng, const std::vector<std::string>& taken) {
    PartyMember m;
    m.body = randomBodyType(rng);
    m.name = randomUnitName(rng, m.body, taken);
    m.sprite = randomModelForBody(rng, m.body);
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
                          const Stats& stats, float share) {
    const float cap = (share < 0.0f ? ENCUMBERED_SHARE : share) * carryLimit(stats);
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
    return id == ScenarioId::Custom ? "SCENARIO_CUSTOM_NAME" : "SCENARIO_VAGABOND_NAME";
}

const char* scenarioDescKey(ScenarioId id) {
    return id == ScenarioId::Custom ? "SCENARIO_CUSTOM_DESC" : "SCENARIO_VAGABOND_DESC";
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
    if (id == ScenarioId::Custom) {
        Party p = randomParty(rng, count >= 1 ? std::min(count, PARTY_MAX_UNITS) : START_UNIT_COUNT);
        for (PartyMember& m : p) keepUnderEncumbrance(m.items, m.gear, m.stats);
        return p;
    }
    // Vagabond: one unit with nothing, every stat at level 1
    Party p = randomParty(rng, 1);
    for (PartyMember& m : p) {
        m.gear = {};
        m.items = {};
        for (int i = 0; i < STAT_COUNT; ++i) setStatLevel(m.stats, (StatId)i, 1);
    }
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
    std::vector<ObjectId> players, mobs;
    for (ObjectId u : w.view<Entity, Commandable>()) {
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
        const ObjectId u = players[i];
        const PartyMember& m = *people[i];
        const std::string name = cleanName(m.name);
        w.add(u, Name{ name.empty() ? std::string(lang("UILABEL_UNNAMED_UNIT")) : name });
        w.add(u, UnitSprite{ modelForBody(m.body, modelRow(m.sprite)) });
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
