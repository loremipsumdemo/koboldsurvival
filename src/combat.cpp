#include "combat.h"
#include "language.h"
#include <algorithm>
#include <cmath>
#include "equip.h"
#include "inventory.h"
#include "items.h"
#include "mobs.h"
#include "useitem.h"

namespace {

// Healing and food are not fixed here any more: each unit's behavior rules decide when it uses them (need_behaviors.h).
const char* const ELIXIR = "Elixir of Might";

constexpr float STAMINA_DRAIN = 2.0f;   // stamina lost per second walking at a full load (scaled by the load)
constexpr float STAMINA_REGEN = 4.0f;   // stamina regained per second while standing still
constexpr float STAMINA_PER_ENDURANCE = 2.0f;
constexpr float STAMINA_BASE = 40.0f;

constexpr float DEXTERITY_SWING_PER_LEVEL = 0.02f;
constexpr float BLOCK_CHANCE_BASE = 0.05f;
constexpr float BLOCK_CHANCE_PER_LEVEL = 0.005f;
constexpr float BLOCK_CHANCE_CAP = 0.5f;
constexpr float ACCURACY_BASE = 0.60f;
constexpr float ACCURACY_PER_LEVEL = 0.01f;
constexpr float ACCURACY_CAP = 0.95f;
constexpr float DODGE_PER_LEVEL = 0.01f;
constexpr float DODGE_CAP = 0.40f;
constexpr float NOTICE_FAIL_PER_LEVEL = 0.01f;   // Stealth: share of hostile notices that fail
constexpr float NOTICE_FAIL_CAP = 0.40f;
constexpr float PERCEPTION_RANGE_PER_LEVEL = 0.005f;
constexpr float PERCEPTION_XP_PER_SPOT = 2.0f;
constexpr float STEALTH_XP_PER_SECOND = 1.0f;
constexpr float DEXTERITY_XP_PER_SWING = 1.0f;
constexpr float MELEE_XP_PER_DAMAGE = 1.0f;
constexpr float STRENGTH_XP_PER_DAMAGE = 0.5f;
constexpr float VITALITY_XP_PER_HP_LOST = 1.0f;
constexpr float BLOCK_XP_PER_BLOCK = 2.0f;
constexpr float AGILITY_XP_PER_DODGE = 2.0f;

Equipment wornOf(World& w, Entity e) {
    const Equipment* q = w.get<Equipment>(e);
    return q ? *q : Equipment{};
}

// Health of a unit. A unit with no Needs still counts as standing.
int hpOf(World& w, Entity e) {
    const Needs* n = w.get<Needs>(e);
    return n ? n->hp : 1;
}

float buffOf(World& w, Entity e) {
    const Combat* c = w.get<Combat>(e);
    return c ? c->buffAttack : 0.0f;
}

const Behaviors* behaviorsOf(World& w, Entity e) { return w.get<Behaviors>(e); }

// The fight numbers for a unit, with its Block mode counted: it blocks more often, even empty-handed
CombatNumbers numbersOf(World& w, Entity e) {
    CombatNumbers n = deriveCombat(statsOf(w, e), wornOf(w, e), buffOf(w, e));
    // A kind of mob with its own health (the Gorlicorn) has that, not the usual from Vitality
    if (const Mob* m = w.get<Mob>(e); m && mobInfo(m->kind).hp > 0) n.maxHp = mobInfo(m->kind).hp;
    if (const Behaviors* b = behaviorsOf(w, e); b && b->block) {
        n.blockChance = std::min(BLOCK_MODE_CAP, std::max(n.blockChance, BLOCK_CHANCE_BASE) * BLOCK_MODE_FACTOR);
    }
    return n;
}

// The inventory slot of the first ammo a ranged weapon can shoot, or -1
int ammoSlotFor(World& w, Entity e, const RangedProfile& rp) {
    const Inventory* inv = w.get<Inventory>(e);
    if (!inv) return -1;
    for (int i = 0; i < INVENTORY_SLOTS; ++i) {
        const ItemDef* d = findItem(inv->slots[i].item);
        if (d && inv->slots[i].count > 0 && hasTag(*d, rp.ammoTag)) return i;
    }
    return -1;
}

// The ranged weapon in a unit's main hand, or nullptr
const RangedProfile* rangedOf(World& w, Entity e) {
    const Equipment* q = w.get<Equipment>(e);
    return q ? rangedProfileOf(q->items[GearSlot::MainHand]) : nullptr;
}

// An Unconscious unit (or a corpse) is out of the fight: nobody swings at it
bool targetable(World& w, Entity e) {
    return w.has<Unit>(e) && w.has<Transform>(e) && !w.has<Unconscious>(e) && !w.has<PlayingDead>(e);
}

// Share of notices that fail against this unit. Sneaking, more fail, and Stealth counts twice.
float noticeFailChance(World& w, Entity p) {
    const float stealth = NOTICE_FAIL_PER_LEVEL * (levelOf(statsOf(w, p), StatId::Stealth) - 1);
    if (isSneaking(w, p)) return std::min(SNEAK_FAIL_CAP, SNEAK_FAIL_BASE + 2.0f * stealth);
    return std::min(NOTICE_FAIL_CAP, stealth);
}
// How close another unit must be to notice this one
float noticeRangeOf(World& w, Entity p) { return AGGRO_RANGE * (isSneaking(w, p) ? SNEAK_NOTICE_RANGE : 1.0f); }
// The most noticeRangeOf can give
constexpr float MAX_NOTICE_RANGE = AGGRO_RANGE * (SNEAK_NOTICE_RANGE > 1.0f ? SNEAK_NOTICE_RANGE : 1.0f) + 0.01f;

// Inventory slot holding an item with this name, or -1
int slotNamed(World& w, Entity e, const char* name) {
    const Inventory* inv = w.get<Inventory>(e);
    if (!inv) return -1;
    for (int i = 0; i < INVENTORY_SLOTS; ++i) {
        const ItemDef* d = findItem(inv->slots[i].item);
        if (d && inv->slots[i].count > 0 && d->name == name) return i;
    }
    return -1;
}

// The hostile a player unit fights next: the one in reach with the least health. A downed one has 0,
// so it is finished off before any other.
Entity pickInReach(World& w, Entity p, const std::vector<Entity>& hostiles) {
    Entity best = NULL_ENTITY;
    int bestHp = 0;
    const float reach = reachOf(w, p);
    for (Entity h : hostiles) {
        if (!targetable(w, h) || flatGap(w, p, h) > reach) continue;
        const int hp = hpOf(w, h);
        if (best == NULL_ENTITY || hp < bestHp) {
            best = h;
            bestHp = hp;
        }
    }
    return best;
}

// An attack seen: a unit d attacked by a. If a's faction was not already at war with d (hostile, or d already
// provoked by it), d and every other unit of d's faction that sees it (within WITNESS_RANGE of d) are provoked by a's
// faction: they fight back against a and its whole faction. Then every unit of d's faction or an ally's that is
// close by and free (not passive, not already fighting, and for your units not busy with an order) goes after a. Your
// units do this for each other: a squad unit attacked brings the others near it.
void onAttacked(World& w, Entity a, Entity d) {
    const int fa = factionOf(w, a), fd = factionOf(w, d);
    if (fa == fd) return;
    const bool atWar = factionsHostile(fa, fd) || unitsHostile(w, a, d);
    const Transform* td = w.get<Transform>(d);
    if (!td) return;
    // An attack ends a pickup chain (PickupChain) the unit was on, so it is free to fight back below
    if (w.has<PickupChain>(d)) {
        w.remove<PickupChain>(d);
        w.remove<PickupOrder>(d);
        w.remove<SoloMove>(d);
    }
    const std::vector<Entity> near = w.view<Unit, Transform, Faction>();
    auto sees = [&](Entity u) {
        const Transform* tu = w.get<Transform>(u);
        const float dx = tu->x - td->x, dz = tu->z - td->z;
        return dx * dx + dz * dz <= WITNESS_RANGE * WITNESS_RANGE;
    };
    if (!atWar && fd != FACTION_PLAYER) {
        for (Entity u : near) {
            if (factionOf(w, u) == fd && (u == d || sees(u)) && !w.has<Unconscious>(u)) provoke(w, u, fa);
        }
    }
    // Helpers: the defender's faction and its allies, never an ally of the attacker
    for (Entity u : near) {
        if (u == a || w.has<Unconscious>(u) || w.has<Carried>(u) || w.has<PlayingDead>(u) || !w.has<Combat>(u)) continue;
        const int fu = factionOf(w, u);
        if (!factionsAllied(fu, fd) || factionsAllied(fu, fa) || !sees(u)) continue;
        if (isPassive(w, u) || isBlocking(w, u)) continue;
        Combat& c = *w.get<Combat>(u);
        if (w.has<Commandable>(u)) {
            // Your units step in only when they are free: no order of their own under way
            const bool busy = w.has<AttackOrder>(u) || w.has<SoloMove>(u) || w.has<PickupOrder>(u) || w.has<PickupChain>(u) ||
                              w.has<TalkOrder>(u) || w.has<CarryOrder>(u) || w.has<LootOrder>(u) ||
                              w.has<PropOrder>(u) || w.has<Working>(u) ||
                              w.has<Occupying>(u) || w.has<Carrying>(u) || w.has<DefendOrder>(u);
            const ActiveSquad* sq = w.has<ActiveSquadMember>(u) ? w.get<ActiveSquad>(w.get<ActiveSquadMember>(u)->squad) : nullptr;
            if (busy || (sq && !sq->arrived) || c.target != NULL_ENTITY) continue;
            leaveSquad(w, u);
            w.add(u, AttackOrder{ a });
            c.target = a;
        } else if (c.target == NULL_ENTITY) {
            // An NPC that steps in fights the attacker's faction too, until it loses sight of it (Provoked)
            if (!factionsHostile(fu, fa)) provoke(w, u, fa);
            c.target = a;
        }
    }
}

// One swing, or one shot from a ranged weapon: rolls, the outcome, the log lines, damage numbers and the XP it gives
void strike(World& w, Rng& rng, Entity a, Entity d, GameLog& log, std::vector<FloatingNumber>& floats) {
    CombatNumbers an = numbersOf(w, a);
    const CombatNumbers dn = numbersOf(w, d);
    const bool downed = hpOf(w, d) <= 0;
    // A ranged weapon shoots one piece of its ammo, which adds its attack. Without ammo it fights hand to hand.
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
    grantXp(w, a, StatId::Dexterity, DEXTERITY_XP_PER_SWING, lang("MISC_REASON_SWINGING"));

    const std::string attacker = nameOf(w, a);
    const std::string defender = nameOf(w, d);
    const Tint tint = tintFor(w, a, d);

    if (r.outcome == Outcome::Miss) {
        ++log.misses;
        queueSoundFrom(w, Sfx::Miss, d);
        log.add(attacker + (shot ? " shoots at " : " misses ") + defender + (shot ? " and misses" : ""), tint, LogKind::Miss, npcOnly(w, a, d));
        return;
    }
    if (r.outcome == Outcome::Dodge) {
        ++log.misses;
        queueSoundFrom(w, Sfx::Miss, d);
        log.add(defender + " dodges " + attacker + "'s swing", tint, LogKind::Miss, npcOnly(w, a, d));
        grantXp(w, d, StatId::Agility, AGILITY_XP_PER_DODGE, lang("MISC_REASON_DODGING"));
        return;
    }

    // The swing landed. A unit already at 0 health takes no more: it is Unconscious (downedSystem in systems.h).
    ++log.hits;
    queueSoundFrom(w, Sfx::Hit, d);
    if (downed) return;

    // A mob walking home after giving up a chase (Patrol Spawn) takes no damage until it is there
    if (const SpawnHome* home = w.get<SpawnHome>(d); home && home->returning) {
        log.add(defender + " shrugs off " + attacker + "'s hit", tint, LogKind::Hit, npcOnly(w, a, d));
        return;
    }
    Needs* nd = w.get<Needs>(d);
    const int before = nd ? nd->hp : 0;
    if (nd) {
        nd->hp = std::max(0, before - r.damage);
        if (nd->hp != before) nd->changed |= NEEDS_HP_CHANGED;
    }
    const int lost = nd ? before - nd->hp : r.damage;

    log.add(attacker + (shot ? " shoots " : " hits ") + defender + (r.blocked ? " (blocked)" : ""), tint, LogKind::Hit, npcOnly(w, a, d));
    if (r.damage > 0) log.add(defender + " takes " + std::to_string(r.damage) + " damage", tint, LogKind::Damage, npcOnly(w, a, d));
    if (const Transform* td = w.get<Transform>(d)) {
        floats.push_back(FloatingNumber{ td->x, td->y, td->z, r.damage, tint, 0.0f });
    }

    grantXp(w, a, shot ? StatId::Projectiles : StatId::Melee, MELEE_XP_PER_DAMAGE * lost, lang("MISC_REASON_DEALING"));
    grantXp(w, a, StatId::Strength, STRENGTH_XP_PER_DAMAGE * lost, lang("MISC_REASON_DEALING"));
    grantXp(w, d, StatId::Vitality, VITALITY_XP_PER_HP_LOST * lost, lang("MISC_REASON_TAKING"));
    if (r.blocked) grantXp(w, d, StatId::Block, BLOCK_XP_PER_BLOCK, lang("MISC_REASON_BLOCKING"));

    if (nd && nd->hp == 0) {
        log.add(langf("MISC_IS_DOWN", { defender }), tint, LogKind::Damage, npcOnly(w, a, d));
        // Whose hit it was, for the reputation when it is knocked out
        if (Combat* cd = w.get<Combat>(d)) cd->downedByFaction = factionOf(w, a);
    }
}

// A unit that has just picked a fight drinks an elixir first, if it has one and none is active
void drinkForFight(World& w, Entity p, Combat& pc, GameLog& log) {
    if (pc.buffLeft > 0.0f) return;
    const int slot = slotNamed(w, p, ELIXIR);
    const ItemDef* elixir = slot >= 0 ? findItem(w.get<Inventory>(p)->slots[slot].item) : nullptr;
    if (elixir && useStackInSlot(w, p, slot) == UseResult::Used) {
        log.add(langf("MISC_DRINKS", { nameOf(w, p), itemDisplayName(*elixir) }), Tint::Gray, LogKind::Other, npcOnly(w, p));
    }
}

} // namespace

// ---- Formulas ----

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
    // A ranged weapon: Projectiles for the aim, its own accuracy, damage and speed, and its range
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

    n.maxStamina = STAMINA_BASE + STAMINA_PER_ENDURANCE * levelOf(s, StatId::Endurance);
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

// ---- Units and the fight ----

std::string nameOf(World& w, Entity e) {
    const Name* n = w.get<Name>(e);
    return (n && !n->text.empty()) ? n->text : std::string(lang("MISC_UNIT"));
}

bool isDowned(World& w, Entity e) {
    const Needs* n = w.get<Needs>(e);
    return n && n->hp <= 0;
}

void ensureCombat(World& w, Entity e) {
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

void forgetSpotted(World& w) {
    for (Entity u : w.view<Combat>()) {
        std::vector<Entity>& s = w.get<Combat>(u)->spotted;
        s.clear();
        s.shrink_to_fit();
    }
}

bool isPassive(World& w, Entity e) {
    const Behaviors* b = w.get<Behaviors>(e);
    return b && b->passive;
}
bool isBlocking(World& w, Entity e) {
    const Behaviors* b = w.get<Behaviors>(e);
    return b && b->block;
}
bool isSneaking(World& w, Entity e) {
    const Behaviors* b = w.get<Behaviors>(e);
    return b && b->sneak;
}

float reachOf(World& w, Entity e) {
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

void combatSystem(World& w, Rng& rng, float dt, GameLog& log, std::vector<FloatingNumber>& floats) {
    // Damage numbers rise and fade, then go
    for (FloatingNumber& f : floats) f.age += dt;
    floats.erase(std::remove_if(floats.begin(), floats.end(),
                                [](const FloatingNumber& f) { return f.age >= FLOAT_LIFE; }),
                 floats.end());

    // Timers, stamina, buffs and max health for every unit
    const std::vector<Entity> everyone = w.view<Unit, Transform>();
    for (Entity e : everyone) ensureCombat(w, e);
    for (Entity e : everyone) {
        Combat& c = *w.get<Combat>(e);
        const CombatNumbers n = numbersOf(w, e);

        if (Needs* nd = w.get<Needs>(e)) {
            const int hpBefore = nd->hp, maxBefore = nd->maxHp;
            nd->maxHp = n.maxHp;
            nd->hp = std::min(nd->hp, n.maxHp);
            // Only a real change counts, since this runs every frame
            if (nd->hp != hpBefore || nd->maxHp != maxBefore) nd->changed |= NEEDS_HP_CHANGED;
        }

        if (c.stamina < 0.0f) c.stamina = n.maxStamina;   // first frame: start full
        if (c.moved > 0.0f) c.stamina -= STAMINA_DRAIN * c.encumbrance * dt;
        else c.stamina += STAMINA_REGEN * dt;
        c.stamina = std::clamp(c.stamina, 0.0f, n.maxStamina);

        if (c.buffLeft > 0.0f) {
            c.buffLeft -= dt;
            if (c.buffLeft <= 0.0f) {
                c.buffLeft = 0.0f;
                c.buffAttack = 0.0f;
                log.add(nameOf(w, e) + "'s Elixir of Might wears off", Tint::Gray, LogKind::Other, npcOnly(w, e));
            }
        }
        c.swingLeft = std::max(0.0f, c.swingLeft - dt);
    }

    const std::vector<Entity> players = w.view<Unit, Transform, Commandable>();
    const std::vector<Entity> hostiles = w.view<Unit, Transform, Hostile>();
    // Out cold, carried or playing dead, a unit does not fight
    auto standing = [&](Entity e) { return hpOf(w, e) > 0 && !w.has<Unconscious>(e) && !w.has<Carried>(e) && !w.has<PlayingDead>(e); };

    // Every unit that is not yours notices the units it fights in range (its enemies by faction, or by provocation),
    // and chases until the target is gone or out of the leash. A passive one starts no fight, but keeps one it has.
    // Only notices of your units are written to the log.
    for (Entity h : everyone) {
        if (w.has<Commandable>(h) || !standing(h) || !w.has<Combat>(h)) continue;
        Combat& hc = *w.get<Combat>(h);
        if (hc.target != NULL_ENTITY) {
            if (targetable(w, hc.target) && flatGap(w, h, hc.target) <= LEASH_RANGE && unitsHostile(w, h, hc.target)) continue;
            hc.target = NULL_ENTITY;
        }
        if (isPassive(w, h)) continue;
        const Transform* th = w.get<Transform>(h);
        for (Entity p : everyone) {
            // Beyond the widest notice range there is nothing to check (noticeRangeOf is never more than this)
            const Transform* tp = w.get<Transform>(p);
            if (!th || !tp) continue;
            const float gx = tp->x - th->x, gz = tp->z - th->z;
            if (gx * gx + gz * gz > MAX_NOTICE_RANGE * MAX_NOTICE_RANGE) continue;
            if (p == h || !standing(p) || !unitsHostile(w, h, p)) continue;
            if (flatGap(w, h, p) > noticeRangeOf(w, p)) continue;
            if (rng.unit() < dt * (1.0f - noticeFailChance(w, p))) {
                hc.target = p;
                if (w.has<Commandable>(p)) log.add(nameOf(w, h) + " notices " + nameOf(w, p), Tint::Gray, LogKind::Seen);
                drinkForFight(w, h, hc, log);
                break;
            }
        }
    }

    // A hostile is aware of your squad while it is fighting one of your units; one that is not (it has no target, or
    // fights another faction) has not noticed you
    auto awareOfYou = [&](Entity h) {
        const Combat* c = w.get<Combat>(h);
        return c && c->target != NULL_ENTITY && w.has<Commandable>(c->target);
    };
    // Spotting and stealth XP, for your units only, against the units hostile to you (the Hostile marker, which follows
    // the reputations and provocations): Perception for each one first seen this in-game hour, and Stealth for standing close to one
    // that has not noticed you
    for (Entity p : everyone) {
        if (!standing(p) || !w.has<Combat>(p)) continue;
        Combat& pc = *w.get<Combat>(p);
        const Stats st = statsOf(w, p);
        const float spotRange = AGGRO_RANGE * (1.0f + PERCEPTION_RANGE_PER_LEVEL * (levelOf(st, StatId::Perception) - 1));
        if (!w.has<Commandable>(p)) continue;   // only your units gain XP
        const std::vector<Entity>& foes = hostiles;
        bool unnoticedInRange = false;
        for (Entity h : foes) {
            if (!standing(h)) continue;
            const float d = flatGap(w, p, h);
            if (d <= spotRange && std::find(pc.spotted.begin(), pc.spotted.end(), h) == pc.spotted.end()) {
                pc.spotted.push_back(h);
                grantXp(w, p, StatId::Perception, PERCEPTION_XP_PER_SPOT, lang("MISC_REASON_SPOTTING"));
            }
            if (d <= STEALTH_RANGE && !awareOfYou(h)) unnoticedInRange = true;
        }
        if (unnoticedInRange) grantXp(w, p, StatId::Stealth, STEALTH_XP_PER_SECOND * dt, lang("MISC_REASON_UNNOTICED"));
    }

    // Targets. A player unit with an attack order keeps its ordered red unit until it is gone, and picks no
    // other. The rest pick from the red units in reach and keep that pick while it stays in reach.
    // A player unit that picks a fight drinks an elixir first, if it has one and none is active.
    for (Entity p : players) {
        if (!standing(p)) continue;
        Combat& pc = *w.get<Combat>(p);
        if (const AttackOrder* ao = w.get<AttackOrder>(p)) {
            if (targetable(w, ao->target)) {
                if (pc.target != ao->target) {
                    pc.target = ao->target;
                    drinkForFight(w, p, pc, log);
                }
                continue;
            }
            w.remove<AttackOrder>(p);   // the red unit is gone, so the order is done
            pc.target = NULL_ENTITY;
        }
        if (pc.target != NULL_ENTITY && targetable(w, pc.target) && flatGap(w, p, pc.target) <= reachOf(w, p)) {
            continue;
        }
        // A passive unit, or one only blocking, picks no fight of its own
        if (isPassive(w, p) || isBlocking(w, p)) {
            pc.target = NULL_ENTITY;
            continue;
        }
        pc.target = pickInReach(w, p, hostiles);
        if (pc.target != NULL_ENTITY) drinkForFight(w, p, pc, log);
    }

    // Swings. Each unit whose swing is ready, standing, and in reach of its target swings. Units removed by a
    // kill are skipped by the existence check.
    for (Entity e : everyone) {
        if (!w.has<Combat>(e) || !w.has<Transform>(e) || !standing(e)) continue;
        Combat& c = *w.get<Combat>(e);
        if (c.target == NULL_ENTITY || c.swingLeft > 0.0f) continue;
        if (isBlocking(w, e)) continue;   // Block mode: it only blocks, and never swings
        const Entity t = c.target;
        if (!targetable(w, t) || flatGap(w, e, t) > reachOf(w, e)) continue;
        strike(w, rng, e, t, log, floats);
    }
}
