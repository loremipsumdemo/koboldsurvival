#include "patrons.h"
#include "building.h"
#include "gates.h"
#include "profile.h"
#include "prop_index.h"
#include <algorithm>
#include <cmath>
#include <deque>
#include <unordered_set>
#include "airship.h"
#include "stuck.h"
#include "encumbrance.h"
#include "experience.h"
#include "factions.h"
#include "hostility.h"
#include "inventory.h"
#include "items.h"
#include "calendar.h"
#include "squads.h"
#include "systems_internal.h"
#include "world_labels.h"
#include "building_internal.h"

using namespace systems_detail;
using namespace building_detail;


// Climbing ladders and stairs, walking routes across floors and single-unit walks (split from building.cpp)

Vec2 nearestFloorSpot(World& w, ObjectId, Vec2 target, int level) {
    const BuildingMap& m = cachedBuildingMap(w);
    Vec2 best = target;
    float bestD2 = -1.0f;
    std::vector<long long> tiles;
    for (const auto& [lk, e] : m.floor) tiles.push_back(lk);
    for (const auto& [lk, e] : m.stairTop) tiles.push_back(lk);
    for (const auto& [lk, e] : m.perimTop) tiles.push_back(lk);
    for (const long long lk : tiles) {
        if ((int)(lk % 16) != level) continue;
        const long long tk = lk / 16;
        const int x = keyX(tk), z = keyZ(tk);
        if (!floorWalkable(w, m, x, z, level)) continue;
        const float cx = x + 0.5f, cz = z + 0.5f;
        const float d2 = (cx - target.x) * (cx - target.x) + (cz - target.z) * (cz - target.z);
        if (bestD2 < 0.0f || d2 < bestD2) {
            bestD2 = d2;
            best = Vec2{ cx, cz };
        }
    }
    return best;
}

bool canClimbUp(World& w, ObjectId ladder) {
    const PropInfo* info = infoOf(w, ladder);
    if (!info || !info->ladder || w.has<Blueprint>(ladder)) return false;
    const BuildingMap& m = cachedBuildingMap(w);
    Vec2 top;
    return ladderTop(w, m, ladder, top);
}

Vec2 climbFoot(World& w, ObjectId ladder) {
    if (isStairs(w, ladder)) {
        const BuildingMap& m = cachedBuildingMap(w);
        int xs[4], zs[4];
        stairsTiles(w, ladder, xs, zs);
        // In front of the bottom step if it is open, else beside it
        return nearestLanding(w, m, ladder, Vec2{ 2.0f * xs[0] - xs[1] + 0.5f, 2.0f * zs[0] - zs[1] + 0.5f });
    }
    const Transform* lt = w.get<Transform>(ladder);
    return lt ? Vec2{ lt->x, lt->z } : Vec2{ 0.0f, 0.0f };
}

bool climbTop(World& w, ObjectId ladder, Vec2& out) {
    const BuildingMap& m = cachedBuildingMap(w);
    return ladderTop(w, m, ladder, out);
}

bool atClimbSpot(World& w, ObjectId u, ObjectId ladder) {
    const Transform* ut = w.get<Transform>(u);
    const Transform* lt = w.get<Transform>(ladder);
    if (!ut || !lt) return false;
    const int ls = storeyOf(w, ladder), us = storeyOf(w, u);
    if (us != ls && us != ls + 1) return false;
    // The tile it goes from: the ladder's own (its head is beside it), or the stairs' bottom step (from an open floor tile
    // beside it) or their top step
    int ax = tileOf(lt->x), az = tileOf(lt->z);
    const int ux = tileOf(ut->x), uz = tileOf(ut->z);
    const BuildingMap& m = cachedBuildingMap(w);
    if (const PropInfo* li = infoOf(w, ladder); li && li->slope) {
        // A Slope stands on open ground: from beside its bottom step on the ground, or from its top step (or the walkway beside it) above
        int xs[4], zs[4];
        stairsTiles(w, ladder, xs, zs);
        const int i = us == ls ? 0 : 2;
        if (std::max(std::abs(ux - xs[i]), std::abs(uz - zs[i])) > 1) return false;
        return us != ls || stairsFootOpen(w, m, ux, uz, ls, true);
    }
    if (isStairs(w, ladder)) {
        int xs[4], zs[4];
        stairsTiles(w, ladder, xs, zs);
        const int i = us == ls ? 0 : 2;
        ax = xs[i];
        az = zs[i];
        if (us == ls && !stairsFootOpen(w, m, ux, uz, ls)) return false;
    }
    if (std::max(std::abs(ux - ax), std::abs(uz - az)) > 1) return false;
    // Inside the building: on one of its foundation tiles, so not outside its walls
    if (!m.foundation.count(tileKey(ux, uz))) return false;
    const std::vector<long long> tiles = buildingTiles(m, ax, az);
    return std::find(tiles.begin(), tiles.end(), tileKey(ux, uz)) != tiles.end();
}

namespace {
// Stairs walked, at the unit's own pace: from where it stands onto the bottom step and up to the top one, or onto the
// top step, down to the bottom one and off onto the floor beside it. Each step is a third of the way up; crossing from
// one height to the next is a hop, noted by how far along the walk it comes.
void planStairsWalk(World& w, const Terrain& t, ObjectId u, ObjectId stairs, bool down, const Vec2* off, Climbing& c) {
    const Transform* ut = w.get<Transform>(u);
    const BuildingMap& m = cachedBuildingMap(w);
    int xs[4], zs[4];
    stairsTiles(w, stairs, xs, zs);
    const Vec2 bottom{ xs[0] + 0.5f, zs[0] + 0.5f }, top{ xs[2] + 0.5f, zs[2] + 0.5f };
    const Vec2 here{ ut->x, ut->z };
    if (down) c.path = { here, top, bottom, off ? *off : nearestLanding(w, m, stairs, Vec2{ 2.0f * bottom.x - xs[1] - 0.5f, 2.0f * bottom.z - zs[1] - 0.5f }) };
    else c.path = { here, bottom, top };
    const int ls = storeyOf(w, stairs);
    const float ground = t.heightAt(bottom.x, bottom.z);
    const float low = ls == 0 ? t.standHeight(bottom.x, bottom.z) - ground : storeyBaseHeight(ls);
    const float rise = storeyBaseHeight(ls + 1) - low;
    float len = 0.0f;
    for (size_t i = 1; i < c.path.size(); ++i) len += std::hypot(c.path[i].x - c.path[i - 1].x, c.path[i].z - c.path[i - 1].z);
    // The height of the step under a point, or off the stairs, the floor it set off from (first half) or gets off onto
    auto heightAt = [&](Vec2 p, float along) {
        const int x = tileOf(p.x), z = tileOf(p.z);
        for (int i = 0; i < 3; ++i) {
            if (x == xs[i] && z == zs[i]) return low + rise * (float)(i + 1) / 3.0f;
        }
        return (along < len * 0.5f) == down ? low + rise : low;
    };
    c.startHeight = heightAt(here, 0.0f);
    c.hops.clear();
    float along = 0.0f, last = c.startHeight;
    constexpr float STEP = 0.02f;
    for (size_t i = 1; i < c.path.size(); ++i) {
        const Vec2 a = c.path[i - 1], b = c.path[i];
        const float seg = std::hypot(b.x - a.x, b.z - a.z);
        for (float s = STEP; s <= seg + 1e-4f; s += STEP) {
            const float k = std::min(1.0f, s / std::max(seg, 1e-4f));
            const float h = heightAt(Vec2{ a.x + (b.x - a.x) * k, a.z + (b.z - a.z) * k }, along + s);
            if (std::fabs(h - last) > 1e-3f) {
                c.hops.push_back(Climbing::Hop{ along + s - STEP * 0.5f, last, h });
                last = h;
            }
        }
        along += seg;
    }
    if (const PropInfo* si = infoOf(w, stairs); si && si->slope) {
        c.hops.clear();
        c.smooth = true;
        c.startHeight = down ? low + rise : low;
        c.endHeight = down ? low : low + rise;
    }
    c.whole = c.left = std::max(0.2f, len / std::max(0.3f, walkSpeedOf(w, u)));
}
}

bool startClimb(World& w, const Terrain& t, ObjectId u, ObjectId ladder, const Vec2* off) {
    const Transform* lt = w.get<Transform>(ladder);
    const Transform* ut = w.get<Transform>(u);
    const PropInfo* info = infoOf(w, ladder);
    if (!lt || !ut || !info || !info->ladder || w.has<Blueprint>(ladder) || w.has<Climbing>(u) || outCold(w, u)) return false;
    const int ls = storeyOf(w, ladder), us = storeyOf(w, u);
    const bool routed = w.has<Route>(u);
    const bool down = us == ls + 1;
    if (down) {
        if (!atClimbSpot(w, u, ladder)) return false;
    } else if (us != ls || !canClimbUp(w, ladder) || !atClimbSpot(w, u, ladder)) {
        return false;
    }
    Climbing c{ ladder, CLIMB_SECONDS, down, CLIMB_SECONDS };
    if (info->stairs) planStairsWalk(w, t, u, ladder, down, off, c);
    if (!down) {
        if (w.has<Carrying>(u)) putDown(w, u);
        leaveSquad(w, u);
    }
    if (!routed) w.remove<SoloMove>(u);
    w.add(u, std::move(c));
    return true;
}

// ---- Up the outside of a Perimeter Wall

namespace {
// A map bigger than this has no navigation regions that count what buildings close (nav_regions.cpp)
constexpr int NAV_MAP_MOST = 2048;

// Whether a unit has no way to any Gate of its faction, as a walk there would be planned: over the ground, or up a Slope and along the
// walkways. A faction with no finished Gate has none to reach.
bool shutOutOfGates(World& w, const Terrain& t, ObjectId u) {
    const Transform* ut = w.get<Transform>(u);
    if (!ut) return false;
    for (ObjectId g : ownGates(w, factionOf(w, u))) {
        const Vec2 front = gateSpot(w, g, 1.5f);
        if (t.size() > NAV_MAP_MOST) {
            // No regions to plan over, so no way over the walls by a Slope either: the ground is searched
            if (stuckReachable(w, t, Vec2{ ut->x, ut->z }, front)) return false;
            continue;
        }
        Route way;
        if (planRoute(w, t, u, front, 0, way)) return false;
    }
    return true;
}

// The walkway tile of this wall nearest a spot (floor 1, as the Slopes come out on it), or false if it has none
bool wallTop(World& w, const BuildingMap& m, ObjectId wall, Vec2 near, Vec2& out) {
    int x0 = 0, z0 = 0, x1 = 0, z1 = 0;
    pieceTiles(w, wall, x0, z0, x1, z1);
    bool found = false;
    float best = 0.0f;
    for (int z = z0; z <= z1; ++z) {
        for (int x = x0; x <= x1; ++x) {
            if (mapGet(m.perimTop, levelKey(x, z, 1)) != wall || !floorWalkable(w, m, x, z, 1)) continue;
            const float d = std::hypot((float)x + 0.5f - near.x, (float)z + 0.5f - near.z);
            if (!found || d < best) {
                found = true;
                best = d;
                out = Vec2{ (float)x + 0.5f, (float)z + 0.5f };
            }
        }
    }
    return found;
}

// Every test of canScalePerimeter but the one about the Gates, which costs the most and is the same for every wall
bool scalableHere(World& w, const Terrain& t, ObjectId u, ObjectId wall) {
    const Prop* wp = w.get<Prop>(wall);
    if (!wp || !w.has<Transform>(u) || !w.has<Entity>(u) || w.has<Blueprint>(wall)) return false;
    // A finished Perimeter Wall of the faction the unit is of: no other faction's unit may climb it
    const PropInfo& info = propInfo(wp->kind);
    if (!info.perimeter || !info.solid || systems_detail::ownerOf(w, wall) != factionOf(w, u)) return false;
    // On its feet on the ground, and not held by anything
    if (w.has<Mob>(u) || outCold(w, u) || w.has<Carried>(u) || w.has<Occupying>(u) || w.has<Climbing>(u) || w.has<Upstairs>(u) ||
        airshipOf(w, u) != NO_OBJECT) {
        return false;
    }
    // It can walk to the wall's outer face, and the wall has a walkway to climb onto
    Vec2 foot{ 0.0f, 0.0f }, top{ 0.0f, 0.0f };
    return wallFoot(w, t, u, wall, foot) && wallTop(w, cachedBuildingMap(w), wall, foot, top);
}

// Off the climb up a wall: the unit steps onto its walkway, nearest where it climbed. A unit of an NPC faction goes straight on down, on
// the inside (yours stays up there until you bring it down); with no ground inside to come down on, it is back where it climbed from.
void landOnWall(World& w, Terrain& t, const BuildingMap& m, ObjectId u, ObjectId wall) {
    Transform* ut = w.get<Transform>(u);
    Vec2 top{ 0.0f, 0.0f };
    // The wall or its walkway is gone: it stays on the ground
    if (!ut || !w.has<Prop>(wall) || !wallTop(w, m, wall, Vec2{ ut->x, ut->z }, top)) return;
    const Vec2 was{ ut->x, ut->z };
    ut->x = top.x;
    ut->z = top.z;
    ut->y = t.heightAt(top.x, top.z) + storeyBaseHeight(1);
    w.add(u, Upstairs{ mapGet(m.foundation, tileKey(tileOf(top.x), tileOf(top.z))), 1 });
    queueSoundFrom(w, Sfx::Occupy, wall);
    if (factionOf(w, u) == FACTION_PLAYER || climbDownPerimeter(w, t, u)) return;
    ut->x = was.x;
    ut->z = was.z;
    ut->y = t.standHeight(was.x, was.z);
    w.remove<Upstairs>(u);
}
}

bool walkOverGround(World& w, const Terrain& t, Vec2 from, Vec2 to) {
    if (t.size() > NAV_MAP_MOST) return stuckReachable(w, t, from, to);
    return t.connected(tileOf(from.x), tileOf(from.z), tileOf(to.x), tileOf(to.z));
}

bool wallFoot(World& w, const Terrain& t, ObjectId u, ObjectId wall, Vec2& out) {
    const Transform* ut = w.get<Transform>(u);
    if (!ut || !w.has<Prop>(wall)) return false;
    // The wall's outer face is two tiles wide: the middle of the tile against each half of it, the one it can reach that is nearer first
    const Vec2 face = gateSpot(w, wall, 0.5f), outward = gateOutward(w, wall);
    const Vec2 along{ -outward.z, outward.x };
    bool found = false;
    float best = 0.0f;
    for (const float s : { -0.5f, 0.5f }) {
        const int sx = tileOf(face.x + along.x * s), sz = tileOf(face.z + along.z * s);
        if (!t.inBounds(sx, sz) || !t.walkable(sx, sz)) continue;
        const Vec2 spot{ (float)sx + 0.5f, (float)sz + 0.5f };
        if (!walkOverGround(w, t, Vec2{ ut->x, ut->z }, spot)) continue;
        const float d = std::hypot(spot.x - ut->x, spot.z - ut->z);
        if (!found || d < best) {
            found = true;
            best = d;
            out = spot;
        }
    }
    return found;
}

bool canScalePerimeter(World& w, const Terrain& t, ObjectId u, ObjectId wall) {
    return scalableHere(w, t, u, wall) && shutOutOfGates(w, t, u);
}

ObjectId scalableWall(World& w, const Terrain& t, ObjectId u) {
    const Transform* ut = w.get<Transform>(u);
    if (!ut) return NO_OBJECT;
    std::vector<ObjectId> nearby;
    propsNear(w, ut->x, ut->z, WALL_CLIMB_SEARCH + propIndex(w).widestReach, nearby);
    // The nearest first: the Gates are only asked about once there is a wall it could climb
    std::vector<std::pair<float, ObjectId>> walls;
    for (ObjectId e : nearby) {
        const Prop* p = w.get<Prop>(e);
        if (!p || !propInfo(p->kind).perimeter || !propInfo(p->kind).solid) continue;
        const float gap = gateGap(w, e, ut->x, ut->z);
        if (gap <= WALL_CLIMB_SEARCH) walls.push_back({ gap, e });
    }
    std::sort(walls.begin(), walls.end());
    // Only the nearest few: a unit shut in against its wall has its wall among them
    constexpr size_t MOST_LOOKED_AT = 8;
    for (size_t i = 0; i < walls.size() && i < MOST_LOOKED_AT; ++i) {
        if (scalableHere(w, t, u, walls[i].second)) return shutOutOfGates(w, t, u) ? walls[i].second : NO_OBJECT;
    }
    return NO_OBJECT;
}

bool startWallClimb(World& w, const Terrain& t, ObjectId u, ObjectId wall) {
    const Transform* ut = w.get<Transform>(u);
    if (!ut || !canScalePerimeter(w, t, u, wall) || gateGap(w, wall, ut->x, ut->z) > WALL_CLIMB_REACH) return false;
    Climbing c{ wall, WALL_CLIMB_SECONDS, false, WALL_CLIMB_SECONDS };
    c.wall = true;
    if (w.has<Carrying>(u)) putDown(w, u);
    leaveSquad(w, u);
    w.remove<SoloMove>(u);
    w.remove<Route>(u);
    w.add(u, std::move(c));
    return true;
}

void moveUpstairs(World& w, Terrain& t, ObjectId u, Vec2 target) {
    if (!w.has<Upstairs>(u)) return;
    clearOrders(w, u);
    w.remove<IdleSpot>(u);
    walkSoloTo(w, t, u, target, -1);
}

void moveToFloor(World& w, Terrain& t, ObjectId u, Vec2 target, int storey) {
    if (!w.has<Commandable>(u) || cannotTakeOrder(w, u)) return;
    clearOrders(w, u);
    w.remove<IdleSpot>(u);
    walkSoloTo(w, t, u, target, storey);
}

void climbSystem(World& w, Terrain& t, float dt) {
    PROFILE_SCOPE("build: climbSystem");
    std::vector<ObjectId> done;
    for (ObjectId u : w.view<Climbing, Transform>()) {
        Climbing& c = *w.get<Climbing>(u);
        const Transform* lt = w.get<Transform>(c.ladder);
        if (!lt || outCold(w, u) || w.has<Carried>(u)) {
            w.remove<Climbing>(u);
            continue;
        }
        c.left -= dt;
        if (c.left <= 0.0f) done.push_back(u);
        if (isStairs(w, c.ladder) && c.whole > 0.0f && c.path.size() >= 2) {
            // Along its walk at its pace, at the height of the step under it, with a quick hop at each change of height
            // that goes a little past the new height and lands on it
            float len = 0.0f;
            for (size_t i = 1; i < c.path.size(); ++i) len += std::hypot(c.path[i].x - c.path[i - 1].x, c.path[i].z - c.path[i - 1].z);
            const float gone = std::clamp(1.0f - c.left / c.whole, 0.0f, 1.0f) * len;
            Transform* ut = w.get<Transform>(u);
            float left = gone;
            for (size_t i = 1; i < c.path.size(); ++i) {
                const Vec2 a = c.path[i - 1], b = c.path[i];
                const float seg = std::hypot(b.x - a.x, b.z - a.z);
                if (left <= seg || i + 1 == c.path.size()) {
                    const float k = std::clamp(left / std::max(seg, 1e-4f), 0.0f, 1.0f);
                    ut->x = a.x + (b.x - a.x) * k;
                    ut->z = a.z + (b.z - a.z) * k;
                    break;
                }
                left -= seg;
            }
            constexpr float HOP_SPAN = 0.22f, HOP_OVER = 0.25f;
            float y = c.startHeight;
            static const std::vector<Climbing::Hop> NO_HOPS;
            if (c.smooth) y = c.startHeight + (c.endHeight - c.startHeight) * std::clamp(gone / std::max(len, 1e-4f), 0.0f, 1.0f);
            for (const Climbing::Hop& h : c.smooth ? NO_HOPS : c.hops) {
                if (gone >= h.at + HOP_SPAN) {
                    y = h.to;
                    continue;
                }
                if (gone > h.at - HOP_SPAN) {
                    const float a = (gone - (h.at - HOP_SPAN)) / (2.0f * HOP_SPAN);
                    const float d = h.to - h.from, over = std::max(d, 0.0f) * 0.5f + HOP_OVER;
                    y = h.from + d * a + over * 4.0f * a * (1.0f - a);
                }
                break;
            }
            int xs[4], zs[4];
            stairsTiles(w, c.ladder, xs, zs);
            ut->y = t.heightAt(xs[0] + 0.5f, zs[0] + 0.5f) + y;
        }
    }
    // A unit off a ladder or stairs: at their foot on their floor, or at their head on the floor above
    // (stairs: the tile beside the bottom step it was heading for, or else the one nearest it)
    auto land = [&](const BuildingMap& m, ObjectId u, ObjectId ladder, bool down, const Vec2* off) {
        const Transform lt = *w.get<Transform>(ladder);
        const int ls = storeyOf(w, ladder);
        Transform* ut = w.get<Transform>(u);
        if (down) {
            const Vec2 foot = !isStairs(w, ladder) ? climbFoot(w, ladder) : off ? *off : nearestLanding(w, m, ladder, Vec2{ ut->x, ut->z });
            ut->x = foot.x;
            ut->z = foot.z;
            if (ls == 0) {
                w.remove<Upstairs>(u);
                ut->y = t.standHeight(ut->x, ut->z);
            } else {
                w.add(u, Upstairs{ mapGet(m.foundation, tileKey(tileOf(lt.x), tileOf(lt.z))), ls });
                ut->y = t.heightAt(ut->x, ut->z) + storeyBaseHeight(ls);
            }
        } else {
            Vec2 spot{ lt.x, lt.z };
            ladderTop(w, m, ladder, spot);
            ut->x = spot.x;
            ut->z = spot.z;
            ut->y = t.heightAt(spot.x, spot.z) + storeyBaseHeight(ls + 1);
            w.add(u, Upstairs{ mapGet(m.foundation, tileKey(tileOf(spot.x), tileOf(spot.z))), ls + 1 });
        }
    };
    if (!done.empty()) {
        const BuildingMap& m = cachedBuildingMap(w);
        for (ObjectId u : done) {
            const Climbing c = *w.get<Climbing>(u);
            w.remove<Climbing>(u);
            w.remove<IdleSpot>(u);
            if (!w.has<Route>(u)) w.remove<SoloMove>(u);
            if (c.wall) {
                landOnWall(w, t, m, u, c.ladder);
                continue;
            }
            land(m, u, c.ladder, c.down, c.down && !c.path.empty() ? &c.path.back() : nullptr);
            queueSoundFrom(w, Sfx::Occupy, c.ladder);
        }
    }
    // Units stopped partway up or down stairs, as by a new order: they step off at the nearer end, and the walk they were
    // just given is planned again from there
    {
        StairWalkers& sw = w.resource<StairWalkers>();
        std::vector<std::pair<ObjectId, ObjectId>> stopped;
        for (const auto& [u, stairs] : sw.units) {
            if (!w.has<Climbing>(u) && w.has<Transform>(u) && w.has<Transform>(stairs) && !w.has<Carried>(u) &&
                std::find(done.begin(), done.end(), u) == done.end()) {
                stopped.push_back({ u, stairs });
            }
        }
        sw.units.clear();
        for (ObjectId u : w.view<Climbing>()) {
            const ObjectId l = w.get<Climbing>(u)->ladder;
            if (isStairs(w, l)) sw.units.push_back({ u, l });
        }
        if (!stopped.empty()) {
            const BuildingMap& m = cachedBuildingMap(w);
            for (const auto& [u, stairs] : stopped) {
                const Transform* ut = w.get<Transform>(u);
                const Vec2 foot = nearestLanding(w, m, stairs, Vec2{ ut->x, ut->z });
                Vec2 top = foot;
                ladderTop(w, m, stairs, top);
                const bool nearTop = std::hypot(ut->x - top.x, ut->z - top.z) < std::hypot(ut->x - foot.x, ut->z - foot.z);
                // The walk it was given: where to, and on which floor (by its route if it had one)
                bool walk = false;
                int storey = -1;
                Vec2 goal{ 0.0f, 0.0f };
                if (const Route* r = w.get<Route>(u); r && !r->legs.empty()) {
                    walk = true;
                    goal = r->goal;
                    storey = r->legs.back().storey;
                } else if (const SoloMove* so = w.get<SoloMove>(u); so && !so->waypoints.empty()) {
                    walk = true;
                    goal = so->waypoints.back();
                }
                land(m, u, stairs, !nearTop, &foot);
                if (walk) walkSoloTo(w, t, u, goal, storey);
            }
        }
    }
    // Upstairs with no floor under it any more: it comes down to the ground
    const std::vector<ObjectId> ups = w.view<Upstairs, Transform>();
    if (ups.empty()) return;
    const BuildingMap& m = cachedBuildingMap(w);
    for (ObjectId u : ups) {
        if (w.has<Climbing>(u)) continue;
        Transform* ut = w.get<Transform>(u);
        const int level = w.get<Upstairs>(u)->level;
        if (mapFloorSurface(w, m, tileOf(ut->x), tileOf(ut->z), level, -1)) {
            if (!w.has<Occupying>(u) && !w.has<Carried>(u)) ut->y = t.heightAt(ut->x, ut->z) + storeyBaseHeight(level);
            continue;
        }
        w.remove<Upstairs>(u);
        w.remove<Route>(u);
        ut->y = t.standHeight(ut->x, ut->z);
    }
}

namespace {
// Plans the way now, whatever the pacing (stuck.h) says
void planSoloNow(World& w, Terrain& t, ObjectId e, Vec2 target) {
    PROFILE_SCOPE("path: planSoloNow");
    SoloMove* so = w.get<SoloMove>(e);
    if (!so) so = &w.add(e, SoloMove{});
    so->held = false;
    const Transform* tr = w.get<Transform>(e);
    if (!tr) return;
    notePathfind(w, e);
    if (const Upstairs* up = w.get<Upstairs>(e)) {
        const BuildingMap& m = cachedBuildingMap(w);
        std::vector<Vec2> path = floorPath(w, m, up->level, Vec2{ tr->x, tr->z }, target);
        if (path.empty()) {
            path = { nearestFloorSpot(w, up->foundation, target, up->level) };
            if (std::hypot(path[0].x - target.x, path[0].z - target.z) > PATH_FAIL_GAP) notePathFailed(w, e, target);
        }
        so->waypoints = std::move(path);
        so->next = 0;
        return;
    }
    so->waypoints = t.findPath({ tr->x, tr->z }, target);
    so->next = 0;
    notePathResult(w, t, e, Vec2{ tr->x, tr->z }, target, so->waypoints);
}
}

void planSoloMove(World& w, Terrain& t, ObjectId e, Vec2 target) {
    if (deferPath(w, e, target, -1, false)) return;
    planSoloNow(w, t, e, target);
}

void walkSoloTo(World& w, Terrain& t, ObjectId e, Vec2 target, int storey) {
    PROFILE_SCOPE("path: walkSoloTo");
    if (deferPath(w, e, target, storey, true)) return;
    w.remove<Route>(e);
    const Transform* tr = w.get<Transform>(e);
    if (!tr) return;
    notePathfind(w, e);
    const int here = storeyOf(w, e);
    if (storey < 0) {
        storey = 0;
        if (here > 0) {
            const BuildingMap& m = cachedBuildingMap(w);
            if (floorWalkable(w, m, tileOf(target.x), tileOf(target.z), here)) storey = here;
        }
    }
    w.add(e, SoloMove{});
    if (storey == here) {
        if (here == 0) {
            // Ground the buildings have parted from this (a closed Perimeter, a shut Gate) may still be reached over the walls: up a Slope,
            // along the walkway and down another, or up and down stairs. Only then is it a route; otherwise the walk is on the ground.
            auto pieceOf = [&](Vec2 p) {
                int piece = t.navPieceAt(tileOf(p.x), tileOf(p.z));
                if (piece == 0) {
                    const Vec2 s = t.snap(p);
                    piece = t.navPieceAt(tileOf(s.x), tileOf(s.z));
                }
                return piece;
            };
            const int from = pieceOf(Vec2{ tr->x, tr->z }), to = pieceOf(target);
            if (from != 0 && to != 0 && from != to && !routeLinks(w).links.empty()) {
                Route over;
                if (planRoute(w, t, e, target, 0, over) &&
                    std::any_of(over.legs.begin(), over.legs.end(), [](const RouteLeg& l) { return l.ladder != NO_OBJECT; })) {
                    const Vec2 first = over.legs.front().to;
                    over.planned = true;
                    w.add(e, std::move(over));
                    planSoloNow(w, t, e, first);
                    return;
                }
            }
            setPathGoal(w, e, target);   // noticed if it cannot get there (stuck.h)
            planSoloNow(w, t, e, target);
            return;
        }
        const BuildingMap& m = cachedBuildingMap(w);
        std::vector<Vec2> path = floorPath(w, m, here, Vec2{ tr->x, tr->z }, target);
        if (!path.empty()) {
            w.get<SoloMove>(e)->waypoints = std::move(path);
            return;
        }
    }
    Route r;
    if (planRoute(w, t, e, target, storey, r)) {
        const Vec2 first = r.legs.front().to;
        r.planned = true;
        w.add(e, std::move(r));
        planSoloNow(w, t, e, first);
        return;
    }
    if (storey != here) notePathFailed(w, e, target);   // no way between the floors
    planSoloNow(w, t, e, target);
}

namespace {
// A unit chasing a foe on another floor: how soon it looks for a way there again. Not saved.
struct FloorChase {
    float retryIn = 0.0f;
};
}

void routeSystem(World& w, Terrain& t, float dt) {
    PROFILE_SCOPE("build: routeSystem");
    for (ObjectId u : w.view<Route>()) {
        if (w.has<Climbing>(u)) continue;
        Route& r = *w.get<Route>(u);
        SoloMove* so = w.get<SoloMove>(u);
        if (!so || r.leg >= r.legs.size() || outCold(w, u) || w.has<Carried>(u)) {
            w.remove<Route>(u);
            continue;
        }
        const RouteLeg leg = r.legs[r.leg];
        if (storeyOf(w, u) != leg.storey) {
            w.remove<Route>(u);
            continue;
        }
        if (!r.planned) {
            planSoloMove(w, t, u, leg.to);
            r.planned = true;
            continue;
        }
        const bool arrived = so->next >= so->waypoints.size();
        if (leg.ladder != NO_OBJECT) {
            const Transform* ut = w.get<Transform>(u);
            const float near = ut ? std::hypot(ut->x - leg.to.x, ut->z - leg.to.z) : 1e9f;
            if (!arrived && (near > 0.9f || !atClimbSpot(w, u, leg.ladder))) continue;
            const Vec2 goal = r.goal;
            if (startClimb(w, t, u, leg.ladder, &leg.off)) {
                ++r.leg;
                r.planned = false;
                so->waypoints = { goal };
                so->next = 0;
                so->held = false;
            } else {
                w.remove<Route>(u);
                so->next = so->waypoints.size();
            }
            continue;
        }
        if (arrived) {
            w.remove<Route>(u);
            if (w.has<FloorChase>(u)) w.remove<SoloMove>(u);
        }
    }
    // Units fighting a foe on another floor go there by way of the ladders
    for (ObjectId u : w.view<Combat, Transform>()) {
        const Combat* c = w.get<Combat>(u);
        ObjectId foe = c->target;
        if (const AttackOrder* ao = w.get<AttackOrder>(u)) foe = ao->target;
        FloorChase* fc = w.get<FloorChase>(u);
        if (foe == NO_OBJECT || !w.has<Entity>(foe) || storeyOf(w, foe) == storeyOf(w, u) || outCold(w, u) || w.has<Carried>(u) ||
            w.has<Occupying>(u)) {
            if (fc && !w.has<Route>(u)) w.remove<FloorChase>(u);
            continue;
        }
        if (w.has<Route>(u) || w.has<Climbing>(u)) continue;
        if (!fc) fc = &w.add(u, FloorChase{});
        fc->retryIn -= dt;
        if (fc->retryIn > 0.0f) continue;
        fc->retryIn = 2.0f;
        const Transform* ft = w.get<Transform>(foe);
        walkSoloTo(w, t, u, Vec2{ ft->x, ft->z }, storeyOf(w, foe));
        if (!w.has<Route>(u)) w.remove<SoloMove>(u);
    }
}
