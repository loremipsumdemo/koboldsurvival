#include "prop_info.h"
#include "commerce.h"
#include "inventory.h"
#include "building.h"
#include <algorithm>
#include <cmath>
#include "claims.h"
#include "factions.h"
#include "items.h"
#include "language.h"
#include "stats.h"
#include "systems.h"

namespace {
std::string upkeepLine(World& w, ObjectId e) {
    const Prop* p = w.get<Prop>(e);
    double hours = 0.0;
    if (const Fuel* f = w.get<Fuel>(e)) hours += f->hours;
    const PropInfo& info = propInfo(p->kind);
    if (const Storage* st = w.get<Storage>(e)) {
        for (const ItemStack& s : st->slots) {
            if (inputMatches(info, s.item) && s.count > 0) hours += s.count * info.fuelHours;
        }
    }
    return hours > 1e-9 ? langf("MISC_PROP_UPKEEP", { std::to_string((int)std::floor(hours)) }) : std::string(lang("MISC_NO_UPKEEP"));
}

std::string healthLine(const Health& hp) {
    const int shown = (int)std::ceil(hp.value - 1e-9);
    return langf("MISC_FLAG_HEALTH", { std::to_string(std::max(0, shown)), std::to_string((int)hp.max) });
}
}

std::vector<std::string> propInfoLines(World& w, ObjectId e) {
    std::vector<std::string> out;
    const Prop* p = w.get<Prop>(e);
    const Transform* tr = w.get<Transform>(e);
    if (!p || !tr) return out;
    const PropInfo& info = propInfo(p->kind);
    out.push_back(propDisplayName(w, e));
    if (const Owner* own = w.get<Owner>(e)) out.push_back(langf("MISC_FLAG_FACTION", { factionName(own->faction) }));
    // A blueprint: the material it needs, and how much of it has been brought (building starts once it has it all)
    if (const Blueprint* bp = w.get<Blueprint>(e)) {
        const int need = materialsNeeded(w, e);
        out.push_back(langf("MISC_BLUEPRINT_NEEDS", { itemNameOf(materialItem(materialOf(w, e))), std::to_string(std::min(bp->supplied, need)),
                                                      std::to_string(need) }));
        if (bp->supplied >= need) {
            out.push_back(langf("MISC_BLUEPRINT_BUILDING", { std::to_string((int)std::floor(bp->progress * 100.0)) }));
        } else {
            out.push_back(lang("MISC_BLUEPRINT_WAITING"));
        }
    }
    if (const int level = storeyOf(w, e); level > 0 && !info.upperFloor) out.push_back(langf("MISC_ON_FLOOR", { std::to_string(level + 1) }));
    if (info.use == PropUse::Occupy) {
        const bool bed = isBed(p->kind);
        out.push_back(lang(bed ? "MISC_PROP_USE_BED" : isSeat(p->kind) ? "MISC_PROP_USE_SEAT" : "MISC_PROP_USE_CAGE"));
        // A bed in a building: only its faction's, unless set Public
        if (bed) {
            const int house = bedHouseOwner(w, e);
            if (w.has<BedPublic>(e)) out.push_back(lang("MISC_BED_PUBLIC"));
            else if (house >= 0) out.push_back(langf("MISC_BED_PRIVATE", { factionName(house) }));
        }
        const ObjectId occupant = propOccupant(w, e);
        if (occupant != NO_OBJECT) {
            if (const Name* name = w.get<Name>(occupant)) out.push_back(langf("MISC_PROP_OCCUPIED", { name->text }));
        }
    }
    if (info.use == PropUse::Resource) out.push_back(lang("MISC_PROP_USE_RESOURCE"));
    if (!info.claimsLand) {
        if (info.use == PropUse::Crafting) out.push_back(lang("MISC_PROP_USE_CRAFTING"));
        if (info.use == PropUse::PassiveCrafting) out.push_back(lang("MISC_PROP_USE_PASSIVE"));
        if (info.input == INPUT_COOKABLE) out.push_back(lang("MISC_PROP_USES_RAW_MEAT"));
        else if (info.input == INPUT_BONES) out.push_back(langf("MISC_PROP_USES", { lang("MISC_ANY_BONES") }));
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
        const Storage* st = w.get<Storage>(e);
        if (st && st->capacity > 0) out.push_back(langf("MISC_PROP_HOLDS_ITEMS", { std::to_string(storedCount(*st)), std::to_string(st->capacity) }));
        else out.push_back(langf("MISC_PROP_INVENTORY", { std::to_string(st ? (int)st->slots.size() : STORAGE_SLOTS) }));
        if (info.stall) {
            out.push_back(lang(stallManned(w, e) ? "MISC_STALL_MANNED" : "MISC_STALL_UNMANNED"));
            if (const StallStock* ss = w.get<StallStock>(e); ss && !ss->requests.empty()) {
                out.push_back(langf("MISC_STALL_ORDERS", { std::to_string(ss->requests.size()) }));
            }
        }
        if (info.accepts != ItemFilter::Any) out.push_back(langf("MISC_PROP_HOLDS_ONLY", { lang(itemFilterKey(info.accepts)) }));
    }
    if (const Health* hp = w.get<Health>(e)) {
        out.push_back(healthLine(*hp));
        out.push_back(lang(propDecaying(w, e) ? "MISC_PROP_DECAYING" : "MISC_PROP_HOLDING"));
    }
    if (const Lock* lock = w.get<Lock>(e)) {
        out.push_back(lang("MISC_PROP_LOCKABLE"));
        if (!lock->locked) out.push_back(lang("MISC_PROP_LOCK_OPEN"));
        else if (!lockShutFor(w, e, NO_OBJECT)) out.push_back(lang("MISC_PROP_LOCK_YOURS"));
        else out.push_back(langf("MISC_PROP_LOCK_SHUT", { std::to_string(lock->level) }));
    }
    if (info.claimsLand) out.push_back(upkeepLine(w, e));
    return out;
}

std::vector<std::string> altarInfoLines(World& w, ObjectId e) {
    std::vector<std::string> out;
    const Prop* p = w.get<Prop>(e);
    if (!p || !propInfo(p->kind).claimsLand) return out;
    if (const Health* hp = w.get<Health>(e)) out.push_back(healthLine(*hp));
    out.push_back(upkeepLine(w, e));
    return out;
}
