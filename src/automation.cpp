#include "systems_internal.h"
#include "inventory.h"
#include "world_labels.h"
#include "taming.h"
#include "faction_rules.h"
#include "recipes.h"
#include "commerce.h"
#include "schedule.h"
#include "formation.h"

using namespace systems_detail;

namespace {
int carriedCount(World& w, ObjectId u, int item) {
    const Inventory* inv = w.get<Inventory>(u);
    return inv ? carriedCount(*inv, item) : 0;
}

bool inOwnClaim(World& w, const std::vector<Claim>& claims, ObjectId prop, int faction) {
    const Transform* t = w.get<Transform>(prop);
    return t && claimAt(claims, t->x, t->z, faction) != nullptr;
}

bool fullFor(World& w, ObjectId u, int item) {
    const Inventory* inv = w.get<Inventory>(u);
    if (!inv || slotsRoomFor(inv->slots.data(), INVENTORY_SLOTS, item) <= 0) return true;
    const ItemDef* def = findItem(item);
    return encumbranceFor(carriedWeightOf(w, u) + (def ? def->weight : 0.0f), statsOf(w, u)) >= ENCUMBERED_SHARE;
}

// Picking up a pile off the ground: no room for it, or it would leave the unit encumbered, unless it carries nothing else
// (then even a heavy pile is taken, one at a time)
bool fullForPickup(World& w, ObjectId u, int item, int count) {
    const Inventory* inv = w.get<Inventory>(u);
    if (!inv || slotsRoomFor(inv->slots.data(), INVENTORY_SLOTS, item) <= 0) return true;
    bool empty = true;
    for (const ItemStack& s : inv->slots) empty = empty && (s.item <= 0 || s.count <= 0);
    if (empty) return false;
    const ItemDef* def = findItem(item);
    const float add = (def ? def->weight : 0.0f) * (float)std::max(1, count);
    return encumbranceFor(carriedWeightOf(w, u) + add, statsOf(w, u)) >= ENCUMBERED_SHARE;
}

// What a haul rule carries to its prop: bones for Auto-Haul Bones; for Auto-Haul, what the prop takes, but only fuel to an Altar
bool haulCarries(World& w, const BehaviorRule& r, ObjectId prop, int item) {
    const Prop* p = w.get<Prop>(prop);
    if (!p || !propTakesItem(w, prop, item)) return false;
    const PropKind kind = p->kind;
    if (r.kind == BehaviorKind::AutoHaulBones) return isBones(item);
    if (propInfo(kind).claimsLand) return item == propInfo(kind).input;
    return true;
}

BehaviorWait haulWait(World& w, const std::vector<Claim>& claims, ObjectId u, const BehaviorRule& r, int item = 0) {
    const Prop* p = w.get<Prop>(r.prop);
    const Storage* st = w.get<Storage>(r.prop);
    const Inventory* inv = w.get<Inventory>(u);
    if (!p || !st || !inv) return BehaviorWait::PropGone;
    if (!inOwnClaim(w, claims, r.prop, factionOf(w, u))) return BehaviorWait::OutsideClaim;
    if (lockShutFor(w, claims, r.prop, u)) return BehaviorWait::Locked;
    bool carries = false;
    for (const ItemStack& s : inv->slots) {
        if (s.item <= 0 || s.count <= 0 || (item != 0 && s.item != item) || !haulCarries(w, r, r.prop, s.item)) continue;
        carries = true;
        if (storeRoomFor(*st, s.item) > 0) return BehaviorWait::None;
    }
    return carries ? BehaviorWait::PropFull : BehaviorWait::NothingToHaul;
}
bool haulable(World& w, const std::vector<Claim>& claims, ObjectId u, const BehaviorRule& r, int item = 0) {
    return haulWait(w, claims, u, r, item) == BehaviorWait::None;
}

// A fingerprint of what a unit carries, which changes whenever an item goes in or out
std::uint64_t carriedPrint(World& w, ObjectId u) {
    const Inventory* inv = w.get<Inventory>(u);
    if (!inv) return 0;
    std::uint64_t h = 1469598103934665603ull;
    for (const ItemStack& s : inv->slots) {
        h = (h ^ (std::uint64_t)(std::uint32_t)s.item) * 1099511628211ull;
        h = (h ^ (std::uint64_t)(std::uint32_t)s.count) * 1099511628211ull;
    }
    return h;
}

// Whether a haul rule's store could take this item now (whether or not the unit carries one)
bool storeCouldTake(World& w, const std::vector<Claim>& claims, ObjectId u, const BehaviorRule& r, int item) {
    const Storage* st = w.get<Storage>(r.prop);
    if (!st || !w.has<Prop>(r.prop) || !inOwnClaim(w, claims, r.prop, factionOf(w, u)) || lockShutFor(w, claims, r.prop, u)) return false;
    return haulCarries(w, r, r.prop, item) && storeRoomFor(*st, item) > 0;
}

// Auto-Haul Ground Items: the nearest item lying in the unit's claim that one of its haul tasks could put away, that it
// has room for and nobody else is fetching
ObjectId groundHaulTarget(World& w, const std::vector<Claim>& claims, ObjectId u, const Behaviors& b, BehaviorWait& why) {
    why = BehaviorWait::NothingToHaul;
    const Transform* ut = w.get<Transform>(u);
    if (!ut) return NO_OBJECT;
    const int faction = factionOf(w, u);
    std::vector<ObjectId> taken;
    for (ObjectId o : w.view<Behaviors>()) {
        const Behaviors* ob = w.get<Behaviors>(o);
        if (o != u && ob->busy && ob->step.kind == BehaviorKind::AutoHaulGround) taken.push_back(ob->stepProp);
    }
    for (ObjectId o : w.view<PickupOrder>()) taken.push_back(w.get<PickupOrder>(o)->item);
    ObjectId best = NO_OBJECT;
    float bestD2 = 0.0f;
    for (ObjectId g : w.view<GroundItem, Transform>()) {
        const Transform* gt = w.get<Transform>(g);
        if (!claimAt(claims, gt->x, gt->z, faction) || std::find(taken.begin(), taken.end(), g) != taken.end()) continue;
        const int item = w.get<GroundItem>(g)->item;
        bool stored = false;
        for (const BehaviorRule& r : b.rules) stored = stored || (r.on && isHaul(r.kind) && storeCouldTake(w, claims, u, r, item));
        if (!stored) continue;
        if (fullForPickup(w, u, item, w.get<GroundItem>(g)->count)) {
            why = BehaviorWait::CarryFull;
            continue;
        }
        const float d2 = distSq(*gt, *ut);
        if (best == NO_OBJECT || d2 < bestD2) {
            best = g;
            bestD2 = d2;
        }
    }
    // What an Advanced Crafting prop has made waits in it like an item on the ground: carried out, if a haul task
    // could put it away and the unit has room
    if (best == NO_OBJECT) {
        const ObjectId bench = craftOutputToHaul(w, u, taken);
        const Output* out = bench != NO_OBJECT ? w.get<Output>(bench) : nullptr;
        for (size_t i = 0; out && i < out->slots.size() && best == NO_OBJECT; ++i) {
            const ItemStack& s = out->slots[i];
            if (s.count <= 0) continue;
            bool stored = false;
            for (const BehaviorRule& r : b.rules) stored = stored || (r.on && isHaul(r.kind) && storeCouldTake(w, claims, u, r, s.item));
            if (!stored) continue;
            if (fullForPickup(w, u, s.item, s.count)) why = BehaviorWait::CarryFull;
            else best = bench;
        }
    }
    if (best != NO_OBJECT) why = BehaviorWait::None;
    return best;
}

bool canHaulItem(World& w, const std::vector<Claim>& claims, ObjectId u, const Behaviors& b, int item) {
    for (const BehaviorRule& r : b.rules) {
        if (r.on && isHaul(r.kind) && haulable(w, claims, u, r, item)) return true;
    }
    return false;
}

// Wood Logs and Stone each have a stockpile of their own: what kind of store takes this item, or Any for none
ItemFilter stockpileFor(int item) {
    const ItemDef* d = findItem(item);
    if (d && hasTag(*d, ITEM_TAG_WOOD_LOG)) return ItemFilter::Wood;
    if (d && hasTag(*d, ITEM_TAG_STONE)) return ItemFilter::Stone;
    return ItemFilter::Any;
}

// The nearest stockpile of the unit's faction for this item with room in it, or NO_OBJECT
ObjectId stockpileWithRoom(World& w, const std::vector<Claim>& claims, ObjectId u, int item) {
    const Transform* ut = w.get<Transform>(u);
    const ItemFilter want = stockpileFor(item);
    if (!ut || want == ItemFilter::Any) return NO_OBJECT;
    const int faction = factionOf(w, u);
    return nearestProp(w, *ut, [&](ObjectId p) {
        const PropInfo& info = propInfo(w.get<Prop>(p)->kind);
        const Storage* st = w.get<Storage>(p);
        const Owner* own = w.get<Owner>(p);
        return info.accepts == want && st && ((own && own->faction == faction) || inOwnClaim(w, claims, p, faction)) &&
               !lockShutFor(w, claims, p, u) && storeRoomFor(*st, item) > 0;
    });
}

bool harvestLoaded(World& w, const std::vector<Claim>& claims, ObjectId u, const Behaviors& b, int item) {
    if (fullFor(w, u, item)) return true;
    if (stockpileFor(item) != ItemFilter::Any) return carriedCount(w, u, item) >= AUTO_LOAD;
    return carriedCount(w, u, item) >= AUTO_LOAD && canHaulItem(w, claims, u, b, item);
}

ObjectId harvestTarget(World& w, const std::vector<Claim>& claims, ObjectId u, int ruleKind) {
    const Transform* ut = w.get<Transform>(u);
    if (!ut || ruleKind < 0 || ruleKind >= propKindCount()) return NO_OBJECT;
    const PropKind kind = (PropKind)ruleKind;
    if (!yieldsItems(kind)) return NO_OBJECT;
    const int faction = factionOf(w, u);
    const std::vector<ObjectId> worked = workedProps(w);
    return nearestProp(w, *ut, [&](ObjectId p) {
        return w.get<Prop>(p)->kind == kind && inOwnClaim(w, claims, p, faction) && !lockShutFor(w, claims, p, u) &&
               countIn(worked, p) < propInfo(kind).workers;
    });
}

bool onOtherOrder(World& w, ObjectId u, const Behaviors& b) {
    if (const PropOrder* po = w.get<PropOrder>(u); po && !po->automatic) return true;
    if (const Working* wk = w.get<Working>(u); wk && (wk->unlocking || !b.busy || wk->prop != b.stepProp)) return true;
    if (w.has<Occupying>(u) || w.has<CarryOrder>(u) || w.has<Carrying>(u) || hasPlayerOrder(w, u)) return true;
    if (const SoloMove* so = w.get<SoloMove>(u); so && so->next < so->waypoints.size() && !w.has<PropOrder>(u) && !b.seeking &&
        !(b.busy && isWalkBehavior(b.step.kind))) {
        return true;
    }
    if (const ActiveSquadMember* sm = w.get<ActiveSquadMember>(u)) {
        if (const ActiveSquad* sq = w.get<ActiveSquad>(sm->squad); sq && !sq->arrived) return true;
    }
    return false;
}

void autoGo(World& w, Terrain& t, ObjectId u, ObjectId prop, PropAction action) {
    const Transform* pt = w.get<Transform>(prop);
    if (!pt) return;
    autoWalk(w, t, u, Vec2{ pt->x, pt->z }, storeyOf(w, prop));
    w.add(u, PropOrder{ prop, action, true });
}

BehaviorRule* stepRule(Behaviors& b) {
    for (BehaviorRule& r : b.rules) {
        if (sameBehavior(r, b.step)) return &r;
    }
    return nullptr;
}

BehaviorWait patrolSpot(World& w, const Terrain& t, const std::vector<Claim>& claims, ObjectId u, Rng& rng, Vec2& to) {
    const Transform* ut = w.get<Transform>(u);
    const Claim* c = ut ? claimAt(claims, ut->x, ut->z, factionOf(w, u)) : nullptr;
    if (!c) return BehaviorWait::NotInClaim;
    for (int attempt = 0; attempt < 12; ++attempt) {
        const float a = rng.unit() * TWO_PI, d = std::sqrt(rng.unit()) * std::max(1.0f, c->radius - 2.0f);
        const float x = c->x + std::cos(a) * d, z = c->z + std::sin(a) * d;
        if (!openTile(t, x, z)) continue;
        to = Vec2{ std::floor(x) + 0.5f, std::floor(z) + 0.5f };
        return BehaviorWait::None;
    }
    return BehaviorWait::NotInClaim;
}

BehaviorWait roamSpot(World& w, const Terrain& t, const std::vector<Claim>& claims, ObjectId u, Rng& rng, Vec2& to) {
    const Transform* ut = w.get<Transform>(u);
    if (!ut) return BehaviorWait::NowhereToRoam;
    for (int attempt = 0; attempt < 12; ++attempt) {
        const float a = rng.unit() * TWO_PI, d = ROAM_STEP_MIN + rng.unit() * (ROAM_STEP_MAX - ROAM_STEP_MIN);
        const float x = ut->x + std::cos(a) * d, z = ut->z + std::sin(a) * d;
        if (!openTile(t, x, z) || claimAt(claims, x, z)) continue;
        to = Vec2{ std::floor(x) + 0.5f, std::floor(z) + 0.5f };
        return BehaviorWait::None;
    }
    return BehaviorWait::NowhereToRoam;
}

ObjectId restTarget(World& w, const std::vector<Claim>& claims, ObjectId u) {
    const Transform* ut = w.get<Transform>(u);
    if (!ut) return NO_OBJECT;
    std::vector<ObjectId> wanted;
    for (ObjectId o : w.view<PropOrder>()) {
        if (o != u) wanted.push_back(w.get<PropOrder>(o)->prop);
    }
    std::sort(wanted.begin(), wanted.end());
    const std::vector<ObjectId> occupied = occupiedProps(w);
    const Occupying* lyingIn = w.get<Occupying>(u);
    const int faction = factionOf(w, u);
    ObjectId best = NO_OBJECT;
    float bestD2 = 0.0f;
    bool bestOwned = false;
    for (ObjectId p : w.view<Prop, Transform>()) {
        const PropKind kind = w.get<Prop>(p)->kind;
        if (restedGainPerHour(kind) <= 0.0) continue;
        const bool ownBed = lyingIn && lyingIn->prop == p;
        if ((countIn(occupied, p) > 0 && !ownBed) || lockShutFor(w, claims, p, u)) continue;
        if (!bedOpenTo(w, p, u)) continue;   // a bed in another faction's building, not set Public
        if (std::binary_search(wanted.begin(), wanted.end(), p)) continue;
        const Transform* pt = w.get<Transform>(p);
        const float d2 = (pt->x - ut->x) * (pt->x - ut->x) + (pt->z - ut->z) * (pt->z - ut->z);
        if (d2 > REST_SEARCH_RANGE * REST_SEARCH_RANGE) continue;
        const Owner* own = w.get<Owner>(p);
        const bool owned = own && own->faction == faction;
        if (best == NO_OBJECT || (owned && !bestOwned) || (owned == bestOwned && d2 < bestD2)) {
            best = p;
            bestD2 = d2;
            bestOwned = owned;
        }
    }
    return best;
}

// How near the middle of its claim a rescued unit is put down when there is no bed for it
constexpr float RESCUE_PARK_REACH = 4.0f;

// The middle of the faction's nearest claim, an Altar's or a camp's
bool claimMiddle(const std::vector<Claim>& claims, int faction, float x, float z, Vec2& out) {
    float bestD2 = 0.0f;
    bool found = false;
    for (const Claim& c : claims) {
        if (c.owner != faction) continue;
        const float d2 = (c.x - x) * (c.x - x) + (c.z - z) * (c.z - z);
        if (!found || d2 < bestD2) {
            out = Vec2{ c.x, c.z };
            bestD2 = d2;
            found = true;
        }
    }
    return found;
}

ObjectId rescueTarget(World& w, ObjectId u) {
    const Transform* ut = w.get<Transform>(u);
    if (!ut) return NO_OBJECT;
    const int mine = factionOf(w, u);
    std::vector<ObjectId> fetched;
    for (ObjectId o : w.view<CarryOrder>()) {
        if (o != u) fetched.push_back(w.get<CarryOrder>(o)->target);
    }
    ObjectId best = NO_OBJECT;
    float bestD2 = RESCUE_RANGE * RESCUE_RANGE;
    for (ObjectId v : w.view<Unconscious, Transform>()) {
        if (v == u || w.has<Corpse>(v) || w.has<Carried>(v) || w.has<Mob>(v) || !factionAids(w, mine, factionOf(w, v))) continue;
        const ObjectId bed = occupiedProp(w, v);
        if (bed != NO_OBJECT && w.has<Prop>(bed) && restedGainPerHour(w.get<Prop>(bed)->kind) > 0.0) continue;
        if (std::find(fetched.begin(), fetched.end(), v) != fetched.end()) continue;
        const Transform* vt = w.get<Transform>(v);
        const float d2 = distSq(*vt, *ut);
        if (d2 <= bestD2) {
            best = v;
            bestD2 = d2;
        }
    }
    return best;
}

BehaviorWait spawnSpot(World& w, const Terrain& t, ObjectId u, Rng& rng, Vec2& to) {
    const SpawnHome* h = w.get<SpawnHome>(u);
    if (!h) return BehaviorWait::NowhereToRoam;
    for (int attempt = 0; attempt < 12; ++attempt) {
        const float a = rng.unit() * TWO_PI, d = std::sqrt(rng.unit()) * SPAWN_PATROL_RADIUS;
        const float x = h->x + std::cos(a) * d, z = h->z + std::sin(a) * d;
        if (!openTile(t, x, z)) continue;
        to = Vec2{ std::floor(x) + 0.5f, std::floor(z) + 0.5f };
        return BehaviorWait::None;
    }
    return BehaviorWait::NowhereToRoam;
}

// A free seat (chair, stool, bench...) of the unit's faction or in its claim: one of the few nearest, picked at random so that
// the units at recreation do not all crowd one seat
ObjectId seatTarget(World& w, const std::vector<Claim>& claims, ObjectId u, Rng& rng) {
    const Transform* ut = w.get<Transform>(u);
    if (!ut) return NO_OBJECT;
    std::vector<ObjectId> wanted;
    for (ObjectId o : w.view<PropOrder>()) {
        if (o != u) wanted.push_back(w.get<PropOrder>(o)->prop);
    }
    std::sort(wanted.begin(), wanted.end());
    const std::vector<ObjectId> occupied = occupiedProps(w);
    const int faction = factionOf(w, u);
    const int level = storeyOf(w, u);
    std::vector<std::pair<float, ObjectId>> found;
    for (ObjectId p : w.view<Prop, Transform>()) {
        if (!isSeat(w.get<Prop>(p)->kind) || w.has<Blueprint>(p) || storeyOf(w, p) != level) continue;
        if (countIn(occupied, p) > 0 || lockShutFor(w, claims, p, u) || std::binary_search(wanted.begin(), wanted.end(), p)) continue;
        const Owner* own = w.get<Owner>(p);
        if (own && own->faction != faction && !inOwnClaim(w, claims, p, faction)) continue;
        const Transform* pt = w.get<Transform>(p);
        const float d2 = (pt->x - ut->x) * (pt->x - ut->x) + (pt->z - ut->z) * (pt->z - ut->z);
        if (d2 > REST_SEARCH_RANGE * REST_SEARCH_RANGE) continue;
        found.push_back({ d2, p });
    }
    if (found.empty()) return NO_OBJECT;
    std::sort(found.begin(), found.end());
    const int few = (int)std::min<size_t>(found.size(), 4);
    return found[(size_t)rng.range(0, few - 1)].second;
}

// The nearest other unit of the faction that is at recreation and free to talk
ObjectId socialTarget(World& w, ObjectId u) {
    const Transform* ut = w.get<Transform>(u);
    if (!ut) return NO_OBJECT;
    const int faction = factionOf(w, u);
    const int level = storeyOf(w, u);
    ObjectId best = NO_OBJECT;
    float bestD2 = SOCIAL_SEARCH_RANGE * SOCIAL_SEARCH_RANGE;
    for (ObjectId v : w.view<Behaviors, Transform>()) {
        if (v == u || w.has<Mob>(v) || w.has<Carried>(v) || w.has<Corpse>(v) || outOfAction(w, v) || factionOf(w, v) != faction) continue;
        if (storeyOf(w, v) != level || activityOf(w, v) != Activity::Recreation) continue;
        if (const Combat* c = w.get<Combat>(v); c && c->target != NO_OBJECT) continue;
        const Transform* vt = w.get<Transform>(v);
        const float d2 = distSq(*vt, *ut);
        if (d2 < bestD2) {
            best = v;
            bestD2 = d2;
        }
    }
    return best;
}

// Where to stand to talk to the target: a little short of it, on the side the unit comes from
Vec2 socialSpot(World& w, ObjectId u, ObjectId target) {
    const Transform* ut = w.get<Transform>(u);
    const Transform* tt = w.get<Transform>(target);
    const float dx = ut->x - tt->x, dz = ut->z - tt->z, d = std::max(1e-3f, std::hypot(dx, dz));
    const float stop = std::min(1.4f, d);
    return Vec2{ tt->x + dx / d * stop, tt->z + dz / d * stop };
}

// Where to stand at one of a Game Table's four sides (0 north, 1 east, 2 south, 3 west)
Vec2 tableSeatPos(const Transform& table, int slot) {
    static const float DX[4] = { 0.0f, 1.0f, 0.0f, -1.0f }, DZ[4] = { -1.0f, 0.0f, 1.0f, 0.0f };
    const int k = ((slot % 4) + 4) % 4;
    return Vec2{ table.x + DX[k] * TABLE_SEAT_DISTANCE, table.z + DZ[k] * TABLE_SEAT_DISTANCE };
}

// A Game Table of the unit's faction (or in its claim) with a side free: one of the few nearest, a side picked at random
bool tableChoice(World& w, const Terrain& t, const std::vector<Claim>& claims, ObjectId u, Rng& rng, ObjectId& table, int& slot) {
    const Transform* ut = w.get<Transform>(u);
    if (!ut) return false;
    const int faction = factionOf(w, u);
    const int level = storeyOf(w, u);
    std::map<ObjectId, int> taken;   // table to the sides in use, as bits
    for (ObjectId v : w.view<Behaviors>()) {
        const Behaviors* vb = w.get<Behaviors>(v);
        if (v != u && vb->busy && vb->step.kind == BehaviorKind::Play && vb->slot >= 0) taken[vb->stepProp] |= 1 << vb->slot;
    }
    struct Option { float d2; ObjectId table; int slot; };
    std::vector<Option> found;
    for (ObjectId p : w.view<Prop, Transform>()) {
        if (w.get<Prop>(p)->kind != PropKind::GameTable || w.has<Blueprint>(p) || storeyOf(w, p) != level) continue;
        const Owner* own = w.get<Owner>(p);
        if (own && own->faction != faction && !inOwnClaim(w, claims, p, faction)) continue;
        const Transform* pt = w.get<Transform>(p);
        const float d2 = (pt->x - ut->x) * (pt->x - ut->x) + (pt->z - ut->z) * (pt->z - ut->z);
        if (d2 > REST_SEARCH_RANGE * REST_SEARCH_RANGE) continue;
        const int mask = taken.count(p) ? taken[p] : 0;
        for (int k = 0; k < 4; ++k) {
            const Vec2 at = tableSeatPos(*pt, k);
            if ((mask & (1 << k)) || !openTile(t, at.x, at.z)) continue;
            found.push_back({ d2, p, k });
        }
    }
    if (found.empty()) return false;
    std::sort(found.begin(), found.end(), [](const Option& a, const Option& b) { return a.d2 < b.d2; });
    const Option& pick = found[(size_t)rng.range(0, (int)std::min<size_t>(found.size(), 4) - 1)];
    table = pick.table;
    slot = pick.slot;
    return true;
}

bool isDanceCenter(PropKind k) { return k == PropKind::Campfire || k == PropKind::Altar; }
int danceCapacity(PropKind k) { return k == PropKind::Altar ? DANCE_ALTAR_DANCERS : DANCE_CAMPFIRE_DANCERS; }

// Where dancer slot of a ring is at this moment: the ring is shared, so they keep their places. Everyone steps on round the circle,
// then back a little the other way, and on again.
Vec2 dancePoint(const Transform& center, PropKind kind, int slot, double seconds) {
    const int cap = danceCapacity(kind);
    const std::vector<Vec2> ring = formationOffsets(FormationShape::Circle, cap, kind == PropKind::Altar ? DANCE_ALTAR_SPACING : DANCE_CAMPFIRE_SPACING);
    const Vec2 o = ring[(size_t)std::clamp(slot, 0, cap - 1)];
    // Clockwise and wider, then counterclockwise and narrower; at 0 seconds the circle is at its default place
    const float phase = std::sin(TWO_PI * (float)std::fmod(seconds, (double)DANCE_PERIOD) / DANCE_PERIOD);
    const float turn = DANCE_TURN * phase, grow = 1.0f + DANCE_GROW * phase;
    const float c = std::cos(turn), s = std::sin(turn);
    return Vec2{ center.x + (o.x * c - o.z * s) * grow, center.z + (o.x * s + o.z * c) * grow };
}

double danceRingTime(World& w, ObjectId center) {
    const std::map<ObjectId, double>& m = w.resource<DanceRings>().seconds;
    const auto it = m.find(center);
    return it == m.end() ? 0.0 : it->second;
}

// Inside the claim of an Altar of the faction: a camp's own small claim does not count
bool inAltarClaim(const std::vector<Claim>& claims, const Transform& at, int faction) {
    for (const Claim& c : claims) {
        if (c.owner != faction || c.camp) continue;
        if ((at.x - c.x) * (at.x - c.x) + (at.z - c.z) * (at.z - c.z) <= c.radius * c.radius) return true;
    }
    return false;
}

// A dancer stops: it moves at once to the open place nearest its Altar (where it stands, if it has none), so it goes back to
// normal collision without being stuck in anyone
void leaveDance(World& w, Terrain& t, const std::vector<Claim>& claims, ObjectId u) {
    Transform* ut = w.get<Transform>(u);
    if (!ut) return;
    const int faction = factionOf(w, u);
    float ax = ut->x, az = ut->z;
    float best = -1.0f;
    for (const Claim& c : claims) {
        if (c.owner != faction || c.camp) continue;
        const float d2 = (ut->x - c.x) * (ut->x - c.x) + (ut->z - c.z) * (ut->z - c.z);
        if (d2 <= c.radius * c.radius && (best < 0.0f || d2 < best)) {
            best = d2;
            ax = c.x;
            az = c.z;
        }
    }
    struct Body2 { float x, z, r; };
    std::vector<Body2> others;
    for (ObjectId v : w.view<Entity, Transform>()) {
        if (v == u || w.has<Carried>(v) || storeyOf(w, v) != 0) continue;
        const Transform* vt = w.get<Transform>(v);
        others.push_back({ vt->x, vt->z, w.get<Entity>(v)->radius });
    }
    const float mine = w.get<Entity>(u)->radius;
    for (int ring = 0; ring <= 60; ++ring) {
        const float r = (float)ring * 0.5f;
        const int around = ring == 0 ? 1 : std::max(8, (int)(TWO_PI * r / 0.5f));
        for (int k = 0; k < around; ++k) {
            const float a = TWO_PI * (float)k / (float)around;
            const float x = ax + std::cos(a) * r, z = az + std::sin(a) * r;
            if (!openTile(t, x, z)) continue;
            bool clear = true;
            for (const Body2& o : others) {
                if ((o.x - x) * (o.x - x) + (o.z - z) * (o.z - z) < (mine + o.r) * (mine + o.r)) {
                    clear = false;
                    break;
                }
            }
            if (!clear) continue;
            ut->x = x;
            ut->z = z;
            ut->y = t.standHeight(x, z);
            return;
        }
    }
}

// The nearest Campfire or Altar of the unit's faction (or in its claim) with a place free on its circle
bool danceChoice(World& w, const std::vector<Claim>& claims, ObjectId u, Rng& rng, ObjectId& center, int& slot) {
    const Transform* ut = w.get<Transform>(u);
    if (!ut) return false;
    const int faction = factionOf(w, u);
    std::map<ObjectId, std::vector<char>> taken;
    for (ObjectId v : w.view<Behaviors>()) {
        const Behaviors* vb = w.get<Behaviors>(v);
        if (v == u || !vb->busy || vb->step.kind != BehaviorKind::Dance || vb->slot < 0) continue;
        std::vector<char>& m = taken[vb->stepProp];
        if ((int)m.size() <= vb->slot) m.resize((size_t)vb->slot + 1, 0);
        m[(size_t)vb->slot] = 1;
    }
    float bestD2 = 40.0f * 40.0f;
    ObjectId best = NO_OBJECT;
    for (ObjectId p : w.view<Prop, Transform>()) {
        const PropKind kind = w.get<Prop>(p)->kind;
        if (!isDanceCenter(kind) || w.has<Blueprint>(p) || storeyOf(w, p) != 0 || storeyOf(w, u) != 0) continue;
        const Owner* own = w.get<Owner>(p);
        if (own && own->faction != faction) continue;
        const Transform* pt = w.get<Transform>(p);
        if (!inAltarClaim(claims, *pt, faction)) continue;   // only round an Altar, or a Campfire within an Altar's claim
        const float d2 = (pt->x - ut->x) * (pt->x - ut->x) + (pt->z - ut->z) * (pt->z - ut->z);
        if (d2 >= bestD2) continue;
        int used = 0;
        if (const auto it = taken.find(p); it != taken.end()) used = (int)std::count(it->second.begin(), it->second.end(), (char)1);
        if (used >= danceCapacity(kind)) continue;
        best = p;
        bestD2 = d2;
    }
    if (best == NO_OBJECT) return false;
    const int cap = danceCapacity(w.get<Prop>(best)->kind);
    std::vector<int> free;
    const auto it = taken.find(best);
    for (int k = 0; k < cap; ++k) {
        if (it == taken.end() || (int)it->second.size() <= k || !it->second[(size_t)k]) free.push_back(k);
    }
    if (free.empty()) return false;
    center = best;
    slot = free[(size_t)rng.range(0, (int)free.size() - 1)];
    return true;
}

bool startStep(World& w, Terrain& t, const std::vector<Claim>& claims, ObjectId u, Behaviors& b, BehaviorRule& r, Rng& rng) {
    if (r.kind == BehaviorKind::Play) {
        r.why = BehaviorWait::None;
        if (w.has<Mob>(u) || w.has<Carrying>(u) || w.has<Occupying>(u)) return false;
        ObjectId table = NO_OBJECT;
        int slot = 0;
        if (!tableChoice(w, t, claims, u, rng, table, slot)) {
            r.why = BehaviorWait::NoTable;
            return false;
        }
        b.busy = true;
        b.step = r;
        b.stepProp = table;
        b.slot = slot;
        b.began = false;
        b.social = 0.0f;
        autoWalk(w, t, u, tableSeatPos(*w.get<Transform>(table), slot), storeyOf(w, table));
        return true;
    }
    if (r.kind == BehaviorKind::Dance) {
        r.why = BehaviorWait::None;
        if (w.has<Mob>(u) || w.has<Carrying>(u) || w.has<Occupying>(u)) return false;
        ObjectId center = NO_OBJECT;
        int slot = 0;
        if (!danceChoice(w, claims, u, rng, center, slot)) {
            r.why = BehaviorWait::NoDancePlace;
            return false;
        }
        b.busy = true;
        b.step = r;
        b.stepProp = center;
        b.slot = slot;
        b.began = false;
        b.social = 0.0f;
        autoWalk(w, t, u, dancePoint(*w.get<Transform>(center), w.get<Prop>(center)->kind, slot, danceRingTime(w, center)), 0);
        return true;
    }
    if (r.kind == BehaviorKind::SitDown) {
        r.why = BehaviorWait::None;
        if (w.has<Mob>(u) || w.has<OccupyBan>(u) || w.has<Carrying>(u) || w.has<Occupying>(u)) return false;
        const ObjectId seat = seatTarget(w, claims, u, rng);
        if (seat == NO_OBJECT) {
            r.why = BehaviorWait::NoSeat;
            return false;
        }
        b.busy = true;
        b.step = r;
        b.stepProp = seat;
        b.began = false;
        b.social = 0.0f;
        autoGo(w, t, u, seat, PropAction::Occupy);
        return true;
    }
    if (r.kind == BehaviorKind::Socialize) {
        r.why = BehaviorWait::None;
        if (w.has<Mob>(u) || w.has<Carrying>(u) || w.has<Occupying>(u)) return false;
        const ObjectId target = socialTarget(w, u);
        if (target == NO_OBJECT) {
            r.why = BehaviorWait::NoCompany;
            return false;
        }
        b.busy = true;
        b.step = r;
        b.stepProp = target;
        b.began = false;
        b.social = 0.0f;
        autoWalk(w, t, u, socialSpot(w, u, target), storeyOf(w, target));
        return true;
    }
    if (isBuildTask(r.kind)) return startBuildStep(w, t, u, b, r);
    if (r.kind == BehaviorKind::AutoHaulStall) return startStallHaulStep(w, t, u, b, r);
    if (r.kind == BehaviorKind::ManStall) return startManStallStep(w, t, u, b, r);
    if (r.kind == BehaviorKind::DealWithDowned) return startDealStep(w, t, u, b, r);
    if (r.kind == BehaviorKind::AutoCraft) return startCraftStep(w, t, u, b, r);
    if (r.kind == BehaviorKind::AutoHaulGround) {
        if (w.has<Mob>(u) || w.has<Carrying>(u)) return false;
        const ObjectId item = groundHaulTarget(w, claims, u, b, r.why);
        if (item == NO_OBJECT) return false;
        const Transform* it = w.get<Transform>(item);
        b.busy = true;
        b.step = r;
        b.stepProp = item;
        autoWalk(w, t, u, Vec2{ it->x, it->z }, storeyOf(w, item));
        return true;
    }
    if (r.kind == BehaviorKind::Rescue) {
        r.why = BehaviorWait::None;
        if (w.has<Mob>(u) || w.has<Carrying>(u)) return false;
        const ObjectId target = rescueTarget(w, u);
        if (target == NO_OBJECT) return false;
        const Transform* tt = w.get<Transform>(target);
        if (restTarget(w, claims, u) == NO_OBJECT) {
            // No bed free: it is still carried in, to near the middle of the claim, unless it is already there
            Vec2 mid{ 0.0f, 0.0f };
            if (!claimMiddle(claims, factionOf(w, u), tt->x, tt->z, mid) ||
                std::hypot(tt->x - mid.x, tt->z - mid.z) <= RESCUE_PARK_REACH) {
                r.why = BehaviorWait::NoBed;
                return false;
            }
        }
        b.busy = true;
        b.step = r;
        b.stepProp = NO_OBJECT;
        autoWalk(w, t, u, Vec2{ tt->x, tt->z }, storeyOf(w, target));
        w.add(u, CarryOrder{ target });
        return true;
    }
    if (r.kind == BehaviorKind::Sleep) {
        r.why = BehaviorWait::None;
        if (activityOf(w, u) != Activity::Sleep || !w.has<Needs>(u) || w.has<Mob>(u) || w.has<OccupyBan>(u)) return false;
        const ObjectId bed = restTarget(w, claims, u);
        if (bed == NO_OBJECT) {
            r.why = BehaviorWait::NoBed;
            return false;
        }
        b.busy = true;
        b.step = r;
        b.stepProp = bed;
        if (const Occupying* oc = w.get<Occupying>(u); oc && oc->prop == bed) return true;   // already in it
        autoGo(w, t, u, bed, PropAction::Occupy);
        return true;
    }
    if (r.kind == BehaviorKind::Rest) {
        const Needs* n = w.get<Needs>(u);
        r.why = BehaviorWait::None;
        if (!n || w.has<Mob>(u) || percentOf(n->rested, n->maxRested) >= r.percent) return false;
        if (w.has<OccupyBan>(u)) return false;
        const ObjectId bed = restTarget(w, claims, u);
        if (bed == NO_OBJECT) {
            r.why = BehaviorWait::NoBed;
            return false;
        }
        b.busy = true;
        b.step = r;
        b.stepProp = bed;
        autoGo(w, t, u, bed, PropAction::Occupy);
        return true;
    }
    if (isWalkBehavior(r.kind)) {
        Vec2 to{ 0.0f, 0.0f };
        r.why = r.kind == BehaviorKind::Patrol ? patrolSpot(w, t, claims, u, rng, to)
              : r.kind == BehaviorKind::PatrolSpawn ? spawnSpot(w, t, u, rng, to) : roamSpot(w, t, claims, u, rng, to);
        if (r.why != BehaviorWait::None) return false;
        b.busy = true;
        b.step = r;
        b.stepProp = NO_OBJECT;
        autoWalk(w, t, u, to);
        return true;
    }
    ObjectId prop = NO_OBJECT;
    PropAction action = PropAction::Use;
    if (isHaul(r.kind)) {
        r.why = haulWait(w, claims, u, r);
        if (r.why != BehaviorWait::None) return false;
        prop = r.prop;
        action = PropAction::Store;
    } else {
        const int item = propInfo((PropKind)r.propKind).item;
        // Wood and stone: with no stockpile of their kind that has room, the task waits; with a load, it is put away there
        if (stockpileFor(item) != ItemFilter::Any) {
            const ObjectId pile = stockpileWithRoom(w, claims, u, item);
            if (pile == NO_OBJECT) {
                r.why = BehaviorWait::NoStockpile;
                return false;
            }
            if (harvestLoaded(w, claims, u, b, item)) {
                r.why = BehaviorWait::None;
                b.busy = true;
                b.step = r;
                b.stepProp = pile;
                autoGo(w, t, u, pile, PropAction::Store);
                return true;
            }
            if ((prop = harvestTarget(w, claims, u, r.propKind)) == NO_OBJECT) {
                r.why = BehaviorWait::NoProp;
                return false;
            }
            r.why = BehaviorWait::None;
            b.busy = true;
            b.step = r;
            b.stepProp = prop;
            autoGo(w, t, u, prop, action);
            return true;
        }
        if (fullFor(w, u, item)) r.why = BehaviorWait::CarryFull;
        else if (harvestLoaded(w, claims, u, b, item)) r.why = BehaviorWait::LoadWaiting;
        else if ((prop = harvestTarget(w, claims, u, r.propKind)) == NO_OBJECT) r.why = BehaviorWait::NoProp;
        if (r.why != BehaviorWait::None) return false;
    }
    r.why = BehaviorWait::None;
    b.busy = true;
    b.step = r;
    b.stepProp = prop;
    autoGo(w, t, u, prop, action);
    return true;
}
}

namespace {
bool tryStep(World& w, Terrain& t, const std::vector<Claim>& claims, ObjectId u, Behaviors& b, BehaviorRule& r, Rng& rng) {
    if (!isAutoStep(r.kind) || !r.on || r.cooldown > 0.0f) return false;
    if (!scheduleAllows(w, u, r.kind)) return false;   // not at this hour of its schedule
    if (r.kind == BehaviorKind::Socialize) {
        // Rested and comfortable (90% of each): it would rather dance, if it can
        const Needs* n = w.get<Needs>(u);
        if (n && n->rested * 10 >= n->maxRested * 9 && n->comfort * 10 >= n->maxComfort * 9) {
            for (BehaviorRule& d : b.rules) {
                if (d.kind != BehaviorKind::Dance || !d.on || d.cooldown > 0.0f || !scheduleAllows(w, u, d.kind)) continue;
                if (startStep(w, t, claims, u, b, d, rng)) return true;
                d.cooldown = BEHAVIOR_RETRY_SECONDS;
                break;
            }
        }
    }
    if (startStep(w, t, claims, u, b, r, rng)) return true;
    r.cooldown = r.why == BehaviorWait::NoBed ? NO_BED_RETRY_SECONDS : BEHAVIOR_RETRY_SECONDS;
    return false;
}

BehaviorRule* rescueRule(Behaviors& b) {
    for (BehaviorRule& r : b.rules) {
        if (r.kind == BehaviorKind::Rescue) return &r;
    }
    return nullptr;
}
}

void systems_detail::autoWalk(World& w, Terrain& t, ObjectId u, Vec2 to, int storey) {
    clearOrdersButFight(w, u);
    w.remove<IdleSpot>(u);
    walkSolo(w, t, u, to, storey);
}

void systems_detail::stopAutoStep(World& w, ObjectId u, Behaviors& b) {
    if (const DealJob* dj = w.get<DealJob>(u); dj && !dj->ordered) {
        if (const Carrying* cr = w.get<Carrying>(u); cr && cr->unit == dj->target) putDown(w, u);
        w.remove<DealJob>(u);
        w.remove<SoloMove>(u);
        w.remove<Route>(u);
    }
    if (w.has<BuildJob>(u) || w.has<StallJob>(u) || w.has<CraftJob>(u)) {
        w.remove<BuildJob>(u);
        w.remove<StallJob>(u);
        w.remove<CraftJob>(u);
        w.remove<SoloMove>(u);
        w.remove<Route>(u);
    }
    if (b.busy && b.step.kind == BehaviorKind::ManStall) {
        w.remove<SoloMove>(u);
        w.remove<Route>(u);
    }
    if (const PropOrder* po = w.get<PropOrder>(u); po && po->automatic) {
        w.remove<PropOrder>(u);
        w.remove<SoloMove>(u);
    }
    if (b.busy && (isWalkBehavior(b.step.kind) || b.step.kind == BehaviorKind::AutoHaulGround)) w.remove<SoloMove>(u);
    if (const Occupying* oc = w.get<Occupying>(u); oc && b.busy && (isBedStep(b.step.kind) || b.step.kind == BehaviorKind::SitDown) &&
                                                  oc->prop == b.stepProp) {
        w.remove<Occupying>(u);
    }
    if (const Working* wk = w.get<Working>(u); wk && !wk->unlocking && b.busy && wk->prop == b.stepProp) w.remove<Working>(u);
    b.busy = false;
}

bool systems_detail::openTile(const Terrain& t, float x, float z) {
    return x >= 0.0f && z >= 0.0f && x < t.size() && z < t.size() && t.walkable((int)std::floor(x), (int)std::floor(z));
}

void systems_detail::stepOffProp(World& w, Terrain& t, ObjectId u, ObjectId prop) {
    Transform* ut = w.get<Transform>(u);
    const Transform* pt = w.get<Transform>(prop);
    const Prop* pp = w.get<Prop>(prop);
    if (!ut || !pt || !pp) return;
    const Footprint& pf = propInfo(pp->kind).foot;
    const int cx = (int)std::floor(pt->x), cz = (int)std::floor(pt->z);
    auto onOtherProp = [&](float x, float z) {
        for (ObjectId o : w.view<Prop, Transform>()) {
            if (o == prop) continue;
            const PropInfo& oi = propInfo(w.get<Prop>(o)->kind);
            if (oi.airship || oi.wheelbarrow) continue;
            const Transform* ot = w.get<Transform>(o);
            const float dx = x - ot->x, dz = z - ot->z;
            const float reach = footprintReach(oi.foot) + PROP_BLOCK_RADIUS;
            const float d2 = distSq(x, z, ot->x, ot->z);
            if (d2 > reach * reach) continue;
            if (insideFootprint(oi.foot, dx, dz, w.get<Prop>(o)->yaw)) return true;
            if (d2 < PROP_BLOCK_RADIUS * PROP_BLOCK_RADIUS) return true;
        }
        return false;
    };
    // The floor the prop is on: the unit comes off onto that floor, not the ground under it
    const int level = storeyOf(w, prop);
    const BuildingMap& bm = cachedBuildingMap(w);
    auto standAt = [&](float x, float y, float z) {
        ut->x = x;
        ut->y = y;
        ut->z = z;
        if (level > 0) {
            const auto fit = bm.foundation.find(tileKey((int)std::floor(x), (int)std::floor(z)));
            w.add(u, Upstairs{ fit != bm.foundation.end() ? fit->second : NO_OBJECT, level });
        } else {
            w.remove<Upstairs>(u);
        }
    };
    const int MAX_RING = level > 0 ? 5 : 3;
    bool haveFallback = false;
    float fx = 0.0f, fy = 0.0f, fz = 0.0f;
    for (int ring = 1; ring <= MAX_RING; ++ring) {
        std::vector<std::pair<int, int>> tiles;
        for (int dz = -ring; dz <= ring; ++dz) {
            for (int dx = -ring; dx <= ring; ++dx) {
                if (std::max(std::abs(dx), std::abs(dz)) == ring) tiles.push_back({ dx, dz });
            }
        }
        std::stable_sort(tiles.begin(), tiles.end(), [](const auto& a, const auto& b) {
            return a.first * a.first + a.second * a.second < b.first * b.first + b.second * b.second;
        });
        for (const auto& [dx, dz] : tiles) {
            const int tx = cx + dx, tz = cz + dz;
            const float x = tx + 0.5f, z = tz + 0.5f;
            if (insideFootprint(pf, x - pt->x, z - pt->z, pp->yaw)) continue;
            bool walk = false;
            float h = 0.0f;
            if (level > 0) {
                // Upstairs: a tile of the same floor
                if (!floorSurfaceOn(w, bm, tx, tz, level)) continue;
                h = t.heightAt(x, z) + storeyBaseHeight(level);
            } else if (!t.tileGround(tx, tz, walk, h) || !walk) {
                continue;
            }
            if (!onOtherProp(x, z)) {
                standAt(x, h, z);
                return;
            }
            if (!haveFallback) {
                haveFallback = true;
                fx = x;
                fy = h;
                fz = z;
            }
        }
    }
    // Nothing clear: the best of the crowded spots, else where the prop stands (its own floor and height), so a unit is never
    // left with nowhere to go
    if (haveFallback) standAt(fx, fy, fz);
    else standAt(pt->x, pt->y, pt->z);
}

void leaveOpenLocksSystem(World& w, Terrain& t) {
    for (ObjectId u : w.view<Occupying>()) {
        if (w.has<Commandable>(u) || outCold(w, u) || w.has<Carried>(u)) continue;
        const ObjectId prop = w.get<Occupying>(u)->prop;
        const Lock* lock = w.get<Lock>(prop);
        if (!lock || lock->locked) continue;
        w.remove<Occupying>(u);
        stepOffProp(w, t, u, prop);
        queueLog(w, langf("MISC_BREAKS_FREE", { nameOf(w, u) }), LogKind::Other, npcOnly(w, u));
    }
}

void automationSystem(World& w, Terrain& t, float dt, Rng& rng) {
    const std::vector<Claim> claims = cachedClaims(w);
    {
        // Whoever stopped dancing, for any reason, goes to an open place; the circles that still have dancers keep turning, and the
        // rest are at rest
        for (ObjectId u : w.view<Dancing, Transform>()) {
            const Behaviors* db = w.get<Behaviors>(u);
            if (db && db->busy && db->began && db->step.kind == BehaviorKind::Dance && w.has<Prop>(db->stepProp)) continue;
            w.remove<Dancing>(u);
            leaveDance(w, t, claims, u);
        }
        DanceRings& rings = w.resource<DanceRings>();
        std::vector<ObjectId> active;
        for (ObjectId u : w.view<Dancing>()) {
            const Behaviors* db = w.get<Behaviors>(u);
            if (db) active.push_back(db->stepProp);
        }
        std::sort(active.begin(), active.end());
        for (auto it = rings.seconds.begin(); it != rings.seconds.end();) {
            if (std::binary_search(active.begin(), active.end(), it->first)) {
                it->second += dt;
                ++it;
            } else {
                it = rings.seconds.erase(it);
            }
        }
        for (ObjectId p : active) rings.seconds.try_emplace(p, 0.0);
        // Each dancer's foot lands once a hop: a light thud, as the clock carries its hop round past the bottom
        DanceHops& hops = w.resource<DanceHops>();
        const double before = hops.clock;
        hops.clock += dt;
        for (ObjectId u : w.view<Dancing>()) {
            const Behaviors* db = w.get<Behaviors>(u);
            if (!db || !db->busy || !db->began) continue;
            const double offset = (double)danceHopPhase(0.0, u);
            if (std::floor(hops.clock / DANCE_HOP_SECONDS + offset) > std::floor(before / DANCE_HOP_SECONDS + offset)) {
                queueSoundFrom(w, Sfx::DanceStep, u);
            }
        }
    }
    for (ObjectId u : w.view<Behaviors, Transform>()) {
        Behaviors* b = w.get<Behaviors>(u);
        bool any = false;
        for (BehaviorRule& r : b->rules) {
            if (!isAutoStep(r.kind)) continue;
            r.cooldown = std::max(0.0f, r.cooldown - dt);
            if (!r.on) r.why = BehaviorWait::None;
            any = any || r.on;
        }
        if (b->busy) {
            const BehaviorRule* current = stepRule(*b);
            if (!current || !current->on) stopAutoStep(w, u, *b);
        }
        if (!any && b->seek == NO_OBJECT && !b->seeking) continue;
        const bool yours = w.has<Commandable>(u);
        const float resume = yours ? AUTO_RESUME_SECONDS : 0.0f;
        const Needs* n = w.get<Needs>(u);
        if (outOfAction(w, u) || (n && n->hp <= 0)) {
            b->busy = false;
            b->seeking = false;
            b->recovered = 0.0f;
            continue;
        }
        const bool night = activityOf(w, u) == Activity::Sleep;   // the unit's hours for sleeping
        {
            const ObjectId bed = occupiedProp(w, u);
            const Prop* bp = bed != NO_OBJECT ? w.get<Prop>(bed) : nullptr;
            bool leaveOn = false;
            for (const BehaviorRule& r : b->rules) leaveOn = leaveOn || (r.on && r.kind == BehaviorKind::LeaveBed);
            const bool sleeping = night && b->busy && b->step.kind == BehaviorKind::Sleep;   // stays in bed till morning
            if (leaveOn && !sleeping && bp && restedGainPerHour(bp->kind) > 0.0 && n && n->hp >= n->maxHp && n->rested >= n->maxRested) {
                b->recovered += dt;
                if (b->recovered >= LEAVE_BED_SECONDS) {
                    w.remove<Occupying>(u);
                    stepOffProp(w, t, u, bed);
                    b->recovered = 0.0f;
                    if (b->busy && isBedStep(b->step.kind)) b->busy = false;
                }
            } else {
                b->recovered = 0.0f;
            }
        }
        const Combat* c = w.get<Combat>(u);
        if ((c && c->target != NO_OBJECT) || w.has<AttackOrder>(u) || w.has<Fleeing>(u)) {
            if (b->busy && !w.has<Fleeing>(u)) stopAutoStep(w, u, *b);
            if (b->seeking) w.remove<SoloMove>(u);
            b->seeking = false;
            b->wait = resume;
            continue;
        }
        // The Work hours are over: what is under way stops, and the unit goes on to sleeping or recreation
        if (b->busy && isWorkTask(b->step.kind) && !scheduleAllows(w, u, b->step.kind)) {
            stopAutoStep(w, u, *b);
            w.remove<SoloMove>(u);
            b->wait = 0.0f;
            continue;
        }
        // A building task, a Stall order or a Deal with Downed trip runs by itself (buildJobSystem, commerce, dealJobSystem),
        // and ends its own step
        if (w.has<BuildJob>(u) || w.has<StallJob>(u) || w.has<DealJob>(u) || w.has<CraftJob>(u)) continue;
        // A Deal with Downed trip ended by an order of yours meanwhile
        if (b->busy && b->step.kind == BehaviorKind::DealWithDowned) {
            b->busy = false;
            if (BehaviorRule* r = stepRule(*b)) r->cooldown = BEHAVIOR_RETRY_SECONDS;
            continue;
        }
        if (b->seeking) {
            const SoloMove* so = w.get<SoloMove>(u);
            const bool walking = so && so->next < so->waypoints.size();
            if (b->seek == NO_OBJECT || !walking || flatGap(w, u, b->seek) <= SHARE_RANGE) {
                if (walking) w.remove<SoloMove>(u);
                b->seeking = false;
            } else if (onOtherOrder(w, u, *b)) {
                b->seeking = false;
                b->wait = resume;
            }
            continue;
        }
        if (b->seek != NO_OBJECT && w.has<Transform>(b->seek) && flatGap(w, u, b->seek) > SHARE_RANGE &&
            !onOtherOrder(w, u, *b) && (b->busy || b->wait <= 0.0f)) {
            if (b->busy) stopAutoStep(w, u, *b);
            const Transform* gt = w.get<Transform>(b->seek);
            const Transform* ut = w.get<Transform>(u);
            const float dx = gt->x - ut->x, dz = gt->z - ut->z, d = std::max(1e-3f, std::hypot(dx, dz));
            const float stop = std::min(SHARE_WALK_STOP, d);
            leaveSquad(w, u);
            w.remove<IdleSpot>(u);
            walkSolo(w, t, u, Vec2{ gt->x - dx / d * stop, gt->z - dz / d * stop }, storeyOf(w, b->seek));
            b->seeking = true;
            continue;
        }
        if (!any) {
            b->wait = onOtherOrder(w, u, *b) ? resume : std::max(0.0f, b->wait - dt);
            continue;
        }
        const PropOrder* po = w.get<PropOrder>(u);
        const SoloMove* walk = w.get<SoloMove>(u);
        const bool walkingStep = b->busy && isWalkBehavior(b->step.kind) && walk && walk->next < walk->waypoints.size();
        const bool going = (po && po->automatic) || walkingStep;
        const Working* wk = w.get<Working>(u);
        const bool working = b->busy && wk && !wk->unlocking && wk->prop == b->stepProp;

        // Whenever what it carries changes, a haul task above the step under way comes first: the step stops and the
        // haul starts, if it has something to take there (harvesting, patrolling and the like; not a bed or a rescue)
        if (const std::uint64_t print = carriedPrint(w, u); print != b->carried) {
            b->carried = print;
            if (b->busy && !isHaul(b->step.kind) && !isBedStep(b->step.kind) && b->step.kind != BehaviorKind::Rescue) {
                bool hauled = false;
                for (BehaviorRule& r : b->rules) {
                    if (sameBehavior(r, b->step)) break;
                    if (!r.on || !isHaul(r.kind) || !haulable(w, claims, u, r)) continue;
                    stopAutoStep(w, u, *b);
                    w.remove<SoloMove>(u);
                    r.cooldown = 0.0f;
                    if ((hauled = tryStep(w, t, claims, u, *b, r, rng))) break;
                }
                if (hauled) continue;
            }
        }
        if (b->busy && b->step.kind == BehaviorKind::Rescue) {
            const PropOrder* rpo = w.get<PropOrder>(u);
            const bool yoursNow = w.has<ActiveSquadMember>(u) || (rpo && !rpo->automatic) || hasPlayerOrder(w, u);
            if (yoursNow) {
                b->busy = false;
                b->wait = resume;
                continue;
            }
            if (w.has<CarryOrder>(u)) continue;
            bool noBed = false;
            if (!w.has<Carrying>(u)) w.remove<RescueWalk>(u);
            if (w.has<Carrying>(u)) {
                if (rpo && rpo->automatic) continue;
                const ObjectId bed = restTarget(w, claims, u);
                if (bed != NO_OBJECT && bed != b->stepProp) {
                    w.remove<RescueWalk>(u);
                    b->stepProp = bed;
                    autoGo(w, t, u, bed, PropAction::Place);
                    continue;
                }
                noBed = bed == NO_OBJECT;
                if (noBed) {
                    // Carried to near the middle of its claim first, and put down there
                    const Transform* ct = w.get<Transform>(u);
                    Vec2 mid{ 0.0f, 0.0f };
                    if (ct && claimMiddle(claims, factionOf(w, u), ct->x, ct->z, mid) &&
                        std::hypot(ct->x - mid.x, ct->z - mid.z) > RESCUE_PARK_REACH) {
                        RescueWalk* rw = w.get<RescueWalk>(u);
                        if (!rw) {
                            w.add(u, RescueWalk{});
                            autoWalk(w, t, u, mid, 0);
                            continue;
                        }
                        rw->left -= dt;
                        if (rw->left > 0.0f && walkingNow(w, u)) continue;
                    }
                    w.remove<RescueWalk>(u);
                }
                putDown(w, u);
            }
            b->busy = false;
            w.remove<SoloMove>(u);
            if (BehaviorRule* r = stepRule(*b)) {
                r->cooldown = noBed ? NO_BED_RETRY_SECONDS : BEHAVIOR_RETRY_SECONDS;
                if (noBed) r->why = BehaviorWait::NoBed;
            }
            continue;
        }
        // A unit out cold that it can rescue comes before lying down
        if (going && b->busy && b->step.kind == BehaviorKind::Rest) {
            BehaviorRule* rescue = rescueRule(*b);
            if (rescue && tryStep(w, t, claims, u, *b, *rescue, rng)) continue;
        }
        if (b->busy && b->step.kind == BehaviorKind::Rest) {
            const Occupying* oc = w.get<Occupying>(u);
            if (oc && oc->prop == b->stepProp) {
                const ObjectId bed = oc->prop;
                BehaviorRule* rescue = rescueRule(*b);
                if (rescue && tryStep(w, t, claims, u, *b, *rescue, rng)) {
                    stepOffProp(w, t, u, bed);
                    const CarryOrder* fetch = w.get<CarryOrder>(u);
                    const Transform* to = fetch ? w.get<Transform>(fetch->target) : nullptr;
                    if (to) planSolo(w, t, u, Vec2{ to->x, to->z });
                    continue;
                }
                if (n && n->rested < n->maxRested) continue;
                // Rested by night: stays in bed and sleeps, if it has Sleep on
                if (night) {
                    const auto sleep = std::find_if(b->rules.begin(), b->rules.end(),
                                                    [](const BehaviorRule& r) { return r.on && r.kind == BehaviorKind::Sleep; });
                    if (sleep != b->rules.end()) {
                        b->step = *sleep;
                        continue;
                    }
                }
                w.remove<Occupying>(u);
                b->busy = false;
                if (BehaviorRule* r = stepRule(*b)) r->cooldown = BEHAVIOR_RETRY_SECONDS;
                continue;
            }
        }
        // Fetching an item off the ground: picked up within reach, given up if it is gone or out of reach
        if (b->busy && b->step.kind == BehaviorKind::AutoHaulGround && w.has<Output>(b->stepProp) && w.has<Prop>(b->stepProp)) {
            // Carrying out what an Advanced Crafting prop has made
            const SoloMove* so = w.get<SoloMove>(u);
            const bool walking = (so && so->next < so->waypoints.size()) || w.has<Route>(u) || w.has<Climbing>(u);
            if (flatGap(w, u, b->stepProp) <= PROP_REACH + 0.3f) {
                takeCraftOutput(w, u, b->stepProp);
                w.remove<SoloMove>(u);
                b->busy = false;
                continue;
            }
            if (walking && !outputEmpty(w, b->stepProp)) continue;
            if (!outputEmpty(w, b->stepProp)) noteTaskFailed(w, u, behaviorName(b->step));   // stopped short of it
            w.remove<SoloMove>(u);
            b->busy = false;
            if (BehaviorRule* r = stepRule(*b)) r->cooldown = BEHAVIOR_RETRY_SECONDS;
            continue;
        }
        if (b->busy && b->step.kind == BehaviorKind::AutoHaulGround) {
            const Transform* it = w.get<Transform>(b->stepProp);
            GroundItem* gi = w.get<GroundItem>(b->stepProp);
            const Transform* ut = w.get<Transform>(u);
            const SoloMove* so = w.get<SoloMove>(u);
            const bool walking = so && so->next < so->waypoints.size();
            if (it && gi && ut && distSq(*it, *ut) <= PICKUP_REACH * PICKUP_REACH) {
                const int count = gi->count;
                const int left = fullForPickup(w, u, gi->item, count) ? count : addToInventory(*w.get<Inventory>(u), gi->item, gi->count);
                if (left < count) {
                    const std::string name = itemNameOf(gi->item);
                    queueLog(w, langf("MISC_PICKS_UP", { nameOf(w, u), std::to_string(count - left), name }), LogKind::Pickup, npcOnly(w, u));
                    queueUnitLabel(w, u, itemGainLabel(count - left, name));
                    queueSoundFrom(w, Sfx::Pickup, u);
                }
                if (left == 0) w.destroy(b->stepProp);
                else gi->count = left;
                w.remove<SoloMove>(u);
                b->busy = false;
                continue;
            }
            if (it && gi && walking) continue;
            if (it && gi) noteTaskFailed(w, u, behaviorName(b->step));   // stopped short of the item
            w.remove<SoloMove>(u);
            b->busy = false;
            if (BehaviorRule* r = stepRule(*b)) r->cooldown = BEHAVIOR_RETRY_SECONDS;
            continue;
        }
        // Keeping a Stall: it stays there, but anything its list ranks higher still comes first
        if (b->busy && b->step.kind == BehaviorKind::ManStall) {
            if (!w.has<StallStock>(b->stepProp)) {
                stopAutoStep(w, u, *b);
                continue;
            }
            bool other = false;
            for (BehaviorRule& r : b->rules) {
                if (sameBehavior(r, b->step)) break;
                if (r.kind == BehaviorKind::Rest) {
                    BehaviorRule* rescue = rescueRule(*b);
                    if (rescue && rescue->on && rescue->cooldown <= 0.0f && tryStep(w, t, claims, u, *b, *rescue, rng)) {
                        other = true;
                        break;
                    }
                }
                if (r.on && r.cooldown <= 0.0f && isAutoStep(r.kind) && tryStep(w, t, claims, u, *b, r, rng)) {
                    other = true;
                    break;
                }
            }
            if (other) continue;
            const SoloMove* so = w.get<SoloMove>(u);
            const bool walking = (so && so->next < so->waypoints.size()) || w.has<Route>(u) || w.has<Climbing>(u);
            if (!walking && flatGap(w, u, b->stepProp) > STALL_KEEPER_REACH - 0.5f) {
                const Transform* st = w.get<Transform>(b->stepProp);
                autoWalk(w, t, u, Vec2{ st->x, st->z }, storeyOf(w, b->stepProp));
            }
            continue;
        }
        // Playing at a Game Table: stands at its side a while
        if (b->busy && b->step.kind == BehaviorKind::Play) {
            const Transform* tt = w.has<Prop>(b->stepProp) ? w.get<Transform>(b->stepProp) : nullptr;
            const Transform* ut = w.get<Transform>(u);
            if (tt && ut && scheduleAllows(w, u, BehaviorKind::Play)) {
                const Vec2 spot = tableSeatPos(*tt, b->slot);
                const float dist = std::hypot(spot.x - ut->x, spot.z - ut->z);
                if (!b->began) {
                    if (dist > 0.6f) {
                        if (!walkingNow(w, u)) autoWalk(w, t, u, spot, storeyOf(w, b->stepProp));
                        continue;
                    }
                    b->began = true;
                    b->social = SOCIAL_SIT_MIN + rng.unit() * (SOCIAL_SIT_MAX - SOCIAL_SIT_MIN);
                    w.remove<SoloMove>(u);
                }
                b->social -= dt;
                if (b->social > 0.0f && dist < 1.6f) continue;
            }
            w.remove<SoloMove>(u);
            b->busy = false;
            b->began = false;
            b->slot = -1;
            if (BehaviorRule* r = stepRule(*b)) r->cooldown = SOCIAL_PAUSE_MIN + rng.unit() * (SOCIAL_PAUSE_MAX - SOCIAL_PAUSE_MIN);
            continue;
        }
        // Dancing round a Campfire or an Altar: takes its place on the circle, and goes round it hopping
        if (b->busy && b->step.kind == BehaviorKind::Dance) {
            const Transform* ct = w.has<Prop>(b->stepProp) ? w.get<Transform>(b->stepProp) : nullptr;
            Transform* ut = w.get<Transform>(u);
            if (ct && ut && scheduleAllows(w, u, BehaviorKind::Dance)) {
                const PropKind ck = w.get<Prop>(b->stepProp)->kind;
                const Vec2 spot = dancePoint(*ct, ck, b->slot, danceRingTime(w, b->stepProp));
                if (!b->began) {
                    if (std::hypot(spot.x - ut->x, spot.z - ut->z) > 0.9f) {
                        if (!walkingNow(w, u)) autoWalk(w, t, u, spot, 0);
                        continue;
                    }
                    b->began = true;
                    w.add(u, Dancing{});
                    b->social = DANCE_MIN + rng.unit() * (DANCE_MAX - DANCE_MIN);
                    w.remove<SoloMove>(u);
                }
                b->social -= dt;
                if (b->social > 0.0f) {
                    if (openTile(t, spot.x, spot.z)) {
                        ut->x = spot.x;
                        ut->z = spot.z;
                        ut->y = t.standHeight(spot.x, spot.z);
                    }
                    continue;
                }
            }
            w.remove<SoloMove>(u);
            b->busy = false;
            b->began = false;
            b->slot = -1;
            if (BehaviorRule* r = stepRule(*b)) r->cooldown = SOCIAL_PAUSE_MIN + rng.unit() * (SOCIAL_PAUSE_MAX - SOCIAL_PAUSE_MIN);
            continue;
        }
        // Sitting down: stays a while, and gets up when it is over or the recreation hours are
        if (b->busy && b->step.kind == BehaviorKind::SitDown) {
            const Occupying* oc = w.get<Occupying>(u);
            if (oc && oc->prop == b->stepProp) {
                if (!b->began) {
                    b->began = true;
                    b->social = SOCIAL_SIT_MIN + rng.unit() * (SOCIAL_SIT_MAX - SOCIAL_SIT_MIN);
                }
                b->social -= dt;
                if (b->social > 0.0f && scheduleAllows(w, u, BehaviorKind::SitDown)) continue;
                const ObjectId seat = oc->prop;
                w.remove<Occupying>(u);
                stepOffProp(w, t, u, seat);
                b->busy = false;
                b->began = false;
                if (BehaviorRule* r = stepRule(*b)) r->cooldown = SOCIAL_PAUSE_MIN + rng.unit() * (SOCIAL_PAUSE_MAX - SOCIAL_PAUSE_MIN);
                continue;
            }
            if (!scheduleAllows(w, u, BehaviorKind::SitDown)) {
                stopAutoStep(w, u, *b);
                w.remove<SoloMove>(u);
                continue;
            }
        }
        // Socializing: walks to someone of its faction at recreation and stays by them a while
        if (b->busy && b->step.kind == BehaviorKind::Socialize) {
            const Transform* tt = w.has<Behaviors>(b->stepProp) ? w.get<Transform>(b->stepProp) : nullptr;
            const Transform* ut = w.get<Transform>(u);
            const bool friendOk = tt && !w.has<Carried>(b->stepProp) && !outOfAction(w, b->stepProp) &&
                                  activityOf(w, b->stepProp) == Activity::Recreation && !w.has<Corpse>(b->stepProp);
            if (friendOk && scheduleAllows(w, u, BehaviorKind::Socialize) && ut) {
                if (std::hypot(tt->x - ut->x, tt->z - ut->z) <= SOCIAL_NEAR) {
                    if (!b->began) {
                        b->began = true;
                        b->social = SOCIAL_CHAT_MIN + rng.unit() * (SOCIAL_CHAT_MAX - SOCIAL_CHAT_MIN);
                        w.remove<SoloMove>(u);
                    }
                    b->social -= dt;
                    if (b->social > 0.0f) continue;
                } else if (!b->began) {
                    if (!walkingNow(w, u)) autoWalk(w, t, u, socialSpot(w, u, b->stepProp), storeyOf(w, b->stepProp));
                    continue;
                }
            }
            w.remove<SoloMove>(u);
            b->busy = false;
            b->began = false;
            if (BehaviorRule* r = stepRule(*b)) r->cooldown = SOCIAL_PAUSE_MIN + rng.unit() * (SOCIAL_PAUSE_MAX - SOCIAL_PAUSE_MIN);
            continue;
        }
        // Asleep: up at dawn
        if (b->busy && b->step.kind == BehaviorKind::Sleep) {
            const Occupying* oc = w.get<Occupying>(u);
            if (oc && oc->prop == b->stepProp) {
                if (night) continue;
                const ObjectId bed = oc->prop;
                w.remove<Occupying>(u);
                stepOffProp(w, t, u, bed);
                b->busy = false;
                if (BehaviorRule* r = stepRule(*b)) r->cooldown = BEHAVIOR_RETRY_SECONDS;
                continue;
            }
        }
        if (b->busy) {
            if (going) continue;
            if (working) {
                if (!harvestLoaded(w, claims, u, *b, propInfo((PropKind)b->step.propKind).item)) continue;
                w.remove<Working>(u);
                b->busy = false;
            } else if (onOtherOrder(w, u, *b)) {
                b->busy = false;
                b->wait = resume;
                continue;
            } else {
                b->busy = false;
                if (BehaviorRule* r = stepRule(*b)) {
                    if (b->step.kind == BehaviorKind::AutoHarvest) {
                        r->cooldown = BEHAVIOR_RETRY_SECONDS;
                        noteTaskFailed(w, u, behaviorName(b->step));   // it never got to work there
                    }
                    if (b->step.kind == BehaviorKind::Patrol || b->step.kind == BehaviorKind::PatrolSpawn) {
                        r->cooldown = PATROL_PAUSE_MIN + rng.unit() * (PATROL_PAUSE_MAX - PATROL_PAUSE_MIN);
                    }
                    if (b->step.kind == BehaviorKind::Roam) r->cooldown = ROAM_PAUSE_MIN + rng.unit() * (ROAM_PAUSE_MAX - ROAM_PAUSE_MIN);
                }
                w.remove<SoloMove>(u);
            }
        } else {
            if (onOtherOrder(w, u, *b) || going) {
                b->wait = resume;
                continue;
            }
            if (b->wait > 0.0f) {
                b->wait -= dt;
                continue;
            }
        }

        // Rescue always comes before Rest, wherever the list has them
        for (BehaviorRule& r : b->rules) {
            if (r.kind == BehaviorKind::Rest) {
                BehaviorRule* rescue = rescueRule(*b);
                if (rescue && tryStep(w, t, claims, u, *b, *rescue, rng)) break;
            }
            if (tryStep(w, t, claims, u, *b, r, rng)) break;
        }
    }
}

ObjectId addHaulForHarvest(World& w, ObjectId u, int propKind) {
    Behaviors* b = w.get<Behaviors>(u);
    const Transform* ut = w.get<Transform>(u);
    if (!b || !ut || propKind < 0 || propKind >= propKindCount()) return NO_OBJECT;
    const int item = propInfo((PropKind)propKind).item;
    if (item <= 0) return NO_OBJECT;
    const std::vector<Claim> claims = claimsOf(w);
    const int faction = factionOf(w, u);
    const ItemFilter pile = stockpileFor(item);
    const ObjectId store = nearestProp(w, *ut, [&](ObjectId p) {
        const PropInfo& info = propInfo(w.get<Prop>(p)->kind);
        if (info.use != PropUse::Storage || info.claimsLand || info.stall || !w.has<Storage>(p) || w.has<Blueprint>(p)) return false;
        if (pile != ItemFilter::Any ? info.accepts != pile : !propTakesItem(w, p, item)) return false;
        const Owner* own = w.get<Owner>(p);
        return ((own && own->faction == faction) || inOwnClaim(w, claims, p, faction)) && !lockShutFor(w, claims, p, u);
    });
    if (store == NO_OBJECT) return NO_OBJECT;
    BehaviorRule haul;
    haul.kind = BehaviorKind::AutoHaul;
    haul.prop = store;
    haul.propKind = idx(w.get<Prop>(store)->kind);
    return addBehavior(b->rules, haul) ? store : NO_OBJECT;
}
