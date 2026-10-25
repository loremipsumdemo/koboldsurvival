#include "combat.h"
#include <algorithm>
#include <cmath>
#include "equip.h"
#include "inventory.h"
#include "items.h"
#include "useitem.h"

namespace {

// Potions a unit drinks or eats in a fight: the share of its max health below which it uses one, and
// how long the use takes. Listed in the order they are tried.
struct AutoUse {
    const char* item;
    float below;
    float seconds;
};
const AutoUse AUTO_USE[] = {
    { "Health Potion", 0.35f, 1.0f },
    { "Bread",         0.50f, 2.0f },
};
const char* const ELIXIR = "Elixir of Might";

constexpr float STAMINA_DRAIN = 2.0f;   // stamina lost per second walking at a full load (scaled by the load)
constexpr float STAMINA_REGEN = 4.0f;   // stamina regained per second while standing still
constexpr float STAMINA_PER_ENDURANCE = 2.0f;
constexpr float STAMINA_BASE = 40.0f;

constexpr float ATHLETICS_PER_LEVEL = 0.005f;   // walk speed gained per Athletics level, as a share
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

Stats statsOf(World& w, Entity e) {
    const Stats* s = w.get<Stats>(e);
    return s ? *s : defaultStats();
}

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

CombatNumbers numbersOf(World& w, Entity e) {
    return deriveCombat(statsOf(w, e), wornOf(w, e), buffOf(w, e));
}

bool targetable(World& w, Entity e) {
    return w.has<Unit>(e) && w.has<Transform>(e);
}

float distanceBetween(World& w, Entity a, Entity b) {
    const Transform* ta = w.get<Transform>(a);
    const Transform* tb = w.get<Transform>(b);
    if (!ta || !tb) return 1e9f;
    const float dx = ta->x - tb->x, dz = ta->z - tb->z;
    return std::sqrt(dx * dx + dz * dz);
}

// Share of hostile notices that fail against this player unit
float noticeFailChance(World& w, Entity p) {
    return std::min(NOTICE_FAIL_CAP, NOTICE_FAIL_PER_LEVEL * (levelOf(statsOf(w, p), STAT_STEALTH) - 1));
}

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
    for (Entity h : hostiles) {
        if (!targetable(w, h) || distanceBetween(w, p, h) > ATTACK_REACH) continue;
        const int hp = hpOf(w, h);
        if (best == NULL_ENTITY || hp < bestHp) {
            best = h;
            bestHp = hp;
        }
    }
    return best;
}

// One swing: rolls, the outcome, the log lines, damage numbers and the XP it gives
void strike(World& w, Rng& rng, Entity a, Entity d, CombatLog& log, std::vector<FloatingNumber>& floats) {
    const CombatNumbers an = numbersOf(w, a);
    const CombatNumbers dn = numbersOf(w, d);
    const bool downed = hpOf(w, d) <= 0;
    const SwingResult r = resolveSwing(an, dn, downed, rng.unit(), rng.unit(), rng.unit());

    if (Combat* ca = w.get<Combat>(a)) ca->swingLeft = an.swingTime;
    grantXp(w, a, STAT_DEXTERITY, DEXTERITY_XP_PER_SWING);

    const std::string attacker = nameOf(w, a);
    const std::string defender = nameOf(w, d);
    const Tint tint = tintFor(w, a, d);

    if (r.outcome == Outcome::Miss) {
        ++log.misses;
        log.add(attacker + " misses " + defender, tint);
        return;
    }
    if (r.outcome == Outcome::Dodge) {
        ++log.misses;
        log.add(defender + " dodges " + attacker + "'s swing", tint);
        grantXp(w, d, STAT_AGILITY, AGILITY_XP_PER_DODGE);
        return;
    }

    // The swing landed. A hit on a downed unit kills it.
    ++log.hits;
    if (downed) {
        log.add(attacker + " slays " + defender, tint);
        removeUnit(w, d);
        return;
    }

    Needs* nd = w.get<Needs>(d);
    const int before = nd ? nd->hp : 0;
    if (nd) nd->hp = std::max(0, before - r.damage);
    const int lost = nd ? before - nd->hp : r.damage;

    log.add(attacker + " hits " + defender + " for " + std::to_string(r.damage) +
            (r.blocked ? " (blocked)" : ""), tint);
    if (const Transform* td = w.get<Transform>(d)) {
        floats.push_back(FloatingNumber{ td->x, td->y, td->z, r.damage, tint, 0.0f });
    }

    grantXp(w, a, STAT_MELEE, MELEE_XP_PER_DAMAGE * lost);
    grantXp(w, a, STAT_STRENGTH, STRENGTH_XP_PER_DAMAGE * lost);
    grantXp(w, d, STAT_VITALITY, VITALITY_XP_PER_HP_LOST * lost);
    if (r.blocked) grantXp(w, d, STAT_BLOCK, BLOCK_XP_PER_BLOCK);

    if (nd && nd->hp == 0) log.add(defender + " is down", tint);
}

// Drinks or eats a potion when a player unit is hurt. True if one was used, which takes its turn.
bool tryAutoUse(World& w, Entity e, Combat& c, CombatLog& log) {
    const CombatNumbers n = numbersOf(w, e);
    for (const AutoUse& a : AUTO_USE) {
        const int before = hpOf(w, e);
        if (before >= a.below * n.maxHp) continue;
        const int slot = slotNamed(w, e, a.item);
        if (slot < 0) continue;
        if (useStackInSlot(w, e, slot) != UseResult::Used) continue;
        c.swingLeft = a.seconds;
        log.add(nameOf(w, e) + " uses " + a.item + ": HP " + std::to_string(before) + " -> " +
                std::to_string(hpOf(w, e)) + " of " + std::to_string(n.maxHp));
        return true;
    }
    return false;
}

// A player unit that has just picked a fight drinks an elixir first, if it has one and none is active
void drinkForFight(World& w, Entity p, Combat& pc, CombatLog& log) {
    if (pc.buffLeft > 0.0f) return;
    const int slot = slotNamed(w, p, ELIXIR);
    if (slot >= 0 && useStackInSlot(w, p, slot) == UseResult::Used) {
        log.add(nameOf(w, p) + " drinks " + ELIXIR);
    }
}

} // namespace

// ---- Formulas ----

int maxHealthFor(const Stats& s) {
    return BASE_HP + HP_PER_VITALITY * levelOf(s, STAT_VITALITY);
}

CombatNumbers deriveCombat(const Stats& s, const Equipment& worn, float buffAttack) {
    CombatNumbers n;
    n.maxHp = maxHealthFor(s);

    const ItemDef* weapon = findItem(worn.items[GEAR_MAIN_HAND]);
    const float base = weapon ? (float)weapon->attackValue : UNARMED_ATTACK;
    const float weight = weapon ? weapon->weight : UNARMED_WEIGHT;
    n.attack = base + STRENGTH_ATTACK * (float)levelOf(s, STAT_STRENGTH) + buffAttack;

    int attack = 0, defense = 0;
    gearTotals(worn, attack, defense);
    n.defense = (float)defense;
    n.mitigation = MITIGATION_K / (MITIGATION_K + n.defense);

    n.accuracy = std::min(ACCURACY_CAP, ACCURACY_BASE + ACCURACY_PER_LEVEL * levelOf(s, STAT_MELEE));
    n.dodge = std::min(DODGE_CAP, DODGE_PER_LEVEL * levelOf(s, STAT_AGILITY));

    const bool canBlock = worn.items[GEAR_MAIN_HAND] != 0 || worn.items[GEAR_OFF_HAND] != 0;
    n.blockChance = canBlock
        ? std::min(BLOCK_CHANCE_CAP, BLOCK_CHANCE_BASE + BLOCK_CHANCE_PER_LEVEL * levelOf(s, STAT_BLOCK))
        : 0.0f;

    const float dexFactor = 1.0f + DEXTERITY_SWING_PER_LEVEL * (levelOf(s, STAT_DEXTERITY) - 1);
    n.swingTime = (1.0f + weight / 5.0f) / dexFactor;

    n.maxStamina = STAMINA_BASE + STAMINA_PER_ENDURANCE * levelOf(s, STAT_ENDURANCE);
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

// ---- Encumbrance and movement ----

float wornWeight(const Equipment& worn) {
    float total = 0.0f;
    for (int item : worn.items) {
        if (const ItemDef* d = findItem(item)) total += d->weight;
    }
    return total;
}

float carriedWeightOf(World& w, Entity e) {
    float total = 0.0f;
    if (const Inventory* inv = w.get<Inventory>(e)) total += carriedWeight(*inv);
    if (const Equipment* worn = w.get<Equipment>(e)) total += wornWeight(*worn);
    return total;
}

float encumbranceFor(float carried, const Stats& s) {
    const float limit = carryLimit(s);
    if (limit <= 0.0f) return 1.0f;
    return std::clamp(carried / limit, 0.0f, 1.0f);
}

float encumbranceOf(World& w, Entity e) {
    return encumbranceFor(carriedWeightOf(w, e), statsOf(w, e));
}

float walkSpeedOf(World& w, Entity e, bool running) {
    const Unit* u = w.get<Unit>(e);
    if (!u || isDowned(w, e)) return 0.0f;
    if (!w.has<Commandable>(e)) return u->speed;   // hostiles walk at their own pace

    const Stats s = statsOf(w, e);
    const Combat* c = w.get<Combat>(e);
    const float stamina = c ? c->stamina : 1.0f;
    const float load = encumbranceOf(w, e);
    float speed = u->speed * (1.0f + ATHLETICS_PER_LEVEL * (levelOf(s, STAT_ATHLETICS) - 1)) * (1.0f - load);
    if (running) speed *= 1.0f + RUN_SPEED_BONUS + RUN_ATHLETICS_PER_LEVEL * (levelOf(s, STAT_ATHLETICS) - 1);
    if (stamina <= 0.0f) speed *= 0.5f;
    return std::max(0.0f, speed);
}

float strengthXpWhileWalking(float encumbrance, float seconds) {
    if (encumbrance < ENCUMBRANCE_XP_FROM || seconds <= 0.0f) return 0.0f;
    const float share = (encumbrance - ENCUMBRANCE_XP_FROM) / (1.0f - ENCUMBRANCE_XP_FROM);
    return 0.5f * share * seconds;
}

// ---- Experience ----

void grantXp(World& w, Entity e, int stat, float amount) {
    if (amount <= 0.0f || stat < 0 || stat >= STAT_COUNT) return;
    if (!w.has<Commandable>(e)) return;
    Stats* st = w.get<Stats>(e);
    if (!st) return;

    XpBuffer& buf = w.has<XpBuffer>(e) ? *w.get<XpBuffer>(e) : w.add(e, XpBuffer{});
    const float total = buf.frac[stat] + amount;
    const float whole = std::floor(total);
    buf.frac[stat] = total - whole;
    if (whole >= 1.0f) addXp(*st, stat, (int)whole);
}

// ---- Log and damage numbers ----

Tint tintFor(World& w, Entity attacker, Entity target) {
    if (w.has<Commandable>(attacker) && w.has<Hostile>(target)) return Tint::Green;
    if (w.has<Hostile>(attacker) && w.has<Commandable>(target)) return Tint::Red;
    return Tint::Gray;
}

void CombatLog::add(const std::string& text, Tint tint) {
    lines.push_back(LogLine{ text, tint });
    if (lines.size() > LOG_LIMIT) {
        lines.erase(lines.begin(), lines.begin() + (lines.size() - LOG_LIMIT));
    }
}

// ---- Units and the fight ----

std::string nameOf(World& w, Entity e) {
    const Name* n = w.get<Name>(e);
    return (n && !n->text.empty()) ? n->text : std::string("Unit");
}

bool isDowned(World& w, Entity e) {
    const Needs* n = w.get<Needs>(e);
    return n && n->hp <= 0;
}

void ensureCombat(World& w, Entity e) {
    if (!w.has<Combat>(e)) w.add(e, Combat{});
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

void removeUnit(World& w, Entity e) {
    if (const SquadMember* sm = w.get<SquadMember>(e)) {
        if (Squad* sq = w.get<Squad>(sm->squad)) {
            if (sq->size > 0) --sq->size;
        }
    }
    w.destroy(e);
}

void combatSystem(World& w, Rng& rng, float dt, CombatLog& log, std::vector<FloatingNumber>& floats) {
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
            nd->maxHp = n.maxHp;
            nd->hp = std::min(nd->hp, n.maxHp);
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
                if (w.has<Commandable>(e)) log.add(nameOf(w, e) + "'s Elixir of Might wears off");
            }
        }
        c.swingLeft = std::max(0.0f, c.swingLeft - dt);
    }

    const std::vector<Entity> players = w.view<Unit, Transform, Commandable>();
    const std::vector<Entity> hostiles = w.view<Unit, Transform, Hostile>();
    auto standing = [&](Entity e) { return hpOf(w, e) > 0; };

    // Hostiles notice player units in range, and chase until the target is gone or out of the leash
    for (Entity h : hostiles) {
        if (!standing(h)) continue;
        Combat& hc = *w.get<Combat>(h);
        if (hc.target != NULL_ENTITY) {
            if (targetable(w, hc.target) && distanceBetween(w, h, hc.target) <= LEASH_RANGE) continue;
            hc.target = NULL_ENTITY;
        }
        for (Entity p : players) {
            if (!standing(p) || distanceBetween(w, h, p) > AGGRO_RANGE) continue;
            if (rng.unit() < dt * (1.0f - noticeFailChance(w, p))) {
                hc.target = p;
                log.add(nameOf(w, h) + " notices " + nameOf(w, p));
                break;
            }
        }
    }

    // Spotting and stealth XP for each standing player unit
    for (Entity p : players) {
        if (!standing(p)) continue;
        Combat& pc = *w.get<Combat>(p);
        const Stats st = statsOf(w, p);
        const float spotRange = AGGRO_RANGE * (1.0f + PERCEPTION_RANGE_PER_LEVEL * (levelOf(st, STAT_PERCEPTION) - 1));
        bool unnoticedInRange = false;
        for (Entity h : hostiles) {
            if (!standing(h)) continue;
            const float d = distanceBetween(w, p, h);
            if (d <= spotRange && std::find(pc.spotted.begin(), pc.spotted.end(), h) == pc.spotted.end()) {
                pc.spotted.push_back(h);
                grantXp(w, p, STAT_PERCEPTION, PERCEPTION_XP_PER_SPOT);
            }
            if (d <= AGGRO_RANGE && w.get<Combat>(h)->target != p) unnoticedInRange = true;
        }
        if (unnoticedInRange) grantXp(w, p, STAT_STEALTH, STEALTH_XP_PER_SECOND * dt);
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
        if (pc.target != NULL_ENTITY && targetable(w, pc.target) && distanceBetween(w, p, pc.target) <= ATTACK_REACH) {
            continue;
        }
        pc.target = pickInReach(w, p, hostiles);
        if (pc.target != NULL_ENTITY) drinkForFight(w, p, pc, log);
    }

    // Swings. Each unit whose swing is ready, standing, and in reach of its target swings, or first
    // uses a potion if it is hurt. Units removed by a kill are skipped by the existence check.
    for (Entity e : everyone) {
        if (!w.has<Combat>(e) || !w.has<Transform>(e) || !standing(e)) continue;
        Combat& c = *w.get<Combat>(e);
        if (c.target == NULL_ENTITY || c.swingLeft > 0.0f) continue;
        const Entity t = c.target;
        if (!targetable(w, t) || distanceBetween(w, e, t) > ATTACK_REACH) continue;
        if (w.has<Commandable>(e) && tryAutoUse(w, e, c, log)) continue;
        strike(w, rng, e, t, log, floats);
    }
}
