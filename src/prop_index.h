#pragma once
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include "components.h"
#include "ecs.h"
#include "building.h"
#include "enum_index.h"
#include "props.h"

// Where the props are, kept for the simulation's searches: the props of each kind, and a grid of cells PROP_CELL_TILES across with the
// props standing in each. A map has thousands of props, most of them trees and boulders, and a search for a bed, a seat or the nearest
// store used to look at every one of them, for every unit that looked, many times a second.
//
// Made again when a Prop or Transform is added or removed anywhere, and every couple of minutes of game time besides (a prop moved in
// place, as by the level editor). The vehicles (props.txt's moves flag: Airships, Wheelbarrows) go wherever they are taken, so they are
// kept in a list of their own that every grid search looks through. Every search reads the props' Transforms as they are now; the grid
// only says where to look.
inline constexpr int PROP_CELL_TILES = 8;

struct PropIndex {
    struct Cell {
        int cx = 0, cz = 0;
        std::vector<ObjectId> props;
    };
    std::uint64_t propRevision = 0, transformRevision = 0;
    double builtAt = 0.0;
    bool filled = false;
    std::vector<std::vector<ObjectId>> byKind;   // by PropKind, every prop of it (blueprints too)
    std::vector<Cell> cells;                     // the cells with props in them
    std::unordered_map<long long, std::uint32_t> cellAt;
    int minCx = 0, minCz = 0, maxCx = -1, maxCz = -1;
    std::vector<ObjectId> movers;                // the vehicles, in no cell
    float widestReach = 0.0f;                    // the largest footprintReach of any kind of prop

    static int cellOf(float v) { return (int)std::floor(v / (float)PROP_CELL_TILES); }
    static long long cellKey(int cx, int cz) { return (long long)cz * 1000003LL + (long long)cx; }
    const Cell* cell(int cx, int cz) const {
        const auto it = cellAt.find(cellKey(cx, cz));
        return it == cellAt.end() ? nullptr : &cells[it->second];
    }
};

// The index, made again first if the props have changed. Don't hold what it gives across a change to the props.
const PropIndex& propIndex(World& w);

// Every prop of this kind (blueprints too)
const std::vector<ObjectId>& propsOfKind(World& w, PropKind kind);

// A copy of every prop of the kinds the test passes (blueprints too), so the loop over it may change the world
template <typename KindTest>
std::vector<ObjectId> propsWhere(World& w, KindTest test) {
    const PropIndex& ix = propIndex(w);
    std::vector<ObjectId> out;
    for (size_t k = 0; k < ix.byKind.size(); ++k) {
        if (test((PropKind)k)) out.insert(out.end(), ix.byKind[k].begin(), ix.byKind[k].end());
    }
    return out;
}

// Every prop whose spot may be within reach of x, z (those in the cells the reach touches, and the vehicles). The caller measures.
void propsNear(World& w, float x, float z, float reach, std::vector<ObjectId>& out);

// The nearest finished prop (no blueprint) that passes the test, or NO_OBJECT: the grid's cells are looked through in rings outward from
// x, z, stopping once no cell further out can hold anything nearer. The test must not change the props.
template <typename Pred>
ObjectId nearestIndexedProp(World& w, float x, float z, Pred pred) {
    const PropIndex& ix = propIndex(w);
    ObjectId best = NO_OBJECT;
    float bestD2 = 0.0f;
    auto consider = [&](ObjectId p) {
        if (w.has<Blueprint>(p)) return;
        const Transform* t = w.get<Transform>(p);
        if (!t) return;
        const float d2 = (t->x - x) * (t->x - x) + (t->z - z) * (t->z - z);
        if (best != NO_OBJECT && (d2 > bestD2 || (d2 == bestD2 && p > best))) return;   // the distance first: the test may be dear
        if (!pred(p)) return;
        best = p;
        bestD2 = d2;
    };
    for (ObjectId p : ix.movers) consider(p);
    if (ix.cells.empty()) return best;
    const int cx = PropIndex::cellOf(x), cz = PropIndex::cellOf(z);
    const int lastRing = std::max(std::max(std::abs(cx - ix.minCx), std::abs(ix.maxCx - cx)), std::max(std::abs(cz - ix.minCz), std::abs(ix.maxCz - cz)));
    // Nothing in ring r is nearer than (r - 1) cells, the point being anywhere in its own cell
    auto ringFar = [&](int r) {
        const float near = (float)std::max(0, r - 1) * (float)PROP_CELL_TILES;
        return best != NO_OBJECT && near * near > bestD2;
    };
    size_t looked = 0;
    for (int r = 0; r <= lastRing; ++r) {
        if (ringFar(r)) return best;
        if (looked > ix.cells.size()) {
            // The rings have looked in more places than there are cells with props: the rest are looked through one by one
            for (const PropIndex::Cell& c : ix.cells) {
                const int ring = std::max(std::abs(c.cx - cx), std::abs(c.cz - cz));
                if (ring < r || ringFar(ring)) continue;
                for (ObjectId p : c.props) consider(p);
            }
            return best;
        }
        auto visit = [&](int ax, int az) {
            if (ax < ix.minCx || ax > ix.maxCx || az < ix.minCz || az > ix.maxCz) return;
            ++looked;
            if (const PropIndex::Cell* c = ix.cell(ax, az)) {
                for (ObjectId p : c->props) consider(p);
            }
        };
        if (r == 0) {
            visit(cx, cz);
            continue;
        }
        for (int ax = cx - r; ax <= cx + r; ++ax) {
            visit(ax, cz - r);
            visit(ax, cz + r);
        }
        for (int az = cz - r + 1; az <= cz + r - 1; ++az) {
            visit(cx - r, az);
            visit(cx + r, az);
        }
    }
    return best;
}
