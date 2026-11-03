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

const Behaviors* behaviorsOf(World& w, Entity e) { return w.get<Behaviors>(e); }

// The fight numbers for a unit, with its Block mode counted: it blocks more often, even empty-handed
CombatNumbers numbersOf(World& w, Entity e) {
    CombatNumbers n = deriveCombat(statsOf(w, e), wornOf(w, e), buffOf(w, e));
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
    return q ? rangedProfileOf(q->items[GEAR_MAIN_HAND]) : nullptr;
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

// Share of notices that fail against this unit. Sneaking, more fail, and Stealth counts twice.
float noticeFailChance(World& w, Entity p) {
    const float stealth = NOTICE_FAIL_PER_LEVEL * (levelOf(statsOf(w, p), STAT_STEALTH) - 1);
    if (isSneaking(w, p)) return std::min(SNEAK_FAIL_CAP, SNEAK_FAIL_BASE + 2.0f * stealth);
    return std::min(NOTICE_FAIL_CAP, stealth);
}
// How close another unit must be to notice this one
float noticeRangeOf(World& w, Entity p) { return AGGRO_RANGE * (isSneaking(w, p) ? SNEAK_NOTICE_RANGE : 1.0f); }

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
        if (!targetable(w, h) || distanceBetween(w, p, h) > reach) continue;
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
        if (u == a || w.has<Unconscious>(u) || w.has<Carried>(u) || !w.has<Combat>(u)) continue;
        const int fu = factionOf(w, u);
        if (!factionsAllied(fu, fd) || factionsAllied(fu, fa) || !sees(u)) continue;
        if (isPassive(w, u) || isBlocking(w, u)) continue;
        Combat& c = *w.get<Combat>(u);
        if (w.has<Commandable>(u)) {
            // Your units step in only when they are free: no order of their own under way
            const bool busy = w.has<AttackOrder>(u) || w.has<SoloMove>(u) || w.has<PickupOrder>(u) ||
                              w.has<TalkOrder>(u) || w.has<CarryOrder>(u) || w.has<LootOrder>(u) ||
                              w.has<PutDownOrder>(u) || w.has<PropOrder>(u) || w.has<Working>(u) ||
                              w.has<Occupying>(u) || w.has<Carrying>(u) || w.has<DefendOrder>(u);
            const Squad* sq = w.has<SquadMember>(u) ? w.get<Squad>(w.get<SquadMember>(u)->squad) : nullptr;
            if (busy || (sq && !sq->arrived) || c.target != NULL_ENTITY) continue;
            if (Squad* s = w.has<SquadMember>(u) ? w.get<Squad>(w.get<SquadMember>(u)->squad) : nullptr; s && s->size > 0) --s->size;
            w.remove<SquadMember>(u);
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
void strike(World& w, Rng& rng, Entity a, Entity d, CombatLog& log, std::vector<FloatingNumber>& floats) {
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
            an.attack = UNARMED_ATTACK + STRENGTH_ATTACK * (float)levelOf(st, STAT_STRENGTH) + buffOf(w, a);
            an.accuracy = std::min(ACCURACY_CAP, ACCURACY_BASE + ACCURACY_PER_LEVEL * levelOf(st, STAT_MELEE));
        }
    }
    const SwingResult r = resolveSwing(an, dn, downed, rng.unit(), rng.unit(), rng.unit());
    onAttacked(w, a, d);

    if (Combat* ca = w.get<Combat>(a)) ca->swingLeft = an.swingTime;
    grantXp(w, a, STAT_DEXTERITY, DEXTERITY_XP_PER_SWING, lang("MISC_REASON_SWINGING"));

    const std::string attacker = nameOf(w, a);
    const std::string defender = nameOf(w, d);
    const Tint tint = tintFor(w, a, d);

    if (r.outcome == Outcome::Miss) {
        ++log.misses;
        log.add(attacker + (shot ? " shoots at " : " misses ") + defender + (shot ? " and misses" : ""), tint, LogKind::Miss);
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

    log.add(attacker + (shot ? " shoots " : " hits ") + defender + (r.blocked ? " (blocked)" : ""), tint, LogKind::Hit);
    if (r.damage > 0) log.add(defender + " takes " + std::to_string(r.damage) + " damage", tint, LogKind::Damage);
    if (const Transform* td = w.get<Transform>(d)) {
        floats.push_back(FloatingNumber{ td->x, td->y, td->z, r.damage, tint, 0.0f });
    }

    grantXp(w, a, shot ? STAT_PROJECTILES : STAT_MELEE, MELEE_XP_PER_DAMAGE * lost, lang("MISC_REASON_DEALING"));
    grantXp(w, a, STAT_STRENGTH, STRENGTH_XP_PER_DAMAGE * lost, lang("MISC_REASON_DEALING"));
    grantXp(w, d, STAT_VITALITY, VITALITY_XP_PER_HP_LOST * lost, lang("MISC_REASON_TAKING"));
    if (r.blocked) grantXp(w, d, STAT_BLOCK, BLOCK_XP_PER_BLOCK, lang("MISC_REASON_BLOCKING"));

    if (nd && nd->hp == 0) {
        log.add(langf("MISC_IS_DOWN", { defender }), tint, LogKind::Damage);
        // Whose hit it was, for the reputation when it is knocked out
        if (Combat* cd = w.get<Combat>(d)) cd->downedByFaction = factionOf(w, a);
    }
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
    n.reach = ATTACK_REACH;
    // A ranged weapon: Projectiles for the aim, its own accuracy, damage and speed, and its range
    if (const RangedProfile* rp = rangedProfileOf(worn.items[GEAR_MAIN_HAND])) {
        n.ranged = true;
        n.reach = rp->range;
        n.accuracy = std::clamp(ACCURACY_BASE + ACCURACY_PER_LEVEL * levelOf(s, STAT_PROJECTILES) + rp->accuracyAdd,
                                0.05f, ACCURACY_CAP);
        n.attack = base * rp->damageMul + STRENGTH_ATTACK * (float)levelOf(s, STAT_STRENGTH) + buffAttack;
    }
    n.dodge = std::min(DODGE_CAP, DODGE_PER_LEVEL * levelOf(s, STAT_AGILITY));

    const bool canBlock = worn.items[GEAR_MAIN_HAND] != 0 || worn.items[GEAR_OFF_HAND] != 0;
    n.blockChance = canBlock
        ? std::min(BLOCK_CHANCE_CAP, BLOCK_CHANCE_BASE + BLOCK_CHANCE_PER_LEVEL * levelOf(s, STAT_BLOCK))
        : 0.0f;

    const float dexFactor = 1.0f + DEXTERITY_SWING_PER_LEVEL * (levelOf(s, STAT_DEXTERITY) - 1);
    n.swingTime = (1.0f + weight / 5.0f) / dexFactor;
    if (const RangedProfile* rp = rangedProfileOf(worn.items[GEAR_MAIN_HAND])) n.swingTime *= rp->shotTimeMul;

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
    if (isSneaking(w, e)) {
        running = false;          // a sneaking unit never runs
        speed *= SNEAK_SPEED;
    }
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
    if (tab == 2) return out;   // the Time tab has no lines
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
    if (levelOf(*st, stat) > before && w.has<Commandable>(e)) queueSound(Sfx::LevelUp);   // only yours are heard
    if (!why.empty()) {
        const LogKind kind = stat == STAT_ATHLETICS ? LogKind::XpAthletics
                           : stat == STAT_ENDURANCE ? LogKind::XpEndurance
                                                    : LogKind::Xp;
        queueLog(langf("MISC_GAINS_XP", { nameOf(w, e), std::to_string((int)whole), statName(stat), why }), kind);
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

int factionOf(World& w, Entity e) {
    if (const Faction* f = w.get<Faction>(e)) return f->id;
    // A unit with no faction goes by its side: yours, a hostile (Red) or a neutral (Green)
    if (w.has<Commandable>(e)) return FACTION_PLAYER;
    return w.has<Hostile>(e) ? FACTION_RED : FACTION_GREEN;
}

namespace {
bool provokedBy(World& w, Entity e, int faction) {
    const Provoked* p = w.get<Provoked>(e);
    if (!p) return false;
    for (const Provoked::Entry& en : p->against) {
        if (en.faction == faction && en.secondsLeft > 0.0f) return true;
    }
    return false;
}
} // namespace

bool unitsHostile(World& w, Entity a, Entity b) {
    if (a == b) return false;
    const int fa = factionOf(w, a), fb = factionOf(w, b);
    if (fa == fb) return false;
    return factionsHostile(fa, fb) || provokedBy(w, a, fb) || provokedBy(w, b, fa);
}

bool hostileToPlayer(World& w, Entity e) {
    if (w.has<Commandable>(e)) return false;
    const int f = factionOf(w, e);
    return f != FACTION_PLAYER && (factionsHostile(f, FACTION_PLAYER) || provokedBy(w, e, FACTION_PLAYER));
}

void forgetSpotted(World& w) {
    for (Entity u : w.view<Combat>()) {
        std::vector<Entity>& s = w.get<Combat>(u)->spotted;
        s.clear();
        s.shrink_to_fit();
    }
}

void provoke(World& w, Entity unit, int faction) {
    if (!w.has<Unit>(unit) || factionOf(w, unit) == faction) return;
    Provoked& p = w.has<Provoked>(unit) ? *w.get<Provoked>(unit) : w.add(unit, Provoked{});
    for (Provoked::Entry& en : p.against) {
        if (en.faction == faction) {
            en.secondsLeft = PROVOKED_SECONDS;
            return;
        }
    }
    p.against.push_back(Provoked::Entry{ faction, PROVOKED_SECONDS });
}

void factionSystem(World& w, float dt) {
    const std::vector<Entity> units = w.view<Unit, Transform>();
    for (Entity u : w.view<Provoked>()) {
        Provoked* p = w.get<Provoked>(u);
        const Transform* tu = w.get<Transform>(u);
        // Out cold, it sees nothing; awake, any standing unit of the faction within sight keeps the grudge fresh
        const bool awake = tu && !w.has<Unconscious>(u);
        for (Provoked::Entry& en : p->against) {
            bool seen = false;
            for (Entity o : units) {
                if (!awake || o == u || factionOf(w, o) != en.faction || w.has<Unconscious>(o)) continue;
                const Transform* to = w.get<Transform>(o);
                const float dx = to->x - tu->x, dz = to->z - tu->z;
                if (dx * dx + dz * dz <= PROVOKED_SIGHT_RANGE * PROVOKED_SIGHT_RANGE) {
                    seen = true;
                    break;
                }
            }
            en.secondsLeft = seen ? PROVOKED_SECONDS : en.secondsLeft - dt;
        }
        p->against.erase(std::remove_if(p->against.begin(), p->against.end(),
                                        [](const Provoked::Entry& en) { return en.secondsLeft <= 0.0f; }),
                         p->against.end());
        if (p->against.empty()) w.remove<Provoked>(u);
    }
    for (Entity u : w.view<Unit>()) {
        if (w.has<Commandable>(u)) {
            w.remove<Hostile>(u);
            w.remove<Neutral>(u);
            continue;
        }
        const bool hostile = hostileToPlayer(w, u);
        if (hostile && !w.has<Hostile>(u)) {
            const bool wasNeutral = w.has<Neutral>(u);
            w.remove<Neutral>(u);
            w.add(u, Hostile{});
            if (wasNeutral && !w.has<Unconscious>(u)) {
                queueSound(Sfx::TurnHostile);
                queueLog(langf("MISC_TURNS_HOSTILE", { nameOf(w, u) }), LogKind::Seen);
            }
        } else if (!hostile && !w.has<Neutral>(u)) {
            const bool wasHostile = w.has<Hostile>(u);
            w.remove<Hostile>(u);
            w.add(u, Neutral{});
            // It lost sight of your units long enough to calm down: it drops its fight with them
            if (wasHostile && !w.has<Unconscious>(u)) {
                queueLog(langf("MISC_CALMS_DOWN", { nameOf(w, u) }), LogKind::Seen);
                if (Combat* c = w.get<Combat>(u); c && c->target != NULL_ENTITY && w.has<Commandable>(c->target)) {
                    c->target = NULL_ENTITY;
                }
            }
        }
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
    // Out cold, or carried (playing dead or not), a unit does not fight
    auto standing = [&](Entity e) { return hpOf(w, e) > 0 && !w.has<Unconscious>(e) && !w.has<Carried>(e); };

    // Every unit that is not yours notices the units it fights in range (its enemies by faction, or by provocation),
    // and chases until the target is gone or out of the leash. A passive one starts no fight, but keeps one it has.
    // Only notices of your units are written to the log.
    for (Entity h : everyone) {
        if (w.has<Commandable>(h) || !standing(h) || !w.has<Combat>(h)) continue;
        Combat& hc = *w.get<Combat>(h);
        if (hc.target != NULL_ENTITY) {
            if (targetable(w, hc.target) && distanceBetween(w, h, hc.target) <= LEASH_RANGE && unitsHostile(w, h, hc.target)) continue;
            hc.target = NULL_ENTITY;
        }
        if (isPassive(w, h)) continue;
        for (Entity p : everyone) {
            if (p == h || !standing(p) || !unitsHostile(w, h, p)) continue;
            if (distanceBetween(w, h, p) > noticeRangeOf(w, p)) continue;
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
        const float spotRange = AGGRO_RANGE * (1.0f + PERCEPTION_RANGE_PER_LEVEL * (levelOf(st, STAT_PERCEPTION) - 1));
        if (!w.has<Commandable>(p)) continue;   // only your units gain XP
        const std::vector<Entity>& foes = hostiles;
        bool unnoticedInRange = false;
        for (Entity h : foes) {
            if (!standing(h)) continue;
            const float d = distanceBetween(w, p, h);
            if (d <= spotRange && std::find(pc.spotted.begin(), pc.spotted.end(), h) == pc.spotted.end()) {
                pc.spotted.push_back(h);
                grantXp(w, p, STAT_PERCEPTION, PERCEPTION_XP_PER_SPOT, lang("MISC_REASON_SPOTTING"));
            }
            if (d <= STEALTH_RANGE && !awareOfYou(h)) unnoticedInRange = true;
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
        if (pc.target != NULL_ENTITY && targetable(w, pc.target) && distanceBetween(w, p, pc.target) <= reachOf(w, p)) {
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
        if (!targetable(w, t) || distanceBetween(w, e, t) > reachOf(w, e)) continue;
        strike(w, rng, e, t, log, floats);
    }
}
