#include "squads.h"
#include "systems_internal.h"

using namespace systems_detail;

std::string squadLabel(World& w, int squad) {
    if (squad < 0 || squad >= SQUAD_COUNT) return std::string();
    const std::string& name = w.resource<SquadBook>().names[(size_t)squad];
    return name.empty() ? langf("UILABEL_SQUAD_DEFAULT_NAME", { std::to_string(squad + 1) }) : name;
}

std::vector<ObjectId> squadUnits(World& w, int squad) {
    std::vector<ObjectId> out;
    for (ObjectId u : w.view<Entity, Commandable, SquadMember>()) {
        if (w.get<SquadMember>(u)->squad == squad) out.push_back(u);
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::array<int, SQUAD_COUNT> squadSizes(World& w) {
    std::array<int, SQUAD_COUNT> n{};
    for (ObjectId u : w.view<Entity, Commandable, SquadMember>()) {
        const int s = w.get<SquadMember>(u)->squad;
        if (s >= 0 && s < SQUAD_COUNT) ++n[(size_t)s];
    }
    return n;
}

std::vector<ObjectId> militaryUnits(World& w) {
    std::vector<ObjectId> out = w.view<Entity, Commandable>();
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<ObjectId> citizenUnits(World& w) {
    std::vector<ObjectId> out = w.view<Entity, Citizen>();
    std::sort(out.begin(), out.end());
    return out;
}

int squadOf(World& w, ObjectId u) {
    const SquadMember* m = w.has<Commandable>(u) ? w.get<SquadMember>(u) : nullptr;
    return m ? m->squad : -1;
}

int squadWithRoom(World& w, int preferred) {
    const std::array<int, SQUAD_COUNT> sizes = squadSizes(w);
    if (preferred >= 0 && preferred < SQUAD_COUNT && sizes[(size_t)preferred] < SQUAD_SIZE) return preferred;
    for (int s = 0; s < SQUAD_COUNT; ++s) {
        if (sizes[(size_t)s] < SQUAD_SIZE) return s;
    }
    return -1;
}

bool moveToSquad(World& w, ObjectId u, int squad) {
    if (!w.has<Commandable>(u) || squad < 0 || squad >= SQUAD_COUNT) return false;
    if (squadOf(w, u) == squad) return true;
    if (squadSizes(w)[(size_t)squad] >= SQUAD_SIZE) return false;
    w.add(u, SquadMember{ squad });
    return true;
}

namespace {
// Where a citizen stays: your altar nearest to it, or where it stands if you have none
Vec2 citizenHome(World& w, ObjectId u) {
    const Transform* tr = w.get<Transform>(u);
    Vec2 home = tr ? Vec2{ tr->x, tr->z } : Vec2{ 0.0f, 0.0f };
    float best = -1.0f;
    for (const Claim& c : claimsOf(w)) {
        if (c.owner != FACTION_PLAYER) continue;
        const float dx = c.x - home.x, dz = c.z - home.z, d2 = dx * dx + dz * dz;
        if (best < 0.0f || d2 < best) {
            best = d2;
            home = Vec2{ c.x, c.z };
        }
    }
    return home;
}

BehaviorRule stayNearHome() {
    BehaviorRule r;
    r.kind = BehaviorKind::PatrolSpawn;
    return r;
}
}

void makeCitizen(World& w, ObjectId u) {
    if (!w.has<Entity>(u) || w.has<Citizen>(u)) return;
    if (w.has<Commandable>(u)) {
        const bool caged = lockedIn(w, u);
        const Occupying keep = w.has<Occupying>(u) ? *w.get<Occupying>(u) : Occupying{};
        if (w.has<Carrying>(u)) putDown(w, u);
        clearOrders(w, u);
        w.remove<FollowOrder>(u);
        w.remove<DefendOrder>(u);
        w.remove<LootOrder>(u);
        w.remove<CarryOrder>(u);
        if (caged) w.add(u, keep);
        if (Combat* c = w.get<Combat>(u)) c->target = NO_OBJECT;
    }
    w.remove<Commandable>(u);
    w.remove<Selected>(u);
    w.remove<SquadMember>(u);
    w.add(u, Citizen{});

    const Vec2 home = citizenHome(w, u);
    w.add(u, SpawnHome{ home.x, home.z, false });
    Behaviors& b = w.has<Behaviors>(u) ? *w.get<Behaviors>(u) : w.add(u, Behaviors{});
    b.busy = false;
    b.seeking = false;
    b.seek = NO_OBJECT;
    b.stepProp = NO_OBJECT;
    b.wait = 0.0f;
    b.pending = true;
    addBehavior(b.rules, stayNearHome());
}

bool makeMilitary(World& w, ObjectId u, int preferred) {
    if (!w.has<Entity>(u)) return false;
    if (w.has<Commandable>(u)) return moveToSquad(w, u, preferred) || squadOf(w, u) >= 0;
    const int squad = squadWithRoom(w, preferred);
    if (squad < 0) return false;
    w.remove<Citizen>(u);
    w.remove<SpawnHome>(u);
    w.remove<Hostile>(u);
    w.remove<Neutral>(u);
    w.add(u, Commandable{});
    w.add(u, SquadMember{ squad });
    if (Behaviors* b = w.get<Behaviors>(u)) {
        const BehaviorRule home = stayNearHome();
        b->rules.erase(std::remove_if(b->rules.begin(), b->rules.end(), [&](const BehaviorRule& r) { return sameBehavior(r, home); }),
                       b->rules.end());
        if (b->busy && b->step.kind == BehaviorKind::PatrolSpawn) b->busy = false;
        w.remove<SoloMove>(u);
    }
    return true;
}

int settleSquads(World& w) {
    std::array<int, SQUAD_COUNT> sizes{};
    std::vector<ObjectId> homeless;
    for (ObjectId u : militaryUnits(w)) {
        const SquadMember* m = w.get<SquadMember>(u);
        const int s = m ? m->squad : -1;
        if (s >= 0 && s < SQUAD_COUNT && sizes[(size_t)s] < SQUAD_SIZE) {
            ++sizes[(size_t)s];
        } else {
            homeless.push_back(u);
        }
    }
    int toCitizens = 0;
    int next = 0;
    for (ObjectId u : homeless) {
        while (next < SQUAD_COUNT && sizes[(size_t)next] >= SQUAD_SIZE) ++next;
        if (next < SQUAD_COUNT) {
            w.add(u, SquadMember{ next });
            ++sizes[(size_t)next];
        } else {
            makeCitizen(w, u);
            queueLog(w, langf("MISC_SQUADS_FULL_CITIZEN", { nameOf(w, u) }), LogKind::Other);
            ++toCitizens;
        }
    }
    // A unit of yours that is neither: a citizen, so it is never left without anyone commanding it
    for (ObjectId u : w.view<Entity, Faction>()) {
        if (w.get<Faction>(u)->id != FACTION_PLAYER || w.has<Commandable>(u) || w.has<Citizen>(u)) continue;
        if (w.has<Corpse>(u) || w.has<Mob>(u)) continue;
        makeCitizen(w, u);
        ++toCitizens;
    }
    SquadBook& book = w.resource<SquadBook>();
    book.current = std::clamp(book.current, 0, SQUAD_COUNT - 1);
    return toCitizens;
}
