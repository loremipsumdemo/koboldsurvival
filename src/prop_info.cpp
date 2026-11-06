#include "prop_info.h"
#include <algorithm>
#include <cmath>
#include "claims.h"
#include "factions.h"
#include "items.h"
#include "language.h"
#include "stats.h"
#include "systems.h"

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
    const PropInfo& info = propInfo(p->kind);
    out.push_back(propDisplayName(w, e));
    if (const Owner* own = w.get<Owner>(e)) out.push_back(langf("MISC_FLAG_FACTION", { factionName(own->faction) }));
    if (info.use == PropUse::Occupy) {
        const bool bed = isBed(p->kind);
        out.push_back(lang(bed ? "MISC_PROP_USE_BED" : isSeat(p->kind) ? "MISC_PROP_USE_SEAT" : "MISC_PROP_USE_CAGE"));
        const Entity occupant = propOccupant(w, e);
        if (occupant != NULL_ENTITY) {
            if (const Name* name = w.get<Name>(occupant)) out.push_back(langf("MISC_PROP_OCCUPIED", { name->text }));
        }
    }
    if (info.use == PropUse::Resource) out.push_back(lang("MISC_PROP_USE_RESOURCE"));
    if (!info.claimsLand) {
        if (info.use == PropUse::Crafting) out.push_back(lang("MISC_PROP_USE_CRAFTING"));
        if (info.use == PropUse::PassiveCrafting) out.push_back(lang("MISC_PROP_USE_PASSIVE"));
        if (info.input == INPUT_COOKABLE) out.push_back(lang("MISC_PROP_USES_RAW_MEAT"));
        else if (info.input > 0) out.push_back(langf("MISC_PROP_USES", { itemNameOf(info.input) }));
        if (info.item > 0) out.push_back(langf("MISC_PROP_MAKES", { itemNameOf(info.item) }));
        else if (info.input == INPUT_COOKABLE) out.push_back(lang("MISC_PROP_MAKES_COOKED_MEAT"));
        if (isWorkProp(info.use) && info.stat != StatId::None) out.push_back(langf("MISC_PROP_SKILL", { statName(info.stat) }));
        if (isWorkProp(info.use) && info.workers > 1) out.push_back(langf("MISC_PROP_WORKERS", { std::to_string(info.workers) }));
        const Fuel* fuel = w.get<Fuel>(e);
        if (info.use == PropUse::PassiveCrafting && fuel && fuel->hours > 1e-9) {
            out.push_back(langf("MISC_PROP_FUEL", { std::to_string((int)std::ceil(fuel->hours)) }));
        }
    }
    if (hasStorage(info)) {
        out.push_back(langf("MISC_PROP_INVENTORY", { std::to_string(STORAGE_SLOTS) }));
        if (info.accepts != ItemFilter::Any) out.push_back(langf("MISC_PROP_HOLDS_ONLY", { lang(itemFilterKey(info.accepts)) }));
    }
    if (const Health* hp = w.get<Health>(e)) {
        out.push_back(healthLine(*hp));
        out.push_back(lang(propDecaying(w, e) ? "MISC_PROP_DECAYING" : "MISC_PROP_HOLDING"));
    }
    if (const Lock* lock = w.get<Lock>(e)) {
        out.push_back(lang("MISC_PROP_LOCKABLE"));
        if (!lock->locked) out.push_back(lang("MISC_PROP_LOCK_OPEN"));
        else if (!lockShutFor(w, e, NULL_ENTITY)) out.push_back(lang("MISC_PROP_LOCK_YOURS"));
        else out.push_back(langf("MISC_PROP_LOCK_SHUT", { std::to_string(lock->level) }));
    }
    if (info.claimsLand) out.push_back(upkeepLine(w, e));
    return out;
}

std::vector<std::string> altarInfoLines(World& w, Entity e) {
    std::vector<std::string> out;
    const Prop* p = w.get<Prop>(e);
    if (!p || !propInfo(p->kind).claimsLand) return out;
    if (const Health* hp = w.get<Health>(e)) out.push_back(healthLine(*hp));
    out.push_back(upkeepLine(w, e));
    return out;
}
