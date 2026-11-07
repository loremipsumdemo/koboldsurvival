#include "app.h"

using namespace play;

static bool canEvacuate(World& world, ObjectId prop, ObjectId occupant) {
    if (prop == NO_OBJECT || occupant == NO_OBJECT) return false;
    for (ObjectId u : selectedUnits(world)) {
        if (u != occupant && !world.has<Mob>(u) && !cannotAct(world, u) && !lockShutFor(world, prop, u)) return true;
    }
    return false;
}

static bool ownAltar(World& world, ObjectId prop) {
    for (const Claim& c : claimsOf(world)) {
        if (c.altar == prop) return c.owner == FACTION_PLAYER;
    }
    return false;
}

ObjectId App::menuTargetAt(float mx, float my) {
    float ex = 0.0f, ey = 0.0f, ez = 0.0f;
    cam.eye(ex, ey, ez);
    float groundD2 = 1e30f;
    if (auto g = pickGround(cam, terrain, mx, my, winW, winH)) {
        const float gy = std::max(terrain.heightAt(g->first, g->second), 0.0f);
        groundD2 = (g->first - ex) * (g->first - ex) + (gy - ey) * (gy - ey) + (g->second - ez) * (g->second - ez);
    }
    auto hidden = [&](ObjectId e) {
        const Transform* tr = world.get<Transform>(e);
        if (!tr) return false;
        const float d = std::sqrt((tr->x - ex) * (tr->x - ex) + (tr->y - ey) * (tr->y - ey) + (tr->z - ez) * (tr->z - ez));
        return d > std::sqrt(groundD2) + HIDDEN_BEHIND_GROUND;
    };
    std::vector<ObjectId> under = renderer.entitiesAtPixel(world, cam, mx, my, winW, winH);
    under.erase(std::remove_if(under.begin(), under.end(), hidden), under.end());
    for (ObjectId u : under) {
        if (!world.has<Selected>(u)) return u;
    }
    if (!under.empty()) {
        return lockedIn(world, under[0]) ? occupiedProp(world, under[0]) : under[0];
    }
    ObjectId prop = NO_OBJECT;
    float best = 0.0f;
    for (ObjectId p : renderer.propsAtPixel(world, cam, mx, my, winW, winH)) {
        const PropKind kind = world.get<Prop>(p)->kind;
        if ((propInfo(kind).use == PropUse::None && !destroyedByHand(kind) && !ownAltar(world, p)) || hidden(p)) continue;
        const Transform* pt = world.get<Transform>(p);
        const float d2 = (pt->x - ex) * (pt->x - ex) + (pt->y - ey) * (pt->y - ey) + (pt->z - ez) * (pt->z - ez);
        if (prop == NO_OBJECT || d2 < best) {
            prop = p;
            best = d2;
        }
    }
    return prop;
}

std::vector<ObjectId> App::menuActors(ObjectId target) {
    std::vector<ObjectId> units;
    for (ObjectId u : selectedUnits(world)) {
        if (outCold(world, u)) continue;
        if (world.has<Carried>(u) && !world.has<PlayingDead>(u)) continue;
        if (world.has<Mob>(u) && world.has<Prop>(target) && propInfo(world.get<Prop>(target)->kind).use != PropUse::Occupy) continue;
        if (u != target && (!lockedIn(world, u) || occupiedProp(world, u) == target)) units.push_back(u);
    }
    std::sort(units.begin(), units.end(), [&](ObjectId a, ObjectId b) {
        return flatGap(world, a, target) < flatGap(world, b, target);
    });
    return units;
}

bool App::talkable(ObjectId u) {
    return world.has<Entity>(u) && world.has<Dialogue>(u) && !world.has<Mob>(u) && !world.has<Commandable>(u) && !world.has<Hostile>(u) &&
           !outCold(world, u) && !world.has<Carried>(u);
}

bool App::sells(ObjectId u) {
    const Dialogue* dg = world.get<Dialogue>(u);
    if (!dg || !talkable(u)) return false;
    for (const DialogueOption& o : dg->options) {
        if (o.opensTrade) return true;
    }
    return false;
}

ObjectId App::putDownCarrier(ObjectId target) {
    if (world.has<Carrying>(target) && world.has<Selected>(target) && !cannotAct(world, target)) return target;
    if (const Carried* c = world.get<Carried>(target)) {
        if (world.has<Selected>(c->by) && !cannotAct(world, c->by)) return c->by;
    }
    return NO_OBJECT;
}

ObjectId App::corpseCarrier(ObjectId prop) {
    for (ObjectId u : menuActors(prop)) {
        const Carrying* cr = world.get<Carrying>(u);
        if (cr && world.has<Corpse>(cr->unit)) return u;
    }
    return NO_OBJECT;
}

std::vector<MenuAction> App::menuActionsFor(ObjectId target) {
    std::vector<MenuAction> out;
    if (world.has<Entity>(target) && putDownCarrier(target) != NO_OBJECT) out.push_back(MenuAction::PutDown);
    if (world.has<Entity>(target) && lockedIn(world, target)) {
        if (talkable(target)) out.push_back(MenuAction::Talk);
        if (sells(target)) out.push_back(MenuAction::Shop);
        if (canEvacuate(world, occupiedProp(world, target), target)) out.push_back(MenuAction::Evacuate);
        out.push_back(MenuAction::Unlock);
        return out;
    }
    if (outCold(world, target)) {
        if (canBeCarried(world, target)) out.push_back(MenuAction::PickUp);
        out.push_back(MenuAction::Inventory);
        if (canEvacuate(world, occupiedProp(world, target), target)) out.push_back(MenuAction::Evacuate);
        return out;
    }
    if (world.has<Entity>(target)) {
        const bool caged = inPrisonerCage(world, target);
        if (world.has<Commandable>(target)) {
            if (!world.has<Mob>(target)) out.push_back(MenuAction::Trade);
            out.insert(out.end(), { MenuAction::Follow, MenuAction::Defend });
            if (canBeCarried(world, target)) out.push_back(MenuAction::PickUp);
        } else if (world.has<Hostile>(target)) {
            if (!caged) out.push_back(MenuAction::Attack);
        } else if (world.has<Mob>(target) && world.has<Faction>(target) && isMobFaction(world.get<Faction>(target)->id)) {
            if (!caged) out.push_back(MenuAction::AttackUnprovoked);
        } else {
            if (talkable(target)) out.push_back(MenuAction::Talk);
            if (sells(target)) out.push_back(MenuAction::Shop);
            out.insert(out.end(), { MenuAction::Follow, MenuAction::Defend });
            if (!caged) out.push_back(MenuAction::AttackUnprovoked);
        }
        if (canEvacuate(world, occupiedProp(world, target), target)) out.push_back(MenuAction::Evacuate);
        if (const Lock* lock = world.get<Lock>(occupiedProp(world, target))) out.push_back(lock->locked ? MenuAction::Unlock : MenuAction::Lock);
    } else if (const Prop* p = world.get<Prop>(target)) {
        const PropUse use = propInfo(p->kind).use;
        // Your Altar manages your units, whether or not any are selected
        if (ownAltar(world, target)) out.push_back(MenuAction::ManageUnits);
        if (propInfo(p->kind).claimsLand) {
            const std::vector<ObjectId> sel = selectedUnits(world);
            bool shut = sel.empty() ? lockShutFor(world, target, NO_OBJECT) : true;
            for (ObjectId u : sel) shut = shut && lockShutFor(world, target, u);
            if (shut) return out;
        }
        if (const Lock* lock = world.get<Lock>(target); lock && lock->locked) {
            const std::vector<ObjectId> sel = selectedUnits(world);
            bool shut = sel.empty() ? lockShutFor(world, target, NO_OBJECT) : true;
            for (ObjectId u : sel) shut = shut && lockShutFor(world, target, u);
            if (shut) return std::vector<MenuAction>{ MenuAction::Unlock };
        }
        bool carrying = false;
        for (ObjectId u : selectedUnits(world)) carrying = carrying || world.has<Carrying>(u);
        if (hasStorage(propInfo(p->kind))) out.push_back(MenuAction::Inventory);
        if (use == PropUse::Occupy && propOccupant(world, target) == NO_OBJECT) out.push_back(carrying ? MenuAction::Place : MenuAction::Occupy);
        if (propInfo(p->kind).burnsCorpses && corpseCarrier(target) != NO_OBJECT) out.push_back(MenuAction::Place);
        if (canEvacuate(world, target, propOccupant(world, target))) out.push_back(MenuAction::Evacuate);
        if (isWorkProp(use)) out.push_back(MenuAction::Use);
        if (yieldsItems(p->kind)) out.push_back(MenuAction::AutoHarvest);
        if (world.has<Storage>(target)) out.push_back(MenuAction::AutoHaul);
        if (const Lock* lock = world.get<Lock>(target)) out.push_back(lock->locked ? MenuAction::Unlock : MenuAction::Lock);
        if (destroyedByHand(p->kind)) out.push_back(MenuAction::Destroy);
    }
    return out;
}

std::vector<std::string> App::contextLabels() {
    std::vector<std::string> labels;
    for (MenuAction a : session.contextMenu.actions) {
        switch (a) {
        case MenuAction::Talk: labels.push_back(lang("UILABEL_TALK")); break;
        case MenuAction::Shop: labels.push_back(lang("UILABEL_SHOP")); break;
        case MenuAction::Trade: labels.push_back(lang("UILABEL_TRADE")); break;
        case MenuAction::Follow: labels.push_back(lang("UILABEL_FOLLOW")); break;
        case MenuAction::Defend: labels.push_back(lang("UILABEL_DEFEND")); break;
        case MenuAction::Attack: labels.push_back(lang("UILABEL_ATTACK")); break;
        case MenuAction::AttackUnprovoked: labels.push_back(lang("UILABEL_ATTACK")); break;
        case MenuAction::Inventory: labels.push_back(lang("UILABEL_INVENTORY")); break;
        case MenuAction::Use: {
            const Prop* tp = world.get<Prop>(session.contextMenu.target);
            labels.push_back(lang(tp && propInfo(tp->kind).debris ? "UILABEL_CLEAR" : "UILABEL_USE"));
            break;
        }
        case MenuAction::Occupy: labels.push_back(lang("UILABEL_OCCUPY")); break;
        case MenuAction::Place: labels.push_back(lang("UILABEL_PLACE")); break;
        case MenuAction::Unlock: labels.push_back(lang("UILABEL_UNLOCK")); break;
        case MenuAction::Lock: labels.push_back(lang("UILABEL_LOCK")); break;
        case MenuAction::Evacuate: labels.push_back(lang("UILABEL_EVACUATE")); break;
        case MenuAction::PickUp: labels.push_back(lang("UILABEL_PICK_UP")); break;
        case MenuAction::PutDown: labels.push_back(lang("UILABEL_PUT_DOWN")); break;
        case MenuAction::AutoHarvest: labels.push_back(lang("UILABEL_AUTO_HARVEST")); break;
        case MenuAction::AutoHaul: labels.push_back(lang("UILABEL_AUTO_HAUL")); break;
        case MenuAction::Disband: labels.push_back(lang("UILABEL_DISBAND")); break;
        case MenuAction::Destroy: labels.push_back(lang("UILABEL_BUILD_DESTROY")); break;
        case MenuAction::ManageUnits: labels.push_back(lang("UILABEL_MANAGE_UNITS")); break;
        }
    }
    return labels;
}

ItemMenuLayout App::contextMenuLayout() {
    return renderer.itemMenuLayout(session.contextMenu.x, session.contextMenu.y, (int)session.contextMenu.actions.size(), winW, winH);
}

bool App::openContextMenuOn(ObjectId target, float mx, float my, bool squadPanel) {
    if (target == NO_OBJECT) return false;
    std::vector<MenuAction> actions = menuActionsFor(target);
    const bool disband = squadPanel && world.has<Commandable>(target);
    if (disband) actions.push_back(MenuAction::Disband);
    if (actions.empty()) return false;
    if (menuActors(target).empty() && actions[0] == MenuAction::ManageUnits) {
        session.contextMenu = ContextMenu{ true, mx, my, target, { MenuAction::ManageUnits } };
        playSfx(Sfx::UiMenuOpen);
        itemMenu.open = false;
        return true;
    }
    if (menuActors(target).empty()) {
        if (disband) {
            std::vector<MenuAction> own;
            if (actions[0] == MenuAction::PutDown) own.push_back(MenuAction::PutDown);
            own.push_back(MenuAction::Disband);
            session.contextMenu = ContextMenu{ true, mx, my, target, own };
            playSfx(Sfx::UiMenuOpen);
            itemMenu.open = false;
            return true;
        }
        if (actions[0] == MenuAction::PutDown) {
            session.contextMenu = ContextMenu{ true, mx, my, target, { MenuAction::PutDown } };
            playSfx(Sfx::UiMenuOpen);
            itemMenu.open = false;
            return true;
        }
        if (world.has<Selected>(target) && world.has<Commandable>(target)) return false;
        if (!noteLockedIn(selectedUnits(world))) refuse(lang("MISC_SELECT_UNIT_FIRST"));
        return true;
    }
    if (actions.size() == 1 && (actions[0] == MenuAction::Attack || actions[0] == MenuAction::AttackUnprovoked)) {
        if (actions[0] == MenuAction::AttackUnprovoked) makeHostile(world, target);
        issueAttackOrder(world, menuActors(target), target);
        if (const Transform* tt = world.get<Transform>(target)) {
            actionX = tt->x;
            actionZ = tt->z;
            actionAlpha = 1.0f;
        }
        session.contextMenu.open = false;
        itemMenu.open = false;
        playSfx(Sfx::UiOrder);
        return true;
    }
    session.contextMenu = ContextMenu{ true, mx, my, target, actions };
    playSfx(Sfx::UiMenuOpen);
    itemMenu.open = false;
    return true;
}

bool App::openContextMenu(float mx, float my) { return openContextMenuOn(menuTargetAt(mx, my), mx, my); }

void App::talkOrWalkUp(ObjectId target, const std::vector<ObjectId>& actors, bool shop) {
    for (ObjectId u : actors) {
        if (!world.has<Mob>(u) && flatGap(world, u, target) <= TALK_RANGE) {
            tryStartTalk(target, u, actors, shop);
            return;
        }
    }
    issueTalkOrder(world, actors, target, false, shop);
}

void App::pickMenuAction(MenuAction a) {
    ObjectId target = session.contextMenu.target;
    session.contextMenu.open = false;
    if ((a == MenuAction::Unlock || a == MenuAction::Lock || a == MenuAction::Evacuate) && world.has<Entity>(target)) {
        target = occupiedProp(world, target);
        if (target == NO_OBJECT) return;
    }
    if (a == MenuAction::Disband) {
        if (world.has<Commandable>(target)) {
            session.disbandTarget = target;
            session.disbandHeldAt = 0;
            itemMenu.open = false;
            playSfx(Sfx::UiMenuOpen);
        }
        return;
    }
    if (a == MenuAction::ManageUnits) {
        openAltar();
        return;
    }
    if (a == MenuAction::PutDown) {
        const ObjectId carrier = putDownCarrier(target);
        if (carrier != NO_OBJECT) putDown(world, carrier);
        return;
    }
    const std::vector<ObjectId> actors = menuActors(target);
    if (actors.empty()) {
        refuse(lang("MISC_SELECT_UNIT_FIRST"));
        return;
    }
    if (!world.has<Entity>(target) && !world.has<Prop>(target)) return;
    switch (a) {
    case MenuAction::Talk:
        talkOrWalkUp(target, actors, false);
        break;
    case MenuAction::Trade: {
        const auto trader = std::find_if(actors.begin(), actors.end(), [&](ObjectId u) { return !world.has<Mob>(u); });
        if (trader == actors.end()) {
            refuse(lang("MISC_MOB_INVENTORY_LOCKED"));
            break;
        }
        if (flatGap(world, *trader, target) <= TALK_RANGE) startFreeTrade(target, *trader);
        else issueTalkOrder(world, { *trader }, target, true);
        break;
    }
    case MenuAction::Follow:
        issueFollowOrder(world, actors, target);
        break;
    case MenuAction::Defend:
        issueDefendOrder(world, actors, target);
        break;
    case MenuAction::AttackUnprovoked:
        makeHostile(world, target);
        issueAttackOrder(world, actors, target);
        break;
    case MenuAction::Attack:
        issueAttackOrder(world, actors, target);
        break;
    case MenuAction::Inventory:
        if (outCold(world, target)) {
            std::vector<ObjectId> looters;
            for (ObjectId u : actors) {
                if (!world.has<Mob>(u)) looters.push_back(u);
            }
            if (looters.empty()) {
                refuse(lang("MISC_MOB_CANT_HOLD"));
                break;
            }
            if (flatGap(world, looters[0], target) <= CARRY_REACH && !cannotAct(world, looters[0])) {
                closeStorage();
                session.lootUnit = target;
                session.lootBy = looters[0];
                playSfx(Sfx::UiMenuOpen);
            } else if (!issueLootOrder(world, terrain, looters, target)) {
                refuse(lang("MISC_SELECT_UNIT_FIRST"));
            }
            break;
        }
        issuePropOrder(world, terrain, { actors[0] }, target, PropAction::Inventory);
        break;
    case MenuAction::PickUp:
        if (!issueCarryOrder(world, terrain, actors, target)) refuse(lang("MISC_ALREADY_CARRYING"));
        break;
    case MenuAction::Place: {
        ObjectId carrier = propInfo(world.get<Prop>(target)->kind).burnsCorpses ? corpseCarrier(target) : NO_OBJECT;
        if (!propInfo(world.get<Prop>(target)->kind).burnsCorpses) {
            for (ObjectId u : actors) {
                if (world.has<Carrying>(u)) {
                    carrier = u;
                    break;
                }
            }
        }
        if (carrier != NO_OBJECT) issuePropOrder(world, terrain, { carrier }, target, PropAction::Place);
        break;
    }
    case MenuAction::Occupy: {
        ObjectId occupier = NO_OBJECT, banned = NO_OBJECT;
        for (ObjectId u : actors) {
            if (world.has<Carrying>(u)) continue;
            if (world.has<OccupyBan>(u)) {
                banned = u;
                continue;
            }
            occupier = u;
            break;
        }
        if (occupier != NO_OBJECT) issuePropOrder(world, terrain, { occupier }, target, PropAction::Occupy);
        else if (banned != NO_OBJECT) refuse(langf("MISC_CANT_OCCUPY_YET", { nameOf(world, banned) }));
        break;
    }
    case MenuAction::PutDown:
    case MenuAction::Disband:
    case MenuAction::ManageUnits:
        break;
    case MenuAction::AutoHarvest:
    case MenuAction::AutoHaul: {
        const PropKind kind = world.get<Prop>(target)->kind;
        const Transform* pt = world.get<Transform>(target);
        if (!pt || !claimAt(claimsOf(world), pt->x, pt->z, FACTION_PLAYER)) {
            refuse(lang("MISC_AUTO_OUTSIDE_CLAIM"));
            return;
        }
        BehaviorRule add;
        add.kind = a == MenuAction::AutoHarvest ? BehaviorKind::AutoHarvest : BehaviorKind::AutoHaul;
        add.propKind = idx(kind);
        if (a == MenuAction::AutoHaul) add.prop = target;
        int given = 0;
        for (ObjectId u : actors) {
            Behaviors* b = world.get<Behaviors>(u);
            if (!b || !addBehavior(b->rules, add)) continue;
            b->wait = 0.0f;
            ++given;
        }
        if (given == 0) {
            refuse(langf("MISC_BEHAVIOR_LIST_FULL", { std::to_string(BEHAVIOR_LIMIT) }));
            return;
        }
        showNotice(langf(a == MenuAction::AutoHarvest ? "MISC_AUTO_HARVEST_SET" : "MISC_AUTO_HAUL_SET",
                         { std::to_string(given), lang(propInfo(kind).nameKey) }));
        playSfx(Sfx::UiOrder);
        return;
    }
    case MenuAction::Destroy:
        issuePropOrder(world, terrain, { actors[0] }, target, PropAction::Destroy);
        break;
    case MenuAction::Shop:
        talkOrWalkUp(target, actors, true);
        break;
    case MenuAction::Unlock: {
        const Lock* lock = world.get<Lock>(target);
        if (!lock || !lock->locked) return;
        ObjectId keyholder = NO_OBJECT;
        for (ObjectId u : actors) {
            if (!world.has<Mob>(u) && !lockShutFor(world, target, u)) {
                keyholder = u;
                break;
            }
        }
        if (keyholder != NO_OBJECT) {
            issuePropOrder(world, terrain, { keyholder }, target, PropAction::Unlock);
            break;
        }
        ObjectId best = NO_OBJECT;
        int bestLevel = 0;
        for (ObjectId u : actors) {
            const int lv = levelOf(statsOf(world, u), StatId::Lockpicking);
            if (lv > bestLevel) {
                best = u;
                bestLevel = lv;
            }
        }
        if (best == NO_OBJECT || bestLevel < lock->level) {
            refuse(langf("MISC_LOCKPICK_NEEDS", { std::to_string(lock->level) }));
            return;
        }
        issuePropOrder(world, terrain, { best }, target, PropAction::Unlock);
        break;
    }
    case MenuAction::Evacuate: {
        const ObjectId inside = propOccupant(world, target);
        const auto pusher = std::find_if(actors.begin(), actors.end(), [&](ObjectId u) {
            return u != inside && !world.has<Mob>(u) && !lockShutFor(world, target, u);
        });
        if (pusher == actors.end()) {
            refuse(lang("MISC_SELECT_UNIT_FIRST"));
            return;
        }
        issuePropOrder(world, terrain, { *pusher }, target, PropAction::Evacuate);
        break;
    }
    case MenuAction::Lock: {
        const auto locker = std::find_if(actors.begin(), actors.end(), [&](ObjectId u) { return !world.has<Mob>(u); });
        if (locker == actors.end()) {
            refuse(lang("MISC_SELECT_UNIT_FIRST"));
            return;
        }
        issuePropOrder(world, terrain, { *locker }, target, PropAction::Lock);
        break;
    }
    case MenuAction::Use: {
        const PropInfo& info = propInfo(world.get<Prop>(target)->kind);
        if (info.use != PropUse::Resource) {
            issuePropOrder(world, terrain, { actors[0] }, target, PropAction::Use);
            break;
        }
        std::vector<ObjectId> units = actors;
        int others = 0;
        for (ObjectId u : world.view<Working>()) {
            if (world.get<Working>(u)->prop == target && std::find(units.begin(), units.end(), u) == units.end()) ++others;
        }
        for (ObjectId u : world.view<PropOrder>()) {
            if (world.get<PropOrder>(u)->prop == target && std::find(units.begin(), units.end(), u) == units.end()) ++others;
        }
        const int room = std::max(0, info.workers - others);
        if (room == 0) {
            refuse(langf("MISC_PROP_WORKERS_FULL", { std::to_string(info.workers) }));
            return;
        }
        if ((int)units.size() > room) {
            units.resize((size_t)room);
            refuse(langf("MISC_PROP_WORKERS_FULL", { std::to_string(info.workers) }));
        }
        issuePropOrder(world, terrain, units, target, PropAction::Use);
        break;
    }
    }
    if (const Transform* tt = world.get<Transform>(target)) {
        actionX = tt->x;
        actionZ = tt->z;
        actionAlpha = 1.0f;
    }
    playSfx(Sfx::UiClick);
}
