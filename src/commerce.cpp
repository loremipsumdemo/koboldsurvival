#include "patrons.h"
#include "commerce.h"
#include <algorithm>
#include <cmath>
#include "calendar.h"
#include "encumbrance.h"
#include "experience.h"
#include "factions.h"
#include "hostility.h"
#include "inventory.h"
#include "items.h"
#include "systems_internal.h"
#include "world_labels.h"

using namespace systems_detail;

namespace {
// When the next shopper, raid and Wandering Trader are looked for. Not saved.
struct CommerceClock {
    float shopIn = 20.0f;
    float raidIn = 45.0f;
    long long traderDay = -1;
};

int valueOf(int item) {
    const ItemDef* d = findItem(item);
    return d ? std::max(1, d->buyValue) : 1;
}

void goTo(World& w, Terrain& t, ObjectId u, ObjectId target) {
    const Transform* tt = w.get<Transform>(target);
    if (!tt) return;
    w.remove<IdleSpot>(u);
    walkSolo(w, t, u, Vec2{ tt->x, tt->z }, storeyOf(w, target));
}

std::vector<Claim> playerClaims(World& w) {
    std::vector<Claim> out;
    for (const Claim& c : claimsOf(w)) {
        if (c.owner == FACTION_PLAYER) out.push_back(c);
    }
    return out;
}

bool nearClaim(const std::vector<Claim>& claims, const Transform& at, float range) {
    for (const Claim& c : claims) {
        if (distSq(at.x, at.z, c.x, c.z) <= range * range) return true;
    }
    return false;
}

// Units of the world that can be sent on an errand: not yours, not mobs, not busy with one already
bool freeNpc(World& w, ObjectId u) {
    if (w.has<Commandable>(u) || w.has<Citizen>(u) || w.has<Mob>(u) || outCold(w, u) || w.has<Carried>(u) || w.has<Occupying>(u)) return false;
    if (w.has<Shopper>(u) || w.has<Raider>(u) || w.has<WanderingTrader>(u) || w.has<InConversation>(u)) return false;
    const Combat* c = w.get<Combat>(u);
    return !(c && c->target != NO_OBJECT) && w.has<Behaviors>(u);
}

std::vector<ObjectId> playerStalls(World& w, bool mannedOnly) {
    std::vector<ObjectId> out;
    for (ObjectId s : w.view<StallStock, Storage, Transform>()) {
        if (ownerOf(w, s) != FACTION_PLAYER || w.has<Blueprint>(s)) continue;
        if (mannedOnly && stallManned(w, s) == NO_OBJECT) continue;
        out.push_back(s);
    }
    return out;
}

ObjectId nearestOf(World& w, const std::vector<ObjectId>& props, const Transform& from) {
    ObjectId best = NO_OBJECT;
    float bestD2 = 0.0f;
    for (ObjectId p : props) {
        const float d2 = distSq(*w.get<Transform>(p), from);
        if (best == NO_OBJECT || d2 < bestD2) {
            best = p;
            bestD2 = d2;
        }
    }
    return best;
}

void dropAt(World& w, ObjectId at, int item, int count) {
    const Transform* tr = w.get<Transform>(at);
    if (!tr || count <= 0) return;
    const ObjectId g = spawnGroundItem(w, *tr, item, count);
    if (const int level = storeyOf(w, at); level > 0) w.add(g, Storey{ level });
}

// Into the Stall's store, and whatever doesn't fit on the ground beside it
void putInStall(World& w, ObjectId stall, int item, int count) {
    Storage* st = w.get<Storage>(stall);
    const int left = st ? addToStore(*st, item, count) : count;
    dropAt(w, stall, item, left);
}

// ---- Stall orders ----

void stallJobSystem(World& w, Terrain& t, float dt) {
    for (ObjectId u : w.view<StallJob, Transform>()) {
        StallJob& job = *w.get<StallJob>(u);
        Behaviors* b = w.get<Behaviors>(u);
        // Each order is tried once: done or not, it leaves the Stall's list
        auto finish = [&]() {
            if (StallStock* ss = w.get<StallStock>(job.stall)) {
                ss->requests.erase(std::remove_if(ss->requests.begin(), ss->requests.end(),
                                                  [&](const StallRequest& r) { return r.takenBy == u; }),
                                   ss->requests.end());
            }
            w.remove<SoloMove>(u);
            w.remove<Route>(u);
            w.remove<StallJob>(u);
            if (b) b->busy = false;
        };
        if (!b || outCold(w, u) || w.has<Carried>(u) || !w.has<Storage>(job.stall) || (!job.fetched && !w.has<Storage>(job.source))) {
            finish();
            continue;
        }
        const ObjectId goal = job.fetched ? job.stall : job.source;
        if (flatGap(w, u, goal) > PROP_REACH + 0.3f) {
            if (!walkingNow(w, u)) {
                job.stuck += dt;
                if (job.stuck > BUILD_GIVE_UP_SECONDS) finish();
                else if (job.stuck > 1.0f && job.stuck - dt <= 1.0f) goTo(w, t, u, goal);
            }
            continue;
        }
        if (w.has<Climbing>(u)) continue;
        job.stuck = 0.0f;
        w.remove<SoloMove>(u);
        w.remove<Route>(u);
        Inventory* inv = w.get<Inventory>(u);
        if (!inv) {
            finish();
            continue;
        }
        if (!job.fetched) {
            Storage* src = w.get<Storage>(job.source);
            const ItemDef* def = findItem(job.item);
            const float each = def ? std::max(0.01f, def->weight) : 1.0f;
            const float spare = ENCUMBERED_SHARE * carryLimit(statsOf(w, u)) - carriedWeightOf(w, u);
            const int byWeight = std::max(0, (int)std::floor(spare / each));
            const int want = std::min({ job.count, countInSlots(src->slots.data(), (int)src->slots.size(), job.item), byWeight,
                                        slotsRoomFor(inv->slots.data(), INVENTORY_SLOTS, job.item) });
            // The item moved out of the store (or there's no carrying it): the order is dropped
            const int took = want > 0 ? takeFromSlots(src->slots.data(), (int)src->slots.size(), job.item, want) : 0;
            if (took <= 0) {
                queueLog(w, langf("MISC_STALL_ORDER_DROPPED", { itemNameOf(job.item) }), LogKind::Other, npcOnly(w, u));
                finish();
                continue;
            }
            addToInventory(*inv, job.item, took);
            queueItemMove(w, job.source, u, job.item, took);
            queueSoundFrom(w, Sfx::Take, job.source);
            propStorageChanged(w, job.source);
            job.count = took;
            job.fetched = true;
            goTo(w, t, u, job.stall);
            continue;
        }
        Storage* st = w.get<Storage>(job.stall);
        const int put = std::min(carriedCount(*inv, job.item), storeRoomFor(*st, job.item));
        if (put > 0) {
            takeFromSlots(inv->slots.data(), INVENTORY_SLOTS, job.item, put);
            addToStore(*st, job.item, put);
            queueItemMove(w, u, job.stall, job.item, put);
            queueSoundFrom(w, Sfx::Store, job.stall);
            queueLog(w, langf("MISC_STALL_STOCKED", { nameOf(w, u), std::to_string(put), itemNameOf(job.item) }), LogKind::Pickup, npcOnly(w, u));
        }
        finish();
    }
    // Orders taken by a unit that has since stopped (called away, out cold) are dropped: each is tried once
    for (ObjectId s : w.view<StallStock>()) {
        StallStock& ss = *w.get<StallStock>(s);
        ss.requests.erase(std::remove_if(ss.requests.begin(), ss.requests.end(),
                                         [&](const StallRequest& r) {
                                             if (!w.has<Storage>(r.from)) return true;
                                             if (r.takenBy == NO_OBJECT) return false;
                                             const StallJob* job = w.get<StallJob>(r.takenBy);
                                             return !job || job->stall != s;
                                         }),
                          ss.requests.end());
    }
}

// ---- Shoppers ----

// What a shopper would take from the Stall: food when hungry, gear better than its own, else a few things it fancies
std::vector<std::pair<int, int>> shoppingList(World& w, Rng& rng, ObjectId buyer, const Storage& st) {
    const Needs* n = w.get<Needs>(buyer);
    const Equipment* worn = w.get<Equipment>(buyer);
    const bool hungry = n && n->maxHunger > 0 && n->hunger * 100 / n->maxHunger < 70;
    std::vector<std::pair<int, int>> scored;   // score, slot
    for (int i = 0; i < (int)st.slots.size(); ++i) {
        const ItemStack& s = st.slots[(size_t)i];
        const ItemDef* d = s.item > 0 ? findItem(s.item) : nullptr;
        if (!d || s.count <= 0) continue;
        int score = 0;
        if (d->typeId == "consumable") score = hungry ? 3 : 1;
        else if (!d->equipSlot.empty() && worn) {
            int have = 0;
            for (int g = 0; g < GEAR_SLOTS; ++g) {
                const ItemDef* h = findItem(worn->items[(size_t)g]);
                if (h && h->equipSlot == d->equipSlot) have = std::max(have, h->attackValue + h->defenseValue);
            }
            if (d->attackValue + d->defenseValue > have) score = 2;
        }
        if (score == 0 && rng.unit() < 0.3f) score = 1;
        if (score > 0) scored.push_back({ score * 1000 + rng.range(0, 999), i });
    }
    std::sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<std::pair<int, int>> out;   // slot, how many
    const int most = rng.range(1, 3);
    for (const auto& [score, slot] : scored) {
        if ((int)out.size() >= most) break;
        const ItemStack& s = st.slots[(size_t)slot];
        const ItemDef* d = findItem(s.item);
        const int count = d && d->typeId == "consumable" ? std::min(s.count, rng.range(1, 3)) : 1;
        out.push_back({ slot, count });
    }
    return out;
}

// Payment: the buyer's items of the kinds the Stall accepts, Gold Coins first, worth at least price. Empty if it can't pay.
std::vector<ItemStack> paymentFor(World& w, ObjectId buyer, std::uint32_t accepts, int price) {
    const Inventory* inv = w.get<Inventory>(buyer);
    if (!inv) return {};
    static const int gold = itemTagged(ITEM_TAG_GOLD_COIN);
    std::vector<int> order;
    for (int i = 0; i < INVENTORY_SLOTS; ++i) {
        const ItemStack& s = inv->slots[(size_t)i];
        if (s.item <= 0 || s.count <= 0 || (accepts & storeBit(storeCategoryOf(baseItemId(s.item)))) == 0) continue;
        order.push_back(i);
    }
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        const int ia = inv->slots[(size_t)a].item, ib = inv->slots[(size_t)b].item;
        if ((baseItemId(ia) == gold) != (baseItemId(ib) == gold)) return baseItemId(ia) == gold;
        return valueOf(ia) > valueOf(ib);
    });
    std::vector<ItemStack> pay;
    int paid = 0;
    for (int i : order) {
        const ItemStack& s = inv->slots[(size_t)i];
        const int each = valueOf(s.item);
        const int n = std::min(s.count, (price - paid + each - 1) / each);
        if (n <= 0) continue;
        pay.push_back(ItemStack{ s.item, n });
        paid += n * each;
        if (paid >= price) return pay;
    }
    return {};
}

void shop(World& w, Rng& rng, ObjectId buyer, ObjectId stall) {
    Storage* st = w.get<Storage>(stall);
    const StallStock* ss = w.get<StallStock>(stall);
    Inventory* inv = w.get<Inventory>(buyer);
    if (!st || !ss || !inv) return;
    const ObjectId keeper = stallManned(w, stall);
    const int buyerFaction = factionOf(w, buyer);
    int bought = 0;
    for (const auto& [slot, want] : shoppingList(w, rng, buyer, *st)) {
        const ItemStack stack = st->slots[(size_t)slot];
        if (stack.item <= 0) continue;
        const int count = std::min(want, stack.count);
        // Your faction's markup on top of the goods' worth
        const int worth = valueOf(stack.item) * count;
        const int price = worth + (worth * tradeMarkup(w, FACTION_PLAYER) + 99) / 100;
        const std::vector<ItemStack> pay = paymentFor(w, buyer, ss->payment, price);
        if (pay.empty()) continue;
        // The goods go to the buyer (its own, if they were stolen from it), the payment into the Stall
        st->slots[(size_t)slot].count -= count;
        if (st->slots[(size_t)slot].count <= 0) st->slots[(size_t)slot] = ItemStack{};
        if (keeper != NO_OBJECT) stolenSaleNoticed(w, rng, keeper, buyerFaction, stack.item);
        const int given = reclaimStolen(w, buyer, stack.item);
        const int left = addToInventory(*inv, given, count);
        dropAt(w, buyer, given, left);
        queueItemMove(w, stall, buyer, stack.item, count);
        for (const ItemStack& p : pay) {
            takeFromSlots(inv->slots.data(), INVENTORY_SLOTS, p.item, p.count);
            putInStall(w, stall, p.item, p.count);
            queueItemMove(w, buyer, stall, p.item, p.count);
        }
        bought += count;
    }
    if (bought > 0) {
        queueSoundFrom(w, Sfx::ItemMove, stall);
        queueLog(w, langf("MISC_SHOPPER_BOUGHT", { nameOf(w, buyer), factionName(buyerFaction), std::to_string(bought) }), LogKind::Pickup, LogWho(false, buyer));
    } else {
        queueUnitLabel(w, buyer, lang("MISC_SHOPPER_NOTHING"));
    }
}

void shopperSystem(World& w, Terrain& t, Rng& rng, CommerceClock& clock, float dt) {
    clock.shopIn -= dt;
    if (clock.shopIn <= 0.0f) {
        clock.shopIn = 25.0f + rng.unit() * 35.0f;
        const std::vector<ObjectId> stalls = playerStalls(w, true);
        if (!stalls.empty()) {
            std::vector<int> busyFactions;
            for (ObjectId s : w.view<Shopper>()) busyFactions.push_back(factionOf(w, s));
            std::vector<ObjectId> pool;
            for (ObjectId u : w.view<Entity, Faction, Transform>()) {
                const int f = w.get<Faction>(u)->id;
                if (f == FACTION_PLAYER || f == FACTION_FACTIONLESS || isMobFaction(f) || !freeNpc(w, u)) continue;
                if (factionsHostile(w, f, FACTION_PLAYER) || w.has<Hostile>(u) || reputation(w, FACTION_PLAYER, f) < 0) continue;
                if (!factionsTrade(w, f, FACTION_PLAYER)) continue;
                if (std::find(busyFactions.begin(), busyFactions.end(), f) != busyFactions.end()) continue;
                const ObjectId s = nearestOf(w, stalls, *w.get<Transform>(u));
                if (s == NO_OBJECT || distSq(*w.get<Transform>(s), *w.get<Transform>(u)) > SHOP_RANGE * SHOP_RANGE) continue;
                pool.push_back(u);
            }
            if (!pool.empty()) {
                const ObjectId u = pool[(size_t)rng.range(0, (int)pool.size() - 1)];
                const ObjectId s = nearestOf(w, stalls, *w.get<Transform>(u));
                holdTasks(w, u);
                w.add(u, Shopper{ s, 150.0f });
                goTo(w, t, u, s);
                queueLog(w, langf("MISC_SHOPPER_COMING", { nameOf(w, u), factionName(factionOf(w, u)) }), LogKind::Seen, LogWho(false, u));
            }
        }
    }
    for (ObjectId u : w.view<Shopper>()) {
        Shopper& sh = *w.get<Shopper>(u);
        sh.timeLeft -= dt;
        auto release = [&]() {
            w.remove<Shopper>(u);
            w.remove<SoloMove>(u);
            w.remove<Route>(u);
            releaseTasks(w, u);
        };
        const Combat* c = w.get<Combat>(u);
        if (sh.timeLeft <= 0.0f || !w.has<StallStock>(sh.stall) || outCold(w, u) || (c && c->target != NO_OBJECT)) {
            release();
            continue;
        }
        holdTasks(w, u);
        if (flatGap(w, u, sh.stall) > STALL_KEEPER_REACH) {
            if (!walkingNow(w, u)) goTo(w, t, u, sh.stall);
            continue;
        }
        w.remove<SoloMove>(u);
        if (stallManned(w, sh.stall) == NO_OBJECT) continue;   // waits for the shopkeeper
        shop(w, rng, u, sh.stall);
        release();
    }
}

// ---- Raids ----

bool insideBuilding(World& w, ObjectId prop) {
    const Transform* tr = w.get<Transform>(prop);
    return tr && (storeyOf(w, prop) > 0 || foundationAt(w, (int)std::floor(tr->x), (int)std::floor(tr->z)) != NO_OBJECT);
}

// Your stores with something in them: outside buildings, or inside them
std::vector<ObjectId> raidableStores(World& w, const std::vector<Claim>& claims, bool inside) {
    std::vector<ObjectId> out;
    for (ObjectId p : w.view<Storage, Prop, Transform>()) {
        const PropInfo& info = propInfo(w.get<Prop>(p)->kind);
        if (info.claimsLand || w.has<Blueprint>(p) || storedCount(*w.get<Storage>(p)) <= 0) continue;
        const Transform* tr = w.get<Transform>(p);
        if (ownerOf(w, p) != FACTION_PLAYER && !claimAt(claims, tr->x, tr->z, FACTION_PLAYER)) continue;
        if (insideBuilding(w, p) != inside) continue;
        out.push_back(p);
    }
    return out;
}

void steal(World& w, ObjectId raider, ObjectId prop) {
    Storage* st = w.get<Storage>(prop);
    Inventory* inv = w.get<Inventory>(raider);
    if (!st || !inv) return;
    // The most valuable first
    std::vector<int> order;
    for (int i = 0; i < (int)st->slots.size(); ++i) {
        if (st->slots[(size_t)i].item > 0 && st->slots[(size_t)i].count > 0) order.push_back(i);
    }
    std::sort(order.begin(), order.end(), [&](int a, int b) { return valueOf(st->slots[(size_t)a].item) > valueOf(st->slots[(size_t)b].item); });
    const int owner = ownerOf(w, prop) >= 0 ? ownerOf(w, prop) : FACTION_PLAYER;
    int took = 0;
    for (int i : order) {
        ItemStack& s = st->slots[(size_t)i];
        const int marked = markStolen(s.item, owner);
        const int left = addToInventory(*inv, marked, s.count);
        const int n = s.count - left;
        if (n <= 0) continue;
        queueItemMove(w, prop, raider, s.item, n);
        took += n;
        s.count = left;
        if (s.count <= 0) s = ItemStack{};
    }
    propStorageChanged(w, prop);
    // Too much to run with: the heaviest things are dropped until it can move at a decent pace again
    const float limit = ENCUMBERED_SHARE * carryLimit(statsOf(w, raider));
    for (int guard = 0; guard < 64 && carriedWeightOf(w, raider) > limit; ++guard) {
        int heaviest = -1;
        float most = 0.0f;
        for (int i = 0; i < INVENTORY_SLOTS; ++i) {
            const ItemDef* d = findItem(inv->slots[(size_t)i].item);
            if (d && inv->slots[(size_t)i].count > 0 && d->weight > most) {
                most = d->weight;
                heaviest = i;
            }
        }
        if (heaviest < 0 || most <= 0.0f) break;
        ItemStack& s = inv->slots[(size_t)heaviest];
        const int n = std::min(s.count, std::max(1, (int)std::ceil((carriedWeightOf(w, raider) - limit) / most)));
        dropAt(w, raider, s.item, n);
        queueUnitLabel(w, raider, itemLossLabelFor(n, s.item), true);
        s.count -= n;
        if (s.count <= 0) s = ItemStack{};
    }
    if (took > 0) {
        queueSoundFrom(w, Sfx::Take, prop);
        queueLog(w, langf("MISC_RAIDER_STOLE", { nameOf(w, raider), std::to_string(took), propDisplayName(w, prop) }), LogKind::Seen,
                 LogWho(false, raider), Tint::Red);
    }
}

Vec2 fleeSpot(World& w, const Terrain& t, const std::vector<Claim>& claims, ObjectId u) {
    const Transform* ut = w.get<Transform>(u);
    float cx = ut->x, cz = ut->z;
    float best = -1.0f;
    for (const Claim& c : claims) {
        const float d2 = (c.x - ut->x) * (c.x - ut->x) + (c.z - ut->z) * (c.z - ut->z);
        if (best < 0.0f || d2 < best) {
            best = d2;
            cx = c.x;
            cz = c.z;
        }
    }
    float dx = ut->x - cx, dz = ut->z - cz;
    const float len = std::max(0.01f, std::hypot(dx, dz));
    dx /= len;
    dz /= len;
    for (int turn = 0; turn < 16; ++turn) {
        const float a = std::atan2(dz, dx) + (turn % 2 ? 1.0f : -1.0f) * (float)((turn + 1) / 2) * 0.35f;
        const float r = CLAIM_RADIUS + 25.0f;
        const float x = cx + std::cos(a) * r, z = cz + std::sin(a) * r;
        if (x >= 0.0f && z >= 0.0f && x < t.size() && z < t.size() && t.walkable((int)std::floor(x), (int)std::floor(z))) {
            return Vec2{ std::floor(x) + 0.5f, std::floor(z) + 0.5f };
        }
    }
    return Vec2{ ut->x + dx * 30.0f, ut->z + dz * 30.0f };
}

void raiderSystem(World& w, Terrain& t, Rng& rng, CommerceClock& clock, float dt) {
    const std::vector<Claim> claims = playerClaims(w);
    clock.raidIn -= dt;
    if (clock.raidIn <= 0.0f && !claims.empty()) {
        clock.raidIn = 40.0f + rng.unit() * 40.0f;
        if ((int)w.view<Raider>().size() < RAIDERS_MOST) {
            std::vector<ObjectId> pool;
            int inside = 0;
            for (ObjectId u : w.view<Entity, Faction, Transform>()) {
                const int f = w.get<Faction>(u)->id;
                if (f == FACTION_PLAYER || f == FACTION_FACTIONLESS || isMobFaction(f) || w.has<Mob>(u) || outCold(w, u)) continue;
                if (!factionsHostile(w, f, FACTION_PLAYER) && !w.has<Hostile>(u)) continue;
                const Transform* ut = w.get<Transform>(u);
                if (nearClaim(claims, *ut, CLAIM_RADIUS)) ++inside;
                // Not one still getting over a beating: it raids again only at full health
                if (w.has<Retreating>(u)) continue;
                const Needs* n = w.get<Needs>(u);
                if (freeNpc(w, u) && n && n->hp >= n->maxHp && nearClaim(claims, *ut, RAID_RANGE)) pool.push_back(u);
            }
            if (!pool.empty()) {
                const ObjectId u = pool[(size_t)rng.range(0, (int)pool.size() - 1)];
                std::vector<ObjectId> stores = raidableStores(w, claims, false);
                bool breakIn = false;
                if (stores.empty() && inside >= RAIDERS_TO_BREAK_IN) {
                    stores = raidableStores(w, claims, true);
                    breakIn = true;
                }
                const ObjectId target = nearestOf(w, stores, *w.get<Transform>(u));
                if (target != NO_OBJECT) {
                    holdTasks(w, u);
                    w.add(u, Raider{ target, false, 240.0f, 0.0f, 0.0f });
                    goTo(w, t, u, target);
                    queueLog(w, langf(breakIn ? "MISC_RAIDER_BREAKING_IN" : "MISC_RAIDER_COMING", { nameOf(w, u), propDisplayName(w, target) }),
                             LogKind::Seen, LogWho(false, u), Tint::Red);
                    // The patrons speak of it: the raiders' faction's, then the one raided
                    int victim = -1;
                    if (const Owner* ow = w.get<Owner>(target)) victim = ow->faction;
                    else if (const Transform* tt = w.get<Transform>(target)) {
                        if (const Claim* in = claimAt(claims, tt->x, tt->z)) victim = in->owner;
                    }
                    if (victim >= 0) patronEvent(w, PatronEvent::Raid, factionOf(w, u), victim);
                }
            }
        }
    }
    for (ObjectId u : w.view<Raider>()) {
        Raider& r = *w.get<Raider>(u);
        r.timeLeft -= dt;
        auto release = [&]() {
            w.remove<Raider>(u);
            w.remove<SoloMove>(u);
            w.remove<Route>(u);
            releaseTasks(w, u);
        };
        if (outCold(w, u) || w.has<Carried>(u) || r.timeLeft <= 0.0f) {
            release();
            continue;
        }
        holdTasks(w, u);
        if (r.fleeing) {
            const Transform* ut = w.get<Transform>(u);
            if (!walkingNow(w, u) || std::hypot(ut->x - r.fleeX, ut->z - r.fleeZ) < 2.0f) release();
            continue;
        }
        const Storage* st = w.get<Storage>(r.target);
        if (!st || storedCount(*st) <= 0) {
            r.fleeing = true;
            const Vec2 to = fleeSpot(w, t, claims, u);
            r.fleeX = to.x;
            r.fleeZ = to.z;
            walkSolo(w, t, u, to, 0);
            continue;
        }
        if (flatGap(w, u, r.target) > PROP_REACH + 0.3f) {
            if (!walkingNow(w, u)) goTo(w, t, u, r.target);
            continue;
        }
        if (w.has<Climbing>(u)) continue;
        steal(w, u, r.target);
        r.fleeing = true;
        const Vec2 to = fleeSpot(w, t, claims, u);
        r.fleeX = to.x;
        r.fleeZ = to.z;
        walkSolo(w, t, u, to, 0);
    }
}

// ---- The Wandering Trader ----

void spawnTrader(World& w, Terrain& t, Rng& rng) {
    const std::vector<Claim> claims = playerClaims(w);
    if (claims.empty()) return;
    const Claim& home = claims[(size_t)rng.range(0, (int)claims.size() - 1)];
    const float start = rng.unit() * TWO_PI;
    for (int i = 0; i < 24; ++i) {
        const float a = start + (float)i * TWO_PI / 24.0f;
        const float r = CLAIM_RADIUS + 3.0f;
        const float x = home.x + std::cos(a) * r, z = home.z + std::sin(a) * r;
        if (x < 0.0f || z < 0.0f || x >= t.size() || z >= t.size() || !t.walkable((int)std::floor(x), (int)std::floor(z))) continue;
        if (claimAt(claimsOf(w), x, z)) continue;
        const float sx = std::floor(x) + 0.5f, sz = std::floor(z) + 0.5f;
        const ObjectId u = spawnUnit(w, t, rng, FACTION_FACTIONLESS, sx, sz);
        if (Name* n = w.get<Name>(u)) n->text = lang("MISC_WANDERING_TRADER");
        if (Inventory* inv = w.get<Inventory>(u)) *inv = Inventory{};
        if (Behaviors* b = w.get<Behaviors>(u)) b->rules.clear();
        w.remove<Hostile>(u);
        w.add(u, Neutral{});
        const std::vector<ObjectId> stalls = playerStalls(w, false);
        const ObjectId stall = nearestOf(w, stalls, *w.get<Transform>(u));
        w.add(u, WanderingTrader{ stall, stall == NO_OBJECT, 420.0f, sx, sz });
        const Transform* ut = w.get<Transform>(u);
        queuePuff(w, ut->x, ut->y, ut->z);
        queueSoundFrom(w, Sfx::Occupy, u);
        queueLog(w, lang("MISC_TRADER_ARRIVES"), LogKind::Seen);
        if (stall != NO_OBJECT) goTo(w, t, u, stall);
        else walkSolo(w, t, u, Vec2{ home.x, home.z }, 0);
        return;
    }
}

void traderBuys(World& w, Rng& rng, ObjectId trader, ObjectId stall) {
    Storage* st = w.get<Storage>(stall);
    if (!st) return;
    std::vector<int> filled;
    for (int i = 0; i < (int)st->slots.size(); ++i) {
        if (st->slots[(size_t)i].item > 0 && st->slots[(size_t)i].count > 0) filled.push_back(i);
    }
    int want = rng.range(1, TRADER_MOST_ITEMS), bought = 0, coins = 0;
    for (int guard = 0; want > 0 && !filled.empty() && guard < 64; ++guard) {
        const size_t k = (size_t)rng.range(0, (int)filled.size() - 1);
        ItemStack& s = st->slots[(size_t)filled[k]];
        const int n = std::min(s.count, rng.range(1, want));
        coins += valueOf(s.item) * n;
        queueItemMove(w, stall, trader, s.item, n);
        // What it buys is gone with it
        s.count -= n;
        want -= n;
        bought += n;
        if (s.count <= 0) {
            s = ItemStack{};
            filled.erase(filled.begin() + (long)k);
        }
    }
    if (bought <= 0) {
        queueUnitLabel(w, trader, lang("MISC_SHOPPER_NOTHING"));
        return;
    }
    static const int gold = itemTagged(ITEM_TAG_GOLD_COIN);
    if (gold > 0) {
        putInStall(w, stall, gold, coins);
        queueItemMove(w, trader, stall, gold, coins);
    }
    propStorageChanged(w, stall);
    queueSoundFrom(w, Sfx::ItemMove, stall);
    queueLog(w, langf("MISC_TRADER_BOUGHT", { std::to_string(bought), std::to_string(coins) }), LogKind::Pickup);
}

void traderSystem(World& w, Terrain& t, Rng& rng, CommerceClock& clock, float dt) {
    const double now = w.resource<WorldClock>().seconds;
    const long long day = dayIndexOf(now);
    const double hour = (now - (double)day * SECONDS_PER_DAY) / SECONDS_PER_HOUR;
    if (clock.traderDay < 0) clock.traderDay = day;
    if (day != clock.traderDay && hour >= DAY_START_HOUR + 2) {
        clock.traderDay = day;
        if (rng.unit() < TRADER_DAILY_CHANCE) spawnTrader(w, t, rng);
    }
    std::vector<ObjectId> gone;
    for (ObjectId u : w.view<WanderingTrader, Transform>()) {
        WanderingTrader& tr = *w.get<WanderingTrader>(u);
        tr.timeLeft -= dt;
        if (Behaviors* b = w.get<Behaviors>(u)) b->wait = 1.0f;
        if (outCold(w, u)) {
            gone.push_back(u);
            continue;
        }
        if (!tr.leaving) {
            if (!w.has<StallStock>(tr.stall)) {
                tr.stall = nearestOf(w, playerStalls(w, false), *w.get<Transform>(u));
                if (tr.stall == NO_OBJECT) tr.leaving = true;
                continue;
            }
            if (flatGap(w, u, tr.stall) > STALL_KEEPER_REACH && tr.timeLeft > 0.0f) {
                if (!walkingNow(w, u)) goTo(w, t, u, tr.stall);
                continue;
            }
            if (tr.timeLeft > 0.0f) traderBuys(w, rng, u, tr.stall);
            tr.leaving = true;
            walkSolo(w, t, u, Vec2{ tr.exitX, tr.exitZ }, 0);
            continue;
        }
        const Transform* ut = w.get<Transform>(u);
        if (!walkingNow(w, u)) walkSolo(w, t, u, Vec2{ tr.exitX, tr.exitZ }, 0);
        if (std::hypot(ut->x - tr.exitX, ut->z - tr.exitZ) < 1.5f || tr.timeLeft < -120.0f) gone.push_back(u);
    }
    for (ObjectId u : gone) {
        const Transform at = *w.get<Transform>(u);
        queuePuff(w, at.x, at.y, at.z);
        queueLog(w, lang("MISC_TRADER_LEAVES"), LogKind::Seen);
        for (ObjectId o : w.view<Combat>()) {
            if (w.get<Combat>(o)->target == u) w.get<Combat>(o)->target = NO_OBJECT;
        }
        w.destroy(u);
    }
}
}

ObjectId stallManned(World& w, ObjectId stall) {
    const int owner = ownerOf(w, stall);
    for (ObjectId u : w.view<Behaviors, Transform>()) {
        const Behaviors* b = w.get<Behaviors>(u);
        if (!b->busy || b->step.kind != BehaviorKind::ManStall || b->stepProp != stall) continue;
        if (factionOf(w, u) != owner || outCold(w, u)) continue;
        if (flatGap(w, u, stall) <= STALL_KEEPER_REACH) return u;
    }
    return NO_OBJECT;
}

bool requestStallHaul(World& w, ObjectId stall, ObjectId from, int item, int count) {
    StallStock* ss = w.get<StallStock>(stall);
    if (!ss || from == stall || item <= 0 || count <= 0 || !w.has<Storage>(from)) return false;
    for (const StallRequest& r : ss->requests) {
        if (r.from == from && r.item == item && r.takenBy == NO_OBJECT) return false;   // already waiting
    }
    ss->requests.push_back(StallRequest{ from, item, count, NO_OBJECT });
    return true;
}

bool startStallHaulStep(World& w, Terrain& t, ObjectId u, Behaviors& b, BehaviorRule& r) {
    const Transform* ut = w.get<Transform>(u);
    r.why = BehaviorWait::NoStallOrder;
    if (!ut || w.has<Mob>(u) || w.has<Carrying>(u) || !w.has<Inventory>(u)) return false;
    const int faction = factionOf(w, u);
    ObjectId bestStall = NO_OBJECT;
    size_t bestIndex = 0;
    float bestD2 = 0.0f;
    for (ObjectId s : w.view<StallStock>()) {
        if (ownerOf(w, s) != faction || w.has<Blueprint>(s)) continue;
        const StallStock& ss = *w.get<StallStock>(s);
        for (size_t i = 0; i < ss.requests.size(); ++i) {
            const StallRequest& req = ss.requests[i];
            const Transform* ft = w.get<Transform>(req.from);
            if (req.takenBy != NO_OBJECT || !ft) continue;
            const float d2 = distSq(*ft, *ut);
            if (bestStall == NO_OBJECT || d2 < bestD2) {
                bestStall = s;
                bestIndex = i;
                bestD2 = d2;
            }
        }
    }
    if (bestStall == NO_OBJECT) return false;
    StallRequest& req = w.get<StallStock>(bestStall)->requests[bestIndex];
    req.takenBy = u;
    r.why = BehaviorWait::None;
    clearOrdersButFight(w, u);
    w.remove<IdleSpot>(u);
    b.busy = true;
    b.step = r;
    b.stepProp = bestStall;
    StallJob job{ bestStall, req.from, req.item, req.count, false, 0.0f };
    if (flatGap(w, u, req.from) > PROP_REACH) goTo(w, t, u, req.from);
    w.add(u, job);
    return true;
}

bool startManStallStep(World& w, Terrain& t, ObjectId u, Behaviors& b, BehaviorRule& r) {
    if (w.has<Mob>(u) || !w.has<StallStock>(r.prop) || w.has<Blueprint>(r.prop)) {
        r.why = BehaviorWait::PropGone;
        return false;
    }
    r.why = BehaviorWait::None;
    b.busy = true;
    b.step = r;
    b.stepProp = r.prop;
    if (flatGap(w, u, r.prop) > STALL_KEEPER_REACH - 0.8f) {
        const Transform* st = w.get<Transform>(r.prop);
        autoWalk(w, t, u, Vec2{ st->x, st->z }, storeyOf(w, r.prop));
    }
    return true;
}

bool stolenSaleNoticed(World& w, Rng& rng, ObjectId seller, int buyerFaction, int item) {
    if (!isStolen(item) || buyerFaction < 0) return false;
    const int from = stolenFromOf(item);
    if (from != buyerFaction && !factionsAllied(w, from, buyerFaction)) return false;
    const int thievery = levelOf(statsOf(w, seller), StatId::Thievery);
    const float chance = std::clamp(0.3f + (float)valueOf(item) / 300.0f - 0.03f * (float)thievery, 0.05f, 0.95f);
    if (rng.unit() >= chance) return false;
    changePlayerReputation(w, buyerFaction, -REP_STOLEN_PENALTY);
    queueLog(w, langf("MISC_STOLEN_NOTICED", { factionName(buyerFaction), itemNameOf(item) }), LogKind::Other, false, Tint::Red);
    queueUnitLabel(w, seller, lang("MISC_STOLEN_LABEL"), true);
    return true;
}

int reclaimStolen(World& w, ObjectId unit, int item) {
    return isStolen(item) && stolenFromOf(item) == factionOf(w, unit) ? baseItemId(item) : item;
}

void commerceSystem(World& w, Terrain& t, Rng& rng, float dt) {
    CommerceClock& clock = w.resource<CommerceClock>();
    stallJobSystem(w, t, dt);
    shopperSystem(w, t, rng, clock, dt);
    raiderSystem(w, t, rng, clock, dt);
    traderSystem(w, t, rng, clock, dt);
}
