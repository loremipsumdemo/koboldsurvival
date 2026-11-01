#include "combat.h"
#include "language.h"
#include <algorithm>
#include <cmath>
#include "equip.h"
#include "inventory.h"
#include "items.h"
#include "useitem.h"

namespace {

// Healing and food are not fixed here any more: each unit's behavior rules decide when it uses them (behavior.h).
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

// An Unconscious unit (or a corpse) is out of the fight: nobody swings at it
bool targetable(World& w, Entity e) {
    return w.has<Unit>(e) && w.has<Transform>(e) && !w.has<Unconscious>(e);
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
    grantXp(w, a, STAT_DEXTERITY, DEXTERITY_XP_PER_SWING, lang("MISC_REASON_SWINGING"));

    const std::string attacker = nameOf(w, a);
    const std::string defender = nameOf(w, d);
    const Tint tint = tintFor(w, a, d);

    if (r.outcome == Outcome::Miss) {
        ++log.misses;
        log.add(attacker + " misses " + defender, tint, LogKind::Miss);
        return;
    }
    if (r.outcome == Outcome::Dodge) {
        ++log.misses;
        log.add(defender + " dodges " + attacker + "'s swing", tint, LogKind::Miss);
        grantXp(w, d, STAT_AGILITY, AGILITY_XP_PER_DODGE, lang("MISC_REASON_DODGING"));
        return;
    }

    // The swing landed. A unit already at 0 health takes no more: it is Unconscious (downedSystem in systems.h).
    ++log.hits;
    if (downed) return;

    Needs* nd = w.get<Needs>(d);
    const int before = nd ? nd->hp : 0;
    if (nd) {
        nd->hp = std::max(0, before - r.damage);
        if (nd->hp != before) nd->changed |= NEEDS_HP_CHANGED;
    }
    const int lost = nd ? before - nd->hp : r.damage;

    log.add(attacker + " hits " + defender + (r.blocked ? " (blocked)" : ""), tint, LogKind::Hit);
    if (r.damage > 0) log.add(defender + " takes " + std::to_string(r.damage) + " damage", tint, LogKind::Damage);
    if (const Transform* td = w.get<Transform>(d)) {
        floats.push_back(FloatingNumber{ td->x, td->y, td->z, r.damage, tint, 0.0f });
    }

    grantXp(w, a, STAT_MELEE, MELEE_XP_PER_DAMAGE * lost, lang("MISC_REASON_DEALING"));
    grantXp(w, a, STAT_STRENGTH, STRENGTH_XP_PER_DAMAGE * lost, lang("MISC_REASON_DEALING"));
    grantXp(w, d, STAT_VITALITY, VITALITY_XP_PER_HP_LOST * lost, lang("MISC_REASON_TAKING"));
    if (r.blocked) grantXp(w, d, STAT_BLOCK, BLOCK_XP_PER_BLOCK, lang("MISC_REASON_BLOCKING"));

    if (nd && nd->hp == 0) log.add(langf("MISC_IS_DOWN", { defender }), tint, LogKind::Damage);
}

// A unit that has just picked a fight drinks an elixir first, if it has one and none is active
void drinkForFight(World& w, Entity p, Combat& pc, CombatLog& log) {
    if (pc.buffLeft > 0.0f) return;
    const int slot = slotNamed(w, p, ELIXIR);
    const ItemDef* elixir = slot >= 0 ? findItem(w.get<Inventory>(p)->slots[slot].item) : nullptr;
    if (elixir && useStackInSlot(w, p, slot) == UseResult::Used) {
        log.add(langf("MISC_DRINKS", { nameOf(w, p), itemDisplayName(*elixir) }));
    }
}

} // namespace

Stats statsOf(World& w, Entity e) {
    const Stats* s = w.get<Stats>(e);
    return s ? *s : defaultStats();
}

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
    // Running is the player's pace for its squads, so a red unit never runs
    if (!w.has<Commandable>(e)) running = false;

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

namespace {
std::vector<LogLine> gQueued;   // game messages waiting for the next frame's log
}

namespace {
std::vector<TradeLabel> gQueuedLabels;
std::vector<Sfx> gQueuedSounds;
}

void queueSound(Sfx s) {
    if (std::find(gQueuedSounds.begin(), gQueuedSounds.end(), s) == gQueuedSounds.end()) gQueuedSounds.push_back(s);
}

std::vector<Sfx> takeQueuedSounds() {
    std::vector<Sfx> out;
    out.swap(gQueuedSounds);
    return out;
}

void discardQueuedSounds() {
    gQueuedSounds.clear();
}

void queueUnitLabel(Entity unit, const std::string& text, bool red) {
    if (unit == NULL_ENTITY || text.empty()) return;
    gQueuedLabels.push_back(TradeLabel{ unit, text, 0.0f, red });
}

std::vector<TradeLabel> takeQueuedLabels() {
    std::vector<TradeLabel> out;
    out.swap(gQueuedLabels);
    return out;
}

void discardQueuedLabels() {
    gQueuedLabels.clear();
}

std::string itemGainLabel(int count, const std::string& itemName) {
    if (count <= 1) return langf("MISC_ITEM_GAINED", { itemName });
    return langf("MISC_ITEMS_GAINED", { std::to_string(count), itemName });
}

std::string itemLossLabel(int count, const std::string& itemName) {
    if (count <= 1) return langf("MISC_ITEM_LOST", { itemName });
    return langf("MISC_ITEMS_LOST", { std::to_string(count), itemName });
}

namespace {
std::string labelItemName(int item) {
    const ItemDef* def = findItem(item);
    return def ? itemDisplayName(*def) : std::string(lang("MISC_AN_ITEM"));
}
}

std::string itemGainLabelFor(int count, int item) { return itemGainLabel(count, labelItemName(item)); }
std::string itemLossLabelFor(int count, int item) { return itemLossLabel(count, labelItemName(item)); }

void queueLog(const std::string& text, LogKind kind) {
    gQueued.push_back(LogLine{ text, Tint::Gray, kind });
}

void CombatLog::say(const std::string& text) {
    spoken.push_back(LogLine{ text, Tint::Gray, LogKind::Other });
    if (spoken.size() > LOG_LIMIT) spoken.erase(spoken.begin());
}

void flushQueuedLog(CombatLog& log) {
    for (const LogLine& ln : gQueued) log.add(ln.text, ln.tint, ln.kind);
    gQueued.clear();
}

void discardQueuedLog() {
    gQueued.clear();
}

std::vector<const LogLine*> infoLines(const CombatLog& log, int tab, const LogFilter& f) {
    std::vector<const LogLine*> out;
    if (tab == 0) {
        for (const LogLine& ln : log.spoken) out.push_back(&ln);
        return out;
    }
    for (const LogLine& ln : log.lines) {
        if (ln.kind == LogKind::Other || f.on[(int)ln.kind]) out.push_back(&ln);
    }
    // The Game tab shows only the newest LOG_LIMIT of what passes the filter
    if (out.size() > LOG_LIMIT) out.erase(out.begin(), out.begin() + (out.size() - LOG_LIMIT));
    return out;
}

void grantXp(World& w, Entity e, int stat, float amount, const std::string& why) {
    if (amount <= 0.0f || stat < 0 || stat >= STAT_COUNT) return;
    Stats* st = w.get<Stats>(e);
    if (!st) return;

    XpBuffer& buf = w.has<XpBuffer>(e) ? *w.get<XpBuffer>(e) : w.add(e, XpBuffer{});
    const float total = buf.frac[stat] + amount;
    const float whole = std::floor(total);
    buf.frac[stat] = total - whole;
    if (whole < 1.0f) return;
    const int before = levelOf(*st, stat);
    addXp(*st, stat, (int)whole);
    if (levelOf(*st, stat) > before) queueSound(Sfx::LevelUp);
    if (!why.empty()) {
        queueLog(langf("MISC_GAINS_XP", { nameOf(w, e), std::to_string((int)whole), statName(stat), why }), LogKind::Xp);
    }
}

// ---- Log and damage numbers ----

Tint tintFor(World& w, Entity attacker, Entity target) {
    if (w.has<Commandable>(attacker) && w.has<Hostile>(target)) return Tint::Green;
    if (w.has<Hostile>(attacker) && w.has<Commandable>(target)) return Tint::Red;
    return Tint::Gray;
}

void CombatLog::add(const std::string& text, Tint tint, LogKind kind) {
    lines.push_back(LogLine{ text, tint, kind });
    // Each kind keeps its own newest LOG_LIMIT, so a flood of one kind never pushes out the others
    if (++kindCount[(int)kind] > LOG_LIMIT) {
        for (auto it = lines.begin(); it != lines.end(); ++it) {
            if (it->kind == kind) {
                lines.erase(it);
                break;
            }
        }
        --kindCount[(int)kind];
    }
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
                log.add(nameOf(w, e) + "'s Elixir of Might wears off");
            }
        }
        c.swingLeft = std::max(0.0f, c.swingLeft - dt);
    }

    const std::vector<Entity> players = w.view<Unit, Transform, Commandable>();
    const std::vector<Entity> hostiles = w.view<Unit, Transform, Hostile>();
    auto standing = [&](Entity e) { return hpOf(w, e) > 0 && !w.has<Unconscious>(e); };

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
                log.add(nameOf(w, h) + " notices " + nameOf(w, p), Tint::Gray, LogKind::Seen);
                drinkForFight(w, h, hc, log);
                break;
            }
        }
    }

    // Spotting and stealth XP for each standing unit, against the opposing side: player units spot red units, and
    // red units spot player units
    for (Entity p : everyone) {
        if (!standing(p) || !w.has<Combat>(p)) continue;
        Combat& pc = *w.get<Combat>(p);
        const Stats st = statsOf(w, p);
        const float spotRange = AGGRO_RANGE * (1.0f + PERCEPTION_RANGE_PER_LEVEL * (levelOf(st, STAT_PERCEPTION) - 1));
        const std::vector<Entity>& foes = w.has<Commandable>(p) ? hostiles : players;
        bool unnoticedInRange = false;
        for (Entity h : foes) {
            if (!standing(h)) continue;
            const float d = distanceBetween(w, p, h);
            if (d <= spotRange && std::find(pc.spotted.begin(), pc.spotted.end(), h) == pc.spotted.end()) {
                pc.spotted.push_back(h);
                grantXp(w, p, STAT_PERCEPTION, PERCEPTION_XP_PER_SPOT, lang("MISC_REASON_SPOTTING"));
            }
            if (d <= STEALTH_RANGE && w.get<Combat>(h)->target != p) unnoticedInRange = true;
        }
        if (unnoticedInRange) grantXp(w, p, STAT_STEALTH, STEALTH_XP_PER_SECOND * dt, lang("MISC_REASON_UNNOTICED"));
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

    // Swings. Each unit whose swing is ready, standing, and in reach of its target swings. Units removed by a
    // kill are skipped by the existence check.
    for (Entity e : everyone) {
        if (!w.has<Combat>(e) || !w.has<Transform>(e) || !standing(e)) continue;
        Combat& c = *w.get<Combat>(e);
        if (c.target == NULL_ENTITY || c.swingLeft > 0.0f) continue;
        const Entity t = c.target;
        if (!targetable(w, t) || distanceBetween(w, e, t) > ATTACK_REACH) continue;
        strike(w, rng, e, t, log, floats);
    }
}
