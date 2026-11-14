#include "gates.h"
#include "recipes.h"
#include "systems_internal.h"

using namespace systems_detail;

bool unitIdle(World& w, ObjectId u) {
    if (w.has<Corpse>(u) || outCold(w, u) || w.has<PlayingDead>(u) || w.has<Carried>(u)) return false;
    if (const Behaviors* b = w.get<Behaviors>(u); b && (b->busy || b->seeking)) return false;
    if (w.has<Working>(u) || w.has<PropOrder>(u) || w.has<Occupying>(u) || w.has<Carrying>(u) || w.has<CarryOrder>(u) ||
        w.has<LootOrder>(u) || w.has<DefendOrder>(u) || w.has<FollowOrder>(u) ||
        w.has<AttackOrder>(u) || w.has<TalkOrder>(u) || w.has<PickupOrder>(u) || w.has<PickupChain>(u)) {
        return false;
    }
    if (const Combat* c = w.get<Combat>(u); c && c->target != NO_OBJECT) return false;
    if (const SoloMove* so = w.get<SoloMove>(u); so && so->next < so->waypoints.size()) return false;
    if (const ActiveSquadMember* sm = w.get<ActiveSquadMember>(u)) {
        if (const ActiveSquad* sq = w.get<ActiveSquad>(sm->squad); sq && !sq->arrived) return false;
    }
    return true;
}

std::string taskNameOf(World& w, ObjectId u) {
    if (w.has<Corpse>(u)) return lang("UILABEL_TASK_DEAD");
    if (outCold(w, u)) return lang("UILABEL_TASK_UNCONSCIOUS");
    if (w.has<PlayingDead>(u)) return lang("UILABEL_TASK_PLAYING_DEAD");
    if (w.has<Carried>(u)) return lang("UILABEL_TASK_CARRIED");
    if (const Behaviors* b = w.get<Behaviors>(u)) {
        if (b->seeking) return langf("UILABEL_TASK_SHARE_WALK", { nameOf(w, b->seek) });
        if (b->busy && (b->step.kind == BehaviorKind::Socialize || b->step.kind == BehaviorKind::Play || b->step.kind == BehaviorKind::Dance)) {
            return behaviorName(b->step);
        }
        const PropOrder* po = w.get<PropOrder>(u);
        const Working* wk = w.get<Working>(u);
        if (b->busy && po && po->automatic) return langf("UILABEL_TASK_AUTO_GOING", { behaviorName(b->step) });
        if (b->busy && wk && !wk->unlocking && wk->prop == b->stepProp) return behaviorName(b->step);
    }
    ObjectId prop = NO_OBJECT;
    if (const Working* g = w.get<Working>(u); g && g->unlocking) return statName(StatId::Lockpicking);
    if (const Working* g = w.get<Working>(u); g && w.has<Prop>(g->prop) && propInfo(w.get<Prop>(g->prop)->kind).debris) {
        return lang("UILABEL_TASK_CLEARING");
    }
    if (const Working* g = w.get<Working>(u)) prop = g->prop;
    else if (const PropOrder* po = w.get<PropOrder>(u); po && useOf(w, po->prop) == PropUse::Resource) prop = po->prop;
    if (const Prop* p = prop != NO_OBJECT ? w.get<Prop>(prop) : nullptr) return statName(propInfo(p->kind).stat);
    if (const Occupying* oc = w.get<Occupying>(u)) {
        if (lockedIn(w, u)) return lang("UILABEL_TASK_LOCKED_IN");
        const Prop* p = w.get<Prop>(oc->prop);
        return lang(p && isSeat(p->kind) ? "UILABEL_TASK_SITTING" : "UILABEL_TASK_RESTING");
    }
    if (w.has<Carrying>(u) || w.has<CarryOrder>(u)) return lang("UILABEL_TASK_CARRYING");
    if (const PropOrder* po = w.get<PropOrder>(u); po && po->action == PropAction::Store) return lang("UILABEL_TASK_STORING");
    if (w.has<DefendOrder>(u)) return lang("UILABEL_TASK_DEFENDING");
    if (w.has<FollowOrder>(u)) return lang("UILABEL_TASK_FOLLOWING");
    if (const GateWork* gw = w.get<GateWork>(u)) return lang(gw->shut ? "UILABEL_TASK_SHUTTING_GATE" : "UILABEL_TASK_OPENING_GATE");
    if (w.has<AskingEntry>(u)) return lang("UILABEL_TASK_ASKING_ENTRY");
    if (w.has<Fortified>(u)) {
        if (w.has<GateKeeper>(u)) return lang("UILABEL_TASK_GUARDING_GATE");
        if (w.has<GuardPostDuty>(u)) return lang("UILABEL_TASK_GUARDING_POST");
        if (w.has<Patrolling>(u)) return lang("UILABEL_TASK_PATROLLING");
    }
    const SoloMove* so = w.get<SoloMove>(u);
    if (so && so->next < so->waypoints.size()) return lang("UILABEL_TASK_MOVING");
    if (const ActiveSquadMember* sm = w.get<ActiveSquadMember>(u)) {
        if (const ActiveSquad* sq = w.get<ActiveSquad>(sm->squad); sq && !sq->arrived) return lang("UILABEL_TASK_MOVING");
    }
    return lang("UILABEL_TASK_IDLE");
}

std::vector<EmoteId> emotesOf(World& w, ObjectId u) {
    if (w.has<Corpse>(u)) return w.has<Mob>(u) ? std::vector<EmoteId>{} : std::vector<EmoteId>{ EmoteId::Dead };
    if (outCold(w, u)) return { EmoteId::Unconscious };
    if (w.has<PlayingDead>(u)) return { EmoteId::PlayingDead };
    if (w.has<TaskFailedEmote>(u)) return { EmoteId::TaskFailed };   // for the second after it failed or gave up a task
    if (const Patrolling* pat = w.get<Patrolling>(u); pat && pat->moving) return { EmoteId::Patrolling };   // a guard marching the perimeter
    if (w.has<GateKeeper>(u) && w.has<Fortified>(u)) return { EmoteId::Gatekeeping };   // keeping a gate
    if (const Behaviors* clearing = w.get<Behaviors>(u); clearing && clearing->busy && clearing->step.kind == BehaviorKind::ClearJunk) return { EmoteId::Cleaning };   // clearing junk
    std::vector<EmoteId> out;
    auto propEmoteOf = [&](ObjectId prop) {
        const Prop* p = w.get<Prop>(prop);
        return p ? propInfo(p->kind).emote : EmoteId::None;
    };
    if (const Working* wk = w.get<Working>(u)) {
        if (wk->unlocking) out.push_back(EmoteId::Lockpicking);
        else if (propEmoteOf(wk->prop) != EmoteId::None) out.push_back(propEmoteOf(wk->prop));
    }
    if (const Occupying* oc = w.get<Occupying>(u); oc && propEmoteOf(oc->prop) != EmoteId::None) out.push_back(propEmoteOf(oc->prop));
    if (lockedIn(w, u)) out.push_back(EmoteId::LockedIn);
    if (const BuildJob* job = w.get<BuildJob>(u)) {
        const SoloMove* so = w.get<SoloMove>(u);
        const bool there = !so || so->next >= so->waypoints.size();
        if (job->phase == BuildJob::Phase::Build) {
            if (there) out.push_back(EmoteId::Building);
        } else {
            out.push_back(EmoteId::Hauling);
        }
    }
    if (w.has<Inventory>(u) && w.has<Stats>(u)) {
        const float limit = carryLimit(statsOf(w, u));
        if (limit > 0.0f && carriedWeightOf(w, u) >= ENCUMBERED_SHARE * limit) out.push_back(EmoteId::Encumbered);
    }
    if (const Needs* n = w.get<Needs>(u); n && n->maxHunger > 0 && n->hunger < 1) out.push_back(EmoteId::Hungry);
    // What it is doing besides working a prop: recreation, carrying, fighting and the rest
    if (const Behaviors* b = w.get<Behaviors>(u); b && b->busy) {
        const BehaviorKind k = b->step.kind;
        if (k == BehaviorKind::Socialize && b->began) out.push_back(EmoteId::Socializing);
        if (k == BehaviorKind::Play && b->began) out.push_back(EmoteId::Playing);
        if (k == BehaviorKind::Dance && b->began) out.push_back(EmoteId::Dancing);
        if (k == BehaviorKind::ManStall) out.push_back(EmoteId::Stall);
    }
    if (const Behaviors* b = w.get<Behaviors>(u); b && b->seeking) out.push_back(EmoteId::Sharing);
    if (w.has<Carried>(u)) out.push_back(EmoteId::Carried);
    if (w.has<Carrying>(u) || w.has<CarryOrder>(u)) out.push_back(EmoteId::Carrying);
    else if (const PropOrder* po = w.get<PropOrder>(u); po && po->action == PropAction::Store) out.push_back(EmoteId::Carrying);
    if (w.has<StallJob>(u)) out.push_back(EmoteId::Stall);
    if (w.has<CraftJob>(u)) out.push_back(EmoteId::Crafting);
    if (w.has<DefendOrder>(u)) out.push_back(EmoteId::Defending);
    if (w.has<FollowOrder>(u)) out.push_back(EmoteId::Following);
    if (w.has<TalkOrder>(u)) out.push_back(EmoteId::Talking);
    if (w.has<LootOrder>(u)) out.push_back(EmoteId::Looting);
    if (!w.has<Mob>(u)) {
        if (w.has<Fleeing>(u)) out.push_back(EmoteId::Fleeing);
        else if (const Combat* c = w.get<Combat>(u); (c && c->target != NO_OBJECT) || w.has<AttackOrder>(u)) out.push_back(EmoteId::Fighting);
    }
    return out;
}

bool cannotAct(World& w, ObjectId u) {
    return outCold(w, u) || lockedIn(w, u) || (w.has<Carried>(u) && !w.has<PlayingDead>(u));
}

bool cannotTakeOrder(World& w, ObjectId u) {
    if (w.has<PlayingDead>(u)) breakFree(w, u);
    return cannotAct(w, u);
}

void breakFree(World& w, ObjectId u) {
    w.remove<PlayingDead>(u);
    const Carried* c = w.get<Carried>(u);
    if (!c) return;
    const ObjectId carrier = c->by;
    w.remove<Carried>(u);
    if (const Carrying* cr = w.get<Carrying>(carrier); cr && cr->unit == u) w.remove<Carrying>(carrier);
    if (Transform* t = w.get<Transform>(u)) {
        if (const Transform* ct = w.get<Transform>(carrier)) *t = *ct;
    }
    queueLog(w, langf("MISC_BREAKS_FREE", { nameOf(w, u) }), LogKind::Other, npcOnly(w, u));
}
