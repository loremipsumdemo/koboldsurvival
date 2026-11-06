#pragma once
#include <cstddef>
#include <string>
#include <vector>
#include "ecs.h"

// The game log: every message the game writes for the Information Panel. The fight's hits, misses and damage, XP,
// pickups, drops, loot, faction changes, and what was said in talks.

constexpr size_t LOG_LIMIT = 500;   // messages each kind keeps, and the talk log keeps: the newest are kept

// What a game message is about, so the Game tab can hide each kind. Other messages are never hidden.
// Npc is no message's kind: it is the filter toggle "Show NPC Info", for the lines about NPCs only (LogLine::npc),
// off at first.
enum class LogKind { Other, Seen, Hit, Miss, Damage, Xp, Pickup, Drop, Loot, Npc };
constexpr int LOG_KIND_COUNT = 10;

// Green when a player unit damages a hostile, red when a hostile damages a player unit, gray otherwise
enum class Tint { Gray, Green, Red };
Tint tintFor(World& w, Entity attacker, Entity target);

struct LogLine {
    std::string text;
    Tint tint = Tint::Gray;
    LogKind kind = LogKind::Other;
    bool npc = false;   // about NPCs only: none of your units is in it. Hidden unless Show NPC Info is on.
};

// Whether a message about these units is about NPCs only: none of them is one of yours
bool npcOnly(World& w, Entity a, Entity b = NULL_ENTITY);

// Which kinds the Game tab shows. All are on at first, except Show NPC Info.
struct LogFilter {
    bool on[LOG_KIND_COUNT];
    LogFilter() {
        for (bool& b : on) b = true;
        on[(int)LogKind::Npc] = false;
    }
};

// Every line the game has written, oldest first, up to LOG_LIMIT of each kind
struct GameLog {
    std::vector<LogLine> lines;
    // Swings that landed, and swings that did not (missed or dodged), since the level began. The caller compares
    // these before and after a frame, to play a sound once for what happened in it.
    size_t hits = 0;
    size_t misses = 0;
    // What was said in talks, as "Name: text", for the Message tab
    std::vector<LogLine> spoken;
    // Messages of each kind held now, so a kind over LOG_LIMIT drops its own oldest and nothing else
    size_t kindCount[LOG_KIND_COUNT] = {};
    // Adds a line to the talk log, keeping the newest LOG_LIMIT
    void say(const std::string& text);
    void add(const std::string& text, Tint tint = Tint::Gray, LogKind kind = LogKind::Other, bool npc = false);
};

// Game messages written outside a fight (XP, pickups, drops). They wait until flushQueuedLog adds them to a log, on
// the next frame. discardQueuedLog drops them, when a new level starts.
void queueLog(const std::string& text, LogKind kind, bool npc = false);
void flushQueuedLog(GameLog& log);
void discardQueuedLog();

// The lines an Information Panel tab shows: 0 what was said in talks, 1 the game messages that pass the filter
std::vector<const LogLine*> infoLines(const GameLog& log, int tab, const LogFilter& f);
