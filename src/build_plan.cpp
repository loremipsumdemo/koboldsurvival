#include "build_plan.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <tuple>
#include "build.h"
#include "claims.h"
#include "game_tables.h"

namespace fs = std::filesystem;

namespace {
constexpr float QUARTER = 1.5707963f;

int dirOfYaw(float yaw) { return (((int)std::lround(yaw / QUARTER)) % 4 + 4) % 4; }

float wrapYaw(float yaw) {
    const float turn = 6.2831853f;
    yaw = std::fmod(yaw, turn);
    return yaw < 0.0f ? yaw + turn : yaw;
}

// A piece that has a place on a foundation tile (walls, doors, ladders, stairs and the upper floors), or the foundation itself
bool isBuildingKind(PropKind k) {
    const PropInfo& info = propInfo(k);
    return info.foundation || info.onFoundation;
}
}

int planStageOf(const PlanPiece& p) {
    const PropInfo& info = propInfo(p.kind);
    if (info.foundation) return 0;
    if (info.upperFloor) return 2 * p.level + 2;
    return 2 * p.level + 1;
}

int planStageCount(const BuildPlan& plan) {
    int most = 0;
    for (const PlanPiece& p : plan.pieces) most = std::max(most, planStageOf(p));
    return most + 1;
}

std::vector<const PlanPiece*> planStagePieces(const BuildPlan& plan, int stage) {
    std::vector<const PlanPiece*> out;
    for (const PlanPiece& p : plan.pieces) {
        if (planStageOf(p) == stage) out.push_back(&p);
    }
    // Doors after the walls: a door turns to face along the walls beside it as it is put down
    std::stable_sort(out.begin(), out.end(), [](const PlanPiece* a, const PlanPiece* b) {
        return propInfo(a->kind).door < propInfo(b->kind).door;
    });
    return out;
}

BuildPlan planTurned(const BuildPlan& plan, int turns) {
    BuildPlan out = plan;
    turns = ((turns % 4) + 4) % 4;
    for (int t = 0; t < turns; ++t) {
        const int w = out.w, d = out.d;
        for (PlanPiece& p : out.pieces) {
            // A quarter turn clockwise seen from above: north goes to east. The plan's box turns with it.
            const float x = p.x, z = p.z;
            p.x = (float)d - z;
            p.z = x;
            const PropInfo& info = propInfo(p.kind);
            if (info.stairs) {
                p.yaw = wrapYaw(p.yaw + QUARTER);
            } else if (p.visual.plain()) {
                p.yaw = wrapYaw(p.yaw - QUARTER);
            } else {
                // An adjusted prop keeps its own turn and is spun about its middle with the rest
                const float dx = p.visual.dx, dz = p.visual.dz;
                p.visual.dx = -dz;
                p.visual.dz = dx;
                p.visual.ry = wrapDegrees(p.visual.ry - 90.0f);
            }
        }
        out.w = d;
        out.d = w;
    }
    return out;
}

std::vector<bool> planFootprint(const BuildPlan& plan) {
    std::vector<bool> out((size_t)plan.w * (size_t)plan.d, false);
    for (const PlanPiece& p : plan.pieces) {
        if (!propInfo(p.kind).foundation) continue;
        const int x = (int)std::floor(p.x), z = (int)std::floor(p.z);
        if (x >= 0 && z >= 0 && x < plan.w && z < plan.d) out[(size_t)x + (size_t)z * (size_t)plan.w] = true;
    }
    return out;
}

std::vector<PlanRect> planFoundationRects(const BuildPlan& plan) {
    std::vector<bool> left = planFootprint(plan);
    std::vector<PlanRect> out;
    const int w = plan.w, d = plan.d;
    auto at = [&](int x, int z) { return left[(size_t)x + (size_t)z * (size_t)w]; };
    for (;;) {
        // The biggest rectangle of what is left, by trying every top left corner and growing it
        PlanRect best;
        int bestArea = 0;
        for (int z = 0; z < d; ++z) {
            for (int x = 0; x < w; ++x) {
                if (!at(x, z)) continue;
                int maxW = w - x;
                for (int h = 1; z + h <= d && at(x, z + h - 1); ++h) {
                    int run = 0;
                    while (run < maxW && at(x + run, z + h - 1)) ++run;
                    maxW = std::min(maxW, run);
                    if (maxW <= 0) break;
                    if (maxW * h > bestArea) {
                        bestArea = maxW * h;
                        best = PlanRect{ x, z, x + maxW - 1, z + h - 1 };
                    }
                }
            }
        }
        if (bestArea == 0) break;
        for (int z = best.z0; z <= best.z1; ++z) {
            for (int x = best.x0; x <= best.x1; ++x) left[(size_t)x + (size_t)z * (size_t)w] = false;
        }
        out.push_back(best);
    }
    return out;
}

PlanCost planCost(const BuildPlan& plan) {
    PlanCost cost;
    for (const PlanPiece& p : plan.pieces) {
        const PropInfo& info = propInfo(p.kind);
        if (info.materials <= 0.0) continue;
        const int n = std::max(1, (int)std::ceil(info.materials - 1e-9));
        if (takesMaterial(p.kind)) cost.material += n;
        else cost.logs += n;
    }
    return cost;
}

std::string planProblem(const BuildPlan& plan) {
    const std::vector<PlanRect> rects = planFoundationRects(plan);
    if (rects.empty()) return "MISC_BLUEPRINT_NO_FOUNDATION";
    const PlanRect& big = rects.front();
    if (big.x1 - big.x0 < FOUNDATION_MIN - 1 || big.z1 - big.z0 < FOUNDATION_MIN - 1) return "MISC_BLUEPRINT_NO_FOUNDATION";
    // A door, so that it is a building a unit can go into and out of
    bool door = false;
    for (const PlanPiece& p : plan.pieces) door = door || (p.level == 0 && propInfo(p.kind).door);
    return door ? std::string() : std::string("MISC_BLUEPRINT_NO_DOOR");
}

// ---- Files

std::string planFilePath(const std::string& name) {
    return std::string(PLAN_FOLDER) + "/" + name + ".blueprint";
}

std::string planToText(const BuildPlan& plan) {
    std::ostringstream out;
    char line[200];
    out << "format 1\nname " << plan.name << "\n";
    std::snprintf(line, sizeof line, "size %d %d %d\n", plan.w, plan.d, plan.floors);
    out << line;
    // Foundation and upper floor tiles go in runs along a row; everything else one to a line, with its adjusting after it
    struct Run {
        PropKind kind;
        int level, z, x0, x1;
    };
    std::vector<const PlanPiece*> single;
    std::map<std::tuple<int, int, int>, std::vector<int>> rows;   // (kind, level, z) to the x of each tile
    for (const PlanPiece& p : plan.pieces) {
        const PropInfo& info = propInfo(p.kind);
        if (info.foundation || info.upperFloor) rows[{ idx(p.kind), p.level, (int)std::floor(p.z) }].push_back((int)std::floor(p.x));
        else single.push_back(&p);
    }
    for (auto& [key, xs] : rows) {
        std::sort(xs.begin(), xs.end());
        xs.erase(std::unique(xs.begin(), xs.end()), xs.end());
        size_t i = 0;
        while (i < xs.size()) {
            size_t j = i;
            while (j + 1 < xs.size() && xs[j + 1] == xs[j] + 1) ++j;
            std::snprintf(line, sizeof line, "fill %s %d %d %d %d\n", propInfo((PropKind)std::get<0>(key)).key.c_str(), xs[i], xs[j],
                          std::get<2>(key), std::get<1>(key));
            out << line;
            i = j + 1;
        }
    }
    for (const PlanPiece* p : single) {
        std::snprintf(line, sizeof line, "piece %s %.3f %.3f %d %.4f\n", propInfo(p->kind).key.c_str(), p->x, p->z, p->level, p->yaw);
        out << line;
        if (!p->visual.plain()) {
            const PropVisual& v = p->visual;
            std::snprintf(line, sizeof line, "visual %.3f %.3f %.3f %.1f %.1f %.1f %.3f %.3f %.3f\n", v.dx, v.dy, v.dz, v.rx, v.ry, v.rz,
                          v.sx, v.sy, v.sz);
            out << line;
        }
    }
    return out.str();
}

bool planFromText(const std::string& text, BuildPlan& out, std::string& err) {
    BuildPlan plan;
    std::istringstream in(text);
    std::string line;
    int number = 0, format = 0;
    bool sized = false;
    while (std::getline(in, line)) {
        ++number;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream ls(line);
        std::string key;
        if (!(ls >> key) || key[0] == '#') continue;
        bool ok = true;
        if (key == "format") {
            ok = (bool)(ls >> format);
        } else if (key == "name") {
            std::getline(ls >> std::ws, plan.name);
        } else if (key == "size") {
            ok = (bool)(ls >> plan.w >> plan.d >> plan.floors) && plan.w >= PLAN_SIZE_MIN && plan.w <= PLAN_SIZE_MAX &&
                 plan.d >= PLAN_SIZE_MIN && plan.d <= PLAN_SIZE_MAX && plan.floors >= 1 && plan.floors <= PLAN_FLOORS_MAX;
            sized = ok;
        } else if (key == "piece" || key == "fill") {
            std::string kindKey;
            ok = sized && (bool)(ls >> kindKey);
            PropKind kind = PropKind::Foundation;
            ok = ok && propKindNamed(currentPropKey(kindKey), kind);
            if (ok && key == "piece") {
                PlanPiece p;
                p.kind = kind;
                ok = (bool)(ls >> p.x >> p.z >> p.level >> p.yaw) && p.x >= 0.0f && p.z >= 0.0f && p.x < (float)plan.w && p.z < (float)plan.d &&
                     p.level >= 0 && p.level < plan.floors;
                if (ok) plan.pieces.push_back(p);
            } else if (ok) {
                int x0 = 0, x1 = 0, z = 0, level = 0;
                ok = (bool)(ls >> x0 >> x1 >> z >> level) && x0 >= 0 && x1 >= x0 && x1 < plan.w && z >= 0 && z < plan.d && level >= 0 &&
                     level < plan.floors;
                for (int x = x0; ok && x <= x1; ++x) {
                    PlanPiece p;
                    p.kind = kind;
                    p.x = (float)x + 0.5f;
                    p.z = (float)z + 0.5f;
                    p.level = level;
                    plan.pieces.push_back(p);
                }
            }
        } else if (key == "visual") {
            // Of the piece on the line before
            PropVisual v;
            ok = (bool)(ls >> v.dx >> v.dy >> v.dz >> v.rx >> v.ry >> v.rz >> v.sx >> v.sy >> v.sz) && !plan.pieces.empty();
            if (ok) plan.pieces.back().visual = clampPropVisual(v);
        } else {
            ok = false;
        }
        if (!ok) {
            err = "line " + std::to_string(number) + ": " + line;
            return false;
        }
    }
    if (format != 1) { err = "not a version 1 plan"; return false; }
    if (!sized) { err = "has no size"; return false; }
    out = std::move(plan);
    return true;
}

bool writePlanFile(const std::string& path, const BuildPlan& plan, std::string& err) {
    std::error_code ec;
    fs::create_directories(fs::u8path(path).parent_path(), ec);
    std::ofstream out(fs::u8path(path), std::ios::binary);
    out << planToText(plan);
    out.close();
    if (!out) { err = "could not write " + path; return false; }
    return true;
}

bool readPlanFile(const std::string& path, BuildPlan& out, std::string& err) {
    std::ifstream in(fs::u8path(path), std::ios::binary);
    if (!in) { err = "could not read " + path; return false; }
    std::stringstream text;
    text << in.rdbuf();
    return planFromText(text.str(), out, err);
}

std::vector<std::string> listPlanFiles() {
    std::vector<std::string> out;
    std::error_code ec;
    for (const fs::directory_entry& e : fs::directory_iterator(fs::u8path(PLAN_FOLDER), ec)) {
        if (e.path().extension() == ".blueprint") out.push_back(e.path().stem().u8string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool deletePlanFile(const std::string& name, std::string& err) {
    std::error_code ec;
    const bool gone = fs::remove(fs::u8path(planFilePath(name)), ec);
    if (ec || !gone) {
        err = "could not delete " + planFilePath(name);
        return false;
    }
    return true;
}

namespace {
std::vector<BuildPlan>& libraryStore() {
    static std::vector<BuildPlan> plans;
    return plans;
}
bool& libraryLoaded() {
    static bool loaded = false;
    return loaded;
}
}

void reloadPlanLibrary() {
    libraryStore().clear();
    for (const std::string& name : listPlanFiles()) {
        BuildPlan plan;
        std::string err;
        if (!readPlanFile(planFilePath(name), plan, err) || !planProblem(plan).empty()) continue;
        plan.name = name;
        libraryStore().push_back(std::move(plan));
    }
    libraryLoaded() = true;
}

const std::vector<BuildPlan>& planLibrary() {
    if (!libraryLoaded()) reloadPlanLibrary();
    return libraryStore();
}

// ---- Reading a plan off the world, and putting one on it

BuildPlan capturePlan(World& w, const std::string& name, int boxW, int boxD, int floors, int x, int z) {
    BuildPlan plan;
    plan.name = name;
    plan.w = boxW;
    plan.d = boxD;
    plan.floors = floors;
    for (ObjectId e : w.view<Prop, Transform>()) {
        const Transform* tr = w.get<Transform>(e);
        const PropKind kind = w.get<Prop>(e)->kind;
        const PropInfo& info = propInfo(kind);
        const float px = tr->x - (float)x, pz = tr->z - (float)z;
        if (px < 0.0f || pz < 0.0f || px >= (float)boxW || pz >= (float)boxD) continue;
        if (info.claimsLand || info.debris || info.group == BuildGroup::Nature) continue;
        const int storey = storeyOf(w, e);
        const int level = info.upperFloor ? std::max(0, storey - 1) : storey;
        if (storey >= floors) continue;
        PlanPiece p;
        p.kind = kind;
        p.x = px;
        p.z = pz;
        p.level = level;
        if (isBuildingKind(kind)) {
            // A building piece is on its tile's middle. Its turn is the game's to work out, but stairs keep theirs.
            p.x = std::floor(px) + 0.5f;
            p.z = std::floor(pz) + 0.5f;
            p.yaw = info.stairs ? (float)dirOfYaw(w.get<Prop>(e)->yaw) * QUARTER : 0.0f;
        } else {
            p.yaw = w.get<Prop>(e)->yaw;
            if (const PropVisual* v = w.get<PropVisual>(e); v && !v->plain()) p.visual = *v;
        }
        plan.pieces.push_back(p);
    }
    // The same plan is always written the same way
    std::sort(plan.pieces.begin(), plan.pieces.end(), [](const PlanPiece& a, const PlanPiece& b) {
        const int sa = planStageOf(a), sb = planStageOf(b);
        if (sa != sb) return sa < sb;
        if (a.z != b.z) return a.z < b.z;
        if (a.x != b.x) return a.x < b.x;
        return idx(a.kind) < idx(b.kind);
    });
    return plan;
}

int planPiecesOutside(World& w, int boxW, int boxD, int x, int z) {
    int outside = 0;
    for (ObjectId e : w.view<Prop, Transform>()) {
        const Transform* tr = w.get<Transform>(e);
        const PropInfo& info = propInfo(w.get<Prop>(e)->kind);
        if (info.claimsLand || info.debris || info.group == BuildGroup::Nature) continue;
        const float px = tr->x - (float)x, pz = tr->z - (float)z;
        if (px < 0.0f || pz < 0.0f || px >= (float)boxW || pz >= (float)boxD) ++outside;
    }
    return outside;
}

bool planPieceFits(World& w, const Terrain& t, const PlanPiece& p, int x, int z, int owner) {
    const PropInfo& info = propInfo(p.kind);
    if (info.foundation) return true;   // laid as a whole, by placePlanFoundation
    if (info.onFoundation) {
        const int tx = x + (int)std::floor(p.x), tz = z + (int)std::floor(p.z);
        return checkPiece(w, p.kind, tx, tz, owner, p.level, info.stairs ? dirOfYaw(p.yaw) : 0) == PieceCheck::Ok;
    }
    return checkBuildSpot(w, t, p.kind, (float)x + p.x, (float)z + p.z, p.level, owner) == BuildCheck::Ok;
}

ObjectId placePlanPiece(World& w, const Terrain& t, const PlanPiece& p, int x, int z, int owner, BuildMaterial material) {
    const PropInfo& info = propInfo(p.kind);
    if (info.foundation || !planPieceFits(w, t, p, x, z, owner)) return NO_OBJECT;
    if (info.onFoundation) {
        const int tx = x + (int)std::floor(p.x), tz = z + (int)std::floor(p.z);
        return placePiece(w, t, p.kind, tx, tz, owner, takesMaterial(p.kind) ? material : BuildMaterial::Wood, p.level,
                          info.stairs ? dirOfYaw(p.yaw) : 0);
    }
    const ObjectId e = placeCostedProp(w, t, p.kind, (float)x + p.x, (float)z + p.z, p.yaw, owner, p.level);
    if (e != NO_OBJECT && !p.visual.plain()) w.add(e, p.visual);
    return e;
}

bool planFoundationFits(World& w, const Terrain& t, const BuildPlan& plan, int x, int z, int owner) {
    return planFoundationFits(w, t, planFoundationRects(plan), x, z, owner);
}

bool planFoundationFits(World& w, const Terrain& t, const std::vector<PlanRect>& rects, int x, int z, int owner) {
    if (rects.empty()) return false;
    const PlanRect& r = rects.front();
    return checkFoundation(w, t, x + r.x0, z + r.z0, x + r.x1, z + r.z1, owner) == PieceCheck::Ok;
}

ObjectId placePlanFoundation(World& w, const Terrain& t, const BuildPlan& plan, int x, int z, int owner, BuildMaterial material) {
    return placePlanFoundation(w, t, planFoundationRects(plan), x, z, owner, material);
}

ObjectId placePlanFoundation(World& w, const Terrain& t, const std::vector<PlanRect>& rects, int x, int z, int owner, BuildMaterial material) {
    ObjectId first = NO_OBJECT;
    for (size_t i = 0; i < rects.size(); ++i) {
        const PlanRect& r = rects[i];
        if (checkFoundation(w, t, x + r.x0, z + r.z0, x + r.x1, z + r.z1, owner) != PieceCheck::Ok) {
            if (i == 0) return NO_OBJECT;   // without its biggest part there is no building to add to
            continue;
        }
        const ObjectId laid = placeFoundation(w, t, x + r.x0, z + r.z0, x + r.x1, z + r.z1, owner, material);
        if (first == NO_OBJECT) first = laid;
    }
    return first;
}

void placePlanInstant(World& w, Terrain& t, const BuildPlan& plan, int x, int z, int owner) {
    const int stages = planStageCount(plan);
    for (int stage = 0; stage < stages; ++stage) {
        if (stage == 0) {
            placePlanFoundation(w, t, plan, x, z, owner, BuildMaterial::Wood);
        } else {
            // A piece that needs one before it (a wall for a floor's edge) is tried again after the rest
            std::vector<const PlanPiece*> todo = planStagePieces(plan, stage);
            for (int pass = 0; pass < 3 && !todo.empty(); ++pass) {
                std::vector<const PlanPiece*> again;
                for (const PlanPiece* p : todo) {
                    if (placePlanPiece(w, t, *p, x, z, owner, BuildMaterial::Wood) == NO_OBJECT) again.push_back(p);
                }
                finishAllBlueprints(w, t);
                todo.swap(again);
            }
        }
        finishAllBlueprints(w, t);
    }
}
