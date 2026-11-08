#include "ghosts.h"
#include "systems_internal.h"

using namespace systems_detail;

bool isBones(int item) {
    const ItemDef* d = findItem(item);
    return d && hasTag(*d, "bones");
}

void recordGhost(World& w, ObjectId u) {
    if (factionOf(w, u) != FACTION_PLAYER || w.has<Mob>(u)) return;
    Ghost g;
    g.name = nameOf(w, u);
    if (const UnitSprite* us = w.get<UnitSprite>(u)) g.sprite = us->id;
    w.resource<Graveyard>().ghosts.push_back(g);
    queueLog(w, langf("MISC_GHOST_RISES", { g.name, std::to_string((int)GHOST_GRACE_HOURS) }), LogKind::Other);
}

int unappeasedGhosts(World& w) {
    int n = 0;
    for (const Ghost& g : w.resource<Graveyard>().ghosts) n += g.appeased ? 0 : 1;
    return n;
}

void ghostSystem(World& w, double gameSeconds) {
    Graveyard& yard = w.resource<Graveyard>();
    bool haunting = false;
    for (Ghost& g : yard.ghosts) {
        if (g.appeased) continue;
        g.hours += gameSeconds / 3600.0;
        haunting = haunting || g.hours >= GHOST_GRACE_HOURS;
    }
    if (haunting == yard.haunted) return;
    yard.haunted = haunting;
    queueLog(w, lang(haunting ? "MISC_HAUNTED" : "MISC_NO_LONGER_HAUNTED"), LogKind::Other);
}

bool hauntedUnit(World& w, ObjectId u) {
    return w.resource<Graveyard>().haunted && factionOf(w, u) == FACTION_PLAYER && !w.has<Mob>(u);
}

int bonesIn(World& w, ObjectId altar) {
    const Storage* st = w.get<Storage>(altar);
    if (!st) return 0;
    int n = 0;
    for (const ItemStack& s : st->slots) n += s.count > 0 && isBones(s.item) ? s.count : 0;
    return n;
}

bool appeaseGhost(World& w, ObjectId altar, size_t ghost) {
    Graveyard& yard = w.resource<Graveyard>();
    Storage* st = w.get<Storage>(altar);
    if (!st || ghost >= yard.ghosts.size() || yard.ghosts[ghost].appeased) return false;
    for (ItemStack& s : st->slots) {
        if (s.count <= 0 || !isBones(s.item)) continue;
        queueItemMove(w, altar, NO_OBJECT, s.item, 1);
        if (--s.count == 0) s = ItemStack{};
        yard.ghosts[ghost].appeased = true;
        queueLog(w, langf("MISC_GHOST_APPEASED", { yard.ghosts[ghost].name }), LogKind::Other);
        ghostSystem(w, 0.0);
        return true;
    }
    return false;
}
