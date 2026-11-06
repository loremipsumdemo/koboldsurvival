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

// A list that changed size means the numbers in a save name other things
constexpr int SAVED_LIST_SIZES[] = { PROP_KIND_COUNT, MOB_KIND_COUNT, STAT_COUNT, idx(BehaviorKind::Count), GEAR_SLOTS, INVENTORY_SLOTS, STORAGE_SLOTS };

void writeUnit(Writer& w, size_t i, const SaveUnit& u) {
    w.line("unit", u.at.x, u.at.y, u.at.z, u.sprite, u.faction, u.mob, u.name);
    const Needs& n = u.needs;
    w.line("needs", i, n.hp, n.maxHp, n.hunger, n.maxHunger, n.rested, n.maxRested, n.comfort, n.maxComfort,
           n.restedPart, n.comfortPart, n.hurried, n.healSeconds);
    for (const SaveSlot& s : u.stacks) w.line("slot", i, s.slot, s.stack.item, s.stack.count);
    for (const SaveGear& g : u.gear) w.line("gear", i, g.slot, g.item);
    if (u.stats) {
        std::ostringstream xp;
        for (size_t k = 0; k < u.stats->xp.size(); ++k) xp << (k ? " " : "") << u.stats->xp[k];
        w.line("stats", i, xp.str());
    }
    if (u.xpParts) {
        std::ostringstream parts;
        parts << std::setprecision(9);
        for (size_t k = 0; k < u.xpParts->frac.size(); ++k) parts << (k ? " " : "") << u.xpParts->frac[k];
        w.line("xpparts", i, parts.str());
    }
    if (u.body) w.line("body", i, *u.body);
    if (u.home) w.line("home", i, u.home->x, u.home->z, u.home->returning);
    if (u.occupying >= 0) w.line("occupy", i, u.occupying);
    if (u.down) w.line("down", i, u.down->dying, u.down->koFaction, u.down->conscious, u.down->tickPart);
    if (u.corpseHours) w.line("corpse", i, *u.corpseHours);
    if (u.carriedBy >= 0) w.line("carried", i, u.carriedBy);
    if (u.playingDead) w.line("still", i);
    if (u.passive || u.block || u.sneak) w.line("modes", i, u.passive, u.block, u.sneak);
    if (u.buffLeft > 0.0f) w.line("buff", i, u.buffLeft, u.buffAttack);
    for (const Provoked::Entry& p : u.provoked) w.line("provoked", i, p.faction, p.secondsLeft);
    if (u.followUnit >= 0) w.line("follow", i, u.followUnit);
    if (u.defendUnit >= 0) w.line("defend", i, u.defendUnit);
    if (u.attackUnit >= 0) w.line("attack", i, u.attackUnit);
    if (u.move) w.line("move", i, u.move->x, u.move->z);
    if (u.propOrder >= 0) w.line("propgo", i, u.propOrder, u.propAction);
    if (u.workProp >= 0) w.line("working", i, u.workProp, u.workUnlocking);
    for (const SaveRule& r : u.rules) {
        w.line("task", i, r.rule.on, (int)r.rule.kind, r.rule.propKind, r.prop, r.rule.percent);
    }
    if (u.stepKind >= 0) w.line("step", i, u.stepKind);
}

void writeProp(Writer& w, size_t i, const SaveProp& p) {
    w.line("prop", p.kind, p.at.x, p.at.y, p.at.z, p.yaw, p.owner);
    if (p.lock) w.line("lock", i, p.lock->locked, p.lock->level, p.lock->faction);
    for (const SaveSlot& s : p.storage) w.line("storage", i, s.slot, s.stack.item, s.stack.count);
    for (const SaveSlot& s : p.output) w.line("output", i, s.slot, s.stack.item, s.stack.count);
    if (p.fuel) w.line("fuel", i, p.fuel->hours, p.fuel->item);
    if (p.workProgress) w.line("work", i, *p.workProgress, p.workTurn);
    if (p.bonesHours) w.line("bones", i, *p.bonesHours);
    for (const ItemStack& s : p.bonesItems) w.line("bonesitem", i, s.item, s.count);
    if (p.health) w.line("health", i, p.health->value, p.health->max);
}

void writeSaveLines(std::ostream& out, const SaveData& d) {
    Writer w(out);
    w.line("version", SAVE_VERSION);
    w.line("sizes", SAVED_LIST_SIZES[0], SAVED_LIST_SIZES[1], SAVED_LIST_SIZES[2], SAVED_LIST_SIZES[3], SAVED_LIST_SIZES[4],
           SAVED_LIST_SIZES[5], SAVED_LIST_SIZES[6]);
    w.line("name", d.name);
    w.line("timestamp", d.timestamp);
    w.line("autosave", d.autosave);
    w.line("map", d.mapSource, d.mapStem);
    w.line("clock", d.gs.clockSeconds);
    w.line("camera", d.cam.fx, d.cam.fy, d.cam.fz, d.cam.yaw, d.cam.pitch, d.cam.distance);
    w.line("rng", d.rngState);
    for (const auto& [faction, value] : d.reputation) w.line("rep", faction, value);
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
    template <typename A>
    bool readArray(A& values) {
        for (auto& v : values) ss >> v;
        return !ss.fail();
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
        { "sizes", [](Line& l) {
            for (int want : SAVED_LIST_SIZES) {
                int got = 0;
                if (!l.read(got) || got != want) return false;
            }
            return true;
        } },
        { "name", [](Line& l) { return l.rest(l.d.name); } },
        { "timestamp", [](Line& l) { return l.read(l.d.timestamp); } },
        { "autosave", [](Line& l) { return l.read(l.d.autosave); } },
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
        { "prop", [](Line& l) {
            SaveProp p;
            if (!l.read(p.kind, p.at.x, p.at.y, p.at.z, p.yaw, p.owner) || p.kind < 0 || p.kind >= PROP_KIND_COUNT) return false;
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
        { "bonesitem", [](Line& l) {
            SaveProp* p = l.prop();
            ItemStack s;
            if (!p || !l.read(s.item, s.count)) return false;
            p->bonesItems.push_back(s);
            return true;
        } },
        { "health", [](Line& l) {
            SaveProp* p = l.prop();
            Health h;
            if (!p || !l.read(h.value, h.max)) return false;
            p->health = h;
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
            if (!l.read(u.at.x, u.at.y, u.at.z, u.sprite, u.faction, u.mob) || u.mob < 0 || u.mob > MOB_KIND_COUNT) return false;
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
            if (!u || !l.readArray(s.xp)) return false;
            u->stats = s;
            return true;
        } },
        { "xpparts", [](Line& l) {
            SaveUnit* u = l.unit();
            XpBuffer parts;
            if (!u || !l.readArray(parts.frac)) return false;
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
        { "carried", [](Line& l) { SaveUnit* u = l.unit(); return u && l.read(u->carriedBy); } },
        { "still", [](Line& l) { SaveUnit* u = l.unit(); if (u) u->playingDead = true; return u != nullptr; } },
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
        { "propgo", [](Line& l) { SaveUnit* u = l.unit(); return u && l.read(u->propOrder, u->propAction); } },
        { "working", [](Line& l) { SaveUnit* u = l.unit(); return u && l.read(u->workProp, u->workUnlocking); } },
        { "task", [](Line& l) {
            SaveUnit* u = l.unit();
            int on = 0, kind = 0, propKind = -1, percent = 0;
            SaveRule r;
            if (!u || !l.read(on, kind, propKind, r.prop, percent)) return false;
            r.rule = behaviorFromFields(on, kind, propKind, percent);
            u->rules.push_back(r);
            return true;
        } },
        { "step", [](Line& l) { SaveUnit* u = l.unit(); return u && l.read(u->stepKind); } },
        { "end", [](Line& l) {
            size_t props = 0, units = 0;
            l.ended = l.read(props, units) && props == l.d.props.size() && units == l.d.units.size();
            return l.ended;
        } },
    };
    return table;
}

// The header ends at the map line, so listing saves stays quick
bool readSaveHeader(const std::string& path, SaveSummary& s) {
    std::ifstream in(fs::u8path(path));
    if (!in) return false;
    s.path = path;
    SaveData d;
    Line l{ {}, d };
    std::string text, key;
    bool versionOk = false;
    while (std::getline(in, text)) {
        if (!text.empty() && text.back() == '\r') text.pop_back();
        l.ss = std::istringstream(text);
        l.ss >> key;
        if (key == "map") break;
        const auto it = handlers().find(key);
        if (it == handlers().end() || !it->second(l)) return false;
        versionOk = versionOk || key == "version";
    }
    s.name = d.name;
    s.timestamp = d.timestamp;
    s.autosave = d.autosave;
    return versionOk;
}
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
    Line l{ {}, d };
    std::string text, key;
    int number = 0;
    while (std::getline(in, text)) {
        ++number;
        if (!text.empty() && text.back() == '\r') text.pop_back();
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
