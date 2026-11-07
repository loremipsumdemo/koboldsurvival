#include "game_tables.h"
#include "level.h"
#include "systems_internal.h"
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
        d.props.push_back(std::move(s));
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
    } else if (so && so->next < so->waypoints.size() && !automated && !po && !hasPlayerOrder(w, u) &&
               !w.has<CarryOrder>(u) && !(own && own->seeking)) {
        su.move = so->waypoints.back();
    }
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
        if (r.kind == BehaviorKind::AutoHaul) {
            sr.prop = idOf(propIds, r.prop);
            if (sr.prop < 0) continue;
        }
        su.rules.push_back(sr);
    }
    // An idle unit would never pick these two up again: lying in the bed, or carrying the patient
    const bool resting = b->busy && b->step.kind == BehaviorKind::Rest && w.has<Occupying>(u);
    const bool rescuing = b->busy && b->step.kind == BehaviorKind::Rescue && w.has<Carrying>(u);
    if (resting || rescuing) su.stepKind = idx(b->step.kind);
}

void captureUnit(World& w, ObjectId u, SaveUnit& su, const IdMap& propIds, const IdMap& unitIds) {
    su.at = *w.get<Transform>(u);
    su.faction = factionOf(w, u);
    if (const UnitSprite* us = w.get<UnitSprite>(u)) su.sprite = us->id;
    if (const Name* n = w.get<Name>(u)) su.name = n->text;
    if (const Mob* m = w.get<Mob>(u)) su.mob = idx(m->kind);
    if (const Body* b = w.get<Body>(u)) su.body = idx(b->type);
    if (const SpawnHome* h = w.get<SpawnHome>(u)) su.home = *h;
    if (const Occupying* oc = w.get<Occupying>(u)) su.occupying = idOf(propIds, oc->prop);
    if (const Unconscious* un = w.get<Unconscious>(u)) su.down = *un;
    if (const Corpse* c = w.get<Corpse>(u)) su.corpseHours = c->hours;
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
    su.squad = squadOf(w, u);
    su.citizen = w.has<Citizen>(u);
}

std::vector<ObjectId> restoreProps(World& world, const SaveData& d) {
    std::vector<ObjectId> props;
    props.reserve(d.props.size());
    for (const SaveProp& s : d.props) {
        const ObjectId e = makeProp(world, (PropKind)s.kind, s.at, s.yaw);
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
    }
    return props;
}

ObjectId restoreUnit(World& world, const SaveUnit& u, const std::vector<ObjectId>& props) {
    const ObjectId e = makeEntity(world, u.at, u.faction);
    if (u.mob > 0) world.add(e, Mob{ (MobKind)u.mob });
    else if (!world.has<Commandable>(e) && !u.corpseHours) world.add(e, defaultDialogue());
    if (u.corpseHours) {
        world.remove<Commandable>(e);
        world.add(e, Corpse{ *u.corpseHours });
    } else if (u.down) {
        world.add(e, *u.down);
    }
    if (u.body) world.add(e, Body{ (BodyType)std::clamp(*u.body, 0, 2) });
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
        if (rule.kind == BehaviorKind::AutoHaul) {
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
    if (u.citizen && world.has<Commandable>(e)) {
        world.remove<Commandable>(e);
        world.add(e, Citizen{});
    } else if (u.squad >= 0 && u.squad < SQUAD_COUNT && world.has<Commandable>(e)) {
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
            if (propInfo(world.get<Prop>(p)->kind).use == PropUse::Occupy && propOccupant(world, p) == NO_OBJECT) {
                *world.get<Transform>(e) = *world.get<Transform>(p);
                world.add(e, Occupying{ p });
            }
        }
        if (u.workProp >= 0 && u.workProp < (int)props.size()) world.add(e, Working{ props[(size_t)u.workProp], u.workUnlocking });
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
        if (u.move) walkers[{ u.move->x, u.move->z }].push_back(e);
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
    SquadBook& book = world.resource<SquadBook>();
    book.names = d.squadNames;
    book.current = std::clamp(d.currentSquad, 0, SQUAD_COUNT - 1);
    const std::vector<ObjectId> props = restoreProps(world, d);
    for (const SaveGround& g : d.ground) {
        if (!findItem(g.item.item) || g.item.count <= 0) continue;
        const ObjectId lying = spawnGroundItem(world, g.at, g.item.item, g.item.count);
        world.get<GroundItem>(lying)->hours = g.item.hours;
    }
    std::vector<ObjectId> units;
    units.reserve(d.units.size());
    for (const SaveUnit& u : d.units) units.push_back(restoreUnit(world, u, props));
    restoreLinks(world, d, units, props);
    restoreOrders(world, terrain, d, units, props);
    settleSquads(world);
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
    d.squadNames = w.resource<SquadBook>().names;
    d.currentSquad = w.resource<SquadBook>().current;
    d.cam = SaveCamera{ cam.focusX, cam.focusY, cam.focusZ, cam.yaw, cam.pitch, cam.distance };
    std::ostringstream state;
    state << rng.gen;
    d.rngState = state.str();

    IdMap propIds;
    captureProps(w, d, propIds);
    for (ObjectId e : w.view<GroundItem, Transform>()) d.ground.push_back(SaveGround{ *w.get<Transform>(e), *w.get<GroundItem>(e) });

    std::vector<ObjectId> units = w.view<Entity, Transform>();
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
