// What a unit is doing (its task name and emotes), and whether it can act.
#include "systems_internal.h"

using namespace systems_detail;

bool unitIdle(World& w, Entity u) {
    if (w.has<Corpse>(u) || outCold(w, u) || w.has<PlayingDead>(u) || w.has<Carried>(u)) return false;
    if (const Behaviors* b = w.get<Behaviors>(u); b && (b->busy || b->seeking)) return false;
    if (w.has<Working>(u) || w.has<PropOrder>(u) || w.has<Occupying>(u) || w.has<Carrying>(u) || w.has<CarryOrder>(u) ||
        w.has<LootOrder>(u) || w.has<DefendOrder>(u) || w.has<FollowOrder>(u) ||
        w.has<AttackOrder>(u) || w.has<TalkOrder>(u) || w.has<PickupOrder>(u) || w.has<PickupChain>(u)) {
        return false;
    }
    if (const Combat* c = w.get<Combat>(u); c && c->target != NULL_ENTITY) return false;
    if (const SoloMove* so = w.get<SoloMove>(u); so && so->next < so->waypoints.size()) return false;
    if (const ActiveSquadMember* sm = w.get<ActiveSquadMember>(u)) {
        if (const ActiveSquad* sq = w.get<ActiveSquad>(sm->squad); sq && !sq->arrived) return false;
    }
    return true;
}

std::string taskNameOf(World& w, Entity u) {
    if (w.has<Corpse>(u)) return lang("UILABEL_TASK_DEAD");
    if (outCold(w, u)) return lang("UILABEL_TASK_UNCONSCIOUS");
    if (w.has<PlayingDead>(u)) return lang("UILABEL_TASK_PLAYING_DEAD");
    if (w.has<Carried>(u)) return lang("UILABEL_TASK_CARRIED");
    // A step of its own behaviors: the behavior's name, such as Auto-Harvest: Tree, while it is on its way or at it
    if (const Behaviors* b = w.get<Behaviors>(u)) {
        if (b->seeking) return langf("UILABEL_TASK_SHARE_WALK", { nameOf(w, b->seek) });
        const PropOrder* po = w.get<PropOrder>(u);
        const Working* wk = w.get<Working>(u);
        if (b->busy && po && po->automatic) return langf("UILABEL_TASK_AUTO_GOING", { behaviorName(b->step) });
        if (b->busy && wk && !wk->unlocking && wk->prop == b->stepProp) return behaviorName(b->step);
    }
    Entity prop = NULL_ENTITY;
    if (const Working* g = w.get<Working>(u); g && g->unlocking) return statName(StatId::Lockpicking);
    if (const Working* g = w.get<Working>(u); g && w.has<Prop>(g->prop) && w.get<Prop>(g->prop)->kind == PropKind::Debris) {
        return lang("UILABEL_TASK_CLEARING");
    }
    if (const Working* g = w.get<Working>(u)) prop = g->prop;
    else if (const PropOrder* po = w.get<PropOrder>(u); po && useOf(w, po->prop) == PropUse::Resource) prop = po->prop;
    if (const Prop* p = prop != NULL_ENTITY ? w.get<Prop>(prop) : nullptr) return statName(propInfo(p->kind).stat);
    if (const Occupying* oc = w.get<Occupying>(u)) {
        if (lockedIn(w, u)) return lang("UILABEL_TASK_LOCKED_IN");
        const Prop* p = w.get<Prop>(oc->prop);
        return lang(p && isSeat(p->kind) ? "UILABEL_TASK_SITTING" : "UILABEL_TASK_RESTING");
    }
    if (w.has<Carrying>(u) || w.has<CarryOrder>(u)) return lang("UILABEL_TASK_CARRYING");
    if (const PropOrder* po = w.get<PropOrder>(u); po && po->action == PropAction::Store) return lang("UILABEL_TASK_STORING");
    if (w.has<DefendOrder>(u)) return lang("UILABEL_TASK_DEFENDING");
    if (w.has<FollowOrder>(u)) return lang("UILABEL_TASK_FOLLOWING");
    const SoloMove* so = w.get<SoloMove>(u);
    if (so && so->next < so->waypoints.size()) return lang("UILABEL_TASK_MOVING");
    if (const ActiveSquadMember* sm = w.get<ActiveSquadMember>(u)) {
        if (const ActiveSquad* sq = w.get<ActiveSquad>(sm->squad); sq && !sq->arrived) return lang("UILABEL_TASK_MOVING");
    }
    return lang("UILABEL_TASK_IDLE");
}

std::vector<EmoteId> emotesOf(World& w, Entity u) {
    // Out cold or dead, that is all it shows
    if (w.has<Corpse>(u)) return { EmoteId::Dead };
    if (outCold(w, u)) return { EmoteId::Unconscious };
    if (w.has<PlayingDead>(u)) return { EmoteId::PlayingDead };
    std::vector<EmoteId> out;
    auto propEmoteOf = [&](Entity prop) {
        const Prop* p = w.get<Prop>(prop);
        return p ? propEmote(p->kind) : EmoteId::None;
    };
    if (const Working* wk = w.get<Working>(u)) {
        if (wk->unlocking) out.push_back(EmoteId::Lockpicking);
        else if (propEmoteOf(wk->prop) != EmoteId::None) out.push_back(propEmoteOf(wk->prop));
    }
    if (const Occupying* oc = w.get<Occupying>(u); oc && propEmoteOf(oc->prop) != EmoteId::None) out.push_back(propEmoteOf(oc->prop));
    if (lockedIn(w, u)) out.push_back(EmoteId::LockedIn);
    if (w.has<Inventory>(u) && w.has<Stats>(u)) {
        const float limit = carryLimit(statsOf(w, u));
        if (limit > 0.0f && carriedWeightOf(w, u) >= ENCUMBERED_SHARE * limit) out.push_back(EmoteId::Encumbered);
    }
    if (const Needs* n = w.get<Needs>(u); n && n->maxHunger > 0 && n->hunger < 1) out.push_back(EmoteId::Hungry);
    return out;
}

bool cannotAct(World& w, Entity u) {
    // Out cold, shut in a cage, or carried (one playing dead can still break free with an order: cannotTakeOrder)
    return outCold(w, u) || lockedIn(w, u) || (w.has<Carried>(u) && !w.has<PlayingDead>(u));
}

bool cannotTakeOrder(World& w, Entity u) {
    if (w.has<PlayingDead>(u)) breakFree(w, u);
    return cannotAct(w, u);
}

void breakFree(World& w, Entity u) {
    w.remove<PlayingDead>(u);
    const Carried* c = w.get<Carried>(u);
    if (!c) return;
    const Entity carrier = c->by;
    w.remove<Carried>(u);
    if (const Carrying* cr = w.get<Carrying>(carrier); cr && cr->unit == u) w.remove<Carrying>(carrier);
    if (Transform* t = w.get<Transform>(u)) {
        if (const Transform* ct = w.get<Transform>(carrier)) *t = *ct;
    }
    queueLog(langf("MISC_BREAKS_FREE", { nameOf(w, u) }), LogKind::Other, npcOnly(w, u));
}
