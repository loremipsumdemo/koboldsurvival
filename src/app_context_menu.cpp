#include "building.h"
#include "app.h"
#include "commerce.h"
#include "systems_internal.h"
#include "faction_rules.h"

using namespace play;

// The prop a unit is in or on, or the Wheelbarrow it pushes
static ObjectId heldProp(World& world, ObjectId u) {
    const ObjectId in = occupiedProp(world, u);
    return in != NO_OBJECT ? in : barrowOf(world, u);
}

// Evacuate: someone else pushes the occupant out, or, on an Airship, those aboard get off, or a Wheelbarrow's pusher
// lets go of it
static bool canEvacuate(World& world, ObjectId prop, ObjectId occupant) {
    if (prop == NO_OBJECT) return false;
    if (isWheelbarrow(world, prop)) {
        for (ObjectId u : selectedUnits(world)) {
            if (barrowOf(world, u) == prop) return true;
        }
        const ObjectId pusher = pusherOf(world, prop);
        return pusher != NO_OBJECT && world.has<Commandable>(pusher);
    }
    if (isAirship(world, prop)) {
        for (ObjectId u : selectedUnits(world)) {
            if (airshipOf(world, u) == prop) return true;
        }
        return occupant != NO_OBJECT && world.has<Commandable>(occupant) && airshipOf(world, occupant) == prop;
    }
    if (occupant == NO_OBJECT) return false;
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
    int storey = 0;
    if (auto g = pickFloor(mx, my, storey)) {
        const float gy = std::max(terrain.heightAt(g->first, g->second), 0.0f) + storeyBaseHeight(storey);
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
        // Props with nothing to do are passed over; blueprints, ladders and doors have their own orders. Finished stairs
        // are walked like floor, so a click on them is a move.
        // The Perimeter pieces, Slopes and Watchtowers are picked with Shift held, for Break (a Gate always: it has a lock)
        const PropInfo& pk = propInfo(kind);
        const bool shiftBreak = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0 && (pk.perimeter || pk.tower || pk.slope);
        if (pk.stairs && !world.has<Blueprint>(p) && !shiftBreak) continue;
        const bool orders = world.has<Blueprint>(p) || pk.ladder || pk.door || shiftBreak || (pk.perimeter && !pk.solid);
        if ((propInfo(kind).use == PropUse::None && !destroyedByHand(kind) && !ownAltar(world, p) && !orders) || (hidden(p) && !world.has<Foundation>(p))) continue;
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

// A finished door of yours
bool App::ownDoor(ObjectId target) {
    const Prop* p = world.get<Prop>(target);
    const Lock* l = world.get<Lock>(target);
    return p && l && propInfo(p->kind).door && !world.has<Blueprint>(target) && l->faction == FACTION_PLAYER;
}

bool App::ownGate(ObjectId target) {
    const Prop* p = world.get<Prop>(target);
    const Lock* l = world.get<Lock>(target);
    return p && l && propInfo(p->kind).perimeter && !propInfo(p->kind).solid && !world.has<Blueprint>(target) && l->faction == FACTION_PLAYER;
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
        // A knocked-out enemy: dealt with as your Faction Rules say
        if (dealableBy(world, FACTION_PLAYER, target)) out.push_back(MenuAction::DealWith);
        if (canBeCarried(world, target)) out.push_back(MenuAction::PickUp);
        out.push_back(MenuAction::Inventory);
        if (canEvacuate(world, occupiedProp(world, target), target)) out.push_back(MenuAction::Evacuate);
        return out;
    }
    if (world.has<Entity>(target)) {
        const bool caged = inPrisonerCage(world, target);
        // Your own units, and your own animals whether military or citizens, are never attacked from here; an animal can't trade
        // (all it holds is its meat and bones)
        const bool ownMob = world.has<Mob>(target) && factionOf(world, target) == FACTION_PLAYER;
        if (world.has<Commandable>(target) || ownMob) {
            if (world.has<Citizen>(target)) out.push_back(MenuAction::Recruit);
            if (!world.has<Mob>(target)) out.push_back(MenuAction::Trade);
            out.insert(out.end(), { MenuAction::Follow, MenuAction::Defend });
            if (canBeCarried(world, target)) out.push_back(MenuAction::PickUp);
        } else if (world.has<Hostile>(target)) {
            if (!caged) out.push_back(MenuAction::Attack);
        } else if (world.has<Mob>(target) && world.has<Faction>(target) && isMobFaction(world.get<Faction>(target)->id)) {
            if (!caged) out.push_back(MenuAction::AttackUnprovoked);
        } else {
            // One of your citizens can be made military, whether or not you have an Altar or any unit selected
            if (world.has<Citizen>(target) && !world.has<Mob>(target) && factionOf(world, target) == FACTION_PLAYER) out.push_back(MenuAction::Recruit);
            if (talkable(target)) out.push_back(MenuAction::Talk);
            if (sells(target)) out.push_back(MenuAction::Shop);
            out.insert(out.end(), { MenuAction::Follow, MenuAction::Defend });
            // Never your own units (your citizens, who take no orders, land here)
            if (!caged && factionOf(world, target) != FACTION_PLAYER) out.push_back(MenuAction::AttackUnprovoked);
        }
        if (canEvacuate(world, heldProp(world, target), target)) out.push_back(MenuAction::Evacuate);
        if (const Lock* lock = world.get<Lock>(occupiedProp(world, target))) out.push_back(lock->locked ? MenuAction::Unlock : MenuAction::Lock);
    } else if (const Prop* p = world.get<Prop>(target)) {
        const PropUse use = propInfo(p->kind).use;
        // Your Altar manages your units, whether or not any are selected
        if (ownAltar(world, target)) {
            out.push_back(MenuAction::ManageUnits);
            out.push_back(MenuAction::BehaviorGroups);
            out.push_back(MenuAction::AppeaseGhosts);
            out.push_back(MenuAction::SetPerimeter);
        }
        if (propInfo(p->kind).claimsLand) {
            const std::vector<ObjectId> sel = selectedUnits(world);
            bool shut = sel.empty() ? lockShutFor(world, target, NO_OBJECT) : true;
            for (ObjectId u : sel) shut = shut && lockShutFor(world, target, u);
            if (shut) return out;
        }
        const bool isGate = propInfo(p->kind).perimeter && !propInfo(p->kind).solid;
        if (const Lock* lock = world.get<Lock>(target); lock && lock->locked && !isGate) {
            const std::vector<ObjectId> sel = selectedUnits(world);
            bool shut = sel.empty() ? lockShutFor(world, target, NO_OBJECT) : true;
            for (ObjectId u : sel) shut = shut && lockShutFor(world, target, u);
            if (shut) return std::vector<MenuAction>{ MenuAction::Unlock };
        }
        bool carrying = false, allOccupying = true;
        const std::vector<ObjectId> picked = selectedUnits(world);
        for (ObjectId u : picked) {
            carrying = carrying || world.has<Carrying>(u);
            allOccupying = allOccupying && world.has<Occupying>(u);
        }
        // Units that all occupy something already have nobody to send in
        allOccupying = allOccupying && !picked.empty();
        if (hasStorage(propInfo(p->kind)) && !world.has<Blueprint>(target)) out.push_back(MenuAction::Inventory);
        // An Advanced Crafting prop or a Research Table that is yours (or no one's): its order, or its research
        if ((propInfo(p->kind).advanced || p->kind == PropKind::ResearchTable) && !world.has<Blueprint>(target)) {
            const Owner* own = world.get<Owner>(target);
            if (!own || own->faction == FACTION_PLAYER) {
                out.push_back(propInfo(p->kind).advanced ? MenuAction::CraftOrder : MenuAction::Research);
            }
        }
        // A general store of yours: which kinds of item it takes
        if (hasStoreFilter(p->kind) && !world.has<Blueprint>(target)) {
            const Owner* own = world.get<Owner>(target);
            const Transform* st = world.get<Transform>(target);
            if ((own && own->faction == FACTION_PLAYER) || (st && claimAt(claimsOf(world), st->x, st->z, FACTION_PLAYER))) {
                out.push_back(MenuAction::StoreFilter);
            }
        }
        if (use == PropUse::Occupy && occupantCount(world, target) < occupyCapacity(world, target) && (carrying || !allOccupying)) {
            out.push_back(carrying && !propInfo(p->kind).wheelbarrow ? MenuAction::Place : MenuAction::Occupy);
        }
        if (propInfo(p->kind).burnsCorpses && corpseCarrier(target) != NO_OBJECT) out.push_back(MenuAction::Place);
        if (canEvacuate(world, target, isAirship(world, target) ? NO_OBJECT : propOccupant(world, target))) {
            out.push_back(MenuAction::Evacuate);
        }
        if (isWorkProp(use)) out.push_back(MenuAction::Use);
        // Auto tasks only work on props inside your claim, so they aren't offered on any outside it
        const Transform* pt = world.get<Transform>(target);
        if (pt && claimAt(claimsOf(world), pt->x, pt->z, FACTION_PLAYER)) {
            if (yieldsItems(p->kind)) out.push_back(MenuAction::AutoHarvest);
            if (world.has<Storage>(target) && !propInfo(p->kind).claimsLand && !propInfo(p->kind).wheelbarrow) {
                out.push_back(MenuAction::AutoHaul);
            }
            if (world.has<Storage>(target) && propInfo(p->kind).claimsLand) out.push_back(MenuAction::AutoHaulBones);
        }
        // A Gate is opened and shut (it stands open); any other lock is locked and unlocked
        if (isGate) {
            if (ownGate(target)) out.push_back(gateShut(world, target) ? MenuAction::OpenGate : MenuAction::CloseGate);
        } else if (const Lock* lock = world.get<Lock>(target)) out.push_back(lock->locked ? MenuAction::Unlock : MenuAction::Lock);
        for (BuildMaterial m : upgradesFor(world, target)) {
            out.push_back(m == BuildMaterial::Stone ? MenuAction::UpgradeStone : m == BuildMaterial::Iron ? MenuAction::UpgradeIron : MenuAction::UpgradeCrystal);
        }
        // Break is added last, and only with Shift held (openContextMenuOn)
        if (breakableKind(p->kind) && !world.has<Blueprint>(target)) out.push_back(MenuAction::Destroy);
        // Buildings: supply and build a blueprint of yours; climb a ladder up or down
        if (const Blueprint* bp = world.get<Blueprint>(target); bp && world.has<Owner>(target) && world.get<Owner>(target)->faction == FACTION_PLAYER) {
            // A ghost is not built yet, and a free blueprint builds itself; either may be cancelled
            if (!bp->ghost && !world.has<FreeBuild>(target)) out.push_back(MenuAction::AutoConstruct);
            out.push_back(MenuAction::CancelBuild);
            if (connectedBlueprints(world, target).size() > 1) out.push_back(MenuAction::CancelAllBuild);
        }
        // A bed in your building: open it to anyone, or keep it for your faction
        if (isBed(p->kind) && bedHouseOwner(world, target) == FACTION_PLAYER) {
            out.push_back(world.has<BedPublic>(target) ? MenuAction::BedPrivate : MenuAction::BedPublic);
        }
        // A Stall of yours: pick what to stock it with, and send the selected units to keep it
        if (propInfo(p->kind).stall && world.has<StallStock>(target) && !world.has<Blueprint>(target) && world.has<Owner>(target) &&
            world.get<Owner>(target)->faction == FACTION_PLAYER) {
            out.push_back(MenuAction::StockStall);
            if (!selectedUnits(world).empty()) out.push_back(MenuAction::ManStall);
        }
        if (propInfo(p->kind).ladder && !world.has<Blueprint>(target)) {
            // Up for units anywhere else (they walk and climb there), down for units on the floor it climbs to
            bool ground = false, up = false;
            const int ladderFloor = storeyOf(world, target);
            for (ObjectId u : selectedUnits(world)) {
                if (storeyOf(world, u) == ladderFloor + 1) up = true;
                else ground = ground || airshipOf(world, u) == NO_OBJECT;
            }
            if (ground && canClimbUp(world, target)) out.push_back(MenuAction::Climb);
            if (up) out.push_back(MenuAction::ClimbDown);
        }
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
        case MenuAction::UpgradeStone: labels.push_back(langf("UILABEL_UPGRADE_TO", { lang("MISC_BUILD_STONE") })); break;
        case MenuAction::UpgradeIron: labels.push_back(langf("UILABEL_UPGRADE_TO", { lang("MISC_BUILD_METAL") })); break;
        case MenuAction::UpgradeCrystal: labels.push_back(langf("UILABEL_UPGRADE_TO", { lang("MISC_BUILD_CRYSTAL") })); break;
        case MenuAction::OpenGate: labels.push_back(lang("UILABEL_OPEN_GATE")); break;
        case MenuAction::CloseGate: labels.push_back(lang("UILABEL_CLOSE_GATE")); break;
        case MenuAction::Evacuate: labels.push_back(lang("UILABEL_EVACUATE")); break;
        case MenuAction::PickUp: labels.push_back(lang("UILABEL_PICK_UP")); break;
        case MenuAction::PutDown: labels.push_back(lang("UILABEL_PUT_DOWN")); break;
        case MenuAction::AutoHarvest: labels.push_back(lang("UILABEL_AUTO_HARVEST")); break;
        case MenuAction::AutoHaul: {
            const Prop* tp = world.get<Prop>(session.contextMenu.target);
            labels.push_back(lang(tp && propInfo(tp->kind).claimsLand ? "UILABEL_AUTO_HAUL_LOGS" : "UILABEL_AUTO_HAUL"));
            break;
        }
        case MenuAction::AutoHaulBones: labels.push_back(lang("UILABEL_AUTO_HAUL_BONES")); break;
        case MenuAction::AppeaseGhosts: labels.push_back(lang("UILABEL_APPEASE_GHOSTS")); break;
        case MenuAction::BehaviorGroups: labels.push_back(lang("UILABEL_BEHAVIOR_GROUPS")); break;
        case MenuAction::SetPerimeter: labels.push_back(lang("UILABEL_SET_PERIMETER")); break;
        case MenuAction::StoreFilter: labels.push_back(lang("UILABEL_STORE_FILTER")); break;
        case MenuAction::CancelBuild: labels.push_back(lang("UILABEL_CANCEL_BUILD")); break;
        case MenuAction::CancelAllBuild: labels.push_back(lang("UILABEL_CANCEL_ALL_BUILD")); break;
        case MenuAction::AutoConstruct: labels.push_back(lang("UILABEL_AUTO_CONSTRUCT")); break;
        case MenuAction::Climb: labels.push_back(lang("UILABEL_CLIMB_UP")); break;
        case MenuAction::ClimbDown: labels.push_back(lang("UILABEL_CLIMB_DOWN")); break;
        case MenuAction::Disband: labels.push_back(lang("UILABEL_DISBAND")); break;
        case MenuAction::Destroy: labels.push_back(lang("UILABEL_BREAK")); break;
        case MenuAction::ManageUnits: labels.push_back(lang("UILABEL_MANAGE_UNITS")); break;
        case MenuAction::Rename: labels.push_back(lang("UILABEL_RENAME")); break;
        case MenuAction::BedPublic: labels.push_back(lang("UILABEL_BED_MAKE_PUBLIC")); break;
        case MenuAction::BedPrivate: labels.push_back(lang("UILABEL_BED_MAKE_PRIVATE")); break;
        case MenuAction::StockStall: labels.push_back(lang("UILABEL_STOCK_STALL")); break;
        case MenuAction::ManStall: labels.push_back(lang("UILABEL_MAN_STALL")); break;
        case MenuAction::DealWith: labels.push_back(lang("UILABEL_DEAL_WITH")); break;
        case MenuAction::GetOut: labels.push_back(lang("UILABEL_EVACUATE")); break;
        case MenuAction::CraftOrder: labels.push_back(lang("UILABEL_CRAFT_ORDER")); break;
        case MenuAction::Research: labels.push_back(lang("UILABEL_RESEARCH")); break;
        case MenuAction::Recruit: labels.push_back(lang("UILABEL_RECRUIT")); break;
        }
    }
    return labels;
}

ItemMenuLayout App::contextMenuLayout() {
    return renderer.itemMenuLayout(session.contextMenu.x, session.contextMenu.y, (int)session.contextMenu.actions.size(), winW, winH, contextLabels());
}

bool App::openContextMenuOn(ObjectId target, float mx, float my, bool squadPanel) {
    if (target == NO_OBJECT) return false;
    std::vector<MenuAction> actions = menuActionsFor(target);
    // Break is the last option, and only offered while Shift is held
    const bool canBreak = std::find(actions.begin(), actions.end(), MenuAction::Destroy) != actions.end();
    actions.erase(std::remove(actions.begin(), actions.end(), MenuAction::Destroy), actions.end());
    const bool disband = squadPanel && world.has<Commandable>(target);
    // From the Squad panel, a unit of yours in or holding something (a bed, a seat, a cage, a Wheelbarrow, an Airship)
    // can be told to get out of it itself
    const bool yours = world.has<Commandable>(target) || world.has<Citizen>(target);
    const bool getOut = squadPanel && yours && heldProp(world, target) != NO_OBJECT;
    if (getOut) {
        actions.erase(std::remove(actions.begin(), actions.end(), MenuAction::Evacuate), actions.end());
        actions.push_back(MenuAction::GetOut);
    }
    if (squadPanel && (world.has<Commandable>(target) || world.has<Citizen>(target))) actions.push_back(MenuAction::Rename);
    if (disband) actions.push_back(MenuAction::Disband);
    if (canBreak && (SDL_GetModState() & SDL_KMOD_SHIFT) != 0) actions.push_back(MenuAction::Destroy);
    if (actions.empty()) return false;
    if (menuActors(target).empty() && actions[0] == MenuAction::ManageUnits) {
        session.contextMenu = ContextMenu{ true, mx, my, target, { MenuAction::ManageUnits, MenuAction::BehaviorGroups, MenuAction::AppeaseGhosts, MenuAction::SetPerimeter } };
        playSfx(Sfx::UiMenuOpen);
        itemMenu.open = false;
        return true;
    }
    if (menuActors(target).empty()) {
        if (disband) {
            std::vector<MenuAction> own;
            if (actions[0] == MenuAction::PutDown) own.push_back(MenuAction::PutDown);
            if (getOut) own.push_back(MenuAction::GetOut);
            own.push_back(MenuAction::Rename);
            own.push_back(MenuAction::Disband);
            session.contextMenu = ContextMenu{ true, mx, my, target, own };
            playSfx(Sfx::UiMenuOpen);
            itemMenu.open = false;
            return true;
        }
        // A blueprint of yours can be cancelled, a store's Allowed Items set, a door of yours locked or unlocked, a bed of
        // yours made Public or Private, and a Stall of yours stocked, with nobody selected
        std::vector<MenuAction> alone;
        for (MenuAction a : actions) {
            if (a == MenuAction::CancelBuild || a == MenuAction::CancelAllBuild || a == MenuAction::StoreFilter || a == MenuAction::BedPublic || a == MenuAction::BedPrivate ||
                a == MenuAction::StockStall || a == MenuAction::Recruit || a == MenuAction::CraftOrder || a == MenuAction::Research || ((a == MenuAction::Lock || a == MenuAction::Unlock) && ownDoor(target)) ||
                ((a == MenuAction::OpenGate || a == MenuAction::CloseGate) && ownGate(target)) ||
                a == MenuAction::UpgradeStone || a == MenuAction::UpgradeIron || a == MenuAction::UpgradeCrystal) {
                alone.push_back(a);
            }
        }
        if (!alone.empty()) {
            session.contextMenu = ContextMenu{ true, mx, my, target, alone };
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
            actionStorey = storeyOf(world, target);
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
    const ObjectId clicked = target;
    session.contextMenu.open = false;
    if (a == MenuAction::CraftOrder || a == MenuAction::Research) {
        openCraftWindow(clicked, a == MenuAction::Research);
        return;
    }
    if (a == MenuAction::Recruit) {
        // A citizen of yours joins the military, in the current squad or the first with room
        if (!world.has<Citizen>(target)) return;
        if (!makeMilitary(world, target, world.resource<SquadBook>().current)) {
            refuse(langf("MISC_MILITARY_FULL", { std::to_string(MILITARY_MAX) }));
            return;
        }
        playSfx(Sfx::UiClick);
        return;
    }
    if (a == MenuAction::GetOut) {
        // The unit itself leaves what it is in or lets go of what it holds
        const ObjectId prop = heldProp(world, clicked);
        if (prop == NO_OBJECT) return;
        if (isWheelbarrow(world, prop)) {
            stopPushing(world, clicked);
        } else if (isAirship(world, prop)) {
            if (!startLeaving(world, clicked)) {
                refuse(lang("MISC_CANT_EVACUATE_AIRSHIP"));
                return;
            }
        } else {
            if (noteLockedIn({ clicked })) return;
            systems_detail::stepOffProp(world, terrain, clicked, prop);
            world.remove<Occupying>(clicked);
        }
        playSfx(Sfx::UiClick);
        return;
    }
    if ((a == MenuAction::Unlock || a == MenuAction::Lock || a == MenuAction::Evacuate) && world.has<Entity>(target)) {
        target = a == MenuAction::Evacuate ? heldProp(world, target) : occupiedProp(world, target);
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
    if (a == MenuAction::Rename) {
        startRename(target);
        return;
    }
    if (a == MenuAction::AppeaseGhosts) {
        openGhosts(target);
        return;
    }
    if (a == MenuAction::BehaviorGroups) {
        openBehaviorGroups(target);
        return;
    }
    if (a == MenuAction::SetPerimeter) {
        openPerimeter(target);
        return;
    }
    if (a == MenuAction::StoreFilter) {
        openStoreFilter(target);
        return;
    }
    // Your own door locks and unlocks at once, from anywhere
    if ((a == MenuAction::Lock || a == MenuAction::Unlock) && ownDoor(target)) {
        if (Lock* l = world.get<Lock>(target)) {
            l->locked = a == MenuAction::Lock;
            showNotice(lang(l->locked ? "MISC_DOOR_LOCKED" : "MISC_DOOR_UNLOCKED"));
            playSfx(Sfx::UiToggle);
        }
        return;
    }
    // Your own piece is rebuilt in a better material
    if (a == MenuAction::UpgradeStone || a == MenuAction::UpgradeIron || a == MenuAction::UpgradeCrystal) {
        const BuildMaterial to = a == MenuAction::UpgradeStone ? BuildMaterial::Stone : a == MenuAction::UpgradeIron ? BuildMaterial::Iron : BuildMaterial::Crystal;
        upgradePiece(world, target, to);
        showNotice(langf("MISC_UPGRADE_BEGUN", { lang(to == BuildMaterial::Stone ? "MISC_BUILD_STONE" : to == BuildMaterial::Iron ? "MISC_BUILD_METAL" : "MISC_BUILD_CRYSTAL") }));
        playSfx(Sfx::UiToggle);
        return;
    }
    // Your own Gate opens and shuts at once, from anywhere
    if ((a == MenuAction::OpenGate || a == MenuAction::CloseGate) && ownGate(target)) {
        setGateShut(world, target, a == MenuAction::CloseGate);
        showNotice(lang(a == MenuAction::CloseGate ? "MISC_GATE_CLOSED" : "MISC_GATE_OPENED"));
        playSfx(Sfx::UiToggle);
        return;
    }
    if (a == MenuAction::BedPublic || a == MenuAction::BedPrivate) {
        if (a == MenuAction::BedPublic) world.add(target, BedPublic{});
        else world.remove<BedPublic>(target);
        showNotice(lang(a == MenuAction::BedPublic ? "MISC_BED_NOW_PUBLIC" : "MISC_BED_NOW_PRIVATE"));
        playSfx(Sfx::UiToggle);
        return;
    }
    if (a == MenuAction::StockStall) {
        openStall(target);
        return;
    }
    if (a == MenuAction::Evacuate && isWheelbarrow(world, target)) {
        // Its pusher lets go: the selected one, or else the unit of yours pushing it
        ObjectId pusher = NO_OBJECT;
        for (ObjectId u : selectedUnits(world)) {
            if (barrowOf(world, u) == target) pusher = u;
        }
        if (pusher == NO_OBJECT) {
            const ObjectId other = pusherOf(world, target);
            if (other != NO_OBJECT && world.has<Commandable>(other)) pusher = other;
        }
        if (pusher != NO_OBJECT) {
            stopPushing(world, pusher);
            playSfx(Sfx::UiClick);
        }
        return;
    }
    if (a == MenuAction::Evacuate && isAirship(world, target)) {
        // Those aboard get off: the selected ones, or else the unit of yours that was clicked
        std::vector<ObjectId> leaving;
        for (ObjectId u : selectedUnits(world)) {
            if (airshipOf(world, u) == target) leaving.push_back(u);
        }
        if (leaving.empty() && clicked != target && world.has<Commandable>(clicked) && airshipOf(world, clicked) == target) {
            leaving.push_back(clicked);
        }
        int off = 0, stuck = 0;
        for (ObjectId u : leaving) {
            if (startLeaving(world, u)) ++off;
            else ++stuck;
        }
        if (stuck > 0) refuse(lang("MISC_CANT_EVACUATE_AIRSHIP"));
        else if (off > 0) playSfx(Sfx::UiClick);
        return;
    }
    if (a == MenuAction::PutDown) {
        const ObjectId carrier = putDownCarrier(target);
        if (carrier != NO_OBJECT) putDown(world, carrier);
        return;
    }
    if (a == MenuAction::CancelAllBuild) {
        // Every blueprint joined to this one (the building's, the floors above it, and what hangs over its edges): asked first, with how many
        if (!world.has<Blueprint>(target)) return;
        session.cancelList = connectedBlueprints(world, target);
        session.askCancel = !session.cancelList.empty();
        playSfx(Sfx::UiMenuOpen);
        return;
    }
    if (a == MenuAction::CancelBuild) {
        // The blueprint goes, and whatever was brought to it and not yet built with drops where it stood; a foundation
        // takes the blueprints laid on it with it
        const Prop* tp = world.get<Prop>(target);
        const std::string name = tp ? lang(propInfo(tp->kind).nameKey) : std::string();
        if (!world.has<Blueprint>(target) || !destroyBuilding(world, target)) return;
        showNotice(langf("MISC_BUILD_CANCELLED", { name }));
        playSfx(Sfx::UiClick);
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
    case MenuAction::DealWith: {
        std::string why;
        if (!issueDealOrder(world, terrain, actors, target, why)) refuse(why);
        break;
    }
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
        if (occupier != NO_OBJECT && isAirship(world, target)) {
            // An Airship takes as many as it has room for
            std::vector<ObjectId> boarding;
            const int room = occupyCapacity(world, target) - occupantCount(world, target);
            for (ObjectId u : actors) {
                if ((int)boarding.size() >= room) break;
                if (!world.has<Carrying>(u) && !world.has<OccupyBan>(u) && airshipOf(world, u) != target) boarding.push_back(u);
            }
            issuePropOrder(world, terrain, boarding, target, PropAction::Occupy);
        } else if (occupier != NO_OBJECT) {
            issuePropOrder(world, terrain, { occupier }, target, PropAction::Occupy);
        }
        else if (banned != NO_OBJECT) logBlocked(langf("MISC_CANT_OCCUPY_YET", { nameOf(world, banned) }), banned);
        break;
    }
    case MenuAction::PutDown:
    case MenuAction::Disband:
    case MenuAction::ManageUnits:
    case MenuAction::Rename:
    case MenuAction::AppeaseGhosts:
    case MenuAction::BehaviorGroups:
    case MenuAction::StoreFilter:
        break;
    case MenuAction::CancelBuild:
        break;
    case MenuAction::ManStall: {
        BehaviorRule add;
        add.kind = BehaviorKind::ManStall;
        add.prop = target;
        add.propKind = idx(world.get<Prop>(target)->kind);
        int given = 0;
        for (ObjectId u : actors) {
            Behaviors* b = world.get<Behaviors>(u);
            if (!b || world.has<Mob>(u) || !addBehavior(b->rules, add)) continue;
            b->wait = 0.0f;
            ++given;
        }
        if (given == 0) {
            refuse(langf("MISC_BEHAVIOR_LIST_FULL", { std::to_string(BEHAVIOR_LIMIT) }));
            return;
        }
        showNotice(langf("MISC_MAN_STALL_SET", { std::to_string(given) }));
        playSfx(Sfx::UiOrder);
        return;
    }
    case MenuAction::BedPublic:
    case MenuAction::BedPrivate:
    case MenuAction::StockStall:
        break;
    case MenuAction::AutoConstruct: {
        BehaviorRule add;
        add.kind = BehaviorKind::AutoConstruct;
        int given = 0;
        for (ObjectId u : actors) {
            Behaviors* b = world.get<Behaviors>(u);
            if (!b || world.has<Mob>(u) || !addBehavior(b->rules, add)) continue;
            b->wait = 0.0f;
            ++given;
        }
        if (given == 0) {
            refuse(langf("MISC_BEHAVIOR_LIST_FULL", { std::to_string(BEHAVIOR_LIMIT) }));
            return;
        }
        showNotice(langf("MISC_AUTO_CONSTRUCT_SET", { std::to_string(given) }));
        playSfx(Sfx::UiOrder);
        return;
    }
    case MenuAction::Climb:
    case MenuAction::ClimbDown: {
        std::vector<ObjectId> climbers;
        for (ObjectId u : actors) {
            const bool above = storeyOf(world, u) == storeyOf(world, target) + 1;
            if ((a == MenuAction::ClimbDown) == above && airshipOf(world, u) == NO_OBJECT) climbers.push_back(u);
        }
        issuePropOrder(world, terrain, climbers, target, PropAction::Climb);
        if (!climbers.empty()) playSfx(Sfx::UiOrder);
        break;
    }
    case MenuAction::AutoHarvest:
    case MenuAction::AutoHaulBones:
    case MenuAction::AutoHaul: {
        const PropKind kind = world.get<Prop>(target)->kind;
        const Transform* pt = world.get<Transform>(target);
        if (!pt || !claimAt(claimsOf(world), pt->x, pt->z, FACTION_PLAYER)) {
            refuse(lang("MISC_AUTO_OUTSIDE_CLAIM"));
            return;
        }
        BehaviorRule add;
        add.kind = a == MenuAction::AutoHarvest ? BehaviorKind::AutoHarvest
                 : a == MenuAction::AutoHaulBones ? BehaviorKind::AutoHaulBones : BehaviorKind::AutoHaul;
        add.propKind = idx(kind);
        if (a != MenuAction::AutoHarvest) add.prop = target;
        int given = 0;
        ObjectId haulTo = NO_OBJECT;
        for (ObjectId u : actors) {
            Behaviors* b = world.get<Behaviors>(u);
            if (!b || !addBehavior(b->rules, add)) continue;
            b->wait = 0.0f;
            ++given;
            // Harvesting brings its haul along: to the stockpile (or store) that takes what it yields
            if (a == MenuAction::AutoHarvest) {
                if (const ObjectId s = addHaulForHarvest(world, u, idx(kind)); s != NO_OBJECT) haulTo = s;
            }
        }
        if (given == 0) {
            refuse(langf("MISC_BEHAVIOR_LIST_FULL", { std::to_string(BEHAVIOR_LIMIT) }));
            return;
        }
        if (haulTo != NO_OBJECT) {
            showNotice(langf("MISC_AUTO_HARVEST_HAUL_SET", { std::to_string(given), lang(propInfo(kind).nameKey),
                                                             lang(propInfo(world.get<Prop>(haulTo)->kind).nameKey) }));
            playSfx(Sfx::UiOrder);
            return;
        }
        showNotice(langf(a == MenuAction::AutoHarvest ? "MISC_AUTO_HARVEST_SET" : a == MenuAction::AutoHaulBones ? "MISC_AUTO_HAUL_BONES_SET" : "MISC_AUTO_HAUL_SET",
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
        actionStorey = storeyOf(world, target);
        actionAlpha = 1.0f;
    }
    playSfx(Sfx::UiClick);
}
