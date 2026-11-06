#include "prop_info.h"
#include <algorithm>
#include <cmath>
#include "claims.h"
#include "factions.h"
#include "language.h"

namespace {
std::string upkeepLine(World& w, Entity e) {
    const Prop* p = w.get<Prop>(e);
    double hours = 0.0;
    if (const Fuel* f = w.get<Fuel>(e)) hours += f->hours;
    const PropInfo& info = propInfo(p->kind);
    if (const Storage* st = w.get<Storage>(e)) {
        for (const ItemStack& s : st->slots) {
            if (s.item == info.input && s.count > 0) hours += s.count * info.fuelHours;
        }
    }
    return hours > 1e-9 ? langf("MISC_PROP_UPKEEP", { std::to_string((int)std::floor(hours)) }) : std::string(lang("MISC_NO_UPKEEP"));
}

std::string healthLine(const Health& hp) {
    const int shown = (int)std::ceil(hp.value - 1e-9);
    return langf("MISC_FLAG_HEALTH", { std::to_string(std::max(0, shown)), std::to_string((int)hp.max) });
}
}

std::vector<std::string> propInfoLines(World& w, Entity e) {
    std::vector<std::string> out;
    const Prop* p = w.get<Prop>(e);
    const Transform* tr = w.get<Transform>(e);
    if (!p || !tr) return out;
    if (const Owner* own = w.get<Owner>(e)) out.push_back(langf("MISC_FLAG_FACTION", { factionName(own->faction) }));
    if (const Health* hp = w.get<Health>(e)) {
        out.push_back(healthLine(*hp));
        out.push_back(lang(propDecaying(w, e) ? "MISC_PROP_DECAYING" : "MISC_PROP_HOLDING"));
    }
    if (const Lock* lock = w.get<Lock>(e)) {
        if (!lock->locked) out.push_back(lang("MISC_PROP_LOCK_OPEN"));
        else if (!lockShutFor(w, e, NULL_ENTITY)) out.push_back(lang("MISC_PROP_LOCK_YOURS"));
        else out.push_back(langf("MISC_PROP_LOCK_SHUT", { std::to_string(lock->level) }));
    }
    if (p->kind == PropKind::Altar) out.push_back(upkeepLine(w, e));
    return out;
}

std::vector<std::string> altarInfoLines(World& w, Entity e) {
    std::vector<std::string> out;
    const Prop* p = w.get<Prop>(e);
    if (!p || p->kind != PropKind::Altar) return out;
    if (const Health* hp = w.get<Health>(e)) out.push_back(healthLine(*hp));
    out.push_back(upkeepLine(w, e));
    return out;
}

Sfx propWorkSound(PropKind kind) {
    switch (kind) {
    case PropKind::Ore: return Sfx::WorkMining;
    case PropKind::Tree: return Sfx::WorkWoodcutting;
    case PropKind::Mushroom: return Sfx::WorkHerbalism;
    case PropKind::Campfire:
    case PropKind::Stove: return Sfx::WorkCooking;
    case PropKind::WaterPump: return Sfx::WorkPump;
    case PropKind::TrainingDummy: return Sfx::WorkTraining;
    case PropKind::Forge: return Sfx::ForgeBurning;
    default: return Sfx::Count;
    }
}

Sfx propDoneSound(PropKind kind) {
    switch (kind) {
    case PropKind::Ore: return Sfx::DoneOre;
    case PropKind::Tree: return Sfx::DoneTree;
    case PropKind::Mushroom: return Sfx::DoneMushroom;
    case PropKind::Stove: return Sfx::DoneCooking;
    case PropKind::WaterPump: return Sfx::DoneWater;
    default: return Sfx::ItemMove;
    }
}
