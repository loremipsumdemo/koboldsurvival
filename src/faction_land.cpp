#include "systems_internal.h"

using namespace systems_detail;

// Where the 16 colored factions go, worked out from the ground alone (the same answer for the same map, so New Game can show it).
//
//  1. Spread: on the biggest landmass (a piece of open ground that is all joined up), as many as fit with their claims clear of one another,
//     as evenly spaced as can be made (the farthest-point pick, then each moved to the middle of the ground nearest it). The ones that
//     do not fit go on the next biggest landmass, and so on down.
//  2. Scattered: those left over each get a random spot with at least an Altar's claim of walkable ground (half of it), clear of the
//     claims already placed.
//  3. Nomadic: those still left are put at random on walkable ground outside every claim, with no Altar.
//  4. Eliminated: where not even that is to be had.
// The factions are given places in the order of the loop, going round the middle of each landmass, so that a faction's neighbors on the
// loop (its allies) are its neighbors on the land.

namespace {
constexpr int CELL = 8;                                       // the grid the ground is looked over on, in tiles
constexpr float MIN_SEP = 2.0f * CLAIM_RADIUS + 6.0f;         // no two camps nearer than this (their claims stay clear, with a little between)
constexpr float SCATTER_SHARE = 0.5f;                         // the share of an Altar's claim that must be walkable, for a scattered spot
constexpr float NOMAD_CLEARANCE = CLAIM_RADIUS + 6.0f;        // how far a nomadic band keeps from every camp
constexpr int MOST_SPAN = 2048;                               // a map bigger than this is looked over this far across, round the middle
constexpr int RELAX_ROUNDS = 8;

struct Cell {
    float x = 0.0f, z = 0.0f;
    int area = 0;
};

float distSqOf(const Cell& a, float x, float z) { return (a.x - x) * (a.x - x) + (a.z - z) * (a.z - z); }

// Open ground all round, CAMP_ROOM tiles out
bool roomyAt(const Terrain& t, float cx, float cz) {
    const int x0 = (int)std::floor(cx), z0 = (int)std::floor(cz);
    const int r = (int)std::ceil(CAMP_ROOM);
    // A quick look at the compass points first, since most of the ground fails there
    static const int DIRS[8][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }, { 1, 1 }, { -1, 1 }, { 1, -1 }, { -1, -1 } };
    if (!t.walkable(x0, z0)) return false;
    for (const auto& d : DIRS) {
        if (!t.walkable(x0 + d[0] * r, z0 + d[1] * r)) return false;
    }
    for (int dz = -r; dz <= r; ++dz) {
        for (int dx = -r; dx <= r; ++dx) {
            if ((float)(dx * dx + dz * dz) > CAMP_ROOM * CAMP_ROOM) continue;
            if (!t.walkable(x0 + dx, z0 + dz)) return false;
        }
    }
    return true;
}

// The share of the ground within an Altar's claim of a spot that is walkable, looked at every few tiles
float claimShare(const Terrain& t, float cx, float cz) {
    constexpr int STEP = 3;
    const int r = (int)std::ceil(CLAIM_RADIUS);
    int seen = 0, open = 0;
    for (int dz = -r; dz <= r; dz += STEP) {
        for (int dx = -r; dx <= r; dx += STEP) {
            if ((float)(dx * dx + dz * dz) > CLAIM_RADIUS * CLAIM_RADIUS) continue;
            ++seen;
            if (t.walkable((int)std::floor(cx) + dx, (int)std::floor(cz) + dz)) ++open;
        }
    }
    return seen > 0 ? (float)open / (float)seen : 0.0f;
}

float minSeparation(const std::vector<Cell>& cells, const std::vector<int>& picked) {
    float best = 1.0e30f;
    for (size_t i = 0; i < picked.size(); ++i) {
        for (size_t j = i + 1; j < picked.size(); ++j) {
            best = std::min(best, std::sqrt(distSqOf(cells[(size_t)picked[i]], cells[(size_t)picked[j]].x, cells[(size_t)picked[j]].z)));
        }
    }
    return best;
}

// Up to `want` of the cells in the group, as far apart as can be: each next one is the cell farthest from those already picked, for as
// long as that is MIN_SEP or more (so those it gives all fit, and no more would). Then each is moved to the cell of its own share of the
// ground that is nearest the share's middle, if that keeps them all MIN_SEP apart.
std::vector<int> spread(const std::vector<Cell>& cells, const std::vector<int>& group, int want) {
    std::vector<int> picked;
    if (group.empty() || want <= 0) return picked;
    float mx = 0.0f, mz = 0.0f;
    for (int i : group) {
        mx += cells[(size_t)i].x;
        mz += cells[(size_t)i].z;
    }
    mx /= (float)group.size();
    mz /= (float)group.size();
    int first = group[0];
    for (int i : group) {
        if (distSqOf(cells[(size_t)i], mx, mz) > distSqOf(cells[(size_t)first], mx, mz)) first = i;
    }
    std::vector<float> nearest(group.size(), 1.0e30f);
    int next = first;
    while ((int)picked.size() < want) {
        picked.push_back(next);
        const Cell& p = cells[(size_t)next];
        int far = -1;
        float farD = -1.0f;
        for (size_t g = 0; g < group.size(); ++g) {
            nearest[g] = std::min(nearest[g], distSqOf(cells[(size_t)group[g]], p.x, p.z));
            if (nearest[g] > farD) {
                farD = nearest[g];
                far = group[g];
            }
        }
        if (far < 0 || std::sqrt(farD) < MIN_SEP) break;
        next = far;
    }
    if (picked.size() < 2) return picked;

    for (int round = 0; round < RELAX_ROUNDS; ++round) {
        std::vector<double> sx(picked.size(), 0.0), sz(picked.size(), 0.0);
        std::vector<int> count(picked.size(), 0), owner(group.size(), 0);
        for (size_t g = 0; g < group.size(); ++g) {
            const Cell& c = cells[(size_t)group[g]];
            size_t best = 0;
            float bestD = 1.0e30f;
            for (size_t k = 0; k < picked.size(); ++k) {
                const float d = distSqOf(c, cells[(size_t)picked[k]].x, cells[(size_t)picked[k]].z);
                if (d < bestD) {
                    bestD = d;
                    best = k;
                }
            }
            owner[g] = (int)best;
            sx[best] += c.x;
            sz[best] += c.z;
            ++count[best];
        }
        std::vector<int> moved = picked;
        std::vector<float> nearestToMiddle(picked.size(), 1.0e30f);
        for (size_t g = 0; g < group.size(); ++g) {
            const size_t k = (size_t)owner[g];
            if (count[k] == 0) continue;
            const float d = distSqOf(cells[(size_t)group[g]], (float)(sx[k] / count[k]), (float)(sz[k] / count[k]));
            if (d < nearestToMiddle[k]) {
                nearestToMiddle[k] = d;
                moved[k] = group[g];
            }
        }
        if (moved == picked || minSeparation(cells, moved) < MIN_SEP) break;
        picked = moved;
    }
    return picked;
}
}

FactionLandPlan planFactionLand(Terrain& t, const std::atomic<bool>* cancel) {
    FactionLandPlan out;
    out.slots.assign((size_t)PALETTE_COUNT, FactionLanding{});
    auto stop = [&]() { return cancel && cancel->load(); };

    // The ground looked over: all of the map, or on a map bigger than MOST_SPAN a stretch of it round the middle
    const std::vector<int> sizes = t.areaSizes();
    int x0 = 0, z0 = 0, x1 = t.size(), z1 = t.size();
    if (t.size() > MOST_SPAN) {
        const Vec2 mid = findSpawn(t);
        x0 = std::clamp((int)mid.x - MOST_SPAN / 2, 0, t.size() - MOST_SPAN);
        z0 = std::clamp((int)mid.z - MOST_SPAN / 2, 0, t.size() - MOST_SPAN);
        x1 = x0 + MOST_SPAN;
        z1 = z0 + MOST_SPAN;
    }

    std::vector<Cell> cells;      // every cell with walkable ground at its middle
    std::vector<char> roomy;      // and whether it has open ground all round
    for (int z = z0; z + CELL <= z1; z += CELL) {
        if (stop()) return FactionLandPlan{};
        for (int x = x0; x + CELL <= x1; x += CELL) {
            const int cx = x + CELL / 2, cz = z + CELL / 2;
            if (!t.walkable(cx, cz)) continue;
            Cell c;
            c.x = (float)cx + 0.5f;
            c.z = (float)cz + 0.5f;
            c.area = t.areaAt(cx, cz);
            cells.push_back(c);
            roomy.push_back(roomyAt(t, c.x, c.z) ? 1 : 0);
        }
    }

    // The landmasses with room for a camp, the biggest first
    std::map<int, std::vector<int>> byArea;
    for (size_t i = 0; i < cells.size(); ++i) {
        if (roomy[i]) {
            byArea[cells[i].area].push_back((int)i);
            out.open.push_back(Vec2{ cells[i].x, cells[i].z });
        }
    }
    std::vector<int> order;
    for (const auto& [area, group] : byArea) order.push_back(area);
    auto landSize = [&](int area) { return area >= 0 && (size_t)area < sizes.size() ? sizes[(size_t)area] : 0; };
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        if (landSize(a) != landSize(b)) return landSize(a) > landSize(b);
        return byArea[a].size() > byArea[b].size();
    });

    std::vector<int> left;   // the slots still to place, in the order of the loop
    for (int s = 0; s < PALETTE_COUNT; ++s) left.push_back(s);
    std::vector<Vec2> placed;

    // 1. Spread, a landmass at a time
    for (int area : order) {
        if (left.empty() || stop()) break;
        const std::vector<int> picked = spread(cells, byArea[area], (int)left.size());
        if (picked.empty()) continue;
        float mx = 0.0f, mz = 0.0f;
        for (int i : picked) {
            mx += cells[(size_t)i].x;
            mz += cells[(size_t)i].z;
        }
        mx /= (float)picked.size();
        mz /= (float)picked.size();
        std::vector<int> round = picked;
        std::sort(round.begin(), round.end(), [&](int a, int b) {
            return std::atan2(cells[(size_t)a].z - mz, cells[(size_t)a].x - mx) < std::atan2(cells[(size_t)b].z - mz, cells[(size_t)b].x - mx);
        });
        for (size_t k = 0; k < round.size(); ++k) {
            const Cell& c = cells[(size_t)round[k]];
            out.slots[(size_t)left[k]] = FactionLanding{ LandTier::Spread, c.x, c.z };
            placed.push_back(Vec2{ c.x, c.z });
        }
        left.erase(left.begin(), left.begin() + (long)round.size());
    }
    if (stop()) return FactionLandPlan{};

    // The rest are placed at random, from the same shuffle every time for the same map
    Rng rng;
    rng.gen.seed(0x5eed1234u ^ (unsigned)t.size());
    std::vector<int> shuffled(cells.size());
    for (size_t i = 0; i < shuffled.size(); ++i) shuffled[i] = (int)i;
    for (size_t i = shuffled.size(); i > 1; --i) std::swap(shuffled[i - 1], shuffled[(size_t)rng.range(0, (int)i - 1)]);
    auto clearOf = [&](const Cell& c, float gap) {
        for (const Vec2& p : placed) {
            if (distSqOf(c, p.x, p.z) < gap * gap) return false;
        }
        return true;
    };

    // 2. Scattered
    std::vector<signed char> share(cells.size(), -1);   // whether a cell has the walkable share, worked out once
    for (size_t li = 0; li < left.size();) {
        if (stop()) return FactionLandPlan{};
        bool found = false;
        for (int i : shuffled) {
            const Cell& c = cells[(size_t)i];
            if (!clearOf(c, 2.0f * CLAIM_RADIUS)) continue;
            if (share[(size_t)i] < 0) share[(size_t)i] = claimShare(t, c.x, c.z) >= SCATTER_SHARE ? 1 : 0;
            if (!share[(size_t)i]) continue;
            out.slots[(size_t)left[li]] = FactionLanding{ LandTier::Scattered, c.x, c.z };
            placed.push_back(Vec2{ c.x, c.z });
            left.erase(left.begin() + (long)li);
            found = true;
            break;
        }
        if (!found) break;
    }

    // 3. Nomadic, and 4. eliminated
    for (int slot : left) {
        bool found = false;
        for (int i : shuffled) {
            const Cell& c = cells[(size_t)i];
            if (!clearOf(c, NOMAD_CLEARANCE)) continue;
            out.slots[(size_t)slot] = FactionLanding{ LandTier::Nomadic, c.x, c.z };
            found = true;
            break;
        }
        if (!found) out.slots[(size_t)slot] = FactionLanding{ LandTier::Eliminated, 0.0f, 0.0f };
    }
    return out;
}
