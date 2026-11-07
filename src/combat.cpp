#include "combat.h"
#include "language.h"
#include <algorithm>
#include <cmath>
#include "equip.h"
#include "inventory.h"
#include "items.h"
#include "mobs.h"
#include "systems.h"
#include "terrain.h"
#include "useitem.h"

namespace {
const char* const ELIXIR = "Elixir of Might";

constexpr float DEXTERITY_SWING_PER_LEVEL = 0.02f;
constexpr float BLOCK_CHANCE_BASE = 0.05f;
constexpr float BLOCK_CHANCE_PER_LEVEL = 0.005f;
constexpr float BLOCK_CHANCE_CAP = 0.5f;
constexpr float ACCURACY_BASE = 0.60f;
constexpr float ACCURACY_PER_LEVEL = 0.01f;
constexpr float ACCURACY_CAP = 0.95f;
constexpr float DODGE_PER_LEVEL = 0.01f;
constexpr float DODGE_CAP = 0.40f;
constexpr float DEXTERITY_XP_PER_SWING = 1.0f;
constexpr float MELEE_XP_PER_DAMAGE = 1.0f;
constexpr float STRENGTH_XP_PER_DAMAGE = 0.5f;
constexpr float VITALITY_XP_PER_HP_LOST = 1.0f;
constexpr float BLOCK_XP_PER_BLOCK = 2.0f;
constexpr float AGILITY_XP_PER_DODGE = 2.0f;

Equipment wornOf(World& w, ObjectId e) {
    const Equipment* q = w.get<Equipment>(e);
    return q ? *q : Equipment{};
}

int hpOf(World& w, ObjectId e) {
    const Needs* n = w.get<Needs>(e);
    return n ? n->hp : 1;
}

float buffOf(World& w, ObjectId e) {
    const Combat* c = w.get<Combat>(e);
    return c ? c->buffAttack : 0.0f;
}

const Behaviors* behaviorsOf(World& w, ObjectId e) { return w.get<Behaviors>(e); }

} // namespace

CombatNumbers numbersOf(World& w, ObjectId e) {
    CombatNumbers n = deriveCombat(statsOf(w, e), wornOf(w, e), buffOf(w, e));
    if (const Mob* m = w.get<Mob>(e); m && mobInfo(m->kind).hp > 0) n.maxHp = mobInfo(m->kind).hp;
    if (const Mob* m = w.get<Mob>(e); m && mobInfo(m->kind).category == MobCategory::Boss) n.attack *= BOSS_DAMAGE_FACTOR;
    if (const Behaviors* b = behaviorsOf(w, e); b && b->block) {
        n.blockChance = std::min(BLOCK_MODE_CAP, std::max(n.blockChance, BLOCK_CHANCE_BASE) * BLOCK_MODE_FACTOR);
    }
    return n;
}

namespace {

int ammoSlotFor(World& w, ObjectId e, const RangedProfile& rp) {
    const Inventory* inv = w.get<Inventory>(e);
    if (!inv) return -1;
    for (int i = 0; i < INVENTORY_SLOTS; ++i) {
        const ItemDef* d = findItem(inv->slots[i].item);
        if (d && inv->slots[i].count > 0 && hasTag(*d, rp.ammoTag)) return i;
    }
    return -1;
}

const RangedProfile* rangedOf(World& w, ObjectId e) {
    const Equipment* q = w.get<Equipment>(e);
    return q ? rangedProfileOf(q->items[GearSlot::MainHand]) : nullptr;
}

bool targetable(World& w, ObjectId e) {
    return w.has<Entity>(e) && w.has<Transform>(e) && !outCold(w, e) && !w.has<PlayingDead>(e) && !inPrisonerCage(w, e);
}

float noticeFailChance(World& w, ObjectId p) { return isSneaking(w, p) ? SNEAK_NOTICE_FAIL : 0.0f; }
float noticeRangeOf(World& w, ObjectId p) { return AGGRO_RANGE * (isSneaking(w, p) ? SNEAK_NOTICE_RANGE : 1.0f); }
constexpr float MAX_NOTICE_RANGE = AGGRO_RANGE * (SNEAK_NOTICE_RANGE > 1.0f ? SNEAK_NOTICE_RANGE : 1.0f) + 0.01f;

int slotNamed(World& w, ObjectId e, const char* name) {
    const Inventory* inv = w.get<Inventory>(e);
    if (!inv) return -1;
    for (int i = 0; i < INVENTORY_SLOTS; ++i) {
        const ItemDef* d = findItem(inv->slots[i].item);
        if (d && inv->slots[i].count > 0 && d->name == name) return i;
    }
    return -1;
}

ObjectId pickInReach(World& w, const Terrain& t, ObjectId p, const std::vector<ObjectId>& hostiles) {
    ObjectId best = NO_OBJECT;
    int bestHp = 0;
    const float reach = reachOf(w, p);
    for (ObjectId h : hostiles) {
        if (!targetable(w, h) || flatGap(w, p, h) > reach) continue;
        const int hp = hpOf(w, h);
        if ((best == NO_OBJECT || hp < bestHp) && canSee(w, t, p, h)) {
            best = h;
            bestHp = hp;
        }
    }
    return best;
}

void onAttacked(World& w, ObjectId a, ObjectId d) {
    const int fa = factionOf(w, a), fd = factionOf(w, d);
    if (fa == fd) return;
    const bool atWar = factionsHostile(w, fa, fd) || unitsHostile(w, a, d);
    const Transform* td = w.get<Transform>(d);
    if (!td) return;
    if (w.has<PickupChain>(d)) {
        w.remove<PickupChain>(d);
        w.remove<PickupOrder>(d);
        w.remove<SoloMove>(d);
    }
    const std::vector<ObjectId> near = w.view<Entity, Transform, Faction>();
    auto sees = [&](ObjectId u) {
        const Transform* tu = w.get<Transform>(u);
        const float dx = tu->x - td->x, dz = tu->z - td->z;
        return dx * dx + dz * dz <= WITNESS_RANGE * WITNESS_RANGE;
    };
    if (!atWar && fd != FACTION_PLAYER) {
        for (ObjectId u : near) {
            if (factionOf(w, u) == fd && (u == d || sees(u)) && !outCold(w, u) && !inPrisonerCage(w, u)) provoke(w, u, fa);
        }
    }
    for (ObjectId u : near) {
        if (u == a || outCold(w, u) || w.has<Carried>(u) || w.has<PlayingDead>(u) || inPrisonerCage(w, u) || !w.has<Combat>(u)) continue;
        const int fu = factionOf(w, u);
        if (!factionsAllied(w, fu, fd) || factionsAllied(w, fu, fa) || !sees(u)) continue;
        if (isPassive(w, u) || isBlocking(w, u)) continue;
        Combat& c = *w.get<Combat>(u);
        if (w.has<Commandable>(u)) {
            const bool busy = w.has<AttackOrder>(u) || w.has<SoloMove>(u) || w.has<PickupOrder>(u) || w.has<PickupChain>(u) ||
                              w.has<TalkOrder>(u) || w.has<CarryOrder>(u) || w.has<LootOrder>(u) ||
                              w.has<PropOrder>(u) || w.has<Working>(u) ||
                              w.has<Occupying>(u) || w.has<Carrying>(u) || w.has<DefendOrder>(u);
            const ActiveSquad* sq = w.has<ActiveSquadMember>(u) ? w.get<ActiveSquad>(w.get<ActiveSquadMember>(u)->squad) : nullptr;
            if (busy || (sq && !sq->arrived) || c.target != NO_OBJECT) continue;
            leaveSquad(w, u);
            w.add(u, AttackOrder{ a });
            c.target = a;
        } else if (c.target == NO_OBJECT) {
            if (!factionsHostile(w, fu, fa)) provoke(w, u, fa);
            c.target = a;
        }
    }
}

void strike(World& w, Rng& rng, ObjectId a, ObjectId d) {
    CombatNumbers an = numbersOf(w, a);
    const CombatNumbers dn = numbersOf(w, d);
    const bool downed = hpOf(w, d) <= 0;
    bool shot = false;
    if (const RangedProfile* rp = an.ranged ? rangedOf(w, a) : nullptr) {
        const int slot = ammoSlotFor(w, a, *rp);
        if (slot >= 0) {
            Inventory* inv = w.get<Inventory>(a);
            const ItemDef* ammo = findItem(inv->slots[slot].item);
            an.attack += ammo ? (float)ammo->attackValue * rp->damageMul : 0.0f;
            if (--inv->slots[slot].count <= 0) inv->slots[slot] = ItemStack{};
            shot = true;
        } else {
            const Stats st = statsOf(w, a);
            an.attack = UNARMED_ATTACK + STRENGTH_ATTACK * (float)levelOf(st, StatId::Strength) + buffOf(w, a);
            an.accuracy = std::min(ACCURACY_CAP, ACCURACY_BASE + ACCURACY_PER_LEVEL * levelOf(st, StatId::Melee));
        }
    }
    const SwingResult r = resolveSwing(an, dn, downed, rng.unit(), rng.unit(), rng.unit());
    onAttacked(w, a, d);

    if (Combat* ca = w.get<Combat>(a)) ca->swingLeft = an.swingTime;
    grantXp(w, a, StatId::Dexterity, DEXTERITY_XP_PER_SWING, "MISC_REASON_SWINGING");

    const std::string attacker = nameOf(w, a);
    const std::string defender = nameOf(w, d);
    const Tint tint = tintFor(w, a, d);

    if (r.outcome == Outcome::Miss) {
        queueSoundFrom(w, Sfx::Miss, d);
        queueLog(w, langf(shot ? "MISC_LOG_SHOOTS_MISS" : "MISC_LOG_MISSES", { attacker, defender }), LogKind::Miss, npcOnly(w, a, d), tint);
        return;
    }
    if (r.outcome == Outcome::Dodge) {
        queueSoundFrom(w, Sfx::Miss, d);
        queueLog(w, langf("MISC_LOG_DODGES", { defender, attacker }), LogKind::Miss, npcOnly(w, a, d), tint);
        grantXp(w, d, StatId::Agility, AGILITY_XP_PER_DODGE, "MISC_REASON_DODGING");
        return;
    }
    queueSoundFrom(w, Sfx::Hit, d);
    if (downed) return;

    if (const SpawnHome* home = w.get<SpawnHome>(d); home && home->returning) {
        queueLog(w, langf("MISC_LOG_SHRUGS", { defender, attacker }), LogKind::Hit, npcOnly(w, a, d), tint);
        return;
    }
    Needs* nd = w.get<Needs>(d);
    const int before = nd ? nd->hp : 0;
    if (nd) {
        nd->hp = std::max(0, before - r.damage);
        if (nd->hp != before) nd->changed |= NEEDS_HP_CHANGED;
    }
    const int lost = nd ? before - nd->hp : r.damage;

    const char* const hitKey = shot ? (r.blocked ? "MISC_LOG_SHOOTS_BLOCKED" : "MISC_LOG_SHOOTS")
                                    : (r.blocked ? "MISC_LOG_HITS_BLOCKED" : "MISC_LOG_HITS");
    queueLog(w, langf(hitKey, { attacker, defender }), LogKind::Hit, npcOnly(w, a, d), tint);
    if (r.damage > 0) queueLog(w, langf("MISC_LOG_DAMAGE", { defender, std::to_string(r.damage) }), LogKind::Damage, npcOnly(w, a, d), tint);
    if (const Transform* td = w.get<Transform>(d)) {
        queueFloatingNumber(w, FloatingNumber{ td->x, td->y, td->z, r.damage, tint, 0.0f });
    }

    grantXp(w, a, shot ? StatId::Projectiles : StatId::Melee, MELEE_XP_PER_DAMAGE * lost, "MISC_REASON_DEALING");
    grantXp(w, a, StatId::Strength, STRENGTH_XP_PER_DAMAGE * lost, "MISC_REASON_DEALING");
    grantXp(w, d, StatId::Vitality, VITALITY_XP_PER_HP_LOST * lost, "MISC_REASON_TAKING");
    if (r.blocked) grantXp(w, d, StatId::Block, BLOCK_XP_PER_BLOCK, "MISC_REASON_BLOCKING");

    if (nd && nd->hp == 0) {
        queueLog(w, langf("MISC_IS_DOWN", { defender }), LogKind::Damage, npcOnly(w, a, d), tint);
        if (Combat* cd = w.get<Combat>(d)) cd->downedByFaction = factionOf(w, a);
        if (w.has<Mob>(a) && w.has<Mob>(d)) {
            knockOut(w, d);
            if (Inventory* inv = w.get<Inventory>(d)) {
                const int meat = mobInfo(w.get<Mob>(d)->kind).meat;
                for (ItemStack& st : inv->slots) {
                    if (st.item == meat) st = ItemStack{};
                }
            }
            finishDying(w, d);
            queueSoundFrom(w, Sfx::Dies, d);
        }
    }
}

void drinkForFight(World& w, ObjectId p, Combat& pc) {
    if (pc.buffLeft > 0.0f) return;
    const int slot = slotNamed(w, p, ELIXIR);
    const ItemDef* elixir = slot >= 0 ? findItem(w.get<Inventory>(p)->slots[slot].item) : nullptr;
    if (elixir && useStackInSlot(w, p, slot) == UseResult::Used) {
        queueLog(w, langf("MISC_DRINKS", { nameOf(w, p), itemDisplayName(*elixir) }), LogKind::Other, npcOnly(w, p), Tint::Gray);
    }
}
}

int maxHealthFor(const Stats& s) {
    return BASE_HP + HP_PER_VITALITY * levelOf(s, StatId::Vitality);
}

CombatNumbers deriveCombat(const Stats& s, const Equipment& worn, float buffAttack) {
    CombatNumbers n;
    n.maxHp = maxHealthFor(s);

    const ItemDef* weapon = findItem(worn.items[GearSlot::MainHand]);
    const float base = weapon ? (float)weapon->attackValue : UNARMED_ATTACK;
    const float weight = weapon ? weapon->weight : UNARMED_WEIGHT;
    n.attack = base + STRENGTH_ATTACK * (float)levelOf(s, StatId::Strength) + buffAttack;

    int attack = 0, defense = 0;
    gearTotals(worn, attack, defense);
    n.defense = (float)defense;
    n.mitigation = MITIGATION_K / (MITIGATION_K + n.defense);

    n.accuracy = std::min(ACCURACY_CAP, ACCURACY_BASE + ACCURACY_PER_LEVEL * levelOf(s, StatId::Melee));
    n.reach = ATTACK_REACH;
    if (const RangedProfile* rp = rangedProfileOf(worn.items[GearSlot::MainHand])) {
        n.ranged = true;
        n.reach = rp->range;
        n.accuracy = std::clamp(ACCURACY_BASE + ACCURACY_PER_LEVEL * levelOf(s, StatId::Projectiles) + rp->accuracyAdd,
                                0.05f, ACCURACY_CAP);
        n.attack = base * rp->damageMul + STRENGTH_ATTACK * (float)levelOf(s, StatId::Strength) + buffAttack;
    }
    n.dodge = std::min(DODGE_CAP, DODGE_PER_LEVEL * levelOf(s, StatId::Agility));

    const bool canBlock = worn.items[GearSlot::MainHand] != 0 || worn.items[GearSlot::OffHand] != 0;
    n.blockChance = canBlock
        ? std::min(BLOCK_CHANCE_CAP, BLOCK_CHANCE_BASE + BLOCK_CHANCE_PER_LEVEL * levelOf(s, StatId::Block))
        : 0.0f;

    const float dexFactor = 1.0f + DEXTERITY_SWING_PER_LEVEL * (levelOf(s, StatId::Dexterity) - 1);
    n.swingTime = (1.0f + weight / 5.0f) / dexFactor;
    if (const RangedProfile* rp = rangedProfileOf(worn.items[GearSlot::MainHand])) n.swingTime *= rp->shotTimeMul;

    return n;
}

SwingResult resolveSwing(const CombatNumbers& a, const CombatNumbers& d, bool defenderDown,
                         float landRoll, float blockRoll, float spreadRoll) {
    SwingResult r;
    const float hitChance = defenderDown ? a.accuracy : a.accuracy * (1.0f - d.dodge);
    if (landRoll < hitChance) r.outcome = Outcome::Hit;
    else if (!defenderDown && landRoll < a.accuracy) r.outcome = Outcome::Dodge;
    else r.outcome = Outcome::Miss;
    if (r.outcome != Outcome::Hit) return r;

    r.blocked = !defenderDown && blockRoll < d.blockChance;
    const float raw = a.attack * d.mitigation * (0.9f + 0.2f * spreadRoll) * (r.blocked ? BLOCK_DAMAGE : 1.0f);
    r.damage = std::max(1, (int)std::lround(raw));
    return r;
}

std::string nameOf(World& w, ObjectId e) {
    const Name* n = w.get<Name>(e);
    return (n && !n->text.empty()) ? n->text : std::string(lang("MISC_UNIT"));
}

bool isDowned(World& w, ObjectId e) {
    const Needs* n = w.get<Needs>(e);
    return n && n->hp <= 0;
}

bool inPrisonerCage(World& w, ObjectId e) {
    const Occupying* oc = w.get<Occupying>(e);
    const Prop* p = oc ? w.get<Prop>(oc->prop) : nullptr;
    return p && propInfo(p->kind).cage;
}

void ensureCombat(World& w, ObjectId e) {
    if (!w.has<Combat>(e)) w.add(e, Combat{});
}

const RangedProfile* rangedProfileOf(int item) {
    static const RangedProfile shortBow{ 8.0f, 0.10f, 1.0f, 1.0f, "arrow" };
    static const RangedProfile longBow{ 14.0f, -0.10f, 1.0f, 1.0f, "arrow" };
    static const RangedProfile crossbow{ 8.0f, 0.10f, 0.5f, 0.5f, "bolt" };
    const ItemDef* d = findItem(item);
    if (!d || d->typeId != "weapon") return nullptr;
    if (hasTag(*d, "bow_short")) return &shortBow;
    if (hasTag(*d, "bow_long")) return &longBow;
    if (hasTag(*d, "crossbow")) return &crossbow;
    return nullptr;
}

bool isPassive(World& w, ObjectId e) {
    const Behaviors* b = w.get<Behaviors>(e);
    return b && b->passive;
}
bool isBlocking(World& w, ObjectId e) {
    const Behaviors* b = w.get<Behaviors>(e);
    return b && b->block;
}
bool isSneaking(World& w, ObjectId e) {
    const Behaviors* b = w.get<Behaviors>(e);
    return b && b->sneak;
}

float reachOf(World& w, ObjectId e) {
    if (const RangedProfile* rp = rangedOf(w, e); rp && ammoSlotFor(w, e, *rp) >= 0) return rp->range;
    return ATTACK_REACH;
}

Equipment randomHostileGear(Rng& rng) {
    Equipment worn;
    for (int slot = 0; slot < GEAR_SLOTS; ++slot) {
        if (rng.range(0, 1) == 0) continue;
        std::vector<int> choices;
        std::string why;
        for (const ItemDef& d : itemDatabase()) {
            if (canEquip(worn, d.id, slot, why)) choices.push_back(d.id);
        }
        if (!choices.empty()) worn.items[slot] = choices[rng.range(0, (int)choices.size() - 1)];
    }
    return worn;
}

bool canSee(World& w, const Terrain& t, ObjectId a, ObjectId b) {
    const Transform* ta = w.get<Transform>(a);
    const Transform* tb = w.get<Transform>(b);
    return ta && tb && t.sightClear(ta->x, ta->y, ta->z, tb->x, tb->y, tb->z);
}

static std::string elixirName() {
    for (const ItemDef& d : itemDatabase()) {
        if (d.name == ELIXIR) return itemDisplayName(d);
    }
    return ELIXIR;
}

void combatSystem(World& w, const Terrain& t, Rng& rng, float dt) {
    const std::vector<ObjectId> everyone = w.view<Entity, Transform>();
    for (ObjectId e : everyone) ensureCombat(w, e);
    for (ObjectId e : everyone) {
        if (w.has<Corpse>(e)) continue;
        Combat& c = *w.get<Combat>(e);
        const CombatNumbers n = numbersOf(w, e);

        if (Needs* nd = w.get<Needs>(e)) {
            const int hpBefore = nd->hp, maxBefore = nd->maxHp;
            nd->maxHp = n.maxHp;
            nd->hp = std::min(nd->hp, n.maxHp);
            if (nd->hp != hpBefore || nd->maxHp != maxBefore) nd->changed |= NEEDS_HP_CHANGED;
        }

        if (c.buffLeft > 0.0f) {
            c.buffLeft -= dt;
            if (c.buffLeft <= 0.0f) {
                c.buffLeft = 0.0f;
                c.buffAttack = 0.0f;
                queueLog(w, langf("MISC_LOG_ELIXIR_ENDS", { nameOf(w, e), elixirName() }), LogKind::Other, npcOnly(w, e), Tint::Gray);
            }
        }
        c.swingLeft = std::max(0.0f, c.swingLeft - dt);
    }

    const std::vector<ObjectId> players = w.view<Entity, Transform, Commandable>();
    const std::vector<ObjectId> hostiles = w.view<Entity, Transform, Hostile>();
    auto standing = [&](ObjectId e) {
        return hpOf(w, e) > 0 && !outCold(w, e) && !w.has<Carried>(e) && !w.has<PlayingDead>(e) && !inPrisonerCage(w, e);
    };
    for (ObjectId e : everyone) {
        if (!inPrisonerCage(w, e)) continue;
        if (Combat* c = w.get<Combat>(e)) c->target = NO_OBJECT;
        w.remove<AttackOrder>(e);
    }

    for (ObjectId h : everyone) {
        if (w.has<Commandable>(h) || !standing(h) || !w.has<Combat>(h)) continue;
        Combat& hc = *w.get<Combat>(h);
        if (hc.target != NO_OBJECT) {
            if (targetable(w, hc.target) && flatGap(w, h, hc.target) <= LEASH_RANGE && unitsHostile(w, h, hc.target)) continue;
            hc.target = NO_OBJECT;
        }
        if (isPassive(w, h)) continue;
        const Transform* th = w.get<Transform>(h);
        for (ObjectId p : everyone) {
            const Transform* tp = w.get<Transform>(p);
            if (!th || !tp) continue;
            const float gx = tp->x - th->x, gz = tp->z - th->z;
            if (gx * gx + gz * gz > MAX_NOTICE_RANGE * MAX_NOTICE_RANGE) continue;
            if (p == h || !standing(p) || !unitsHostile(w, h, p)) continue;
            if (flatGap(w, h, p) > noticeRangeOf(w, p)) continue;
            if (rng.unit() < dt * (1.0f - noticeFailChance(w, p)) && canSee(w, t, h, p)) {
                hc.target = p;
                if (w.has<Commandable>(p)) queueLog(w, langf("MISC_LOG_NOTICES", { nameOf(w, h), nameOf(w, p) }), LogKind::Seen, false, Tint::Gray);
                drinkForFight(w, h, hc);
                break;
            }
        }
    }

    for (ObjectId p : players) {
        if (!standing(p)) continue;
        Combat& pc = *w.get<Combat>(p);
        if (const AttackOrder* ao = w.get<AttackOrder>(p)) {
            if (targetable(w, ao->target)) {
                if (pc.target != ao->target) {
                    pc.target = ao->target;
                    drinkForFight(w, p, pc);
                }
                continue;
            }
            w.remove<AttackOrder>(p);
            pc.target = NO_OBJECT;
        }
        if (pc.target != NO_OBJECT && targetable(w, pc.target) && flatGap(w, p, pc.target) <= reachOf(w, p) &&
            canSee(w, t, p, pc.target)) {
            continue;
        }
        if (isPassive(w, p) || isBlocking(w, p)) {
            pc.target = NO_OBJECT;
            continue;
        }
        pc.target = pickInReach(w, t, p, hostiles);
        if (pc.target != NO_OBJECT) drinkForFight(w, p, pc);
    }

    for (ObjectId e : everyone) {
        if (!w.has<Combat>(e) || !w.has<Transform>(e) || !standing(e)) continue;
        Combat& c = *w.get<Combat>(e);
        if (c.target == NO_OBJECT || c.swingLeft > 0.0f) continue;
        if (isBlocking(w, e)) continue;
        const ObjectId foe = c.target;
        if (!targetable(w, foe) || flatGap(w, e, foe) > reachOf(w, e) || !canSee(w, t, e, foe)) continue;
        strike(w, rng, e, foe);
    }
}
