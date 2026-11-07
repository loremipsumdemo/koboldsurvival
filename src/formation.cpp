#include "formation.h"
#include <algorithm>
#include <cmath>
#include "terrain.h"

namespace {
constexpr float FULL_TURN = 6.2831853f;
constexpr int RECTANGLE_COLUMNS = 5;
constexpr float WEDGE_ROW_DEPTH = 0.87f;   // of the spacing, so a unit is as far from the row behind as from its neighbour
constexpr int MOST_FIT_RINGS = 8;
constexpr int MOST_SWAP_PASSES = 6;

float distSquared(Vec2 a, Vec2 b) {
    return (a.x - b.x) * (a.x - b.x) + (a.z - b.z) * (a.z - b.z);
}

// A row of this many, side by side, with its middle on x = 0
void addRow(std::vector<Vec2>& out, int count, float spacing, float z) {
    for (int i = 0; i < count; ++i) out.push_back(Vec2{ ((float)i - (float)(count - 1) * 0.5f) * spacing, z });
}

void centerOnMiddle(std::vector<Vec2>& offsets) {
    if (offsets.empty()) return;
    float low = offsets[0].z, high = offsets[0].z;
    for (const Vec2& o : offsets) {
        low = std::min(low, o.z);
        high = std::max(high, o.z);
    }
    const float middle = (low + high) * 0.5f;
    for (Vec2& o : offsets) o.z -= middle;
}
}

const char* formationLabelKey(FormationShape shape) {
    switch (shape) {
    case FormationShape::Line: return "UILABEL_FORMATION_LINE";
    case FormationShape::Circle: return "UILABEL_FORMATION_CIRCLE";
    case FormationShape::Wedge: return "UILABEL_FORMATION_WEDGE";
    default: return "UILABEL_FORMATION_RECTANGLE";
    }
}

std::vector<Vec2> formationOffsets(FormationShape shape, int count, float spacing, float width) {
    std::vector<Vec2> out;
    if (count <= 0) return out;
    out.reserve((size_t)count);
    switch (shape) {
    case FormationShape::Line:
        if (count > 1) spacing = std::max(spacing, width / (float)(count - 1));
        addRow(out, count, spacing, 0.0f);
        break;
    case FormationShape::Circle: {
        if (count == 1) {
            out.push_back(Vec2{ 0.0f, 0.0f });
            break;
        }
        // Round enough that neighbours keep their spacing, measured straight across
        const float radius = std::max(spacing / (2.0f * std::sin(FULL_TURN * 0.5f / (float)count)), width * 0.5f);
        for (int i = 0; i < count; ++i) {
            const float a = FULL_TURN * (float)i / (float)count;
            out.push_back(Vec2{ std::sin(a) * radius, -std::cos(a) * radius });
        }
        break;
    }
    case FormationShape::Wedge: {
        int rows = 0, held = 0;
        while (held < count) held += ++rows;
        if (rows > 1) spacing = std::max(spacing, width / (float)(rows - 1));
        int left = count;
        for (int r = 0; r < rows; ++r) {
            const int inRow = std::min(r + 1, left);
            addRow(out, inRow, spacing, (float)r * spacing * WEDGE_ROW_DEPTH);
            left -= inRow;
        }
        break;
    }
    default: {
        int columns = std::min(RECTANGLE_COLUMNS, count);
        if (width > 0.0f) columns = std::clamp((int)std::lround(width / spacing) + 1, 1, count);
        const int rows = (count + columns - 1) / columns;
        int left = count;
        for (int r = 0; r < rows; ++r) {
            const int inRow = std::min(columns, left);
            addRow(out, inRow, spacing, (float)r * spacing);
            left -= inRow;
        }
        break;
    }
    }
    centerOnMiddle(out);
    return out;
}

Vec2 formationSpot(Vec2 center, float headX, float headZ, Vec2 offset) {
    const float rightX = -headZ, rightZ = headX;
    return Vec2{ center.x + rightX * offset.x - headX * offset.z, center.z + rightZ * offset.x - headZ * offset.z };
}

std::vector<Vec2> fitFormation(const Terrain& t, Vec2 center, float headX, float headZ, const std::vector<Vec2>& offsets, float spacing) {
    const Vec2 middle = t.snap(center);
    std::vector<Vec2> spots;
    std::vector<char> placed(offsets.size(), 0);
    spots.assign(offsets.size(), middle);
    const float apart = (spacing * 0.95f) * (spacing * 0.95f);
    const auto usable = [&](Vec2 at) {
        if (!t.isFree(at.x, at.z) || !t.lineClear(middle, at)) return false;
        for (size_t i = 0; i < spots.size(); ++i) {
            if (placed[i] && distSquared(spots[i], at) < apart) return false;
        }
        return true;
    };
    // Those that fit where they should go first, so one moved over never takes the place of one that fits
    for (size_t i = 0; i < offsets.size(); ++i) {
        const Vec2 wanted = formationSpot(middle, headX, headZ, offsets[i]);
        if (usable(wanted)) {
            spots[i] = wanted;
            placed[i] = 1;
        }
    }
    for (size_t i = 0; i < offsets.size(); ++i) {
        if (placed[i]) continue;
        const Vec2 wanted = formationSpot(middle, headX, headZ, offsets[i]);
        for (int ring = 1; ring <= MOST_FIT_RINGS && !placed[i]; ++ring) {
            const int around = 8 * ring;
            for (int k = 0; k < around; ++k) {
                const float a = FULL_TURN * (float)k / (float)around;
                const Vec2 at{ wanted.x + std::cos(a) * spacing * (float)ring, wanted.z + std::sin(a) * spacing * (float)ring };
                if (usable(at)) {
                    spots[i] = at;
                    placed[i] = 1;
                    break;
                }
            }
        }
    }
    return spots;
}

std::vector<int> assignFormationSlots(const std::vector<Vec2>& units, const std::vector<Vec2>& spots, float approachX, float approachZ) {
    const size_t count = std::min(units.size(), spots.size());
    std::vector<size_t> order(spots.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return spots[a].x * approachX + spots[a].z * approachZ > spots[b].x * approachX + spots[b].z * approachZ;
    });
    std::vector<int> slotOf(units.size(), -1);
    std::vector<char> taken(units.size(), 0);
    for (size_t s = 0; s < count; ++s) {
        int best = -1;
        float bestD2 = 0.0f;
        for (size_t u = 0; u < count; ++u) {
            if (taken[u]) continue;
            const float d2 = distSquared(units[u], spots[order[s]]);
            if (best < 0 || d2 < bestD2) {
                best = (int)u;
                bestD2 = d2;
            }
        }
        taken[(size_t)best] = 1;
        slotOf[(size_t)best] = (int)order[s];
    }
    for (int pass = 0; pass < MOST_SWAP_PASSES; ++pass) {
        bool swapped = false;
        for (size_t a = 0; a < count; ++a) {
            for (size_t b = a + 1; b < count; ++b) {
                const Vec2 sa = spots[(size_t)slotOf[a]], sb = spots[(size_t)slotOf[b]];
                const float now = distSquared(units[a], sa) + distSquared(units[b], sb);
                const float then = distSquared(units[a], sb) + distSquared(units[b], sa);
                if (then + 1e-4f < now) {
                    std::swap(slotOf[a], slotOf[b]);
                    swapped = true;
                }
            }
        }
        if (!swapped) break;
    }
    return slotOf;
}
