#include "claims.h"
#include "spatial.h"
#include <algorithm>
#include <cmath>
#include "build.h"
#include "factions.h"
#include "language.h"

namespace {

bool isFoundationKind(PropKind kind) {
    return kind == PropKind::FoundationSmall || kind == PropKind::FoundationLarge || kind == PropKind::FoundationLong ||
           kind == PropKind::FoundationCircle;
}

// Lowers a building's wear by hours / decayHours, if it has any
void wear(World& w, Entity e, double amount) {
    if (Wear* wr = w.get<Wear>(e)) wr->value -= amount;
}

}  // namespace

std::vector<Claim> claimsOf(World& w) {
    std::vector<Claim> out;
    for (Entity e : w.view<Prop, Transform, Owner>()) {
        if (w.get<Prop>(e)->kind != PropKind::Flag) continue;
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
        if (w.get<Prop>(v)->kind == PropKind::Vault) vaults.push_back(v);
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
    if (!p || !t || p->kind == PropKind::Flag || p->kind == PropKind::Vault) return;
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

// A level's prop going for good is remembered, or a save would bring it back from the seed
static void rememberGone(World& w, Entity prop) {
    const Prop* p = w.get<Prop>(prop);
    if (!p || p->serial < 0) return;
    const std::vector<Entity> holder = w.view<GoneProps>();
    GoneProps& gone = holder.empty() ? w.add(w.create(), GoneProps{}) : *w.get<GoneProps>(holder.front());
    gone.serials.push_back(p->serial);
}

void breakBuilding(World& w, Entity e) {
    if (!w.has<Prop>(e) || !w.has<Transform>(e)) return;
    if (w.get<Prop>(e)->kind == PropKind::Debris) {
        releaseProp(w, e);
        rememberGone(w, e);
        w.destroy(e);
        return;
    }
    // Bones leave no Debris: only the Bones item, where they lay
    if (destroyedByHand(w.get<Prop>(e)->kind)) {
        const Transform at = *w.get<Transform>(e);
        releaseProp(w, e);
        rememberGone(w, e);
        w.destroy(e);
        spawnGroundItem(w, at, BONES_ITEM, BONES_ITEM_COUNT);
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
    for (Entity g : gone) {
        rememberGone(w, g);
        w.destroy(g);
    }
    for (const Drop& d : drops) {
        spawnGroundItem(w, Transform{ d.at.x, d.at.y, d.at.z }, d.item, d.count);
    }
    for (const Transform& at : spots) {
        const Entity debris = w.create();
        w.add(debris, Transform{ at.x, at.y, at.z });
        w.add(debris, Prop{ PropKind::Debris, -1, 0.0f });
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
        if (w.get<Prop>(v)->kind != PropKind::Vault) continue;
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

void settleClaimOwnership(World& w) {
    for (Entity e : w.view<Prop, Transform>()) claimOwnership(w, e);
}

bool vaultHasUpkeep(World& w, Entity vault) {
    const Prop* p = w.get<Prop>(vault);
    const Storage* st = w.get<Storage>(vault);
    if (!p || !st) return false;
    const int input = propInfo(p->kind).input;
    for (const ItemStack& s : st->slots) {
        if (s.item == input && s.count > 0) return true;
    }
    return false;
}

bool propDecaying(World& w, Entity e, const std::vector<Claim>& claims) {
    const Prop* p = w.get<Prop>(e);
    const Transform* tr = w.get<Transform>(e);
    if (!p || !tr) return false;
    const Owner* own = w.get<Owner>(e);
    // By the same rules as claimDecaySystem
    bool decaying = false;
    if (p->kind == PropKind::Flag) {
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
    return decaying;
}
