// The right-click menu: what it targets, what it offers, and carrying out what was picked.
#include "app.h"

using namespace play;

Entity App::menuTargetAt(float mx, float my) {
    // What the pointer is on is in front of the ground it is over: a unit or prop farther from the eye than
    // that ground (behind a hill, past a ridge) is hidden by it, so it is passed over
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
    // A unit locked in a cage stands for the cage: the menu offers to unlock it
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
        if (propInfo(world.get<Prop>(p)->kind).use == PropUse::None || hidden(p)) continue;
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
        if (world.has<Unconscious>(u)) continue;   // out cold: it does nothing
        if (world.has<Carried>(u) && !world.has<PlayingDead>(u)) continue;   // carried: it does nothing
        // A mob works no props: of a prop, it can only lie or sit on one (a bed to rest in)
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
           !world.has<Unconscious>(u) && !world.has<Carried>(u);
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

std::vector<MenuAction> App::menuActionsFor(Entity target) {
    std::vector<MenuAction> out;
    // A selected unit carrying someone, or the unit it carries: Put down comes first
    if (world.has<Unit>(target) && putDownCarrier(target) != NULL_ENTITY) out.push_back(MenuAction::PutDown);
    if (world.has<Unconscious>(target)) {
        // Out cold or dead: carried off (unless someone has it already) and looted
        if (canBeCarried(world, target)) out.push_back(MenuAction::PickUp);
        out.push_back(MenuAction::Inventory);
        return out;
    }
    if (world.has<Unit>(target)) {
        if (world.has<Commandable>(target)) {
            // One of your squad: trade, follow, defend, and pick it up (unless it carries or is carried). A
            // mob is not traded with: its inventory is shut while it is awake.
            if (!world.has<Mob>(target)) out.push_back(MenuAction::Trade);
            out.insert(out.end(), { MenuAction::Follow, MenuAction::Defend });
            if (canBeCarried(world, target)) out.push_back(MenuAction::PickUp);
        } else if (world.has<Hostile>(target)) {
            out.push_back(MenuAction::Attack);   // a hostile unit is only fought, never followed
        } else if (world.has<Mob>(target) && world.has<Faction>(target) && isMobFaction(world.get<Faction>(target)->id)) {
            out.push_back(MenuAction::AttackUnprovoked);   // a wild mob is only hunted, never followed or defended
        } else {
            if (talkable(target)) out.push_back(MenuAction::Talk);
            if (sells(target)) out.push_back(MenuAction::Shop);
            out.insert(out.end(), { MenuAction::Follow, MenuAction::Defend, MenuAction::AttackUnprovoked });
        }
    } else if (const Prop* p = world.get<Prop>(target)) {
        const PropUse use = propInfo(p->kind).use;
        // A prop locked to all the selected units offers only Unlock, until it is open. One of their own
        // faction's, or inside their faction's claim, is open to them.
        if (const Lock* lock = world.get<Lock>(target); lock && lock->locked) {
            const std::vector<Entity> sel = selectedUnits(world);
            bool shut = sel.empty() ? lockShutFor(world, target, NULL_ENTITY) : true;
            for (Entity u : sel) shut = shut && lockShutFor(world, target, u);
            if (shut) return std::vector<MenuAction>{ MenuAction::Unlock };
        }
        // An occupiable prop (a bed, a chair, a cage): Occupy it, or, while a selected unit carries someone,
        // Place them on it in its stead, so a carrier is never sent to occupy one
        bool carrying = false;
        for (Entity u : selectedUnits(world)) carrying = carrying || world.has<Carrying>(u);
        if (hasStorage(propInfo(p->kind))) out.push_back(MenuAction::Inventory);
        if (use == PropUse::Occupy) out.push_back(carrying ? MenuAction::Place : MenuAction::Occupy);
        if (isWorkProp(use)) out.push_back(MenuAction::Use);
        // The automated behaviors: Auto-Harvest of a resource prop's kind, Auto-Haul to a prop that keeps items
        if (yieldsItems(p->kind)) out.push_back(MenuAction::AutoHarvest);   // not the Debris, which gives nothing
        if (world.has<Storage>(target)) out.push_back(MenuAction::AutoHaul);
        // A lockable prop: Lock it when it is open (it stays open until then), or, when the lock is the
        // selected units' own, Unlock it with the key
        if (const Lock* lock = world.get<Lock>(target)) out.push_back(lock->locked ? MenuAction::Unlock : MenuAction::Lock);
    }
    return out;
}

std::vector<std::string> App::contextLabels() {
    std::vector<std::string> labels;
    for (MenuAction a : contextMenu.actions) {
        switch (a) {
        case MenuAction::Talk: labels.push_back(lang("UILABEL_TALK")); break;
        case MenuAction::Shop: labels.push_back(lang("UILABEL_SHOP")); break;
        case MenuAction::Trade: labels.push_back(lang("UILABEL_TRADE")); break;
        case MenuAction::Follow: labels.push_back(lang("UILABEL_FOLLOW")); break;
        case MenuAction::Defend: labels.push_back(lang("UILABEL_DEFEND")); break;
        case MenuAction::Attack: labels.push_back(lang("UILABEL_ATTACK")); break;
        case MenuAction::AttackUnprovoked: labels.push_back(lang("UILABEL_ATTACK")); break;   // it turns them hostile
        case MenuAction::Inventory: labels.push_back(lang("UILABEL_INVENTORY")); break;
        case MenuAction::Use: {
            // Debris is cleared, not used
            const Prop* tp = world.get<Prop>(contextMenu.target);
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
        }
    }
    return labels;
}

ItemMenuLayout App::contextMenuLayout() {
    return renderer.itemMenuLayout(contextMenu.x, contextMenu.y, (int)contextMenu.actions.size(), winW, winH);
}

bool App::openContextMenuOn(Entity target, float mx, float my, bool squadPanel) {
    if (target == NULL_ENTITY) return false;
    std::vector<MenuAction> actions = menuActionsFor(target);
    const bool disband = squadPanel && world.has<Commandable>(target);
    if (disband) actions.push_back(MenuAction::Disband);
    if (actions.empty()) return false;
    if (menuActors(target).empty()) {
        // Put down and Disband need no other unit
        if (disband) {
            std::vector<MenuAction> own;
            if (actions[0] == MenuAction::PutDown) own.push_back(MenuAction::PutDown);
            own.push_back(MenuAction::Disband);
            contextMenu = ContextMenu{ true, mx, my, target, own };
            playSfx(Sfx::UiMenuOpen);
            itemMenu.open = false;
            return true;
        }
        // Only Put down needs no other unit: the carrier does it
        if (actions[0] == MenuAction::PutDown) {
            contextMenu = ContextMenu{ true, mx, my, target, { MenuAction::PutDown } };
            playSfx(Sfx::UiMenuOpen);
            itemMenu.open = false;
            return true;
        }
        // Your lone selected unit clicked on itself: a move order, as on the ground under it
        if (world.has<Selected>(target) && world.has<Commandable>(target)) return false;
        if (!noteLockedIn(selectedUnits(world))) refuse(lang("MISC_SELECT_UNIT_FIRST"));
        return true;
    }
    // A hostile unit, or a wild mob, offers only Attack, so the right click is the attack order itself, with no
    // menu. A wild mob that was not hostile turns on the attackers, as any unit attacked unprovoked does.
    if (actions.size() == 1 && (actions[0] == MenuAction::Attack || actions[0] == MenuAction::AttackUnprovoked)) {
        if (actions[0] == MenuAction::AttackUnprovoked) makeHostile(world, target);
        issueAttackOrder(world, menuActors(target), target);
        if (const Transform* tt = world.get<Transform>(target)) {
            actionX = tt->x;
            actionZ = tt->z;
            actionAlpha = 1.0f;
        }
        contextMenu.open = false;
        itemMenu.open = false;
        playSfx(Sfx::UiOrder);
        return true;
    }
    contextMenu = ContextMenu{ true, mx, my, target, actions };
    playSfx(Sfx::UiMenuOpen);
    itemMenu.open = false;
    return true;
}

bool App::openContextMenu(float mx, float my) { return openContextMenuOn(menuTargetAt(mx, my), mx, my); }

void App::pickMenuAction(MenuAction a) {
    const Entity target = contextMenu.target;
    contextMenu.open = false;
    if (a == MenuAction::Disband) {
        // The popout asks first: Disband has to be held down to confirm
        if (world.has<Commandable>(target)) {
            disbandTarget = target;
            disbandHeldAt = 0;
            itemMenu.open = false;
            playSfx(Sfx::UiMenuOpen);
        }
        return;
    }
    if (a == MenuAction::PutDown) {
        // Put down at once, where the carrier stands
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
    case MenuAction::Talk: {
        // A selected unit already in talk range talks now; otherwise they walk up, and the first there talks.
        // A mob never talks.
        Entity talker = NULL_ENTITY;
        for (Entity u : actors) {
            if (world.has<Mob>(u)) continue;
            if (flatGap(world, u, target) <= TALK_RANGE) {
                talker = u;
                break;
            }
        }
        if (talker != NULL_ENTITY) tryStartTalk(target, talker, actors);
        else issueTalkOrder(world, actors, target);
        break;
    }
    case MenuAction::Trade: {
        // The nearest selected unit that is not a mob trades: now if it is close enough, or once it has walked up
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
        // The unit attacked fights back at once; others of its faction that see the attack join in
        makeHostile(world, target);
        issueAttackOrder(world, actors, target);
        break;
    case MenuAction::Attack:
        issueAttackOrder(world, actors, target);
        break;
    case MenuAction::Inventory:
        if (world.has<Unconscious>(target)) {
            // Looting: the nearest unit that can goes (never a mob, which takes nothing), and the panel opens
            // when it gets there (or now, if it is)
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
                lootUnit = target;
                lootBy = looters[0];
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
        // The nearest selected unit that carries someone places them on the prop
        Entity carrier = NULL_ENTITY;
        for (Entity u : actors) {
            if (world.has<Carrying>(u)) {
                carrier = u;
                break;
            }
        }
        if (carrier != NULL_ENTITY) issuePropOrder(world, terrain, { carrier }, target, PropAction::Place);
        break;
    }
    case MenuAction::Occupy: {
        // The nearest selected unit that carries nobody occupies it
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
        break;   // done above
    case MenuAction::AutoHarvest:
    case MenuAction::AutoHaul: {
        // Every selected unit gets the behavior, at the bottom of its list (or switched on, if it has it), and
        // looks for its first step at once. Both work only on props in your faction's claim, so a prop outside
        // one is refused.
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
    case MenuAction::Shop: {
        // As Talk, then Show me your wares at once
        Entity talker = NULL_ENTITY;
        for (Entity u : actors) {
            if (world.has<Mob>(u)) continue;
            if (flatGap(world, u, target) <= TALK_RANGE) {
                talker = u;
                break;
            }
        }
        if (talker != NULL_ENTITY) tryStartTalk(target, talker, actors, true);
        else issueTalkOrder(world, actors, target, false, true);
        break;
    }
    case MenuAction::Unlock: {
        // The selected unit best at Lockpicking goes (the nearest, if several are as good), if it is good enough
        const Lock* lock = world.get<Lock>(target);
        if (!lock || !lock->locked) return;
        // A lock that is the selected units' own: the nearest that is not a mob opens it with the key
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
        for (Entity u : actors) {   // nearest first
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
        // The nearest selected unit that is not a mob goes and locks it
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
        // A resource prop takes the nearest selected units, as many as it has room for. Units already on it
        // keep their places; the rest share what is left.
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
