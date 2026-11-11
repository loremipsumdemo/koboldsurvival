#include "recipes.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <sstream>
#include "claims.h"
#include "inventory.h"
#include "items.h"
#include "systems_internal.h"
#include "world_labels.h"

using namespace systems_detail;

namespace {
std::vector<Recipe>& table() {
    static std::vector<Recipe> t;
    return t;
}

// The recipes each faction has researched, by key. Part of the world.
struct Researched {
    std::map<int, std::set<std::string>> byFaction;
};


void walkTo(World& w, Terrain& t, ObjectId u, ObjectId target) {
    const Transform* tt = w.get<Transform>(target);
    if (tt) walkSoloTo(w, t, u, Vec2{ tt->x, tt->z }, storeyOf(w, target));
}

int held(World& w, ObjectId prop, int item) {
    const Storage* st = w.get<Storage>(prop);
    return st ? countInSlots(st->slots.data(), (int)st->slots.size(), item) : 0;
}

const Recipe* orderRecipe(World& w, ObjectId prop) {
    const CraftOrder* o = w.get<CraftOrder>(prop);
    const Prop* p = w.get<Prop>(prop);
    if (!o || !p || o->recipe < 0 || o->recipe >= (int)table().size()) return nullptr;
    const Recipe& r = table()[(size_t)o->recipe];
    return r.station == p->kind ? &r : nullptr;
}

// The first ingredient the prop's store is short of, or nullptr when it holds them all
const RecipeInput* missingInput(World& w, ObjectId prop, const Recipe& r) {
    for (const RecipeInput& in : r.inputs) {
        if (held(w, prop, in.item) < in.count) return &in;
    }
    return nullptr;
}

// The nearest store of the unit's faction holding this item, other than the crafting prop itself
ObjectId storeHolding(World& w, const std::vector<Claim>& claims, ObjectId u, ObjectId bench, int item) {
    const Transform* ut = w.get<Transform>(u);
    const int faction = factionOf(w, u);
    ObjectId best = NO_OBJECT;
    float bestD2 = 0.0f;
    for (ObjectId s : w.view<Storage, Prop, Transform>()) {
        const PropInfo& info = propInfo(w.get<Prop>(s)->kind);
        if (s == bench || info.advanced || info.claimsLand || info.stall || w.has<Blueprint>(s) || lockShutFor(w, claims, s, u)) continue;
        if (!propOfFaction(w, claims, s, faction) || held(w, s, item) <= 0) continue;
        const float d2 = distSq(*w.get<Transform>(s), *ut);
        if (best == NO_OBJECT || d2 < bestD2) {
            best = s;
            bestD2 = d2;
        }
    }
    return best;
}

// An order Auto-Craft would fill now: its stock is short, its output is clear, no one is on it, and its ingredients
// are in the prop, on the unit or in the faction's stores
bool orderWantsFilling(World& w, const std::vector<Claim>& claims, ObjectId u, ObjectId prop) {
    const CraftOrder* o = w.get<CraftOrder>(prop);
    const Recipe* r = orderRecipe(w, prop);
    const int faction = factionOf(w, u);
    if (!o || !r || o->keep <= 0 || w.has<Blueprint>(prop) || !outputEmpty(w, prop) || !recipeUnlocked(w, faction, o->recipe)) return false;
    if (!propOfFaction(w, claims, prop, faction) || lockShutFor(w, claims, prop, u) || workersAt(w, prop) > 0) return false;
    if (factionStock(w, faction, r->item) >= o->keep) return false;
    const Inventory* inv = w.get<Inventory>(u);
    for (const RecipeInput& in : r->inputs) {
        int have = held(w, prop, in.item) + (inv ? carriedCount(*inv, in.item) : 0);
        for (ObjectId s : w.view<Storage, Prop, Transform>()) {
            if (have >= in.count) break;
            const PropInfo& info = propInfo(w.get<Prop>(s)->kind);
            if (s == prop || info.advanced || info.claimsLand || info.stall || w.has<Blueprint>(s) || lockShutFor(w, claims, s, u)) continue;
            if (propOfFaction(w, claims, s, faction)) have += held(w, s, in.item);
        }
        if (have < in.count) return false;
    }
    return true;
}
}

bool isGizmo(int item) {
    const ItemDef* d = findItem(item);
    return d && hasTag(*d, "gizmo");
}

bool loadRecipes(const std::string& text, std::string& err) {
    std::vector<Recipe> out;
    std::istringstream in(text);
    std::string line;
    int lineNo = 0;
    std::set<std::string> keys;
    while (std::getline(in, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos || line[start] == '#') continue;
        std::istringstream ls(line);
        std::string word, station, inputs;
        Recipe r;
        ls >> word >> r.key >> station >> r.item >> r.count >> r.hours >> r.gizmos >> inputs;
        const std::string at = "line " + std::to_string(lineNo) + ": ";
        if (ls.fail() || word != "recipe") {
            err = at + "not 'recipe <key> <prop> <item> <count> <hours> <gizmos> <item:count,...>'";
            return false;
        }
        if (!keys.insert(r.key).second) {
            err = at + "recipe " + r.key + " is listed twice";
            return false;
        }
        if (!propKindNamed(station, r.station) || !propInfo(r.station).advanced) {
            err = at + station + " is not an Advanced Crafting prop (props.txt flag advanced)";
            return false;
        }
        if (!findItem(r.item) || r.count < 1 || r.hours <= 0.0 || r.gizmos < 0) {
            err = at + "makes no item, or has a count, hours or Gizmos out of range";
            return false;
        }
        std::istringstream parts(inputs);
        std::string part;
        while (std::getline(parts, part, ',')) {
            RecipeInput ri;
            if (std::sscanf(part.c_str(), "%d:%d", &ri.item, &ri.count) != 2 || !findItem(ri.item) || ri.count < 1) {
                err = at + "'" + part + "' is not an item:count of an item there is";
                return false;
            }
            r.inputs.push_back(ri);
        }
        if (r.inputs.empty()) {
            err = at + "takes nothing";
            return false;
        }
        out.push_back(std::move(r));
    }
    // Cooking: each cookable item is a Stove recipe that everyone knows, five at a time
    PropKind stove = PropKind::Stove;
    if (propInfo(stove).advanced) {
        for (const ItemDef& d : itemDatabase()) {
            if (d.cooksInto <= 0 || !findItem(d.cooksInto)) continue;
            Recipe r;
            r.key = "Cook" + std::to_string(d.id);
            r.station = stove;
            r.item = d.cooksInto;
            r.count = 5;
            r.hours = 0.5;
            r.inputs.push_back(RecipeInput{ d.id, 5 });
            if (keys.insert(r.key).second) out.push_back(std::move(r));
        }
    }
    table() = std::move(out);
    return true;
}

const std::vector<Recipe>& recipes() { return table(); }

int recipeNamed(const std::string& key) {
    for (size_t i = 0; i < table().size(); ++i) {
        if (table()[i].key == key) return (int)i;
    }
    return -1;
}

std::string recipeName(const Recipe& r) {
    const std::string name = itemNameOf(r.item);
    return r.count > 1 ? name + " x" + std::to_string(r.count) : name;
}

std::string recipeInputsText(const Recipe& r) {
    std::string out;
    for (const RecipeInput& in : r.inputs) {
        if (!out.empty()) out += ", ";
        out += std::to_string(in.count) + " " + itemNameOf(in.item);
    }
    return out;
}

bool stationTakesItem(PropKind station, int item) {
    for (const Recipe& r : table()) {
        if (r.station != station) continue;
        for (const RecipeInput& in : r.inputs) {
            if (in.item == item) return true;
        }
    }
    return false;
}

int gizmosIn(World& w, ObjectId table_) {
    const Storage* st = w.get<Storage>(table_);
    int n = 0;
    if (st) {
        for (const ItemStack& s : st->slots) n += s.count > 0 && isGizmo(s.item) ? s.count : 0;
    }
    return n;
}

bool recipeUnlocked(World& w, int faction, int recipe) {
    if (recipe < 0 || recipe >= (int)table().size()) return false;
    const Recipe& r = table()[(size_t)recipe];
    if (r.gizmos <= 0) return true;
    const auto& by = w.resource<Researched>().byFaction;
    const auto it = by.find(faction);
    return it != by.end() && it->second.count(r.key) > 0;
}

std::vector<std::string> researchedRecipes(World& w, int faction) {
    const auto& by = w.resource<Researched>().byFaction;
    const auto it = by.find(faction);
    return it == by.end() ? std::vector<std::string>{} : std::vector<std::string>(it->second.begin(), it->second.end());
}

void setResearched(World& w, int faction, const std::string& key) {
    if (recipeNamed(key) >= 0) w.resource<Researched>().byFaction[faction].insert(key);
}

bool researchRecipe(World& w, ObjectId table_, int recipe, int faction) {
    if (recipe < 0 || recipe >= (int)table().size() || recipeUnlocked(w, faction, recipe)) return false;
    const Recipe& r = table()[(size_t)recipe];
    Storage* st = w.get<Storage>(table_);
    if (!st || gizmosIn(w, table_) < r.gizmos) return false;
    int left = r.gizmos;
    for (ItemStack& s : st->slots) {
        if (left <= 0 || s.count <= 0 || !isGizmo(s.item)) continue;
        const int take = std::min(left, s.count);
        s.count -= take;
        left -= take;
        if (s.count <= 0) s = ItemStack{};
    }
    setResearched(w, faction, r.key);
    queueLog(w, langf("MISC_RECIPE_RESEARCHED", { factionName(faction), recipeName(r) }), LogKind::Other, faction != FACTION_PLAYER, Tint::Green);
    queueSoundFrom(w, Sfx::Unlocked, table_);
    return true;
}

int craftFaction(World& w, ObjectId prop) {
    const Owner* own = w.get<Owner>(prop);
    return own ? own->faction : FACTION_PLAYER;
}

int factionStock(World& w, int faction, int item) {
    const std::vector<Claim> claims = claimsOf(w);
    int n = 0;
    for (ObjectId s : w.view<Prop, Transform>()) {
        if (w.has<Blueprint>(s) || !propOfFaction(w, claims, s, faction)) continue;
        if (const Storage* st = w.get<Storage>(s)) n += countInSlots(st->slots.data(), (int)st->slots.size(), item);
        if (const Output* out = w.get<Output>(s)) n += countInSlots(out->slots.data(), (int)out->slots.size(), item);
    }
    return n;
}

bool outputEmpty(World& w, ObjectId prop) {
    const Output* out = w.get<Output>(prop);
    if (!out) return true;
    for (const ItemStack& s : out->slots) {
        if (s.count > 0) return false;
    }
    return true;
}

const char* craftBlock(World& w, ObjectId prop) {
    const CraftOrder* o = w.get<CraftOrder>(prop);
    const Recipe* r = orderRecipe(w, prop);
    if (!o || !r) return "MISC_CRAFT_NO_RECIPE";
    if (!recipeUnlocked(w, craftFaction(w, prop), o->recipe)) return "MISC_CRAFT_NOT_RESEARCHED";
    if (!outputEmpty(w, prop)) return "MISC_CRAFT_OUTPUT_FULL";
    if (missingInput(w, prop, *r)) return "MISC_CRAFT_MISSING";
    return nullptr;
}

void finishCraft(World& w, ObjectId prop, ObjectId worker) {
    const Recipe* r = orderRecipe(w, prop);
    Storage* st = w.get<Storage>(prop);
    if (!r || !st || missingInput(w, prop, *r)) return;
    for (const RecipeInput& in : r->inputs) takeFromSlots(st->slots.data(), (int)st->slots.size(), in.item, in.count);
    Output& out = w.has<Output>(prop) ? *w.get<Output>(prop) : w.add(prop, Output{});
    const int left = addToSlots(out.slots.data(), (int)out.slots.size(), r->item, r->count);
    if (left > 0) {
        if (const Transform* pt = w.get<Transform>(prop)) spawnGroundItem(w, *pt, r->item, left, storeyOf(w, prop));
    }
    queueItemMove(w, NO_OBJECT, prop, r->item, r->count);
    queueLog(w, langf("MISC_CRAFTS_BATCH", { nameOf(w, worker), recipeName(*r) }), LogKind::Pickup, npcOnly(w, worker));
}

bool setCraftOrder(World& w, ObjectId prop, int recipe, int keep) {
    const Prop* p = w.get<Prop>(prop);
    if (!p || !propInfo(p->kind).advanced) return false;
    if (recipe >= 0) {
        if (recipe >= (int)table().size() || table()[(size_t)recipe].station != p->kind) return false;
        if (!recipeUnlocked(w, craftFaction(w, prop), recipe)) return false;
    }
    w.add(prop, CraftOrder{ recipe, std::clamp(keep, 0, 999) });
    propStorageChanged(w, prop);
    return true;
}

bool startCraftStep(World& w, Terrain& t, ObjectId u, Behaviors& b, BehaviorRule& r) {
    r.why = BehaviorWait::NoCraftOrder;
    if (w.has<Mob>(u) || !w.has<Transform>(u) || w.has<Carrying>(u) || w.has<CraftJob>(u)) return false;
    const std::vector<Claim> claims = claimsOf(w);
    const Transform* ut = w.get<Transform>(u);
    std::vector<ObjectId> taken;
    for (ObjectId o : w.view<CraftJob>()) {
        if (o != u) taken.push_back(w.get<CraftJob>(o)->prop);
    }
    ObjectId best = NO_OBJECT;
    float bestD2 = 0.0f;
    for (ObjectId p : w.view<CraftOrder, Prop, Transform>()) {
        if (std::find(taken.begin(), taken.end(), p) != taken.end() || !orderWantsFilling(w, claims, u, p)) continue;
        const float d2 = distSq(*w.get<Transform>(p), *ut);
        if (best == NO_OBJECT || d2 < bestD2) {
            best = p;
            bestD2 = d2;
        }
    }
    if (best == NO_OBJECT) return false;
    r.why = BehaviorWait::None;
    clearOrdersButFight(w, u);
    w.remove<IdleSpot>(u);
    b.busy = true;
    b.step = r;
    b.stepProp = best;
    CraftJob job;
    job.prop = best;
    w.add(u, job);
    return true;
}

void craftJobSystem(World& w, Terrain& t, float dt) {
    std::vector<Claim> claims;
    bool read = false;
    auto claimList = [&]() -> const std::vector<Claim>& {
        if (!read) claims = claimsOf(w);
        read = true;
        return claims;
    };
    for (ObjectId u : w.view<CraftJob, Transform>()) {
        CraftJob& job = *w.get<CraftJob>(u);
        Behaviors* b = w.get<Behaviors>(u);
        const ObjectId prop = job.prop;
        auto end = [&](float cooldown) {
            w.remove<CraftJob>(u);
            w.remove<SoloMove>(u);
            w.remove<Route>(u);
            if (!b || !b->busy || b->step.kind != BehaviorKind::AutoCraft) return;
            b->busy = false;
            if (BehaviorRule* r = ruleOf(*b, BehaviorKind::AutoCraft)) r->cooldown = std::max(r->cooldown, cooldown);
        };
        const Recipe* recipe = w.has<Prop>(prop) ? orderRecipe(w, prop) : nullptr;
        if (!b || outCold(w, u) || w.has<Carried>(u) || !recipe || !w.has<Transform>(prop)) {
            end(BEHAVIOR_RETRY_SECONDS);
            continue;
        }
        // Working: the job lasts as long as the work does, and ends with the batch
        if (job.working) {
            const Working* wk = w.get<Working>(u);
            if (!wk || wk->prop != prop) end(0.0f);
            continue;
        }
        if (!outputEmpty(w, prop)) {
            end(BEHAVIOR_RETRY_SECONDS);
            continue;
        }
        Inventory* inv = w.get<Inventory>(u);
        const RecipeInput* need = missingInput(w, prop, *recipe);
        ObjectId goal = prop;
        if (need) {
            const int carried = inv ? carriedCount(*inv, need->item) : 0;
            if (carried > 0) {
                job.phase = CraftJob::Phase::Deliver;
            } else {
                job.phase = CraftJob::Phase::Fetch;
                if (job.item != need->item || !w.has<Storage>(job.source) || held(w, job.source, need->item) <= 0) {
                    job.item = need->item;
                    job.source = storeHolding(w, claimList(), u, prop, need->item);
                }
                if (job.source == NO_OBJECT) {
                    end(BEHAVIOR_RETRY_SECONDS * 5.0f);
                    continue;
                }
                goal = job.source;
            }
        } else {
            job.phase = CraftJob::Phase::Work;
        }
        if (flatGap(w, u, goal) > PROP_REACH + 0.3f) {
            if (!walkingNow(w, u)) {
                job.stuck += dt;
                if (job.stuck > 12.0f) end(BEHAVIOR_RETRY_SECONDS * 3.0f);
                else if (job.stuck <= dt * 1.5f || (job.stuck > 1.0f && job.stuck - dt <= 1.0f)) walkTo(w, t, u, goal);
            }
            continue;
        }
        if (w.has<Climbing>(u)) continue;
        job.stuck = 0.0f;
        w.remove<SoloMove>(u);
        w.remove<Route>(u);
        if (job.phase == CraftJob::Phase::Fetch) {
            Storage* st = w.get<Storage>(job.source);
            const int want = std::min(need->count - held(w, prop, need->item),
                                      inv ? slotsRoomFor(inv->slots.data(), INVENTORY_SLOTS, need->item) : 0);
            const int took = st && want > 0 ? takeFromSlots(st->slots.data(), (int)st->slots.size(), need->item, want) : 0;
            if (took <= 0) {
                end(BEHAVIOR_RETRY_SECONDS * 3.0f);
                continue;
            }
            addToInventory(*inv, need->item, took);
            propStorageChanged(w, job.source);
            queueItemMove(w, job.source, u, need->item, took);
            queueSoundFrom(w, Sfx::ItemMove, job.source);
            continue;
        }
        if (job.phase == CraftJob::Phase::Deliver) {
            Storage* st = w.get<Storage>(prop);
            const int give = std::min(inv ? carriedCount(*inv, need->item) : 0, need->count - held(w, prop, need->item));
            const int left = st && give > 0 ? addToStore(*st, need->item, give) : give;
            if (give - left <= 0) {
                end(BEHAVIOR_RETRY_SECONDS * 3.0f);   // the prop's store is full of other things
                continue;
            }
            takeFromSlots(inv->slots.data(), INVENTORY_SLOTS, need->item, give - left);
            queueItemMove(w, u, prop, need->item, give - left);
            queueSoundFrom(w, Sfx::ItemMove, prop);
            continue;
        }
        // Everything is in: to work
        if (tryStartWork(w, u, prop) == WorkStart::Started) {
            job.working = true;
        } else {
            end(BEHAVIOR_RETRY_SECONDS * 3.0f);
        }
    }
}

ObjectId craftOutputToHaul(World& w, ObjectId u, const std::vector<ObjectId>& taken) {
    const Transform* ut = w.get<Transform>(u);
    if (!ut) return NO_OBJECT;
    const std::vector<Claim> claims = claimsOf(w);
    const int faction = factionOf(w, u);
    ObjectId best = NO_OBJECT;
    float bestD2 = 0.0f;
    for (ObjectId p : w.view<Output, Prop, Transform>()) {
        if (!propInfo(w.get<Prop>(p)->kind).advanced || outputEmpty(w, p) || w.has<Blueprint>(p)) continue;
        if (std::find(taken.begin(), taken.end(), p) != taken.end()) continue;
        if (!propOfFaction(w, claims, p, faction) || lockShutFor(w, claims, p, u)) continue;
        const float d2 = distSq(*w.get<Transform>(p), *ut);
        if (best == NO_OBJECT || d2 < bestD2) {
            best = p;
            bestD2 = d2;
        }
    }
    return best;
}

int takeCraftOutput(World& w, ObjectId u, ObjectId prop) {
    Output* out = w.get<Output>(prop);
    Inventory* inv = w.get<Inventory>(u);
    if (!out || !inv) return 0;
    int moved = 0;
    for (ItemStack& s : out->slots) {
        if (s.count <= 0) continue;
        const int left = addToInventory(*inv, s.item, s.count);
        const int took = s.count - left;
        if (took > 0) {
            queueItemMove(w, prop, u, s.item, took);
            moved += took;
        }
        if (left > 0) s.count = left;
        else s = ItemStack{};
    }
    if (moved > 0) {
        queueSoundFrom(w, Sfx::ItemMove, prop);
        propStorageChanged(w, prop);
    }
    return moved;
}
