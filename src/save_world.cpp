// Turning a running level into a save and back.
#include "level.h"
#include "systems.h"
#include "play_state.h"
#include "renderer.h"
#include "need_behaviors.h"

using namespace play;

// Rebuilds a level from a save. The ground comes from the map, and the props from the seed.
// The units are then swapped for the saved ones.
bool restoreLevel(World& world, Terrain& terrain, Renderer& renderer, Rng& rng, const SaveData& d,
                  const MapChoice& map, std::string& err) {
    // A save from before items existed has none, so its items are placed from the seed as on a new level.
    // A save that has items gets exactly those, and nothing is placed from the seed.
    levelPlayerCamp = d.playerCamp;   // the level is rebuilt as it was first made: with your camp or without
    if (!prepareLevel(map, world, terrain, renderer, rng, d.seed, !d.hasItems, err)) return false;

    // Drop the units that setupWorld generated
    for (Entity e : world.view<Unit>()) world.destroy(e);

    std::vector<Entity> restoredUnits;   // in the save's order
    for (const SaveUnit& u : d.units) {
        Entity e = world.create();
        restoredUnits.push_back(e);
        world.add(e, Transform{ u.x, u.y, u.z });
        // Hostiles walk at their own pace, as on a new level. Every unit fights, so each one gets Combat.
        Unit unit;
        if (u.hostile || u.neutral) unit.speed = HOSTILE_WALK_SPEED;
        world.add(e, unit);
        world.add(e, Combat{});
        if (u.hostile) {
            world.add(e, Hostile{});
        } else if (u.neutral) {
            world.add(e, Neutral{});
        } else if (!u.corpse) {
            world.add(e, Commandable{});   // one of yours; a corpse is nobody's
        }
        if (u.unconscious) {
            Unconscious un{ u.corpse ? 100.0f : u.dying, u.koFaction };
            un.conscious = u.conscious;
            world.add(e, un);
            if (u.corpse) world.add(e, Corpse{});
        } else if (u.playingDead && u.carriedBy < 0) {
            world.add(e, PlayingDead{});   // woke near hostile units, and lies still on the ground
        }
        if ((u.hostile || u.neutral) && u.mob <= 0) world.add(e, Dialogue{});   // a mob has nothing to say
        if (u.mob > 0) world.add(e, Mob{ (MobKind)u.mob });
        if (u.hasHome) world.add(e, SpawnHome{ u.homeX, u.homeZ, u.homeReturning });
        // Every unit carries items and has behaviors, whichever side it is on
        Inventory inv;
        for (const SaveStack& s : u.stacks) {
            if (s.slot >= 0 && s.slot < INVENTORY_SLOTS && s.stack.item > 0 && s.stack.count > 0) {
                inv.slots[s.slot] = s.stack;
            }
        }
        world.add(e, inv);
        Equipment worn;
        for (const SaveGear& g : u.gear) worn.items[g.slot] = g.item;
        world.add(e, worn);
        world.add(e, u.hasNeeds ? u.needs : Needs{});
        Behaviors behaviors;   // older saves have none, so their units get the defaults
        if (u.mob > 0) behaviors.rules = mobBehaviors();
        if (u.hasBehaviors) {
            // An Auto-Haul's prop is found below, once the props are all there
            behaviors.rules.clear();
            for (const SaveBehavior& b : u.behaviors) behaviors.rules.push_back(b.rule);
            // A list saved whole keeps any needs behavior deleted from it; a mob still never keeps one
            if (!u.behaviorsWhole || u.mob > 0) ensureNeedsBehaviors(behaviors.rules, u.mob > 0);
        }
        behaviors.passive = u.passive;
        behaviors.block = u.block;
        behaviors.sneak = u.sneak;
        world.add(e, behaviors);
        world.add(e, u.hasStats ? Stats{ u.stats } : defaultStats());
        world.add(e, Name{ u.name });
        world.add(e, UnitSprite{ u.sprite });
        if (u.mob <= 0) world.add(e, Body{ u.hasBody ? (BodyType)u.body : bodyForSavedName(u.name) });
        // Older saves have no faction: it comes from the side the unit is on. A faction no longer listed (the old Gray
        // Faction) maps to the one that replaced it.
        const int savedFaction = u.hasFaction ? u.faction : u.hostile ? FACTION_RED : u.neutral ? FACTION_RETIRED_GRAY : FACTION_PLAYER;
        world.add(e, Faction{ savedFaction == FACTION_PLAYER ? FACTION_PLAYER : knownFactionId(savedFaction) });
        if (world.get<Faction>(e)->id == FACTION_EXILE && u.mob <= 0) {
            Behaviors* eb = world.get<Behaviors>(e);
            keepExileBehaviors(eb->rules);
        }
    }

    // Who was carrying whom: the carrier is named by its place in the save
    for (size_t i = 0; i < d.units.size() && i < restoredUnits.size(); ++i) {
        const int by = d.units[i].carriedBy;
        if (by < 0 || by >= (int)restoredUnits.size() || by == (int)i) continue;
        const Entity carrier = restoredUnits[(size_t)by];
        if (world.has<Carrying>(carrier) || world.has<Unconscious>(carrier) || world.has<Carried>(carrier)) continue;
        world.add(restoredUnits[i], Carried{ carrier });
        if (d.units[i].playingDead && !d.units[i].unconscious) world.add(restoredUnits[i], PlayingDead{});
        world.add(carrier, Carrying{ restoredUnits[i] });
    }

    if (d.hasItems) {
        for (const SaveGround& g : d.ground) {
            if (g.item <= 0 || g.count <= 0) continue;
            spawnGroundItem(world, Transform{ g.x, g.y, g.z }, g.item, g.count);
        }
    }
    // Auto-Haul props are found by where they stand (a camp's props are placed again from the seed, on the same spots).
    // One whose prop is not there any more is dropped. An older save's routine becomes Auto-Haul to its store prop
    // and Auto-Harvest of its gather prop's kind.
    {
        auto propNear = [&](float x, float z, bool storage) {
            Entity best = NULL_ENTITY;
            float bestD2 = 0.25f;   // within half a tile
            for (Entity p : world.view<Prop, Transform>()) {
                const PropUse use = propInfo(world.get<Prop>(p)->kind).use;
                if (storage ? !world.has<Storage>(p) : use != PropUse::Resource) continue;
                const Transform* pt = world.get<Transform>(p);
                const float d2 = (pt->x - x) * (pt->x - x) + (pt->z - z) * (pt->z - z);
                if (d2 <= bestD2) {
                    best = p;
                    bestD2 = d2;
                }
            }
            return best;
        };
        for (size_t i = 0; i < d.units.size() && i < restoredUnits.size(); ++i) {
            const SaveUnit& u = d.units[i];
            Behaviors* b = world.get<Behaviors>(restoredUnits[i]);
            if (!b) continue;
            size_t at = 0;
            for (const SaveBehavior& sb : u.behaviors) {
                if (at >= b->rules.size()) break;
                if (b->rules[at].kind == BehaviorKind::AutoHaul) {
                    const Entity prop = propNear(sb.x, sb.z, true);
                    if (prop == NULL_ENTITY) {
                        b->rules.erase(b->rules.begin() + (long)at);
                        continue;
                    }
                    b->rules[at].prop = prop;
                    b->rules[at].propKind = idx(world.get<Prop>(prop)->kind);
                }
                ++at;
            }
            if (u.hasRoutine) {
                const Entity store = propNear(u.storeX, u.storeZ, true);
                const Entity gather = propNear(u.gatherX, u.gatherZ, false);
                if (store != NULL_ENTITY && gather != NULL_ENTITY) {
                    BehaviorRule haul;
                    haul.kind = BehaviorKind::AutoHaul;
                    haul.on = u.routineOn;
                    haul.prop = store;
                    haul.propKind = idx(world.get<Prop>(store)->kind);
                    BehaviorRule harvest;
                    harvest.kind = BehaviorKind::AutoHarvest;
                    harvest.on = u.routineOn;
                    harvest.propKind = idx(world.get<Prop>(gather)->kind);
                    addBehavior(b->rules, haul);
                    addBehavior(b->rules, harvest);
                }
            }
        }
    }
    // The props' states below name each prop by its serial. A save made when the level laid its props out another way
    // (other kinds, or another number of each) would put them on the wrong props, so they are left out.
    if (d.propLayout != PROP_LAYOUT) {
        SDL_Log("Save has prop layout %d, the game has %d: prop contents, locks and seats start fresh", d.propLayout, PROP_LAYOUT);
        return true;
    }
    // Which locks were open. The props were placed again from the seed, locked, so each is found by its serial.
    for (Entity e : world.view<Prop, Lock>()) {
        for (const SaveLock& l : d.locks) {
            if (l.prop == world.get<Prop>(e)->serial) world.get<Lock>(e)->locked = l.locked;
        }
    }
    // What the barrels held. The props were placed again from the seed, so each is found by its serial.
    for (Entity e : world.view<Prop, Storage>()) {
        const int serial = world.get<Prop>(e)->serial;
        Storage* st = world.get<Storage>(e);
        for (const SaveStorage& s : d.storage) {
            if (s.prop != serial || s.slot < 0 || s.slot >= STORAGE_SLOTS || s.item <= 0 || s.count <= 0) continue;
            st->slots[(size_t)s.slot] = ItemStack{ s.item, s.count };
        }
    }
    // A forge's output, and its fuel and progress
    for (Entity e : world.view<Prop, Output>()) {
        const int serial = world.get<Prop>(e)->serial;
        Output* out = world.get<Output>(e);
        for (const SaveStorage& s : d.output) {
            if (s.prop != serial || s.slot < 0 || s.slot >= STORAGE_SLOTS || s.item <= 0 || s.count <= 0) continue;
            out->slots[(size_t)s.slot] = ItemStack{ s.item, s.count };
        }
        for (const SaveFuel& f : d.fuel) {
            if (f.prop != serial) continue;
            if (Fuel* fuel = world.get<Fuel>(e)) {
                fuel->hours = std::max(0.0, f.hours);
                fuel->item = std::max(0, f.item);
            }
            if (PropWork* pw = world.get<PropWork>(e)) pw->progress = std::clamp(f.progress, 0.0, 1.0);
        }
    }
    // Units on beds, chairs and in cages, put back on them. Two saved on one prop: the first keeps it.
    for (size_t i = 0; i < d.units.size() && i < restoredUnits.size(); ++i) {
        if (d.units[i].occupying < 0) continue;
        for (Entity p : world.view<Prop, Transform>()) {
            if (world.get<Prop>(p)->serial != d.units[i].occupying) continue;
            if (propInfo(world.get<Prop>(p)->kind).use != PropUse::Occupy || propOccupant(world, p) != NULL_ENTITY) break;
            const Transform* pt = world.get<Transform>(p);
            *world.get<Transform>(restoredUnits[i]) = *pt;
            world.add(restoredUnits[i], Occupying{ p });
            break;
        }
    }
    return true;
}

// Captures what a save needs from the running world, including the camera
SaveData captureWorld(World& w, const GameState& gs, const Camera& cam, unsigned seed,
                      long long ts, const std::string& name, const MapChoice& map) {
    SaveData d;
    d.name = name;
    d.timestamp = ts;
    d.seed = seed;
    d.mapSource = map.source;
    d.mapStem = map.stem;
    d.gs = gs;
    d.propLayout = PROP_LAYOUT;
    d.reputation = playerReputation();
    d.playerCamp = levelPlayerCamp;

    d.cam.present = true;
    d.cam.fx = cam.focusX;
    d.cam.fy = cam.focusY;
    d.cam.fz = cam.focusZ;
    d.cam.yaw = cam.yaw;
    d.cam.pitch = cam.pitch;
    d.cam.distance = cam.distance;

    const std::vector<Entity> saved = w.view<Unit, Transform>();
    auto indexOf = [&](Entity e) {
        const auto it = std::find(saved.begin(), saved.end(), e);
        return it == saved.end() ? -1 : (int)(it - saved.begin());
    };
    for (Entity u : saved) {
        Transform* t = w.get<Transform>(u);
        Name* n = w.get<Name>(u);
        UnitSprite* us = w.get<UnitSprite>(u);
        SaveUnit su;
        if (const Unconscious* un = w.get<Unconscious>(u)) {
            su.unconscious = true;
            su.dying = un->dying;
            su.conscious = un->conscious;
            su.corpse = w.has<Corpse>(u);
        }
        su.playingDead = w.has<PlayingDead>(u);
        if (const Carried* cd = w.get<Carried>(u)) su.carriedBy = indexOf(cd->by);
        su.x = t->x;
        su.y = t->y;
        su.z = t->z;
        su.hostile = w.has<Hostile>(u);
        su.neutral = w.has<Neutral>(u);
        if (const Faction* f = w.get<Faction>(u)) {
            su.hasFaction = true;
            su.faction = f->id;
        }
        if (const Unconscious* un = w.get<Unconscious>(u)) su.koFaction = un->koFaction;
        if (const Behaviors* bh = w.get<Behaviors>(u)) {
            su.passive = bh->passive;
            su.block = bh->block;
            su.sneak = bh->sneak;
        }
        su.sprite = us ? us->id : 1;
        su.name = n ? n->text : lang("UILABEL_UNNAMED_UNIT");
        if (const Body* bd = w.get<Body>(u)) {
            su.hasBody = true;
            su.body = idx(bd->type);
        }
        if (const Mob* m = w.get<Mob>(u)) su.mob = idx(m->kind);
        if (const SpawnHome* h = w.get<SpawnHome>(u)) {
            su.hasHome = true;
            su.homeX = h->x;
            su.homeZ = h->z;
            su.homeReturning = h->returning;
        }
        if (const Occupying* oc = w.get<Occupying>(u)) {
            if (const Prop* p = w.get<Prop>(oc->prop)) su.occupying = p->serial;
        }
        if (const Needs* nd = w.get<Needs>(u)) {
            su.hasNeeds = true;
            su.needs = *nd;
        }
        if (const Stats* st = w.get<Stats>(u)) {
            su.hasStats = true;
            su.stats = *st;
        }
        if (const Behaviors* b = w.get<Behaviors>(u)) {
            su.hasBehaviors = true;
            su.behaviorsWhole = true;
            for (const BehaviorRule& r : b->rules) {
                SaveBehavior sb;
                sb.rule = r;
                if (r.kind == BehaviorKind::AutoHaul) {
                    const Transform* pt = w.get<Transform>(r.prop);
                    if (!pt) continue;   // its prop is gone
                    sb.x = pt->x;
                    sb.z = pt->z;
                }
                su.behaviors.push_back(sb);
            }
        }
        if (const Inventory* inv = w.get<Inventory>(u)) {
            for (int i = 0; i < INVENTORY_SLOTS; ++i) {
                const ItemStack& st = inv->slots[i];
                if (st.item > 0 && st.count > 0) su.stacks.push_back(SaveStack{ i, st });
            }
        }
        if (const Equipment* worn = w.get<Equipment>(u)) {
            for (int g = 0; g < GEAR_SLOTS; ++g) {
                if (worn->items[g] > 0) su.gear.push_back(SaveGear{ g, worn->items[g] });
            }
        }
        d.units.push_back(su);
    }

    d.hasItems = true;
    for (Entity e : w.view<GroundItem, Transform>()) {
        const GroundItem* g = w.get<GroundItem>(e);
        const Transform* t = w.get<Transform>(e);
        d.ground.push_back(SaveGround{ t->x, t->y, t->z, g->item, g->count });
    }
    for (Entity e : w.view<Prop, Lock>()) d.locks.push_back(SaveLock{ w.get<Prop>(e)->serial, w.get<Lock>(e)->locked });
    for (Entity e : w.view<Prop, Storage>()) {
        const int serial = w.get<Prop>(e)->serial;
        const Storage* st = w.get<Storage>(e);
        for (int i = 0; i < STORAGE_SLOTS; ++i) {
            const ItemStack& s = st->slots[(size_t)i];
            if (s.item > 0 && s.count > 0) d.storage.push_back(SaveStorage{ serial, i, s.item, s.count });
        }
    }
    for (Entity e : w.view<Prop, Output>()) {
        const int serial = w.get<Prop>(e)->serial;
        const Output* out = w.get<Output>(e);
        for (int i = 0; i < STORAGE_SLOTS; ++i) {
            const ItemStack& s = out->slots[(size_t)i];
            if (s.item > 0 && s.count > 0) d.output.push_back(SaveStorage{ serial, i, s.item, s.count });
        }
        const Fuel* fuel = w.get<Fuel>(e);
        const PropWork* pw = w.get<PropWork>(e);
        d.fuel.push_back(SaveFuel{ serial, fuel ? fuel->hours : 0.0, pw ? pw->progress : 0.0, fuel ? fuel->item : 0 });
    }
    return d;
}

// Puts the camera back where it was saved. Saves from before the camera was stored keep
// the default focus set by focusCameraOnFirstUnit.
void applySaveCamera(const SaveCamera& c, Camera& cam) {
    if (!c.present) return;
    cam.focusX = c.fx;
    cam.focusY = c.fy;
    cam.focusZ = c.fz;
    cam.yaw = c.yaw;
    cam.pitch = c.pitch;
    cam.distance = c.distance;
    cam.clampState();
}
