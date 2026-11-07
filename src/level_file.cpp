#include "level_file.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "build.h"
#include "game_tables.h"
#include "items.h"
#include "spatial.h"
#include "systems.h"

namespace fs = std::filesystem;

std::string levelFilePath(const std::string& name) {
    return std::string(LEVEL_FOLDER) + "/" + name + ".level";
}

std::string levelToText(const LevelData& d) {
    std::ostringstream out;
    out << "format 1\nmap " << d.map << "\nnext " << d.nextId << "\n";
    char line[160];
    if (d.hasSpawn) {
        std::snprintf(line, sizeof line, "spawn %.2f %.2f\n", d.spawnX, d.spawnZ);
        out << line;
    }
    for (const LevelObject& o : d.objects) {
        switch (o.type) {
        case LevelObject::Type::Prop:
            std::snprintf(line, sizeof line, "prop %d %s %.2f %.2f %.3f %d\n", o.id, propInfo((PropKind)o.kind).key.c_str(), o.x, o.z, o.yaw, o.owner);
            break;
        case LevelObject::Type::Mob:
            std::snprintf(line, sizeof line, "mob %d %s %.2f %.2f\n", o.id, mobInfo((MobKind)o.kind).key.c_str(), o.x, o.z);
            break;
        case LevelObject::Type::Item:
            std::snprintf(line, sizeof line, "item %d %d %d %.2f %.2f\n", o.id, o.kind, o.count, o.x, o.z);
            break;
        }
        out << line;
    }
    return out.str();
}

bool levelFromText(const std::string& text, LevelData& out, std::string& err) {
    LevelData d;
    std::istringstream in(text);
    std::string line;
    int number = 0, format = 0;
    while (std::getline(in, line)) {
        ++number;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream ls(line);
        std::string key;
        if (!(ls >> key) || key[0] == '#') continue;
        bool ok = true;
        if (key == "format") {
            ok = (bool)(ls >> format);
        } else if (key == "map") {
            std::getline(ls >> std::ws, d.map);
        } else if (key == "next") {
            ok = (bool)(ls >> d.nextId);
        } else if (key == "spawn") {
            ok = (bool)(ls >> d.spawnX >> d.spawnZ);
            d.hasSpawn = ok;
        } else if (key == "prop") {
            LevelObject o;
            std::string kind;
            PropKind named = PropKind::Wagon;
            ok = (ls >> o.id >> kind >> o.x >> o.z >> o.yaw >> o.owner) && propKindNamed(kind, named);
            o.type = LevelObject::Type::Prop;
            o.kind = idx(named);
            d.objects.push_back(o);
        } else if (key == "mob") {
            LevelObject o;
            std::string kind;
            MobKind named = MobKind::None;
            ok = (ls >> o.id >> kind >> o.x >> o.z) && mobKindNamed(kind, named);
            o.type = LevelObject::Type::Mob;
            o.kind = idx(named);
            d.objects.push_back(o);
        } else if (key == "item") {
            LevelObject o;
            ok = (ls >> o.id >> o.kind >> o.count >> o.x >> o.z) && findItem(o.kind) != nullptr;
            o.type = LevelObject::Type::Item;
            d.objects.push_back(o);
        } else {
            ok = false;
        }
        if (!ok) {
            err = "line " + std::to_string(number) + ": " + line;
            return false;
        }
    }
    if (format != 1) { err = "not a version 1 level"; return false; }
    if (d.map.empty()) { err = "names no map"; return false; }
    out = std::move(d);
    return true;
}

bool writeLevelFile(const std::string& path, const LevelData& d, std::string& err) {
    std::error_code ec;
    fs::create_directories(fs::u8path(path).parent_path(), ec);
    std::ofstream out(fs::u8path(path), std::ios::binary);
    out << levelToText(d);
    out.close();
    if (!out) { err = "could not write " + path; return false; }
    return true;
}

bool readLevelFile(const std::string& path, LevelData& out, std::string& err) {
    std::ifstream in(fs::u8path(path), std::ios::binary);
    if (!in) { err = "could not read " + path; return false; }
    std::ostringstream all;
    all << in.rdbuf();
    return levelFromText(all.str(), out, err);
}

std::vector<std::string> listLevelFiles(const std::string& map) {
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(fs::u8path(LEVEL_FOLDER), ec)) {
        if (entry.path().extension() != ".level") continue;
        LevelData d;
        std::string err;
        if (readLevelFile(entry.path().u8string(), d, err) && d.map == map) out.push_back(entry.path().stem().u8string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

Entity placeLevelObject(World& w, const Terrain& t, Rng& rng, const LevelObject& o) {
    Entity e = NULL_ENTITY;
    const Transform at{ o.x, t.heightAt(o.x, o.z), o.z };
    switch (o.type) {
    case LevelObject::Type::Prop: {
        const PropKind kind = (PropKind)o.kind;
        if (propInfo(kind).built && o.owner >= 0) {
            e = placeBuiltProp(w, t, kind, o.x, o.z, o.yaw, o.owner);
        } else {
            e = makeProp(w, kind, at, o.yaw);
            if (o.owner >= 0) w.add(e, Owner{ o.owner });
        }
        break;
    }
    case LevelObject::Type::Mob:
        e = spawnMob(w, t, rng, (MobKind)o.kind, mobFaction((MobKind)o.kind), o.x, o.z);
        break;
    case LevelObject::Type::Item:
        e = spawnGroundItem(w, at, o.kind, o.count);
        break;
    }
    if (e != NULL_ENTITY) w.add(e, LevelId{ o.id });
    return e;
}

void applyLevel(World& w, const Terrain& t, Rng& rng, const LevelData& d) {
    w = World{};
    LevelInfo& info = w.resource<LevelInfo>();
    info.spawnSet = d.hasSpawn;
    info.spawnX = d.spawnX;
    info.spawnZ = d.spawnZ;
    info.nextId = d.nextId;
    for (const LevelObject& o : d.objects) placeLevelObject(w, t, rng, o);
}

LevelData captureLevel(World& w, const std::string& map) {
    LevelData d;
    d.map = map;
    LevelInfo& info = w.resource<LevelInfo>();
    d.hasSpawn = info.spawnSet;
    d.spawnX = info.spawnX;
    d.spawnZ = info.spawnZ;
    const auto idOf = [&](Entity e) {
        if (const LevelId* id = w.get<LevelId>(e)) return id->id;
        return w.add(e, LevelId{ info.nextId++ }).id;
    };
    std::vector<Entity> props = w.view<Prop, Transform>();
    std::sort(props.begin(), props.end());
    for (Entity e : props) {
        LevelObject o;
        o.type = LevelObject::Type::Prop;
        o.id = idOf(e);
        o.kind = idx(w.get<Prop>(e)->kind);
        o.yaw = w.get<Prop>(e)->yaw;
        o.x = w.get<Transform>(e)->x;
        o.z = w.get<Transform>(e)->z;
        if (const Owner* own = w.get<Owner>(e)) o.owner = own->faction;
        d.objects.push_back(o);
    }
    std::vector<Entity> mobs = w.view<Mob, Transform>();
    std::sort(mobs.begin(), mobs.end());
    for (Entity e : mobs) {
        LevelObject o;
        o.type = LevelObject::Type::Mob;
        o.id = idOf(e);
        o.kind = idx(w.get<Mob>(e)->kind);
        o.x = w.get<Transform>(e)->x;
        o.z = w.get<Transform>(e)->z;
        d.objects.push_back(o);
    }
    std::vector<Entity> items = w.view<GroundItem, Transform>();
    std::sort(items.begin(), items.end());
    for (Entity e : items) {
        LevelObject o;
        o.type = LevelObject::Type::Item;
        o.id = idOf(e);
        o.kind = w.get<GroundItem>(e)->item;
        o.count = w.get<GroundItem>(e)->count;
        o.x = w.get<Transform>(e)->x;
        o.z = w.get<Transform>(e)->z;
        d.objects.push_back(o);
    }
    d.nextId = info.nextId;
    return d;
}
