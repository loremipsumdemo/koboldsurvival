#pragma once
#include <cstddef>
#include <string>
#include <vector>
#include "ecs.h"

constexpr size_t LOG_LIMIT = 500;

enum class LogKind { Other, Seen, Hit, Miss, Damage, Xp, Pickup, Drop, Loot, Npc, PathFail, TaskFail, Blocked, Claim };
constexpr int LOG_KIND_COUNT = 14;

enum class Tint { Gray, Green, Red };
Tint tintFor(World& w, ObjectId attacker, ObjectId target);

struct LogLine {
    std::string text;
    Tint tint = Tint::Gray;
    LogKind kind = LogKind::Other;
    bool npc = false;
    // Where it happened, for the * at the start of the line that takes the camera there. Not every message has a place.
    bool located = false;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    int storey = 0;   // the floor it happened on
};

// Who a message is about: whether it is only about other factions' business (shown only with the NPC messages), and whose place
// it happened at (a unit or a prop, read when the message is made). A plain bool says the first and leaves the place out.
struct LogWho {
    bool npc = false;
    ObjectId at = NO_OBJECT;
    LogWho(bool npcOnlyMessage = false, ObjectId place = NO_OBJECT) : npc(npcOnlyMessage), at(place) {}
};

// Not about the player's own units, so an NPC message; the place is the first of the two that is given
LogWho npcOnly(World& w, ObjectId a, ObjectId b = NO_OBJECT);
LogLine makeLogLine(World& w, const std::string& text, LogKind kind, LogWho who = {}, Tint tint = Tint::Gray);

struct LogFilter {
    bool on[LOG_KIND_COUNT];
    LogFilter() {
        for (bool& b : on) b = true;
        on[(int)LogKind::Npc] = false;
        on[(int)LogKind::PathFail] = false;
        on[(int)LogKind::TaskFail] = false;
        on[(int)LogKind::Blocked] = false;
    }
};

struct GameLog {
    std::vector<LogLine> lines;
    std::vector<LogLine> spoken;
    size_t kindCount[LOG_KIND_COUNT] = {};
    void say(const std::string& text);
    void add(const std::string& text, Tint tint = Tint::Gray, LogKind kind = LogKind::Other, bool npc = false);
    void add(const LogLine& line);
};

void queueLog(World& w, const std::string& text, LogKind kind, LogWho who = {}, Tint tint = Tint::Gray);

std::vector<const LogLine*> infoLines(const GameLog& log, int tab, const LogFilter& f);
size_t infoLineCount(const GameLog& log, int tab, const LogFilter& f);
