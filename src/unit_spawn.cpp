#include "systems_internal.h"
#include "build.h"

using namespace systems_detail;

namespace {
Stats startingStats(Rng& rng) {
    Stats s;
    for (int i = 0; i < STAT_COUNT; ++i) {
        const bool attribute = statInfo((StatId)i).group == StatGroup::Attributes;
        s.xp[i] = xpForLevel(attribute ? rng.range(5, 15) : rng.range(1, 5));
    }
    return s;
}
}

Stats randomStartingStats(Rng& rng) {
    return startingStats(rng);
}

BodyType randomBodyType(Rng& rng) {
    const int r = rng.range(1, 100);
    return r <= 45 ? BodyType::Masculine : r <= 90 ? BodyType::Feminine : BodyType::Androgynous;
}

std::string randomUnitName(Rng& rng, BodyType body, const std::vector<std::string>& taken) {
    const NameSet set = body == BodyType::Feminine ? NameSet::Feminine
                      : body == BodyType::Androgynous ? NameSet::Androgynous : NameSet::Masculine;
    const std::vector<std::string> names = languageNames(set);
    if (names.empty()) return lang("UILABEL_UNNAMED_UNIT");
    for (int attempt = 0; attempt < 200; ++attempt) {
        const std::string& name = names[rng.range(0, (int)names.size() - 1)];
        if (std::find(taken.begin(), taken.end(), name) == taken.end()) return name;
    }
    return names[0];
}

void systems_detail::spawnPlayerUnit(World& w, Terrain& t, Rng& rng, Vec2 spawn, int i) {
    const Vec2 p = t.snap({ spawn.x - 3.0f + (i % SQUAD_COLUMNS) * 1.5f,
                            spawn.z - 3.0f + (i / SQUAD_COLUMNS) * 1.5f });
    const ObjectId u = makeEntity(w, Transform{ p.x, t.heightAt(p.x, p.z), p.z }, FACTION_PLAYER);
    const BodyType body = randomBodyType(rng);
    w.add(u, UnitSprite{ randomModelForBody(rng, body) });
    w.add(u, Body{ body });
}

void ensurePlayerUnits(World& w, Terrain& t, Rng& rng, int count) {
    std::vector<ObjectId> players;
    for (ObjectId u : w.view<Entity, Commandable>()) {
        if (!w.has<Mob>(u)) players.push_back(u);
    }
    std::sort(players.begin(), players.end());
    for (size_t i = (size_t)std::max(0, count); i < players.size(); ++i) w.destroy(players[i]);
    const int have = std::min((int)players.size(), std::max(0, count));
    if (have >= count) return;
    const LevelInfo& level = w.resource<LevelInfo>();
    const Vec2 spawn = level.spawnSet ? Vec2{ level.spawnX, level.spawnZ } : findSpawn(t);
    for (int i = have; i < count; ++i) spawnPlayerUnit(w, t, rng, spawn, i);
}

void ensurePlayerMobs(World& w, Terrain& t, Rng& rng, const std::vector<MobKind>& kinds, int firstSlot) {
    for (ObjectId u : w.view<Mob, Commandable>()) w.destroy(u);
    if (kinds.empty()) return;
    const LevelInfo& level = w.resource<LevelInfo>();
    const Vec2 spawn = level.spawnSet ? Vec2{ level.spawnX, level.spawnZ } : findSpawn(t);
    for (size_t j = 0; j < kinds.size(); ++j) {
        const int i = firstSlot + (int)j;
        const Vec2 p = t.snap({ spawn.x - 3.0f + (i % SQUAD_COLUMNS) * 1.5f, spawn.z - 3.0f + (i / SQUAD_COLUMNS) * 1.5f });
        spawnMob(w, t, rng, kinds[j], FACTION_PLAYER, p.x, p.z);
    }
}

constexpr int NPC_AMMO_MIN = 10;
constexpr int NPC_AMMO_MAX = 30;
constexpr int NPC_LOOT_MIN = 2;
constexpr int NPC_LOOT_MAX = 5;
constexpr float NPC_MOST_LOAD = 0.15f;   // share of what it can carry: it walks at least 85% of full speed
constexpr int STORAGE_STACKS_MIN = 2;
constexpr int STORAGE_STACKS_MAX = 5;
constexpr int STORAGE_STACK_MOST = 10;
constexpr int STORAGE_VALUABLE = 30;

Inventory systems_detail::randomNpcLoot(Rng& rng) {
    Inventory inv;
    const std::vector<ItemDef>& items = itemDatabase();
    if (items.empty()) return inv;
    const int stacks = rng.range(NPC_LOOT_MIN, NPC_LOOT_MAX);
    for (int i = 0; i < stacks; ++i) {
        const ItemDef& d = items[(size_t)rng.range(0, (int)items.size() - 1)];
        addToInventory(inv, d.id, rng.range(1, std::max(1, stackLimit(d.id))));
    }
    return inv;
}

Storage startingStorage(PropKind kind, Rng& rng) {
    // The slots and capacity props.txt gives this kind, as a store made by makeProp has them
    Storage st;
    st.slots.assign((size_t)std::max(1, propInfo(kind).slots), ItemStack{});
    st.capacity = propInfo(kind).capacity;
    std::vector<int> choices;
    for (const ItemDef& d : itemDatabase()) {
        if (!propAcceptsItem(kind, d.id)) continue;
        const bool gear = !d.equipSlot.empty();
        if (kind == PropKind::Barrel && gear) continue;
        if (kind == PropKind::LockedChest && !gear && d.buyValue < STORAGE_VALUABLE) continue;
        choices.push_back(d.id);
    }
    if (choices.empty()) return st;
    const bool chest = kind == PropKind::LockedChest;
    const int stacks = rng.range(chest ? 3 : STORAGE_STACKS_MIN, chest ? 6 : STORAGE_STACKS_MAX);
    int slot = 0;
    for (int i = 0; i < stacks && slot < (int)st.slots.size(); ++i) {
        const int item = choices[(size_t)rng.range(0, (int)choices.size() - 1)];
        const int most = std::max(1, std::min(stackLimit(item), STORAGE_STACK_MOST));
        st.slots[(size_t)slot++] = ItemStack{ item, rng.range(1, most) };
    }
    return st;
}

void systems_detail::finishUnit(World& w, Rng& rng, ObjectId u, std::vector<std::string>& takenNames) {
    if (!w.has<Stats>(u)) w.add(u, randomStartingStats(rng));
    if (Needs* nd = w.get<Needs>(u)) {
        nd->maxHp = maxHealthFor(*w.get<Stats>(u));
        nd->hp = nd->maxHp;
    }
    if (w.has<Hostile>(u) || w.has<Neutral>(u)) {
        w.add(u, randomHostileGear(rng));
        const Equipment* worn = w.get<Equipment>(u);
        if (const RangedProfile* rp = worn ? rangedProfileOf(worn->items[GearSlot::MainHand]) : nullptr) {
            std::vector<int> ammo;
            for (const ItemDef& d : itemDatabase()) {
                if (hasTag(d, rp->ammoTag)) ammo.push_back(d.id);
            }
            Inventory* inv = w.get<Inventory>(u);
            if (inv && !ammo.empty()) {
                addToInventory(*inv, ammo[(size_t)rng.range(0, (int)ammo.size() - 1)], rng.range(NPC_AMMO_MIN, NPC_AMMO_MAX));
            }
        }
    }
    if (Inventory* inv = w.get<Inventory>(u)) {
        Equipment* worn = w.get<Equipment>(u);
        GearItems gear = worn ? worn->items : GearItems{};
        // Your units may start well loaded; everyone else carries little enough to give chase at near full speed
        keepUnderEncumbrance(inv->slots, gear, *w.get<Stats>(u), w.has<Commandable>(u) ? -1.0f : NPC_MOST_LOAD);
        if (worn) worn->items = gear;
    }
    if (!w.has<Body>(u)) w.add(u, Body{ randomBodyType(rng) });
    const BodyType body = w.get<Body>(u)->type;
    if (UnitSprite* us = w.get<UnitSprite>(u)) us->id = randomModelForBody(rng, body);
    Name* nm = w.get<Name>(u);
    if (!nm) return;
    if (w.has<Commandable>(u)) {
        nm->text = randomUnitName(rng, body, takenNames);
        takenNames.push_back(nm->text);
    } else {
        nm->text = randomUnitName(rng, body, {});
    }
}

ObjectId spawnUnit(World& w, const Terrain& t, Rng& rng, int faction, float x, float z) {
    const ObjectId u = makeEntity(w, Transform{ x, t.heightAt(x, z), z }, faction);
    if (!factionPlayerControlled(faction)) {
        w.add(u, randomNpcLoot(rng));
        w.add(u, defaultDialogue());
        Behaviors* b = w.get<Behaviors>(u);
        if (faction == FACTION_FACTIONLESS) {
            b->rules = factionlessBehaviors();
        } else {
            b->rules = npcFactionBehaviors();
        }
    }
    std::vector<std::string> noNamesTaken;
    finishUnit(w, rng, u, noNamesTaken);
    return u;
}

Stats mobStats(MobKind kind, Rng& rng) {
    const MobInfo& m = mobInfo(kind);
    Stats s;
    auto level = [&](int base) { return std::clamp(base + rng.range(-1, 1), 1, MAX_LEVEL); };
    for (int i = 0; i < STAT_COUNT; ++i) {
        s.xp[i] = xpForLevel(mobHasStat((StatId)i) && statInfo((StatId)i).group == StatGroup::Attributes ? level(10) : 1);
    }
    s.xp[StatId::Strength] = xpForLevel(level(m.strength));
    s.xp[StatId::Dexterity] = xpForLevel(level(m.dexterity));
    s.xp[StatId::Vitality] = xpForLevel(level(m.vitality));
    s.xp[StatId::Agility] = xpForLevel(level(m.agility));
    s.xp[StatId::Melee] = xpForLevel(level(m.melee));
    s.xp[StatId::Block] = xpForLevel(level(m.block));
    if (m.maxStats) {
        for (int i = 0; i < STAT_COUNT; ++i) {
            if (mobHasStat((StatId)i)) s.xp[i] = xpForLevel(MAX_LEVEL);
        }
    }
    return s;
}

ObjectId spawnMob(World& w, const Terrain& t, Rng& rng, MobKind kind, int faction, float x, float z) {
    const MobInfo& m = mobInfo(kind);
    // A Factionless mob is a wild one: in its own kind's faction
    if (faction < 0 || faction == FACTION_FACTIONLESS) faction = mobFaction(kind);
    const ObjectId e = makeEntity(w, Transform{ x, t.heightAt(x, z), z }, faction);
    w.add(e, Mob{ kind });
    if (isBossMob(kind)) w.add(e, Coloration{ rng.range(0, 359) });
    Inventory inv;
    addToInventory(inv, m.meat, m.meatCount);
    addToInventory(inv, mobBones(kind), mobBoneCount(kind));
    w.add(e, inv);
    const Stats st = mobStats(kind, rng);
    w.add(e, st);
    Needs n;
    n.maxHp = n.hp = m.hp > 0 ? m.hp : maxHealthFor(st);
    w.add(e, n);
    Behaviors b;
    b.rules = mobBehaviors();
    // A mob of a faction that runs itself (a Red Faction bear) keeps near where it was put, as that faction's units patrol
    if (!isMobFaction(faction) && !factionPlayerControlled(faction)) {
        BehaviorRule patrol;
        patrol.kind = BehaviorKind::Patrol;
        addBehavior(b.rules, patrol);
    }
    if (isMobFaction(faction)) {
        BehaviorRule roam;
        roam.kind = m.patrolSpawn ? BehaviorKind::PatrolSpawn : BehaviorKind::Roam;
        addBehavior(b.rules, roam);
        if (m.patrolSpawn) w.add(e, SpawnHome{ x, z, false });
        const MobTemperament tm = mobTemperament(kind);
        b.passive = tm == MobTemperament::Avoidant || tm == MobTemperament::Passive;
    }
    w.add(e, b);
    w.add(e, Name{ lang(m.nameKey) });
    return e;
}

