#include "claims.h"
#include <algorithm>
#include <cmath>
#include "build.h"
#include "factions.h"
#include "language.h"

namespace {

bool isFoundationKind(int kind) {
    return kind == PROP_FOUNDATION_SMALL || kind == PROP_FOUNDATION_LARGE || kind == PROP_FOUNDATION_LONG ||
           kind == PROP_FOUNDATION_CIRCLE;
}

// Lowers a building's wear by hours / decayHours, if it has any
void wear(World& w, Entity e, double amount) {
    if (Wear* wr = w.get<Wear>(e)) wr->value -= amount;
}

}  // namespace

std::vector<Claim> claimsOf(World& w) {
    std::vector<Claim> out;
    for (Entity e : w.view<Prop, Transform, Owner>()) {
        if (w.get<Prop>(e)->kind != PROP_FLAG) continue;
        Claim c;
        c.flag = e;
        c.x = w.get<Transform>(e)->x;
        c.z = w.get<Transform>(e)->z;
        c.owner = w.get<Owner>(e)->faction;
        out.push_back(c);
    }
    std::sort(out.begin(), out.end(), [](const Claim& a, const Claim& b) { return a.flag < b.flag; });
    // The vault standing in each circle, if any. Vaults are taken in placement order (the lower entity id first), so
    // where two stand in one circle, the one placed first holds it.
    std::vector<Entity> vaults;
    for (Entity v : w.view<Prop, Transform, Owner>()) {
        if (w.get<Prop>(v)->kind == PROP_VAULT) vaults.push_back(v);
    }
    std::sort(vaults.begin(), vaults.end());
    for (Entity v : vaults) {
        const float vx = w.get<Transform>(v)->x, vz = w.get<Transform>(v)->z;
        for (Claim& c : out) {
            if (c.vault != NULL_ENTITY) continue;
            const float dx = vx - c.x, dz = vz - c.z;
            if (dx * dx + dz * dz <= CLAIM_RADIUS * CLAIM_RADIUS) {
                c.vault = v;
                c.sealer = w.get<Owner>(v)->faction;
                break;
            }
        }
    }
    return out;
}

const Claim* claimAt(const std::vector<Claim>& claims, float x, float z, int owner) {
    for (const Claim& c : claims) {
        if (owner >= 0 && c.owner != owner) continue;
        const float dx = x - c.x, dz = z - c.z;
        if (dx * dx + dz * dz <= CLAIM_RADIUS * CLAIM_RADIUS) return &c;
    }
    return nullptr;
}

int sealingFactionAt(const std::vector<Claim>& claims, float x, float z) {
    const Claim* c = claimAt(claims, x, z);
    if (!c || c->vault == NULL_ENTITY) return -1;
    return c->sealer;
}

bool claimsOverlap(float ax, float az, float bx, float bz) {
    const float dx = ax - bx, dz = az - bz;
    const float reach = 2.0f * CLAIM_RADIUS;
    return dx * dx + dz * dz < reach * reach;
}

bool vaultFueled(World& w, Entity vault) {
    const Prop* p = w.get<Prop>(vault);
    if (!p) return false;
    if (const Fuel* f = w.get<Fuel>(vault); f && f->hours > 1e-9) return true;
    const int input = propInfo(p->kind).input;
    if (const Storage* st = w.get<Storage>(vault)) {
        for (const ItemStack& s : st->slots) {
            if (s.item == input && s.count > 0) return true;
        }
    }
    return false;
}

bool lockShutFor(World& w, Entity prop, Entity unit) {
    const Lock* lock = w.get<Lock>(prop);
    if (!lock || !lock->locked) return false;
    const Faction* f = unit != NULL_ENTITY ? w.get<Faction>(unit) : nullptr;
    const int who = f ? f->id : FACTION_PLAYER;
    return lockShutForFaction(w, prop, who);
}

bool lockShutForFaction(World& w, Entity prop, int faction) {
    const Lock* lock = w.get<Lock>(prop);
    if (!lock || !lock->locked) return false;
    if (lock->faction >= 0 && lock->faction == faction) return false;
    // A locked prop inside a claim of this faction is open to it, whoever's lock it is
    if (const Transform* t = w.get<Transform>(prop)) {
        if (claimAt(claimsOf(w), t->x, t->z, faction)) return false;
    }
    return true;
}

void claimOwnership(World& w, Entity prop) {
    const Prop* p = w.get<Prop>(prop);
    const Transform* t = w.get<Transform>(prop);
    if (!p || !t || p->kind == PROP_FLAG || p->kind == PROP_VAULT) return;
    const Claim* c = claimAt(claimsOf(w), t->x, t->z);
    if (!c) return;
    const int faction = c->sealer >= 0 ? c->sealer : c->owner;
    if (Owner* own = w.get<Owner>(prop)) own->faction = faction;
    else w.add(prop, Owner{ faction });
    if (Lock* lock = w.get<Lock>(prop)) lock->faction = faction;
    else if (propInfo(p->kind).use == PropUse::Storage) w.add(prop, Lock{ true, BUILT_LOCK_LEVEL, faction });
}

namespace {

// Everything that points at a prop about to go lets go of it: units lying, sitting or shut in it get off, units
// working it or walking to it stop, and Auto-Hauls to it are dropped
void releaseProp(World& w, Entity prop) {
    for (Entity u : w.view<Occupying>()) {
        if (w.get<Occupying>(u)->prop == prop) w.remove<Occupying>(u);
    }
    for (Entity u : w.view<Working>()) {
        if (w.get<Working>(u)->prop == prop) w.remove<Working>(u);
    }
    for (Entity u : w.view<PropOrder>()) {
        if (w.get<PropOrder>(u)->prop != prop) continue;
        w.remove<PropOrder>(u);
        w.remove<SoloMove>(u);
    }
    for (Entity u : w.view<Behaviors>()) {
        Behaviors* b = w.get<Behaviors>(u);
        if (b->busy && b->stepProp == prop) b->busy = false;
        b->rules.erase(std::remove_if(b->rules.begin(), b->rules.end(), [&](const BehaviorRule& r) {
                           return r.kind == BehaviorKind::AutoHaul && r.prop == prop;
                       }),
                       b->rules.end());
    }
}

}  // namespace

void breakBuilding(World& w, Entity e) {
    if (!w.has<Prop>(e) || !w.has<Transform>(e)) return;
    if (w.get<Prop>(e)->kind == PROP_DEBRIS) {
        releaseProp(w, e);
        w.destroy(e);
        return;
    }
    std::vector<Entity> gone{ e };
    if (isFoundationKind(w.get<Prop>(e)->kind)) {
        for (Entity c : w.view<Prop, OnBase>()) {
            if (w.get<OnBase>(c)->foundation == e) gone.push_back(c);
        }
    }
    for (Entity g : gone) releaseProp(w, g);
    // Copy what is needed before creating entities, which can move the stored components
    struct Drop { Transform at; int item; int count; };
    std::vector<Drop> drops;
    std::vector<Transform> spots;
    for (Entity g : gone) {
        const Transform at = *w.get<Transform>(g);
        spots.push_back(at);
        auto collect = [&](const ItemStack* slots) {
            for (int i = 0; i < STORAGE_SLOTS; ++i) {
                if (slots[i].item > 0 && slots[i].count > 0) drops.push_back(Drop{ at, slots[i].item, slots[i].count });
            }
        };
        if (const Storage* st = w.get<Storage>(g)) collect(st->slots.data());
        if (const Output* out = w.get<Output>(g)) collect(out->slots.data());
    }
    for (Entity g : gone) w.destroy(g);
    for (const Drop& d : drops) {
        const Entity item = w.create();
        w.add(item, Transform{ d.at.x, d.at.y, d.at.z });
        w.add(item, GroundItem{ d.item, d.count });
    }
    for (const Transform& at : spots) {
        const Entity debris = w.create();
        w.add(debris, Transform{ at.x, at.y, at.z });
        w.add(debris, Prop{ PROP_DEBRIS, -1, 0.0f });
        claimOwnership(w, debris);
    }
}

bool destroyBuilding(World& w, Entity e) {
    if (!w.has<Prop>(e)) return false;
    breakBuilding(w, e);
    return true;
}

// Where two vaults stand in one flag's claim, the one placed first (the lower entity id) keeps the claim. The newer one
// becomes debris, and its items fall on the ground. A vault must sit inside a flag's circle to count.
void resolveDuplicateVaults(World& w) {
    // claimsOf gives each claim the vault placed first among those in its circle. Every other vault in a circle is
    // a duplicate: the ones that are not the holder of any claim.
    std::vector<Entity> holders;
    for (const Claim& c : claimsOf(w)) {
        if (c.vault != NULL_ENTITY) holders.push_back(c.vault);
    }
    std::vector<Entity> extra;
    for (Entity v : w.view<Prop, Transform, Owner>()) {
        if (w.get<Prop>(v)->kind != PROP_VAULT) continue;
        if (std::find(holders.begin(), holders.end(), v) != holders.end()) continue;
        const float vx = w.get<Transform>(v)->x, vz = w.get<Transform>(v)->z;
        for (const Claim& c : claimsOf(w)) {
            const float dx = vx - c.x, dz = vz - c.z;
            if (dx * dx + dz * dz <= CLAIM_RADIUS * CLAIM_RADIUS) {
                extra.push_back(v);
                break;
            }
        }
    }
    for (Entity v : extra) breakBuilding(w, v);
}

void claimDecaySystem(World& w, double gameSeconds) {
    const double hours = gameSeconds / 3600.0;
    if (hours <= 0.0) return;
    resolveDuplicateVaults(w);
    // A vault's faction takes its claim: the flag and the claim become that faction's
    for (const Claim& c : claimsOf(w)) {
        if (c.vault != NULL_ENTITY && w.has<Owner>(c.flag)) w.get<Owner>(c.flag)->faction = w.get<Owner>(c.vault)->faction;
    }
    const std::vector<Claim> claims = claimsOf(w);

    // Flags. A flag's health falls while its claim has no fuelled vault, and while an older flag's circle overlaps it
    // (the claims are oldest first, so only the older ones before it are checked). It recovers one point an hour while
    // its own vault stands in the claim and is fuelled.
    for (size_t i = 0; i < claims.size(); ++i) {
        const Claim& c = claims[i];
        Health* hp = w.get<Health>(c.flag);
        if (!hp) continue;
        const bool held = c.vault != NULL_ENTITY && w.get<Owner>(c.vault)->faction == c.owner && vaultFueled(w, c.vault);
        bool overlapped = false;
        for (size_t j = 0; j < i && !overlapped; ++j) {
            overlapped = claimsOverlap(c.x, c.z, claims[j].x, claims[j].z);
        }
        if (held && !overlapped) {
            hp->value = std::min(hp->max, hp->value + FLAG_RECOVER_PER_HOUR * hours);
        } else {
            hp->value -= 10.0 * hours / FLAG_DECAY_HOURS * FLAG_HEALTH;   // a flag with no vault wears through in FLAG_DECAY_HOURS
        }
    }

    // Foundations: a foundation wears while it lies outside every claim of its faction, or in one whose vault is dry
    for (Entity e : w.view<Prop, Transform, Owner, Wear>()) {
        if (!isFoundationKind(w.get<Prop>(e)->kind)) continue;
        const int owner = w.get<Owner>(e)->faction;
        const Claim* c = claimAt(claims, w.get<Transform>(e)->x, w.get<Transform>(e)->z, owner);
        const bool held = c && c->vault != NULL_ENTITY && w.get<Owner>(c->vault)->faction == owner && vaultFueled(w, c->vault);
        if (!held) wear(w, e, hours / FOUNDATION_DECAY_HOURS);
    }

    // What is worn through breaks. A foundation takes the props on it with it, so each one is checked as it is reached.
    std::vector<Entity> broken;
    for (Entity e : w.view<Wear>()) {
        if (w.get<Wear>(e)->value <= 0.0) broken.push_back(e);
    }
    for (Entity e : w.view<Health>()) {
        if (w.get<Health>(e)->value <= 0.0) broken.push_back(e);
    }
    for (Entity e : broken) {
        if (w.has<Prop>(e)) breakBuilding(w, e);
    }
    // Props whose foundation has gone (or was never there) break too
    std::vector<Entity> orphans;
    for (Entity e : w.view<Prop, OnBase>()) {
        const Entity base = w.get<OnBase>(e)->foundation;
        if (!w.has<Prop>(base)) orphans.push_back(e);
    }
    for (Entity e : orphans) {
        if (w.has<Prop>(e)) breakBuilding(w, e);
    }
}

namespace {

// The camp kit's pieces, offsets in tiles from the camp centre (which is snapped to a tile middle). The flag stands on
// the centre. The foundation, with its vault, sits two tiles east; the campfire two tiles west; four bedrolls around
// the campfire; the barrel two tiles south; two trees just past the bedrolls, for the camp's wood. Every piece is on
// its own tile, so none shares ground with another.
struct KitSpot { int kind; float dx, dz; };
const KitSpot CAMP_KIT[] = {
    { PROP_FLAG, 0.0f, 0.0f },
    { PROP_FOUNDATION_SMALL, 2.0f, 0.0f },
    { PROP_CAMPFIRE, -2.0f, 0.0f },
    { PROP_BEDROLL, -2.0f, -1.0f },
    { PROP_BEDROLL, -2.0f, 1.0f },
    { PROP_BEDROLL, -3.0f, 0.0f },
    { PROP_BEDROLL, -1.0f, 0.0f },
    { PROP_BARREL, 0.0f, 2.0f },
    { PROP_TREE, -5.0f, -2.0f },
    { PROP_TREE, -5.0f, 2.0f },
};

}  // namespace

bool campKitFits(World& w, const Terrain& t, float cx, float cz) {
    const float ox = std::floor(cx) + 0.5f, oz = std::floor(cz) + 0.5f;
    // Every piece's tile must be walkable and free of anything. A free tile has no prop or built prop standing on it.
    const std::vector<Entity> props = w.view<Prop, Transform>();
    for (const KitSpot& k : CAMP_KIT) {
        const float x = ox + k.dx, z = oz + k.dz;
        if (!t.walkable((int)std::floor(x), (int)std::floor(z))) return false;
        for (Entity e : props) {
            const Transform* tr = w.get<Transform>(e);
            if (std::fabs(tr->x - x) < 0.5f && std::fabs(tr->z - z) < 0.5f) return false;
        }
    }
    return true;
}

CampKit placeCampKit(World& w, Terrain& t, int faction, float cx, float cz, float yaw, int* serial) {
    CampKit kit;
    // A camp that can't hold the whole kit gets none of it
    if (!campKitFits(w, t, cx, cz)) return kit;
    const float ox = std::floor(cx) + 0.5f, oz = std::floor(cz) + 0.5f;
    kit.x = ox;
    kit.z = oz;
    // Each piece gets the next camp serial, so a save can name it (its storage, lock, fuel, who sleeps on it)
    auto stamp = [&](Entity e) {
        if (serial) w.get<Prop>(e)->serial = (*serial)++;
    };
    for (const KitSpot& k : CAMP_KIT) {
        const float x = ox + k.dx, z = oz + k.dz;
        Entity e = NULL_ENTITY;
        if (propInfo(k.kind).built) {
            e = placeBuiltProp(w, t, k.kind, x, z, yaw, faction);
        } else {
            // A level kind (the campfire, bedrolls, barrel, trees): placed as the level places them. All but the trees
            // are owned by the camp; a tree is nobody's.
            e = w.create();
            w.add(e, Transform{ x, t.heightAt(x, z), z });
            w.add(e, Prop{ k.kind, -1, yaw });
            if (k.kind != PROP_TREE) w.add(e, Owner{ faction });
            if (hasStorage(propInfo(k.kind))) w.add(e, Storage{});   // the camp's barrel starts empty
            claimOwnership(w, e);   // it stands in the camp's own claim, so the trees are the camp's too
        }
        stamp(e);
        if (k.kind == PROP_FLAG) kit.flag = e;
        if (k.kind == PROP_TREE) kit.trees.push_back(e);
        if (k.kind == PROP_FOUNDATION_SMALL) kit.foundation = e;
    }
    kit.faction = faction;
    kit.yaw = yaw;
    kit.placed = true;
    return kit;
}

void placeCampVault(World& w, Terrain& t, CampKit& kit, int* serial) {
    if (!kit.placed || !w.has<Prop>(kit.foundation)) return;
    const Transform at = *w.get<Transform>(kit.foundation);
    const Entity vault = placeBuiltProp(w, t, PROP_VAULT, at.x, at.z, kit.yaw, kit.faction);
    if (vault == NULL_ENTITY) return;
    if (serial) w.get<Prop>(vault)->serial = (*serial)++;
    const int logs = (int)std::ceil(CAMP_UPKEEP_DAYS * 24.0 / propInfo(PROP_VAULT).fuelHours);   // three days of logs
    w.get<Storage>(vault)->slots[0] = ItemStack{ propInfo(PROP_VAULT).input, logs };
    kit.vault = vault;
}

void settleClaimOwnership(World& w) {
    for (Entity e : w.view<Prop, Transform>()) claimOwnership(w, e);
}

std::vector<std::string> propInfoLines(World& w, Entity e) {
    std::vector<std::string> out;
    const Prop* p = w.get<Prop>(e);
    const Transform* tr = w.get<Transform>(e);
    if (!p || !tr) return out;
    const std::vector<Claim> claims = claimsOf(w);
    const Owner* own = w.get<Owner>(e);
    if (own) out.push_back(langf("MISC_FLAG_FACTION", { factionName(own->faction) }));

    // Whether it is wearing down now, by the same rules as claimDecaySystem
    bool decaying = false;
    if (p->kind == PROP_FLAG) {
        size_t idx = claims.size();
        for (size_t i = 0; i < claims.size(); ++i) {
            if (claims[i].flag == e) idx = i;
        }
        if (idx < claims.size()) {
            const Claim& c = claims[idx];
            const bool held = c.vault != NULL_ENTITY && w.get<Owner>(c.vault)->faction == c.owner && vaultFueled(w, c.vault);
            bool overlapped = false;
            for (size_t j = 0; j < idx && !overlapped; ++j) overlapped = claimsOverlap(c.x, c.z, claims[j].x, claims[j].z);
            decaying = !held || overlapped;
        }
    } else if (w.has<Wear>(e) && own && isFoundationKind(p->kind)) {
        const Claim* c = claimAt(claims, tr->x, tr->z, own->faction);
        decaying = !(c && c->vault != NULL_ENTITY && w.get<Owner>(c->vault)->faction == own->faction && vaultFueled(w, c->vault));
    }

    if (const Health* hp = w.get<Health>(e)) {
        const int shown = (int)std::ceil(hp->value - 1e-9);
        out.push_back(langf("MISC_FLAG_HEALTH", { std::to_string(std::max(0, shown)), std::to_string((int)hp->max) }));
    }
    if (const Wear* wr = w.get<Wear>(e)) {
        const int pct = std::clamp((int)std::ceil(wr->value * 100.0 - 1e-9), 0, 100);
        out.push_back(langf("MISC_PROP_CONDITION", { std::to_string(pct) }));
    }
    if (w.has<Health>(e) || w.has<Wear>(e)) out.push_back(lang(decaying ? "MISC_PROP_DECAYING" : "MISC_PROP_HOLDING"));

    if (const Lock* lock = w.get<Lock>(e)) {
        if (!lock->locked) out.push_back(lang("MISC_PROP_LOCK_OPEN"));
        else if (!lockShutFor(w, e, NULL_ENTITY)) out.push_back(lang("MISC_PROP_LOCK_YOURS"));
        else out.push_back(langf("MISC_PROP_LOCK_SHUT", { std::to_string(lock->level) }));
    }

    if (p->kind == PROP_FLAG) {
        const Claim* c = nullptr;
        for (const Claim& k : claims) {
            if (k.flag == e) c = &k;
        }
        out.push_back(lang(c && c->vault != NULL_ENTITY ? "MISC_FLAG_VAULT_YES" : "MISC_FLAG_VAULT_NO"));
    }
    if (p->kind == PROP_VAULT) {
        // Upkeep left: what is left of the log burning now, and the hours of each log waiting
        double hours = 0.0;
        if (const Fuel* f = w.get<Fuel>(e)) hours += f->hours;
        const int input = propInfo(p->kind).input;
        if (const Storage* st = w.get<Storage>(e)) {
            for (const ItemStack& s : st->slots) {
                if (s.item == input && s.count > 0) hours += s.count * propInfo(p->kind).fuelHours;
            }
        }
        out.push_back(langf("MISC_PROP_UPKEEP", { std::to_string((int)std::floor(hours)) }));
        if (!claimAt(claims, tr->x, tr->z)) out.push_back(lang("MISC_PROP_VAULT_UNCLAIMED"));
    }
    return out;
}

std::vector<std::string> flagInfoLines(World& w, Entity e) {
    std::vector<std::string> out;
    const Prop* p = w.get<Prop>(e);
    if (!p || p->kind != PROP_FLAG) return out;
    const Owner* own = w.get<Owner>(e);
    const Health* hp = w.get<Health>(e);
    const std::vector<Claim> claims = claimsOf(w);
    const Claim* c = nullptr;
    for (const Claim& k : claims) {
        if (k.flag == e) c = &k;
    }
    const int owner = own ? own->faction : FACTION_PLAYER;
    out.push_back(langf("MISC_FLAG_FACTION", { factionName(owner) }));
    const int shown = hp ? (int)std::ceil(hp->value - 1e-9) : 0;
    out.push_back(langf("MISC_FLAG_HEALTH", { std::to_string(std::max(0, shown)), std::to_string((int)(hp ? hp->max : 0)) }));
    out.push_back(lang(c && c->vault != NULL_ENTITY ? "MISC_FLAG_VAULT_YES" : "MISC_FLAG_VAULT_NO"));
    return out;
}
