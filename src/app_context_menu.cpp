#include "app.h"

using namespace play;

Entity App::menuTargetAt(float mx, float my) {
    float ex = 0.0f, ey = 0.0f, ez = 0.0f;
    cam.eye(ex, ey, ez);
    float groundD2 = 1e30f;
    if (auto g = pickGround(cam, terrain, mx, my, winW, winH)) {
        const float gy = std::max(terrain.heightAt(g->first, g->second), 0.0f);
        groundD2 = (g->first - ex) * (g->first - ex) + (gy - ey) * (gy - ey) + (g->second - ez) * (g->second - ez);
    }
    auto hidden = [&](Entity e) {
        const Transform* tr = world.get<Transform>(e);
        if (!tr) return false;
        const float d = std::sqrt((tr->x - ex) * (tr->x - ex) + (tr->y - ey) * (tr->y - ey) + (tr->z - ez) * (tr->z - ez));
        return d > std::sqrt(groundD2) + HIDDEN_BEHIND_GROUND;
    };
    std::vector<Entity> under = renderer.unitsAtPixel(world, cam, mx, my, winW, winH);
    under.erase(std::remove_if(under.begin(), under.end(), hidden), under.end());
    for (Entity u : under) {
        if (lockedIn(world, u)) return occupiedProp(world, u);
    }
    for (Entity u : under) {
        if (!world.has<Selected>(u)) return u;
    }
    if (!under.empty()) return under[0];
    Entity prop = NULL_ENTITY;
    float best = 0.0f;
    for (Entity p : renderer.propsAtPixel(world, cam, mx, my, winW, winH)) {
        const PropKind kind = world.get<Prop>(p)->kind;
        if ((propInfo(kind).use == PropUse::None && !destroyedByHand(kind)) || hidden(p)) continue;
        const Transform* pt = world.get<Transform>(p);
        const float d2 = (pt->x - ex) * (pt->x - ex) + (pt->y - ey) * (pt->y - ey) + (pt->z - ez) * (pt->z - ez);
        if (prop == NULL_ENTITY || d2 < best) {
            prop = p;
            best = d2;
        }
    }
    return prop;
}

std::vector<Entity> App::menuActors(Entity target) {
    std::vector<Entity> units;
    for (Entity u : selectedUnits(world)) {
        if (outCold(world, u)) continue;
        if (world.has<Carried>(u) && !world.has<PlayingDead>(u)) continue;
        if (world.has<Mob>(u) && world.has<Prop>(target) && propInfo(world.get<Prop>(target)->kind).use != PropUse::Occupy) continue;
        if (u != target && (!lockedIn(world, u) || occupiedProp(world, u) == target)) units.push_back(u);
    }
    std::sort(units.begin(), units.end(), [&](Entity a, Entity b) {
        return flatGap(world, a, target) < flatGap(world, b, target);
    });
    return units;
}

bool App::talkable(Entity u) {
    return world.has<Unit>(u) && world.has<Dialogue>(u) && !world.has<Mob>(u) && !world.has<Commandable>(u) && !world.has<Hostile>(u) &&
           !outCold(world, u) && !world.has<Carried>(u);
}

bool App::sells(Entity u) {
    const Dialogue* dg = world.get<Dialogue>(u);
    if (!dg || !talkable(u)) return false;
    for (const DialogueOption& o : dg->options) {
        if (o.opensTrade) return true;
    }
    return false;
}

Entity App::putDownCarrier(Entity target) {
    if (world.has<Carrying>(target) && world.has<Selected>(target) && !cannotAct(world, target)) return target;
    if (const Carried* c = world.get<Carried>(target)) {
        if (world.has<Selected>(c->by) && !cannotAct(world, c->by)) return c->by;
    }
    return NULL_ENTITY;
}

Entity App::corpseCarrier(Entity prop) {
    for (Entity u : menuActors(prop)) {
        const Carrying* cr = world.get<Carrying>(u);
        if (cr && world.has<Corpse>(cr->unit)) return u;
    }
    return NULL_ENTITY;
}

std::vector<MenuAction> App::menuActionsFor(Entity target) {
    std::vector<MenuAction> out;
    if (world.has<Unit>(target) && putDownCarrier(target) != NULL_ENTITY) out.push_back(MenuAction::PutDown);
    if (outCold(world, target)) {
        if (canBeCarried(world, target)) out.push_back(MenuAction::PickUp);
        out.push_back(MenuAction::Inventory);
        return out;
    }
    if (world.has<Unit>(target)) {
        if (world.has<Commandable>(target)) {
            if (!world.has<Mob>(target)) out.push_back(MenuAction::Trade);
            out.insert(out.end(), { MenuAction::Follow, MenuAction::Defend });
            if (canBeCarried(world, target)) out.push_back(MenuAction::PickUp);
        } else if (world.has<Hostile>(target)) {
            out.push_back(MenuAction::Attack);
        } else if (world.has<Mob>(target) && world.has<Faction>(target) && isMobFaction(world.get<Faction>(target)->id)) {
            out.push_back(MenuAction::AttackUnprovoked);
        } else {
            if (talkable(target)) out.push_back(MenuAction::Talk);
            if (sells(target)) out.push_back(MenuAction::Shop);
            out.insert(out.end(), { MenuAction::Follow, MenuAction::Defend, MenuAction::AttackUnprovoked });
        }
    } else if (const Prop* p = world.get<Prop>(target)) {
        const PropUse use = propInfo(p->kind).use;
        if (p->kind == PropKind::Altar) {
            const std::vector<Entity> sel = selectedUnits(world);
            bool shut = sel.empty() ? lockShutFor(world, target, NULL_ENTITY) : true;
            for (Entity u : sel) shut = shut && lockShutFor(world, target, u);
            if (shut) return out;
        }
        if (const Lock* lock = world.get<Lock>(target); lock && lock->locked) {
            const std::vector<Entity> sel = selectedUnits(world);
            bool shut = sel.empty() ? lockShutFor(world, target, NULL_ENTITY) : true;
            for (Entity u : sel) shut = shut && lockShutFor(world, target, u);
            if (shut) return std::vector<MenuAction>{ MenuAction::Unlock };
        }
        bool carrying = false;
        for (Entity u : selectedUnits(world)) carrying = carrying || world.has<Carrying>(u);
        if (hasStorage(propInfo(p->kind))) out.push_back(MenuAction::Inventory);
        if (use == PropUse::Occupy) out.push_back(carrying ? MenuAction::Place : MenuAction::Occupy);
        if (p->kind == PropKind::Campfire && corpseCarrier(target) != NULL_ENTITY) out.push_back(MenuAction::Place);
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
            labels.push_back(lang(tp && tp->kind == PropKind::Debris ? "UILABEL_CLEAR" : "UILABEL_USE"));
            break;
        }
        case MenuAction::Occupy: labels.push_back(lang("UILABEL_OCCUPY")); break;
        case MenuAction::Place: labels.push_back(lang("UILABEL_PLACE")); break;
        case MenuAction::Unlock: labels.push_back(lang("UILABEL_UNLOCK")); break;
        case MenuAction::Lock: labels.push_back(lang("UILABEL_LOCK")); break;
        case MenuAction::PickUp: labels.push_back(lang("UILABEL_PICK_UP")); break;
        case MenuAction::PutDown: labels.push_back(lang("UILABEL_PUT_DOWN")); break;
        case MenuAction::AutoHarvest: labels.push_back(lang("UILABEL_AUTO_HARVEST")); break;
        case MenuAction::AutoHaul: labels.push_back(lang("UILABEL_AUTO_HAUL")); break;
        case MenuAction::Disband: labels.push_back(lang("UILABEL_DISBAND")); break;
        case MenuAction::Destroy: labels.push_back(lang("UILABEL_BUILD_DESTROY")); break;
        }
    }
    return labels;
}

ItemMenuLayout App::contextMenuLayout() {
    return renderer.itemMenuLayout(session.contextMenu.x, session.contextMenu.y, (int)session.contextMenu.actions.size(), winW, winH);
}

bool App::openContextMenuOn(Entity target, float mx, float my, bool squadPanel) {
    if (target == NULL_ENTITY) return false;
    std::vector<MenuAction> actions = menuActionsFor(target);
    const bool disband = squadPanel && world.has<Commandable>(target);
    if (disband) actions.push_back(MenuAction::Disband);
    if (actions.empty()) return false;
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

void App::talkOrWalkUp(Entity target, const std::vector<Entity>& actors, bool shop) {
    for (Entity u : actors) {
        if (!world.has<Mob>(u) && flatGap(world, u, target) <= TALK_RANGE) {
            tryStartTalk(target, u, actors, shop);
            return;
        }
    }
    issueTalkOrder(world, actors, target, false, shop);
}

void App::pickMenuAction(MenuAction a) {
    const Entity target = session.contextMenu.target;
    session.contextMenu.open = false;
    if (a == MenuAction::Disband) {
        if (world.has<Commandable>(target)) {
            session.disbandTarget = target;
            session.disbandHeldAt = 0;
            itemMenu.open = false;
            playSfx(Sfx::UiMenuOpen);
        }
        return;
    }
    if (a == MenuAction::PutDown) {
        const Entity carrier = putDownCarrier(target);
        if (carrier != NULL_ENTITY) putDown(world, carrier);
        return;
    }
    const std::vector<Entity> actors = menuActors(target);
    if (actors.empty()) {
        refuse(lang("MISC_SELECT_UNIT_FIRST"));
        return;
    }
    if (!world.has<Unit>(target) && !world.has<Prop>(target)) return;
    switch (a) {
    case MenuAction::Talk:
        talkOrWalkUp(target, actors, false);
        break;
    case MenuAction::Trade: {
        const auto trader = std::find_if(actors.begin(), actors.end(), [&](Entity u) { return !world.has<Mob>(u); });
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
            std::vector<Entity> looters;
            for (Entity u : actors) {
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
        Entity carrier = world.get<Prop>(target)->kind == PropKind::Campfire ? corpseCarrier(target) : NULL_ENTITY;
        if (world.get<Prop>(target)->kind != PropKind::Campfire) {
            for (Entity u : actors) {
                if (world.has<Carrying>(u)) {
                    carrier = u;
                    break;
                }
            }
        }
        if (carrier != NULL_ENTITY) issuePropOrder(world, terrain, { carrier }, target, PropAction::Place);
        break;
    }
    case MenuAction::Occupy: {
        Entity occupier = NULL_ENTITY;
        for (Entity u : actors) {
            if (!world.has<Carrying>(u)) {
                occupier = u;
                break;
            }
        }
        if (occupier != NULL_ENTITY) issuePropOrder(world, terrain, { occupier }, target, PropAction::Occupy);
        break;
    }
    case MenuAction::PutDown:
    case MenuAction::Disband:
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
        for (Entity u : actors) {
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
        Entity keyholder = NULL_ENTITY;
        for (Entity u : actors) {
            if (!world.has<Mob>(u) && !lockShutFor(world, target, u)) {
                keyholder = u;
                break;
            }
        }
        if (keyholder != NULL_ENTITY) {
            issuePropOrder(world, terrain, { keyholder }, target, PropAction::Unlock);
            break;
        }
        Entity best = NULL_ENTITY;
        int bestLevel = 0;
        for (Entity u : actors) {
            const int lv = levelOf(statsOf(world, u), StatId::Lockpicking);
            if (lv > bestLevel) {
                best = u;
                bestLevel = lv;
            }
        }
        if (best == NULL_ENTITY || bestLevel < lock->level) {
            refuse(langf("MISC_LOCKPICK_NEEDS", { std::to_string(lock->level) }));
            return;
        }
        issuePropOrder(world, terrain, { best }, target, PropAction::Unlock);
        break;
    }
    case MenuAction::Lock: {
        const auto locker = std::find_if(actors.begin(), actors.end(), [&](Entity u) { return !world.has<Mob>(u); });
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
        std::vector<Entity> units = actors;
        int others = 0;
        for (Entity u : world.view<Working>()) {
            if (world.get<Working>(u)->prop == target && std::find(units.begin(), units.end(), u) == units.end()) ++others;
        }
        for (Entity u : world.view<PropOrder>()) {
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
