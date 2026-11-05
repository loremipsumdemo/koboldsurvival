// The lines of text the hover list and Show Prop Info show for a prop (prop_info.h)
#include "prop_info.h"
#include <algorithm>
#include <cmath>
#include "claims.h"
#include "factions.h"
#include "language.h"

std::vector<std::string> propInfoLines(World& w, Entity e) {
    std::vector<std::string> out;
    const Prop* p = w.get<Prop>(e);
    const Transform* tr = w.get<Transform>(e);
    if (!p || !tr) return out;
    const std::vector<Claim> claims = claimsOf(w);
    const Owner* own = w.get<Owner>(e);
    if (own) out.push_back(langf("MISC_FLAG_FACTION", { factionName(own->faction) }));

    // Whether it is wearing down now
    const bool decaying = propDecaying(w, e, claims);

    if (const Health* hp = w.get<Health>(e)) {
        const int shown = (int)std::ceil(hp->value - 1e-9);
        out.push_back(langf("MISC_FLAG_HEALTH", { std::to_string(std::max(0, shown)), std::to_string((int)hp->max) }));
    }
    if (const Wear* wr = w.get<Wear>(e)) {
        const int pct = std::clamp((int)std::ceil(wr->value * 100.0 - 1e-9), 0, 100);
        out.push_back(langf("MISC_PROP_CONDITION", { std::to_string(pct) }));
    }
    if (w.has<Health>(e) || w.has<Wear>(e)) out.push_back(lang(decaying ? "MISC_PROP_DECAYING" : "MISC_PROP_HOLDING"));

    if (const Lock* lock = w.get<Lock>(e)) {
        if (!lock->locked) out.push_back(lang("MISC_PROP_LOCK_OPEN"));
        else if (!lockShutFor(w, e, NULL_ENTITY)) out.push_back(lang("MISC_PROP_LOCK_YOURS"));
        else out.push_back(langf("MISC_PROP_LOCK_SHUT", { std::to_string(lock->level) }));
    }

    if (p->kind == PropKind::Flag) {
        const Claim* c = nullptr;
        for (const Claim& k : claims) {
            if (k.flag == e) c = &k;
        }
        out.push_back(lang(c && c->vault != NULL_ENTITY ? "MISC_FLAG_VAULT_YES" : "MISC_FLAG_VAULT_NO"));
    }
    if (p->kind == PropKind::Vault) {
        // Upkeep left: what is left of the log burning now, and the hours of each log waiting
        double hours = 0.0;
        if (const Fuel* f = w.get<Fuel>(e)) hours += f->hours;
        const int input = propInfo(p->kind).input;
        if (const Storage* st = w.get<Storage>(e)) {
            for (const ItemStack& s : st->slots) {
                if (s.item == input && s.count > 0) hours += s.count * propInfo(p->kind).fuelHours;
            }
        }
        out.push_back(langf("MISC_PROP_UPKEEP", { std::to_string((int)std::floor(hours)) }));
        if (!claimAt(claims, tr->x, tr->z)) out.push_back(lang("MISC_PROP_VAULT_UNCLAIMED"));
    }
    return out;
}

std::vector<std::string> flagInfoLines(World& w, Entity e) {
    std::vector<std::string> out;
    const Prop* p = w.get<Prop>(e);
    if (!p || p->kind != PropKind::Flag) return out;
    const Owner* own = w.get<Owner>(e);
    const Health* hp = w.get<Health>(e);
    const std::vector<Claim> claims = claimsOf(w);
    const Claim* c = nullptr;
    for (const Claim& k : claims) {
        if (k.flag == e) c = &k;
    }
    const int owner = own ? own->faction : FACTION_PLAYER;
    out.push_back(langf("MISC_FLAG_FACTION", { factionName(owner) }));
    const int shown = hp ? (int)std::ceil(hp->value - 1e-9) : 0;
    out.push_back(langf("MISC_FLAG_HEALTH", { std::to_string(std::max(0, shown)), std::to_string((int)(hp ? hp->max : 0)) }));
    out.push_back(lang(c && c->vault != NULL_ENTITY ? "MISC_FLAG_VAULT_YES" : "MISC_FLAG_VAULT_NO"));
    return out;
}
