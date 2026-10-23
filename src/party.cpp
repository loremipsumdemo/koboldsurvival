#include "party.h"
#include <algorithm>
#include "inventory.h"
#include "items.h"
#include "systems.h"

namespace {

// The ids sorted by item name, so the lists read in order
std::vector<int> sortedByName(std::vector<int> ids) {
    std::stable_sort(ids.begin(), ids.end(), [](int a, int b) { return itemNameOf(a) < itemNameOf(b); });
    return ids;
}

} // namespace

std::string itemNameOf(int item) {
    const ItemDef* d = findItem(item);
    return d ? d->name : std::string();
}

std::string cleanName(const std::string& s) {
    const size_t a = s.find_first_not_of(' ');
    if (a == std::string::npos) return "";
    const size_t b = s.find_last_not_of(' ');
    std::string out = s.substr(a, b - a + 1);
    if (out.size() > PARTY_NAME_LIMIT) {
        out.resize(PARTY_NAME_LIMIT);
        // A UTF-8 character cut in half at the end is dropped
        size_t i = out.size();
        while (i > 0 && (static_cast<unsigned char>(out[i - 1]) & 0xC0) == 0x80) --i;
        if (i > 0) {
            const unsigned char lead = static_cast<unsigned char>(out[i - 1]);
            const size_t need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
            if (out.size() - (i - 1) < need) out.resize(i - 1);
        }
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
    m.name = randomUnitName(rng, taken);
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
    return m;
}

Party randomParty(Rng& rng) {
    Party p;
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
        w.add(u, Name{ name.empty() ? std::string("Unnamed Unit") : name });
        w.add(u, UnitSprite{ m.sprite });
        w.add(u, m.stats);
        Inventory inv;
        inv.slots = m.items;
        w.add(u, inv);
        Equipment worn;
        worn.items = m.gear;
        w.add(u, worn);
    }
}
