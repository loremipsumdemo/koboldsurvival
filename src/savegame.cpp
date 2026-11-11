#include "savegame.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <system_error>
#include <type_traits>
#include <unordered_map>
#include "game_tables.h"
#include "user_files.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace fs = std::filesystem;

std::string saveDir() {
    return userFilesDir() + "saves";
}

namespace {
constexpr int RENAME_TRIES = 10;
constexpr Uint32 RENAME_RETRY_MS = 50;

class Writer {
public:
    explicit Writer(std::ostream& out) : out_(out) {}

    template <typename... T>
    void line(const char* key, const T&... values) {
        out_ << key;
        (put(values), ...);
        out_ << '\n';
    }

private:
    template <typename T>
    void put(const T& v) {
        out_ << ' ';
        if constexpr (std::is_floating_point_v<T>) {
            // A value the reader could not parse back (not finite, or too small to represent) is written as 0
            const T safe = std::isfinite(v) && std::fabs(v) >= (T)1e-30 ? v : (T)0;
            out_ << std::setprecision(std::is_same_v<T, float> ? 9 : 17) << safe;
        } else if constexpr (std::is_same_v<T, bool>) {
            out_ << (v ? 1 : 0);
        } else {
            out_ << v;
        }
    }

    std::ostream& out_;
};

void writeUnit(Writer& w, size_t i, const SaveUnit& u) {
    w.line("unit", u.at.x, u.at.y, u.at.z, u.sprite, u.faction, u.mob > 0 ? mobInfo((MobKind)u.mob).key : std::string("-"), u.name);
    const Needs& n = u.needs;
    w.line("needs", i, n.hp, n.maxHp, n.hunger, n.maxHunger, n.rested, n.maxRested, n.comfort, n.maxComfort,
           n.restedPart, n.comfortPart, n.hurried, n.healSeconds);
    for (const SaveSlot& s : u.stacks) w.line("slot", i, s.slot, s.stack.item, s.stack.count);
    for (const SaveGear& g : u.gear) w.line("gear", i, g.slot, g.item);
    if (u.stats) {
        std::ostringstream xp;
        for (size_t k = 0; k < u.stats->xp.size(); ++k) xp << (k ? " " : "") << statKey((StatId)k) << ' ' << u.stats->xp[k];
        w.line("stats", i, xp.str());
    }
    if (u.xpParts) {
        std::ostringstream parts;
        parts << std::setprecision(9);
        for (size_t k = 0; k < u.xpParts->frac.size(); ++k) parts << (k ? " " : "") << statKey((StatId)k) << ' ' << u.xpParts->frac[k];
        w.line("xpparts", i, parts.str());
    }
    if (u.body) w.line("body", i, *u.body);
    if (u.home) w.line("home", i, u.home->x, u.home->z, u.home->returning);
    if (u.occupying >= 0) w.line("occupy", i, u.occupying);
    if (u.pushing >= 0) w.line("push", i, u.pushing);
    if (u.upstairs >= 0) w.line("upstairs", i, u.upstairs);
    if (u.down) w.line("down", i, u.down->dying, u.down->koFaction, u.down->conscious, u.down->tickPart);
    if (u.corpseHours) w.line("corpse", i, *u.corpseHours);
    if (u.retreat) {
        const Retreating& r = *u.retreat;
        w.line("retreat", i, r.x, r.z, r.planned, r.home, r.arrived, r.wasPassive, r.hours);
    }
    if (u.carriedBy >= 0) w.line("carried", i, u.carriedBy);
    if (u.playingDead) w.line("still", i);
    if (u.coloration) w.line("color", i, *u.coloration);
    if (u.tamingHours) w.line("taming", i, *u.tamingHours);
    if (u.passive || u.block || u.sneak) w.line("modes", i, u.passive, u.block, u.sneak);
    if (u.buffLeft > 0.0f) w.line("buff", i, u.buffLeft, u.buffAttack);
    for (const Provoked::Entry& p : u.provoked) w.line("provoked", i, p.faction, p.secondsLeft);
    if (u.followUnit >= 0) w.line("follow", i, u.followUnit);
    if (u.defendUnit >= 0) w.line("defend", i, u.defendUnit);
    if (u.attackUnit >= 0) w.line("attack", i, u.attackUnit);
    if (u.move) w.line("move", i, u.move->x, u.move->z);
    if (u.floorMove) w.line("floormove", i, u.floorMove->x, u.floorMove->z, u.floorMoveStorey);
    if (u.propOrder >= 0) w.line("propgo", i, u.propOrder, u.propAction);
    if (u.workProp >= 0) w.line("working", i, u.workProp, u.workUnlocking);
    for (const SaveRule& r : u.rules) {
        const bool namesProp = r.rule.propKind >= 0 && r.rule.propKind < propKindCount();
        w.line("task", i, r.rule.on, behaviorKey(r.rule.kind), namesProp ? propInfo((PropKind)r.rule.propKind).key : std::string("-"), r.prop, r.rule.percent);
    }
    if (u.citizen && u.citizenGroup > 0) w.line("citgroup", i, u.citizenGroup);
    if (u.stepKind >= 0) w.line("step", i, behaviorKey((BehaviorKind)u.stepKind));
    if (u.squad >= 0) w.line("squad", i, u.squad);
    if (u.citizen) w.line("citizen", i);
}

void writeProp(Writer& w, size_t i, const SaveProp& p) {
    w.line("prop", propInfo((PropKind)p.kind).key, p.at.x, p.at.y, p.at.z, p.yaw, p.owner);
    if (p.lock) w.line("lock", i, p.lock->locked, p.lock->level, p.lock->faction);
    for (const SaveSlot& s : p.storage) w.line("storage", i, s.slot, s.stack.item, s.stack.count);
    for (const SaveSlot& s : p.output) w.line("output", i, s.slot, s.stack.item, s.stack.count);
    if (p.fuel) w.line("fuel", i, p.fuel->hours, p.fuel->item);
    if (p.workProgress) w.line("work", i, *p.workProgress, p.workTurn);
    if (p.bonesHours) w.line("bones", i, *p.bonesHours);
    if (p.outdoorHours) w.line("outdoor", i, *p.outdoorHours);
    for (const ItemStack& s : p.bonesItems) w.line("bonesitem", i, s.item, s.count);
    if (p.health) w.line("health", i, p.health->value, p.health->max);
    if (p.visual) {
        const PropVisual& v = *p.visual;
        w.line("visual", i, v.dx, v.dy, v.dz, v.rx, v.ry, v.rz, v.sx, v.sy, v.sz);
    }
    if (p.foundation) {
        w.line("foundation", i, p.foundation->x0, p.foundation->z0, p.foundation->w, p.foundation->h);
    }
    if (p.blueprint) w.line("blueprint", i, p.blueprint->supplied, p.blueprint->progress);
    if (p.material > 0) w.line("material", i, p.material);
    if (p.storeFilter) w.line("storefilter", i, *p.storeFilter);
    if (!p.craftRecipe.empty() || p.craftKeep > 0) w.line("craftorder", i, p.craftRecipe.empty() ? std::string("-") : p.craftRecipe, p.craftKeep);
    if (p.bedPublic) w.line("bedpublic", i);
    if (p.stallPayment) w.line("stallpay", i, *p.stallPayment);
    for (const auto& r : p.stallRequests) w.line("stallreq", i, r[0], r[1], r[2]);
}

void writeSaveLines(std::ostream& out, const SaveData& d) {
    Writer w(out);
    w.line("version", SAVE_VERSION);
    w.line("name", d.name);
    if (d.storesListed) w.line("storeslisted", 1);
    if (d.stallHaulListed) w.line("stallhaul", 1);
    w.line("timestamp", d.timestamp);
    w.line("autosave", d.autosave);
    w.line("map", d.mapSource, d.mapStem);
    w.line("clock", d.gs.clockSeconds);
    w.line("camera", d.cam.fx, d.cam.fy, d.cam.fz, d.cam.yaw, d.cam.pitch, d.cam.distance);
    w.line("rng", d.rngState);
    for (const auto& [faction, value] : d.reputation) w.line("rep", faction, value);
    for (const auto& [faction, rules] : d.factionRules) w.line("factionrules", faction, factionRulesText(rules));
    for (const auto& [faction, key] : d.researched) w.line("research", faction, key);
    for (size_t k = 0; k < d.squadNames.size(); ++k) {
        if (!d.squadNames[k].empty()) w.line("squadname", k, d.squadNames[k]);
    }
    w.line("squadat", d.currentSquad);
    for (const Ghost& g : d.ghosts) w.line("ghost", g.appeased, g.hours, g.sprite, g.name);
    // Behavior groups: a bgroup line, then its tasks. A Group task's ref goes where a haul's prop does.
    for (const SaveGroup& g : d.groups) {
        w.line("bgroup", g.ref);
        for (const SaveRule& r : g.rules) {
            const bool group = r.rule.kind == BehaviorKind::Group;
            const bool namesProp = !group && r.rule.propKind >= 0 && r.rule.propKind < propKindCount();
            w.line("btask", r.rule.on, behaviorKey(r.rule.kind), namesProp ? propInfo((PropKind)r.rule.propKind).key : std::string("-"),
                   group ? r.rule.propKind : r.prop, r.rule.percent);
        }
    }
    for (const auto& g : d.groundEdits) w.line("groundedit", (int)g[0], (int)g[1], g[2]);
    for (size_t i = 0; i < d.props.size(); ++i) writeProp(w, i, d.props[i]);
    for (const SaveGround& g : d.ground) w.line("ground", g.at.x, g.at.y, g.at.z, g.item.item, g.item.count, g.item.hours);
    for (size_t i = 0; i < d.units.size(); ++i) writeUnit(w, i, d.units[i]);
    w.line("end", d.props.size(), d.units.size());
}

// On Windows, a rename can commit before the file's contents reach the disk
void flushToDisk(const fs::path& file) {
#ifdef _WIN32
    const HANDLE h = CreateFileW(file.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    FlushFileBuffers(h);
    CloseHandle(h);
#else
    (void)file;
#endif
}

struct Line {
    std::istringstream ss;
    SaveData& d;
    bool ended = false;

    template <typename... T>
    bool read(T&... v) {
        ((ss >> v), ...);
        return !ss.fail();
    }
    bool rest(std::string& text) {
        std::getline(ss >> std::ws, text);
        return true;
    }
    SaveUnit* unit() {
        size_t i = 0;
        return read(i) && i < d.units.size() ? &d.units[i] : nullptr;
    }
    SaveProp* prop() {
        size_t i = 0;
        return read(i) && i < d.props.size() ? &d.props[i] : nullptr;
    }
    // Stat key and value pairs. A stat the file does not have stays as it was, and a stat this game does not have is skipped.
    template <typename A>
    bool readStats(A& values) {
        std::string key;
        while (ss >> key) {
            typename A::value_type value{};
            if (!(ss >> value)) return false;
            const StatId stat = statNamed(key);
            if (stat != StatId::None) values[(size_t)idx(stat)] = value;
        }
        return true;
    }
    bool slotInto(std::vector<SaveSlot>& slots) {
        SaveSlot s;
        if (!read(s.slot, s.stack.item, s.stack.count)) return false;
        slots.push_back(s);
        return true;
    }
};

using Handler = bool (*)(Line&);

const std::unordered_map<std::string, Handler>& handlers() {
    static const std::unordered_map<std::string, Handler> table = {
        { "version", [](Line& l) { int v = 0; return l.read(v) && v == SAVE_VERSION; } },
        { "name", [](Line& l) { return l.rest(l.d.name); } },
        { "timestamp", [](Line& l) { return l.read(l.d.timestamp); } },
        { "autosave", [](Line& l) { return l.read(l.d.autosave); } },
        { "storeslisted", [](Line& l) { int v = 0; if (!l.read(v)) return false; l.d.storesListed = v != 0; return true; } },
        { "stallhaul", [](Line& l) { int v = 0; if (!l.read(v)) return false; l.d.stallHaulListed = v != 0; return true; } },
        { "bedpublic", [](Line& l) { SaveProp* p = l.prop(); if (!p) return false; p->bedPublic = true; return true; } },
        { "stallpay", [](Line& l) {
            SaveProp* p = l.prop();
            unsigned m = 0;
            if (!p || !l.read(m)) return false;
            p->stallPayment = (std::uint32_t)m & STORE_ALL;
            return true;
        } },
        { "stallreq", [](Line& l) {
            SaveProp* p = l.prop();
            std::array<int, 3> r{};
            if (!p || !l.read(r[0], r[1], r[2])) return false;
            p->stallRequests.push_back(r);
            return true;
        } },
        { "map", [](Line& l) { return l.read(l.d.mapSource) && l.rest(l.d.mapStem); } },
        { "clock", [](Line& l) { return l.read(l.d.gs.clockSeconds); } },
        { "camera", [](Line& l) { SaveCamera& c = l.d.cam; return l.read(c.fx, c.fy, c.fz, c.yaw, c.pitch, c.distance); } },
        { "rng", [](Line& l) { return l.rest(l.d.rngState); } },
        { "rep", [](Line& l) {
            int faction = 0, value = 0;
            if (!l.read(faction, value)) return false;
            l.d.reputation[faction] = std::clamp(value, REP_MIN, REP_MAX);
            return true;
        } },
        { "factionrules", [](Line& l) {
            int faction = 0;
            std::string words, err;
            if (!l.read(faction) || !l.rest(words)) return false;
            FactionRules r = findFaction(faction) ? findFaction(faction)->rules : FactionRules{};
            if (!parseFactionRules(words, r, err)) return false;
            l.d.factionRules[faction] = r;
            return true;
        } },
        { "prop", [](Line& l) {
            SaveProp p;
            std::string key;
            PropKind kind = PropKind::Wagon;
            if (!l.read(key, p.at.x, p.at.y, p.at.z, p.yaw, p.owner)) return false;
            key = currentPropKey(key);   // a prop that is gone loads as the one that took its place
            if (!propKindNamed(key, kind)) return false;
            p.kind = idx(kind);
            l.d.props.push_back(std::move(p));
            return true;
        } },
        { "lock", [](Line& l) {
            SaveProp* p = l.prop();
            Lock lock;
            if (!p || !l.read(lock.locked, lock.level, lock.faction)) return false;
            p->lock = lock;
            return true;
        } },
        { "storage", [](Line& l) { SaveProp* p = l.prop(); return p && l.slotInto(p->storage); } },
        { "output", [](Line& l) { SaveProp* p = l.prop(); return p && l.slotInto(p->output); } },
        { "fuel", [](Line& l) {
            SaveProp* p = l.prop();
            Fuel f;
            if (!p || !l.read(f.hours, f.item)) return false;
            p->fuel = f;
            return true;
        } },
        { "work", [](Line& l) {
            SaveProp* p = l.prop();
            double progress = 0.0;
            if (!p || !l.read(progress, p->workTurn)) return false;
            p->workProgress = progress;
            return true;
        } },
        { "bones", [](Line& l) {
            SaveProp* p = l.prop();
            double hours = 0.0;
            if (!p || !l.read(hours)) return false;
            p->bonesHours = hours;
            return true;
        } },
        { "outdoor", [](Line& l) {
            SaveProp* p = l.prop();
            double hours = 0.0;
            if (!p || !l.read(hours)) return false;
            p->outdoorHours = hours;
            return true;
        } },
        { "bonesitem", [](Line& l) {
            SaveProp* p = l.prop();
            ItemStack s;
            if (!p || !l.read(s.item, s.count)) return false;
            p->bonesItems.push_back(s);
            return true;
        } },
        { "visual", [](Line& l) {
            SaveProp* p = l.prop();
            PropVisual v;
            if (!p || !l.read(v.dx, v.dy, v.dz, v.rx, v.ry, v.rz, v.sx, v.sy, v.sz)) return false;
            p->visual = clampPropVisual(v);
            return true;
        } },
        { "health", [](Line& l) {
            SaveProp* p = l.prop();
            Health h;
            if (!p || !l.read(h.value, h.max)) return false;
            p->health = h;
            return true;
        } },
        { "groundedit", [](Line& l) {
            int x = 0, z = 0;
            float h = 0.0f;
            if (!l.read(x, z, h)) return false;
            l.d.groundEdits.push_back({ (float)x, (float)z, h });
            return true;
        } },
        { "ground", [](Line& l) {
            SaveGround g;
            if (!l.read(g.at.x, g.at.y, g.at.z, g.item.item, g.item.count, g.item.hours)) return false;
            l.d.ground.push_back(g);
            return true;
        } },
        { "unit", [](Line& l) {
            SaveUnit u;
            std::string key;
            if (!l.read(u.at.x, u.at.y, u.at.z, u.sprite, u.faction, key)) return false;
            if (key != "-") {
                MobKind kind = MobKind::None;
                if (!mobKindNamed(key, kind)) return false;
                u.mob = idx(kind);
            }
            l.rest(u.name);
            l.d.units.push_back(std::move(u));
            return true;
        } },
        { "needs", [](Line& l) {
            SaveUnit* u = l.unit();
            if (!u) return false;
            Needs& n = u->needs;
            return l.read(n.hp, n.maxHp, n.hunger, n.maxHunger, n.rested, n.maxRested, n.comfort, n.maxComfort,
                          n.restedPart, n.comfortPart, n.hurried, n.healSeconds);
        } },
        { "slot", [](Line& l) { SaveUnit* u = l.unit(); return u && l.slotInto(u->stacks); } },
        { "squad", [](Line& l) { SaveUnit* u = l.unit(); return u && l.read(u->squad); } },
        { "citizen", [](Line& l) { SaveUnit* u = l.unit(); if (u) u->citizen = true; return u != nullptr; } },
        { "citgroup", [](Line& l) { SaveUnit* u = l.unit(); return u && l.read(u->citizenGroup) && u->citizenGroup >= 0; } },
        { "bgroup", [](Line& l) {
            SaveGroup g;
            if (!l.read(g.ref)) return false;
            l.d.groups.push_back(std::move(g));
            return true;
        } },
        { "btask", [](Line& l) {
            if (l.d.groups.empty()) return false;
            int on = 0, percent = 0, propKind = -1;
            std::string kindKey, propKey;
            SaveRule r;
            if (!l.read(on, kindKey, propKey, r.prop, percent)) return false;
            const int kind = behaviorNamed(kindKey);
            if (kind < 0) return false;
            if (kind == (int)BehaviorKind::Group) {
                propKind = r.prop;
                r.prop = -1;
            } else if (propKey != "-") {
                PropKind named = PropKind::Wagon;
                if (!propKindNamed(propKey, named)) return false;
                propKind = idx(named);
            }
            r.rule = behaviorFromFields(on, kind, propKind, percent);
            l.d.groups.back().rules.push_back(r);
            return true;
        } },
        { "squadname", [](Line& l) {
            size_t k = 0;
            if (!l.read(k) || k >= l.d.squadNames.size()) return false;
            return l.rest(l.d.squadNames[k]);
        } },
        { "squadat", [](Line& l) { return l.read(l.d.currentSquad); } },
        { "ghost", [](Line& l) {
            Ghost g;
            if (!l.read(g.appeased, g.hours, g.sprite)) return false;
            l.rest(g.name);
            l.d.ghosts.push_back(std::move(g));
            return true;
        } },
        { "gear", [](Line& l) {
            SaveUnit* u = l.unit();
            SaveGear g;
            if (!u || !l.read(g.slot, g.item) || g.slot < 0 || g.slot >= GEAR_SLOTS) return false;
            u->gear.push_back(g);
            return true;
        } },
        { "stats", [](Line& l) {
            SaveUnit* u = l.unit();
            Stats s;
            if (!u || !l.readStats(s.xp)) return false;
            u->stats = s;
            return true;
        } },
        { "xpparts", [](Line& l) {
            SaveUnit* u = l.unit();
            XpBuffer parts;
            if (!u || !l.readStats(parts.frac)) return false;
            u->xpParts = parts;
            return true;
        } },
        { "body", [](Line& l) {
            SaveUnit* u = l.unit();
            int body = 0;
            if (!u || !l.read(body)) return false;
            u->body = body;
            return true;
        } },
        { "home", [](Line& l) {
            SaveUnit* u = l.unit();
            SpawnHome h;
            if (!u || !l.read(h.x, h.z, h.returning)) return false;
            u->home = h;
            return true;
        } },
        { "occupy", [](Line& l) { SaveUnit* u = l.unit(); return u && l.read(u->occupying); } },
        { "push", [](Line& l) { SaveUnit* u = l.unit(); return u && l.read(u->pushing); } },
        { "upstairs", [](Line& l) { SaveUnit* u = l.unit(); return u && l.read(u->upstairs); } },
        { "foundation", [](Line& l) {
            SaveProp* p = l.prop();
            Foundation f;
            if (!p || !l.read(f.x0, f.z0, f.w, f.h)) return false;
            if (f.w < 1 || f.h < 1 || f.w > FOUNDATION_MAX || f.h > FOUNDATION_MAX) return false;
            p->foundation = std::move(f);
            return true;
        } },
        { "craftorder", [](Line& l) {
            SaveProp* p = l.prop();
            std::string key;
            int keep = 0;
            if (!p || !l.read(key, keep)) return false;
            p->craftRecipe = key == "-" ? std::string() : key;
            p->craftKeep = std::clamp(keep, 0, 999);
            return true;
        } },
        { "research", [](Line& l) {
            int faction = 0;
            std::string key;
            if (!l.read(faction, key)) return false;
            l.d.researched.push_back({ faction, key });
            return true;
        } },
        { "storefilter", [](Line& l) {
            SaveProp* p = l.prop();
            unsigned m = 0;
            if (!p || !l.read(m)) return false;
            p->storeFilter = (std::uint32_t)m & STORE_ALL;
            return true;
        } },
        { "material", [](Line& l) {
            SaveProp* p = l.prop();
            int m = 0;
            if (!p || !l.read(m) || m < 0 || m >= (int)BuildMaterial::Count) return false;
            p->material = m;
            return true;
        } },
        { "blueprint", [](Line& l) {
            SaveProp* p = l.prop();
            Blueprint b;
            if (!p || !l.read(b.supplied, b.progress)) return false;
            b.supplied = std::max(0, b.supplied);
            b.progress = std::clamp(b.progress, 0.0, 1.0);
            p->blueprint = b;
            return true;
        } },
        { "down", [](Line& l) {
            SaveUnit* u = l.unit();
            Unconscious un;
            if (!u || !l.read(un.dying, un.koFaction, un.conscious, un.tickPart)) return false;
            u->down = un;
            return true;
        } },
        { "corpse", [](Line& l) {
            SaveUnit* u = l.unit();
            double hours = 0.0;
            if (!u || !l.read(hours)) return false;
            u->corpseHours = hours;
            return true;
        } },
        { "retreat", [](Line& l) {
            SaveUnit* u = l.unit();
            Retreating r;
            if (!u || !l.read(r.x, r.z, r.planned, r.home, r.arrived, r.wasPassive, r.hours)) return false;
            u->retreat = r;
            return true;
        } },
        { "carried", [](Line& l) { SaveUnit* u = l.unit(); return u && l.read(u->carriedBy); } },
        { "still", [](Line& l) { SaveUnit* u = l.unit(); if (u) u->playingDead = true; return u != nullptr; } },
        { "color", [](Line& l) {
            SaveUnit* u = l.unit();
            int hue = 0;
            if (!u || !l.read(hue)) return false;
            u->coloration = ((hue % 360) + 360) % 360;
            return true;
        } },
        { "taming", [](Line& l) {
            SaveUnit* u = l.unit();
            double hours = 0.0;
            if (!u || !l.read(hours) || hours < 0.0) return false;
            u->tamingHours = hours;
            return true;
        } },
        { "modes", [](Line& l) { SaveUnit* u = l.unit(); return u && l.read(u->passive, u->block, u->sneak); } },
        { "buff", [](Line& l) { SaveUnit* u = l.unit(); return u && l.read(u->buffLeft, u->buffAttack); } },
        { "provoked", [](Line& l) {
            SaveUnit* u = l.unit();
            Provoked::Entry p;
            if (!u || !l.read(p.faction, p.secondsLeft)) return false;
            u->provoked.push_back(p);
            return true;
        } },
        { "follow", [](Line& l) { SaveUnit* u = l.unit(); return u && l.read(u->followUnit); } },
        { "defend", [](Line& l) { SaveUnit* u = l.unit(); return u && l.read(u->defendUnit); } },
        { "attack", [](Line& l) { SaveUnit* u = l.unit(); return u && l.read(u->attackUnit); } },
        { "move", [](Line& l) {
            SaveUnit* u = l.unit();
            Vec2 to{ 0.0f, 0.0f };
            if (!u || !l.read(to.x, to.z)) return false;
            u->move = to;
            return true;
        } },
        { "floormove", [](Line& l) {
            SaveUnit* u = l.unit();
            Vec2 to{ 0.0f, 0.0f };
            int storey = 0;
            if (!u || !l.read(to.x, to.z, storey) || storey < 0 || storey > STOREY_MAX) return false;
            u->floorMove = to;
            u->floorMoveStorey = storey;
            return true;
        } },
        { "propgo", [](Line& l) { SaveUnit* u = l.unit(); return u && l.read(u->propOrder, u->propAction); } },
        { "working", [](Line& l) { SaveUnit* u = l.unit(); return u && l.read(u->workProp, u->workUnlocking); } },
        { "task", [](Line& l) {
            SaveUnit* u = l.unit();
            int on = 0, percent = 0, propKind = -1;
            std::string kindKey, propKey;
            SaveRule r;
            if (!u || !l.read(on, kindKey, propKey, r.prop, percent)) return false;
            const int kind = behaviorNamed(kindKey);
            if (kind < 0) return false;
            if (propKey != "-") {
                PropKind named = PropKind::Wagon;
                if (!propKindNamed(propKey, named)) return false;
                propKind = idx(named);
            }
            r.rule = behaviorFromFields(on, kind, propKind, percent);
            u->rules.push_back(r);
            return true;
        } },
        { "step", [](Line& l) {
            SaveUnit* u = l.unit();
            std::string key;
            if (!u || !l.read(key)) return false;
            u->stepKind = behaviorNamed(key);
            return u->stepKind >= 0;
        } },
        { "end", [](Line& l) {
            size_t props = 0, units = 0;
            l.ended = l.read(props, units) && props == l.d.props.size() && units == l.d.units.size();
            return l.ended;
        } },
    };
    return table;
}

// The header ends at the map line and the clock after it, so listing saves stays quick
bool readSaveHeader(const std::string& path, SaveSummary& s) {
    std::ifstream in(fs::u8path(path));
    if (!in) return false;
    s.path = path;
    SaveData d;
    d.gs.clockSeconds = -1.0;
    Line l{ {}, d };
    std::string text, key;
    bool versionOk = false, mapRead = false;
    while (std::getline(in, text)) {
        if (!text.empty() && text.back() == '\r') text.pop_back();
        l.ss = std::istringstream(text);
        l.ss >> key;
        if (mapRead) {
            // Only the clock is wanted past the map line; a file of any version is still listed if it isn't there
            if (key == "clock") {
                const auto it = handlers().find(key);
                if (it == handlers().end() || !it->second(l)) d.gs.clockSeconds = -1.0;
            }
            break;
        }
        if (key == "map") {
            const auto it = handlers().find(key);
            if (it == handlers().end() || !it->second(l)) break;
            mapRead = true;
            continue;
        }
        if (key == "version") {
            int version = 0;
            if (!l.read(version) || version < SAVE_OLDEST_VERSION || version > SAVE_VERSION) return false;
            versionOk = true;
            continue;
        }
        if (key == "sizes") continue;   // only in version 6
        const auto it = handlers().find(key);
        if (it == handlers().end() || !it->second(l)) return false;
    }
    s.name = d.name;
    s.timestamp = d.timestamp;
    s.autosave = d.autosave;
    s.mapSource = d.mapSource;
    s.mapStem = d.mapStem;
    s.clock = d.gs.clockSeconds;
    return versionOk;
}
}

std::string savePreviewPath(const std::string& path) {
    return fs::u8path(path).replace_extension(".png").u8string();
}

bool writeSaveFile(const std::string& path, const SaveData& d) {
    std::error_code ec;
    fs::create_directories(fs::u8path(saveDir()), ec);

    const fs::path target = fs::u8path(path), tmp = fs::u8path(path + ".tmp");
    bool written = false;
    {
        std::ofstream out(tmp);
        if (out) {
            writeSaveLines(out, d);
            out.close();
            written = !out.fail();
        }
    }
    if (written) {
        flushToDisk(tmp);
        // Antivirus and cloud sync can hold the target open for a moment
        for (int attempt = 0; attempt < RENAME_TRIES; ++attempt) {
            fs::rename(tmp, target, ec);
            if (!ec) return true;
            SDL_Delay(RENAME_RETRY_MS);
        }
    }
    fs::remove(tmp, ec);
    return false;
}

bool readSaveFile(const std::string& path, SaveData& d) {
    std::ifstream in(fs::u8path(path));
    if (!in) return false;
    std::vector<std::string> lines;
    std::string text, key;
    while (std::getline(in, text)) {
        if (!text.empty() && text.back() == '\r') text.pop_back();
        lines.push_back(text);
    }
    if (!upgradeSaveLines(lines)) {
        SDL_Log("%s: of a version this game can't read, or damaged", path.c_str());
        return false;
    }
    Line l{ {}, d };
    int number = 0;
    for (const std::string& line : lines) {
        ++number;
        text = line;
        l.ss = std::istringstream(text);
        l.ss >> key;
        const auto it = handlers().find(key);
        if (it == handlers().end() || !it->second(l)) {
            SDL_Log("%s: bad line %d: %s", path.c_str(), number, text.c_str());
            return false;
        }
    }
    if (!l.ended) SDL_Log("%s: the file ends early", path.c_str());
    return l.ended;
}

std::vector<SaveSummary> listSaves() {
    std::vector<SaveSummary> out;
    std::error_code ec;
    const fs::path dir = fs::u8path(saveDir());
    if (!fs::is_directory(dir, ec)) return out;

    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (entry.path().extension() != ".sav") continue;
        SaveSummary s;
        if (readSaveHeader(entry.path().u8string(), s)) out.push_back(s);
    }
    std::sort(out.begin(), out.end(),
              [](const SaveSummary& a, const SaveSummary& b) { return a.timestamp > b.timestamp; });
    return out;
}

std::string nextAutosavePath() {
    std::string best = saveDir() + "/autosave0.sav";
    long long oldest = std::numeric_limits<long long>::max();
    for (int i = 0; i < AUTOSAVE_SLOTS; ++i) {
        std::string p = saveDir() + "/autosave" + std::to_string(i) + ".sav";
        std::error_code ec;
        if (!fs::exists(fs::u8path(p), ec)) return p;
        SaveSummary s;
        const long long ts = readSaveHeader(p, s) ? s.timestamp : 0;
        if (ts < oldest) { oldest = ts; best = p; }
    }
    return best;
}

std::string manualSavePath(long long ts) {
    return saveDir() + "/manual_" + std::to_string(ts) + ".sav";
}
