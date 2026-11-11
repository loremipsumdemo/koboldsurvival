#include "behavior_groups.h"
#include "game_tables.h"
#include "level.h"
#include "systems_internal.h"
#include "wheelbarrow.h"
#include "taming.h"
#include "recipes.h"
#include "build.h"
#include "hostility.h"
#include <sstream>
#include <unordered_map>

using namespace systems_detail;

namespace {
using IdMap = std::unordered_map<ObjectId, int>;

int idOf(const IdMap& ids, ObjectId e) {
    const auto it = ids.find(e);
    return it == ids.end() ? -1 : it->second;
}

template <size_t N>
std::vector<SaveSlot> occupiedSlots(const std::array<ItemStack, N>& slots) {
    std::vector<SaveSlot> out;
    for (size_t i = 0; i < N; ++i) {
        if (slots[i].item > 0 && slots[i].count > 0) out.push_back(SaveSlot{ (int)i, slots[i] });
    }
    return out;
}

template <size_t N>
void restoreSlots(const std::vector<SaveSlot>& saved, std::array<ItemStack, N>& slots) {
    for (const SaveSlot& s : saved) {
        if (s.slot < 0 || s.slot >= (int)N || !findItem(s.stack.item) || s.stack.count <= 0) continue;
        slots[(size_t)s.slot] = ItemStack{ s.stack.item, std::min(s.stack.count, std::max(1, stackLimit(s.stack.item))) };
    }
}

std::vector<SaveSlot> occupiedSlots(const std::vector<ItemStack>& slots) {
    std::vector<SaveSlot> out;
    for (size_t i = 0; i < slots.size(); ++i) {
        if (slots[i].item > 0 && slots[i].count > 0) out.push_back(SaveSlot{ (int)i, slots[i] });
    }
    return out;
}

void restoreSlots(const std::vector<SaveSlot>& saved, std::vector<ItemStack>& slots) {
    for (const SaveSlot& s : saved) {
        if (s.slot < 0 || s.slot >= (int)slots.size() || !findItem(s.stack.item) || s.stack.count <= 0) continue;
        slots[(size_t)s.slot] = ItemStack{ s.stack.item, std::min(s.stack.count, std::max(1, stackLimit(s.stack.item))) };
    }
}

// Oldest first, since the older of two Altars holds a claim
void captureProps(World& w, SaveData& d, IdMap& ids) {
    std::vector<ObjectId> props = w.view<Prop, Transform>();
    std::sort(props.begin(), props.end());
    for (ObjectId e : props) {
        ids[e] = (int)d.props.size();
        const Prop* p = w.get<Prop>(e);
        SaveProp s;
        s.kind = idx(p->kind);
        s.at = *w.get<Transform>(e);
        s.yaw = p->yaw;
        if (const PropVisual* pv = w.get<PropVisual>(e); pv && !pv->plain()) s.visual = *pv;
        if (const Owner* own = w.get<Owner>(e)) s.owner = own->faction;
        if (const Lock* lock = w.get<Lock>(e)) s.lock = *lock;
        if (const Storage* st = w.get<Storage>(e)) s.storage = occupiedSlots(st->slots);
        if (const Output* out = w.get<Output>(e)) s.output = occupiedSlots(out->slots);
        if (const Fuel* fuel = w.get<Fuel>(e)) s.fuel = *fuel;
        if (const PropWork* pw = w.get<PropWork>(e)) {
            s.workProgress = pw->progress;
            s.workTurn = pw->turn;
        }
        if (const BonesPile* pile = w.get<BonesPile>(e)) {
            s.bonesHours = pile->hours;
            s.bonesItems = pile->items;
        }
        if (const Health* h = w.get<Health>(e)) s.health = *h;
        if (const OutdoorDecay* od = w.get<OutdoorDecay>(e)) s.outdoorHours = od->hours;
        if (const Foundation* f = w.get<Foundation>(e)) s.foundation = *f;
        if (const Blueprint* bp = w.get<Blueprint>(e)) s.blueprint = *bp;
        s.material = (int)materialOf(w, e);
        if (const StoreFilter* f = w.get<StoreFilter>(e)) s.storeFilter = f->allowed;
        if (const CraftOrder* co = w.get<CraftOrder>(e)) {
            if (co->recipe >= 0 && co->recipe < (int)recipes().size()) s.craftRecipe = recipes()[(size_t)co->recipe].key;
            s.craftKeep = co->keep;
        }
        s.bedPublic = w.has<BedPublic>(e);
        if (const StallStock* ss = w.get<StallStock>(e)) s.stallPayment = ss->payment;
        d.props.push_back(std::move(s));
    }
    // A Stall's orders name their stores, all of which have their numbers by now
    for (ObjectId e : props) {
        const StallStock* ss = w.get<StallStock>(e);
        if (!ss) continue;
        SaveProp& s = d.props[(size_t)ids[e]];
        for (const StallRequest& r : ss->requests) {
            const int from = idOf(ids, r.from);
            if (from >= 0 && r.item > 0 && r.count > 0) s.stallRequests.push_back({ from, r.item, r.count });
        }
    }
}

void captureOrders(World& w, ObjectId u, SaveUnit& su, const IdMap& propIds, const IdMap& unitIds) {
    const Behaviors* own = w.get<Behaviors>(u);
    const bool automated = own && own->busy;
    if (const FollowOrder* fo = w.get<FollowOrder>(u)) su.followUnit = idOf(unitIds, fo->target);
    if (const DefendOrder* de = w.get<DefendOrder>(u)) su.defendUnit = idOf(unitIds, de->ward);
    else if (const AttackOrder* ao = w.get<AttackOrder>(u)) su.attackUnit = idOf(unitIds, ao->target);
    const PropOrder* po = w.get<PropOrder>(u);
    const Working* wk = w.get<Working>(u);
    const SoloMove* so = w.get<SoloMove>(u);
    const ActiveSquadMember* sm = w.get<ActiveSquadMember>(u);
    const ActiveSquad* sq = sm ? w.get<ActiveSquad>(sm->squad) : nullptr;
    if (po && !po->automatic) {
        su.propOrder = idOf(propIds, po->prop);
        su.propAction = idx(po->action);
    } else if (wk && !automated) {
        // Not as an order: starting work again would charge a lockpick's reputation penalty again
        su.workProp = idOf(propIds, wk->prop);
        su.workUnlocking = wk->unlocking;
    } else if (sq && !sq->arrived && !sq->waypoints.empty()) {
        su.move = sq->waypoints.back();
    } else if (const Route* r = w.get<Route>(u); r && !r->legs.empty() && !automated && !po && !hasPlayerOrder(w, u) &&
               !w.has<CarryOrder>(u) && !(own && own->seeking)) {
        // Bound for another floor: its goal and floor, not just the end of the leg it is on
        su.floorMove = r->goal;
        su.floorMoveStorey = r->legs.back().storey;
    } else if (so && so->next < so->waypoints.size() && !automated && !po && !hasPlayerOrder(w, u) &&
               !w.has<CarryOrder>(u) && !(own && own->seeking)) {
        su.move = so->waypoints.back();
    }
}

void captureGroups(World& w, SaveData& d, const IdMap& propIds) {
    BehaviorGroups& g = behaviorGroups(w);
    d.storesListed = g.storesListed;
    std::vector<int> refs = { GROUP_REF_MILITARY, GROUP_REF_CITIZEN };
    for (int k = 0; k < (int)g.groups.size(); ++k) refs.push_back(k);
    for (int ref : refs) {
        SaveGroup sg;
        sg.ref = ref;
        for (const BehaviorRule& r : *groupList(g, ref)) {
            SaveRule sr;
            sr.rule = r;
            if (ruleNamesProp(r.kind)) {
                sr.prop = idOf(propIds, r.prop);
                if (sr.prop < 0) continue;
            }
            sg.rules.push_back(sr);
        }
        d.groups.push_back(std::move(sg));
    }
}

void restoreGroups(World& world, const SaveData& d, const std::vector<ObjectId>& props) {
    BehaviorGroups& g = behaviorGroups(world);
    if (d.groups.empty()) return;   // from before behavior groups: the defaults stand
    g.groups.clear();
    for (const SaveGroup& sg : d.groups) {
        std::vector<BehaviorRule>* list = nullptr;
        if (sg.ref == GROUP_REF_MILITARY) list = &g.military;
        else if (sg.ref == GROUP_REF_CITIZEN) list = &g.citizen;
        else if (sg.ref >= 0 && sg.ref < BEHAVIOR_GROUP_MAX) {
            if ((int)g.groups.size() <= sg.ref) g.groups.resize((size_t)sg.ref + 1);
            list = &g.groups[(size_t)sg.ref];
        }
        if (!list) continue;
        list->clear();
        for (const SaveRule& r : sg.rules) {
            BehaviorRule rule = r.rule;
            if (rule.kind == BehaviorKind::TameAnimals) continue;
            if (ruleNamesProp(rule.kind)) {
                if (r.prop < 0 || r.prop >= (int)props.size()) continue;
                rule.prop = props[(size_t)r.prop];
            }
            // Patrol Spawn is for wild mobs: your citizens patrol their town
            if (rule.kind == BehaviorKind::PatrolSpawn) rule.kind = BehaviorKind::Patrol;
            if (holdsBehavior(*list, rule)) continue;
            list->push_back(rule);
        }
    }
    if (g.groups.empty()) resetBehaviorGroups(g);
    if (d.storesListed) markStoresListed(world);
    ++g.version;
}

void captureBehaviors(World& w, ObjectId u, SaveUnit& su, const IdMap& propIds) {
    const Behaviors* b = w.get<Behaviors>(u);
    if (!b) return;
    su.passive = b->passive;
    su.block = b->block;
    su.sneak = b->sneak;
    for (const BehaviorRule& r : b->rules) {
        SaveRule sr;
        sr.rule = r;
        if (ruleNamesProp(r.kind)) {
            sr.prop = idOf(propIds, r.prop);
            if (sr.prop < 0) continue;
        }
        su.rules.push_back(sr);
    }
    // An idle unit would never pick these two up again: lying in the bed, or carrying the patient
    const bool resting = b->busy && isBedStep(b->step.kind) && w.has<Occupying>(u);
    const bool rescuing = b->busy && b->step.kind == BehaviorKind::Rescue && w.has<Carrying>(u);
    if (resting || rescuing) su.stepKind = idx(b->step.kind);
}

void captureUnit(World& w, ObjectId u, SaveUnit& su, const IdMap& propIds, const IdMap& unitIds) {
    su.at = *w.get<Transform>(u);
    su.faction = factionOf(w, u);
    if (const UnitSprite* us = w.get<UnitSprite>(u)) su.sprite = us->id;
    if (const Name* n = w.get<Name>(u)) su.name = n->text;
    if (const Mob* m = w.get<Mob>(u)) su.mob = idx(m->kind);
    if (const Coloration* c = w.get<Coloration>(u)) su.coloration = c->hue;
    if (const Taming* tm = w.get<Taming>(u)) su.tamingHours = tm->hours;
    if (const Body* b = w.get<Body>(u)) su.body = idx(b->type);
    if (const SpawnHome* h = w.get<SpawnHome>(u)) su.home = *h;
    if (const Occupying* oc = w.get<Occupying>(u)) su.occupying = idOf(propIds, oc->prop);
    if (const Pushing* pu = w.get<Pushing>(u)) su.pushing = idOf(propIds, pu->barrow);
    if (const Upstairs* up = w.get<Upstairs>(u)) su.upstairs = idOf(propIds, up->foundation);
    if (const Unconscious* un = w.get<Unconscious>(u)) su.down = *un;
    if (const Corpse* c = w.get<Corpse>(u)) su.corpseHours = c->hours;
    if (const Retreating* r = w.get<Retreating>(u)) su.retreat = *r;
    if (const Carried* cd = w.get<Carried>(u)) su.carriedBy = idOf(unitIds, cd->by);
    su.playingDead = w.has<PlayingDead>(u);
    if (const Needs* n = w.get<Needs>(u)) su.needs = *n;
    if (const Inventory* inv = w.get<Inventory>(u)) su.stacks = occupiedSlots(inv->slots);
    if (const Equipment* worn = w.get<Equipment>(u)) {
        for (int g = 0; g < GEAR_SLOTS; ++g) {
            if (worn->items[g] > 0) su.gear.push_back(SaveGear{ g, worn->items[g] });
        }
    }
    if (const Stats* st = w.get<Stats>(u)) su.stats = *st;
    if (const XpBuffer* xp = w.get<XpBuffer>(u)) su.xpParts = *xp;
    captureBehaviors(w, u, su, propIds);
    if (const Combat* c = w.get<Combat>(u)) {
        su.buffLeft = c->buffLeft;
        su.buffAttack = c->buffAttack;
    }
    if (const Provoked* pv = w.get<Provoked>(u)) su.provoked = pv->against;
    if (w.has<Commandable>(u)) captureOrders(w, u, su, propIds, unitIds);
    if (const SquadMember* sm = w.get<SquadMember>(u)) su.squad = sm->squad;
    su.citizen = w.has<Citizen>(u);
    if (const Citizen* c = w.get<Citizen>(u)) su.citizenGroup = c->group;
}

std::vector<ObjectId> restoreProps(World& world, const SaveData& d) {
    std::vector<ObjectId> props;
    props.reserve(d.props.size());
    for (const SaveProp& s : d.props) {
        const ObjectId e = makeProp(world, (PropKind)s.kind, s.at, s.yaw);
        world.get<Prop>(e)->yaw = s.yaw;   // as it was saved, whatever way a new one would face
        if (s.visual) world.add(e, *s.visual);
        props.push_back(e);
        if (s.owner >= 0) world.add(e, Owner{ s.owner });
        if (s.lock) world.add(e, *s.lock);
        if (Storage* st = world.get<Storage>(e)) restoreSlots(s.storage, st->slots);
        if (Output* out = world.get<Output>(e)) restoreSlots(s.output, out->slots);
        if (s.fuel) world.add(e, *s.fuel);
        if (s.workProgress) {
            PropWork pw;
            pw.progress = std::clamp(*s.workProgress, 0.0, 1.0);
            pw.turn = s.workTurn;
            world.add(e, pw);
        }
        if (s.bonesHours) {
            BonesPile pile{ *s.bonesHours, {} };
            for (const ItemStack& item : s.bonesItems) {
                if (findItem(item.item) && item.count > 0) pile.items.push_back(item);
            }
            world.add(e, std::move(pile));
        }
        if (s.health) world.add(e, *s.health);
        if (s.outdoorHours) world.add(e, OutdoorDecay{ *s.outdoorHours });
        if (s.foundation) world.add(e, *s.foundation);
        if (s.blueprint) world.add(e, *s.blueprint);
        if (s.material > 0) world.add(e, Material{ (BuildMaterial)s.material });
        if (s.storeFilter) world.add(e, StoreFilter{ *s.storeFilter });
        // Its order, if the recipe is still one made at it (a recipe gone from recipes.txt leaves it with none)
        if (!s.craftRecipe.empty() || s.craftKeep > 0) {
            const int recipe = recipeNamed(s.craftRecipe);
            const bool fits = recipe >= 0 && recipes()[(size_t)recipe].station == (PropKind)s.kind;
            world.add(e, CraftOrder{ fits ? recipe : -1, s.craftKeep });
        }
        if (s.blueprint && propInfo((PropKind)s.kind).door) world.remove<Lock>(e);
        if (s.bedPublic) world.add(e, BedPublic{});
        if (StallStock* ss = world.get<StallStock>(e); ss && s.stallPayment) ss->payment = *s.stallPayment;
    }
    for (size_t i = 0; i < d.props.size(); ++i) {
        StallStock* ss = world.get<StallStock>(props[i]);
        if (!ss) continue;
        for (const auto& r : d.props[i].stallRequests) {
            if (r[0] < 0 || r[0] >= (int)props.size() || !findItem(r[1]) || r[2] <= 0) continue;
            ss->requests.push_back(StallRequest{ props[(size_t)r[0]], r[1], r[2], NO_OBJECT });
        }
    }
    return props;
}

ObjectId restoreUnit(World& world, const SaveUnit& u, const std::vector<ObjectId>& props) {
    const ObjectId e = makeEntity(world, u.at, u.faction);
    if (u.mob > 0) world.add(e, Mob{ (MobKind)u.mob });
    // A boss from before bosses had their own coloring gets one, the same each time that save is loaded
    if (u.coloration) world.add(e, Coloration{ *u.coloration });
    else if (u.mob > 0 && isBossMob((MobKind)u.mob)) world.add(e, Coloration{ (int)((u.at.x * 131.0f + u.at.z * 71.0f)) % 360 });
    if (u.tamingHours) world.add(e, Taming{ *u.tamingHours });
    else if (!world.has<Commandable>(e) && !u.corpseHours) world.add(e, defaultDialogue());
    if (u.corpseHours) {
        world.remove<Commandable>(e);
        world.add(e, Corpse{ *u.corpseHours });
    } else if (u.down) {
        world.add(e, *u.down);
    }
    if (u.body) world.add(e, Body{ (BodyType)std::clamp(*u.body, 0, 2) });
    if (u.retreat && !u.corpseHours) world.add(e, *u.retreat);
    if (u.home) world.add(e, *u.home);
    if (u.stats) world.add(e, *u.stats);
    if (u.xpParts) world.add(e, *u.xpParts);
    *world.get<Needs>(e) = u.needs;
    world.get<Name>(e)->text = u.name;
    world.get<UnitSprite>(e)->id = std::clamp(u.sprite, 1, UNIT_SPRITE_COUNT);
    restoreSlots(u.stacks, world.get<Inventory>(e)->slots);
    for (const SaveGear& g : u.gear) {
        if (findItem(g.item)) world.get<Equipment>(e)->items[g.slot] = g.item;
    }
    Behaviors& b = *world.get<Behaviors>(e);
    b.rules.clear();
    for (const SaveRule& r : u.rules) {
        BehaviorRule rule = r.rule;
        if (rule.kind == BehaviorKind::TameAnimals) continue;   // no longer a task: taming takes time alone
        if (ruleNamesProp(rule.kind)) {
            if (r.prop < 0 || r.prop >= (int)props.size()) continue;
            rule.prop = props[(size_t)r.prop];
        }
        b.rules.push_back(rule);
    }
    b.passive = u.passive;
    b.block = u.block;
    b.sneak = u.sneak;
    Combat& c = *world.get<Combat>(e);
    c.buffLeft = u.buffLeft;
    c.buffAttack = u.buffAttack;
    if (!u.provoked.empty()) world.add(e, Provoked{ u.provoked });
    if (!world.has<Commandable>(e)) {
        world.remove<Hostile>(e);
        world.remove<Neutral>(e);
        if (hostileToPlayer(world, e)) world.add(e, Hostile{});
        else world.add(e, Neutral{});
    }
    // Saved as citizens, they keep their home and behaviors from the file
    if (u.citizen) {
        world.remove<Commandable>(e);
        world.add(e, Citizen{ std::max(0, u.citizenGroup), -1 });
    } else if (u.squad >= 0 && u.squad < SQUAD_COUNT) {
        world.add(e, SquadMember{ u.squad });
    }
    return e;
}

void restoreLinks(World& world, const SaveData& d, const std::vector<ObjectId>& units, const std::vector<ObjectId>& props) {
    for (size_t i = 0; i < d.units.size(); ++i) {
        const SaveUnit& u = d.units[i];
        const ObjectId e = units[i];
        if (u.carriedBy >= 0 && u.carriedBy < (int)units.size() && u.carriedBy != (int)i) {
            const ObjectId carrier = units[(size_t)u.carriedBy];
            if (!world.has<Carrying>(carrier) && !world.has<Carried>(carrier) && !outCold(world, carrier)) {
                world.add(e, Carried{ carrier });
                world.add(carrier, Carrying{ e });
            }
        }
        if (u.playingDead && canPlayDead(world, e) && !outCold(world, e)) world.add(e, PlayingDead{});
        if (u.occupying >= 0 && u.occupying < (int)props.size()) {
            const ObjectId p = props[(size_t)u.occupying];
            if (propInfo(world.get<Prop>(p)->kind).use == PropUse::Occupy && occupantCount(world, p) < occupyCapacity(world, p)) {
                *world.get<Transform>(e) = *world.get<Transform>(p);
                world.add(e, Occupying{ p });
            }
        }
        if (u.pushing >= 0 && u.pushing < (int)props.size() && !world.has<Occupying>(e) && !world.has<Carried>(e)) {
            startPushing(world, e, props[(size_t)u.pushing]);
        }
        if (u.workProp >= 0 && u.workProp < (int)props.size()) world.add(e, Working{ props[(size_t)u.workProp], u.workUnlocking });
        if (u.upstairs >= 0 && u.upstairs < (int)props.size() && world.has<Foundation>(props[(size_t)u.upstairs])) {
            world.add(e, Upstairs{ props[(size_t)u.upstairs] });
        }
        if (u.stepKind >= 0) {
            Behaviors& b = *world.get<Behaviors>(e);
            for (const BehaviorRule& r : b.rules) {
                if (idx(r.kind) != u.stepKind) continue;
                b.busy = true;
                b.step = r;
                if (const Occupying* oc = world.get<Occupying>(e)) b.stepProp = oc->prop;
                break;
            }
        }
    }
}

void restoreOrders(World& world, Terrain& terrain, const SaveData& d, const std::vector<ObjectId>& units, const std::vector<ObjectId>& props) {
    const auto unitAt = [&](int i) { return i >= 0 && i < (int)units.size() ? units[(size_t)i] : NO_OBJECT; };
    // Units walking to one point set off together, as a formation
    std::map<std::pair<float, float>, std::vector<ObjectId>> walkers;
    for (size_t i = 0; i < d.units.size(); ++i) {
        const SaveUnit& u = d.units[i];
        const ObjectId e = units[i];
        if (!world.has<Commandable>(e)) continue;
        if (const ObjectId lead = unitAt(u.followUnit); lead != NO_OBJECT) issueFollowOrder(world, { e }, lead);
        if (const ObjectId ward = unitAt(u.defendUnit); ward != NO_OBJECT) issueDefendOrder(world, { e }, ward);
        if (const ObjectId foe = unitAt(u.attackUnit); foe != NO_OBJECT) issueAttackOrder(world, { e }, foe);
        if (u.floorMove) moveToFloor(world, terrain, e, *u.floorMove, u.floorMoveStorey);
        else if (u.move) walkers[{ u.move->x, u.move->z }].push_back(e);
        if (u.propOrder >= 0 && u.propOrder < (int)props.size()) {
            const PropAction action = (PropAction)u.propAction;
            if (action == PropAction::Use || action == PropAction::Occupy || action == PropAction::Unlock || action == PropAction::Destroy) {
                issuePropOrder(world, terrain, { e }, props[(size_t)u.propOrder], action);
            }
        }
    }
    for (const auto& [point, group] : walkers) issueMoveOrder(world, terrain, group, Vec2{ point.first, point.second });
}
}

void restoreWorld(World& world, Terrain& terrain, Rng& rng, const SaveData& d) {
    world = World{};
    setPlayerReputation(world, d.reputation);
    setChangedFactionRules(world, d.factionRules);
    for (const auto& [faction, key] : d.researched) setResearched(world, faction, key);
    SquadBook& book = world.resource<SquadBook>();
    book.names = d.squadNames;
    book.current = std::clamp(d.currentSquad, 0, SQUAD_COUNT - 1);
    world.resource<Graveyard>().ghosts = d.ghosts;
    ghostSystem(world, 0.0);
    {
        std::unordered_map<long long, float> edits;
        for (const auto& g : d.groundEdits) edits[Terrain::cornerKey((int)g[0], (int)g[1])] = g[2];
        world.resource<GroundEdits>().corners = edits;
        terrain.setHeightEdits(edits);
    }
    const std::vector<ObjectId> props = restoreProps(world, d);
    restoreGroups(world, d, props);
    for (const SaveGround& g : d.ground) {
        if (!findItem(g.item.item) || g.item.count <= 0) continue;
        const ObjectId lying = spawnGroundItem(world, g.at, g.item.item, g.item.count);
        world.get<GroundItem>(lying)->hours = g.item.hours;
    }
    std::vector<ObjectId> units;
    units.reserve(d.units.size());
    for (const SaveUnit& u : d.units) units.push_back(restoreUnit(world, u, props));
    restoreLinks(world, d, units, props);
    settleBuildingsAfterLoad(world, terrain);
    // From before Auto-Haul to Stalls: your units and the Citizen Default get it, switched on
    if (!d.stallHaulListed) {
        BehaviorRule stall;
        stall.kind = BehaviorKind::AutoHaulStall;
        for (ObjectId u : world.view<Commandable, Behaviors>()) addBehavior(world.get<Behaviors>(u)->rules, stall, BEHAVIOR_TASKS_MAX);
        BehaviorGroups& g = behaviorGroups(world);
        addBehavior(g.citizen, stall, BEHAVIOR_TASKS_MAX);
        ++g.version;
    }
    restoreOrders(world, terrain, d, units, props);
    settleSquads(world);
    rebuildBlocking(world, terrain);
    std::istringstream state(d.rngState);
    state >> rng.gen;
}

SaveData captureWorld(World& w, const GameState& gs, const Camera& cam, const Rng& rng,
                      long long ts, const std::string& name, const MapChoice& map) {
    SaveData d;
    d.name = name;
    d.timestamp = ts;
    d.mapSource = map.source;
    d.mapStem = map.stem;
    d.gs = gs;
    d.reputation = playerReputation(w);
    d.factionRules = changedFactionRules(w);
    for (const FactionDef& f : factions()) {
        for (const std::string& key : researchedRecipes(w, f.id)) d.researched.push_back({ f.id, key });
    }
    d.squadNames = w.resource<SquadBook>().names;
    d.currentSquad = w.resource<SquadBook>().current;
    d.ghosts = w.resource<Graveyard>().ghosts;
    d.stallHaulListed = true;
    d.cam = SaveCamera{ cam.focusX, cam.focusY, cam.focusZ, cam.yaw, cam.pitch, cam.distance };
    std::ostringstream state;
    state << rng.gen;
    d.rngState = state.str();

    for (const auto& [key, h] : w.resource<GroundEdits>().corners) {
        d.groundEdits.push_back({ (float)(key % 1000003LL), (float)(key / 1000003LL), h });
    }
    std::sort(d.groundEdits.begin(), d.groundEdits.end(), [](const auto& a, const auto& b) {
        return a[1] != b[1] ? a[1] < b[1] : a[0] < b[0];
    });

    IdMap propIds;
    captureProps(w, d, propIds);
    captureGroups(w, d, propIds);
    for (ObjectId e : w.view<GroundItem, Transform>()) d.ground.push_back(SaveGround{ *w.get<Transform>(e), *w.get<GroundItem>(e) });

    std::vector<ObjectId> units = w.view<Entity, Transform>();
    // The Wandering Trader comes and goes within a day: it isn't kept
    units.erase(std::remove_if(units.begin(), units.end(), [&](ObjectId u) { return w.has<WanderingTrader>(u); }), units.end());
    std::sort(units.begin(), units.end());
    IdMap unitIds;
    for (size_t i = 0; i < units.size(); ++i) unitIds[units[i]] = (int)i;
    d.units.resize(units.size());
    for (size_t i = 0; i < units.size(); ++i) captureUnit(w, units[i], d.units[i], propIds, unitIds);
    return d;
}

void applySaveCamera(const SaveCamera& c, Camera& cam) {
    cam.focusX = c.fx;
    cam.focusY = c.fy;
    cam.focusZ = c.fz;
    cam.yaw = c.yaw;
    cam.pitch = c.pitch;
    cam.distance = c.distance;
    cam.clampState();
}
