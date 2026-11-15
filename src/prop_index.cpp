#include "prop_index.h"
#include <algorithm>
#include "calendar.h"
#include "profile.h"

namespace {
// Game seconds between makings of the index when nothing says the props changed (a couple of real seconds)
constexpr double REFRESH_GAME_SECONDS = 2.0 * GAME_SECONDS_PER_REAL_SECOND;
}

const PropIndex& propIndex(World& w) {
    PropIndex& ix = w.resource<PropIndex>();
    const double clock = w.resource<WorldClock>().seconds;
    if (ix.filled && ix.propRevision == w.revision<Prop>() && ix.transformRevision == w.revision<Transform>() &&
        std::fabs(clock - ix.builtAt) < REFRESH_GAME_SECONDS) {
        return ix;
    }
    PROFILE_SCOPE("propIndex: rebuild");
    ix.filled = true;
    ix.propRevision = w.revision<Prop>();
    ix.transformRevision = w.revision<Transform>();
    ix.builtAt = clock;
    const int kinds = propKindCount();
    ix.byKind.assign((size_t)kinds, {});
    ix.cells.clear();
    ix.cellAt.clear();
    ix.movers.clear();
    ix.minCx = ix.minCz = 0;
    ix.maxCx = ix.maxCz = -1;
    if (ix.widestReach <= 0.0f) {
        for (int k = 0; k < kinds; ++k) ix.widestReach = std::max(ix.widestReach, footprintReach(propInfo((PropKind)k).foot));
    }
    bool first = true;
    for (ObjectId e : w.view<Prop, Transform>()) {
        const PropKind kind = w.get<Prop>(e)->kind;
        if (idx(kind) < 0 || idx(kind) >= kinds) continue;
        ix.byKind[(size_t)idx(kind)].push_back(e);
        if (propInfo(kind).moves) {
            ix.movers.push_back(e);
            continue;
        }
        const Transform* t = w.get<Transform>(e);
        const int cx = PropIndex::cellOf(t->x), cz = PropIndex::cellOf(t->z);
        const long long key = PropIndex::cellKey(cx, cz);
        auto it = ix.cellAt.find(key);
        if (it == ix.cellAt.end()) {
            it = ix.cellAt.emplace(key, (std::uint32_t)ix.cells.size()).first;
            ix.cells.push_back(PropIndex::Cell{ cx, cz, {} });
        }
        ix.cells[it->second].props.push_back(e);
        if (first) {
            ix.minCx = ix.maxCx = cx;
            ix.minCz = ix.maxCz = cz;
            first = false;
        } else {
            ix.minCx = std::min(ix.minCx, cx);
            ix.maxCx = std::max(ix.maxCx, cx);
            ix.minCz = std::min(ix.minCz, cz);
            ix.maxCz = std::max(ix.maxCz, cz);
        }
    }
    return ix;
}

const std::vector<ObjectId>& propsOfKind(World& w, PropKind kind) {
    static const std::vector<ObjectId> none;
    const PropIndex& ix = propIndex(w);
    return idx(kind) >= 0 && (size_t)idx(kind) < ix.byKind.size() ? ix.byKind[(size_t)idx(kind)] : none;
}

void propsNear(World& w, float x, float z, float reach, std::vector<ObjectId>& out) {
    out.clear();
    const PropIndex& ix = propIndex(w);
    out.insert(out.end(), ix.movers.begin(), ix.movers.end());
    const int x0 = PropIndex::cellOf(x - reach), x1 = PropIndex::cellOf(x + reach);
    const int z0 = PropIndex::cellOf(z - reach), z1 = PropIndex::cellOf(z + reach);
    for (int cz = z0; cz <= z1; ++cz) {
        for (int cx = x0; cx <= x1; ++cx) {
            if (const PropIndex::Cell* c = ix.cell(cx, cz)) out.insert(out.end(), c->props.begin(), c->props.end());
        }
    }
}

void propsInRange(World& w, float x, float z, float range, std::vector<ObjectId>& out) {
    propsNear(w, x, z, range, out);
    std::sort(out.begin(), out.end());
}
