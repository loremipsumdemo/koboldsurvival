#pragma once
#include <cstddef>
#include <string>
#include <vector>
#include "ecs.h"

constexpr size_t LOG_LIMIT = 500;

enum class LogKind { Other, Seen, Hit, Miss, Damage, Xp, Pickup, Drop, Loot, Npc };
constexpr int LOG_KIND_COUNT = 10;

enum class Tint { Gray, Green, Red };
Tint tintFor(World& w, ObjectId attacker, ObjectId target);

struct LogLine {
    std::string text;
    Tint tint = Tint::Gray;
    LogKind kind = LogKind::Other;
    bool npc = false;
};

bool npcOnly(World& w, ObjectId a, ObjectId b = NO_OBJECT);

struct LogFilter {
    bool on[LOG_KIND_COUNT];
    LogFilter() {
        for (bool& b : on) b = true;
        on[(int)LogKind::Npc] = false;
    }
};

struct GameLog {
    std::vector<LogLine> lines;
    std::vector<LogLine> spoken;
    size_t kindCount[LOG_KIND_COUNT] = {};
    void say(const std::string& text);
    void add(const std::string& text, Tint tint = Tint::Gray, LogKind kind = LogKind::Other, bool npc = false);
};

void queueLog(World& w, const std::string& text, LogKind kind, bool npc = false, Tint tint = Tint::Gray);

std::vector<const LogLine*> infoLines(const GameLog& log, int tab, const LogFilter& f);
size_t infoLineCount(const GameLog& log, int tab, const LogFilter& f);
