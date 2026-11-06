#include "game_log.h"
#include "components.h"

namespace {
std::vector<LogLine> gQueued;
}

void queueLog(const std::string& text, LogKind kind, bool npc) {
    gQueued.push_back(LogLine{ text, Tint::Gray, kind, npc });
}

bool npcOnly(World& w, Entity a, Entity b) {
    const bool yours = (a != NULL_ENTITY && w.has<Commandable>(a)) || (b != NULL_ENTITY && w.has<Commandable>(b));
    return !yours;
}

void GameLog::say(const std::string& text) {
    spoken.push_back(LogLine{ text, Tint::Gray, LogKind::Other });
    if (spoken.size() > LOG_LIMIT) spoken.erase(spoken.begin());
}

void flushQueuedLog(GameLog& log) {
    for (const LogLine& ln : gQueued) log.add(ln.text, ln.tint, ln.kind, ln.npc);
    gQueued.clear();
}

void discardQueuedLog() {
    gQueued.clear();
}

std::vector<const LogLine*> infoLines(const GameLog& log, int tab, const LogFilter& f) {
    std::vector<const LogLine*> out;
    if (tab == 2) return out;
    if (tab == 0) {
        for (const LogLine& ln : log.spoken) out.push_back(&ln);
        return out;
    }
    for (const LogLine& ln : log.lines) {
        if (ln.npc && !f.on[(int)LogKind::Npc]) continue;
        if (ln.kind == LogKind::Other || f.on[(int)ln.kind]) out.push_back(&ln);
    }
    if (out.size() > LOG_LIMIT) out.erase(out.begin(), out.begin() + (out.size() - LOG_LIMIT));
    return out;
}

size_t infoLineCount(const GameLog& log, int tab, const LogFilter& f) {
    if (tab == 2) return 0;
    if (tab == 0) return log.spoken.size();
    size_t n = 0;
    for (const LogLine& ln : log.lines) {
        if (ln.npc && !f.on[(int)LogKind::Npc]) continue;
        if (ln.kind == LogKind::Other || f.on[(int)ln.kind]) ++n;
    }
    return std::min(n, LOG_LIMIT);
}

Tint tintFor(World& w, Entity attacker, Entity target) {
    if (w.has<Commandable>(attacker) && w.has<Hostile>(target)) return Tint::Green;
    if (w.has<Hostile>(attacker) && w.has<Commandable>(target)) return Tint::Red;
    return Tint::Gray;
}

void GameLog::add(const std::string& text, Tint tint, LogKind kind, bool npc) {
    lines.push_back(LogLine{ text, tint, kind, npc });
    const LogKind bucket = npc ? LogKind::Npc : kind;
    if (++kindCount[(int)bucket] > LOG_LIMIT) {
        for (auto it = lines.begin(); it != lines.end(); ++it) {
            if ((it->npc ? LogKind::Npc : it->kind) == bucket) {
                lines.erase(it);
                break;
            }
        }
        --kindCount[(int)bucket];
    }
}
