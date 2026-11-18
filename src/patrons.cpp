#include "patrons.h"
#include <algorithm>
#include <cstdio>
#include <sstream>
#include "factions.h"
#include "mobs.h"

namespace {
std::vector<PatronInfo>& table() {
    static std::vector<PatronInfo> t;
    return t;
}

bool holdsPatron(int faction) { return factionCanClaim(faction) && !isMobFaction(faction) && faction != FACTION_FACTIONLESS; }

// Gives each faction its own patron: the one its row names (yours has the one of its color), and to one that names none, or a taken one, the
// first of those left
void fill(World& w) {
    Patrons& p = w.resource<Patrons>();
    p.byFaction.clear();
    const int count = (int)table().size();
    if (count == 0) return;
    std::vector<bool> taken((size_t)count, false);
    std::vector<int> without;
    for (const FactionDef& f : factions()) {
        if (!holdsPatron(f.id)) continue;
        int index = -1;
        for (int i = 0; i < count && f.patron > 0; ++i) {
            if (table()[(size_t)i].id == f.patron) index = i;
        }
        if (index >= 0 && !taken[(size_t)index]) {
            p.byFaction[f.id] = index;
            taken[(size_t)index] = true;
        } else {
            without.push_back(f.id);
        }
    }
    for (const int faction : without) {
        for (int i = 0; i < count; ++i) {
            if (taken[(size_t)i]) continue;
            p.byFaction[faction] = i;
            taken[(size_t)i] = true;
            break;
        }
    }
}
}

bool loadPatrons(const std::string& text, std::string& err) {
    std::vector<PatronInfo> list;
    std::istringstream in(text);
    std::string line;
    int n = 0;
    while (std::getline(in, line)) {
        ++n;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> cols;
        std::istringstream row(line);
        std::string col;
        while (std::getline(row, col, '\t')) cols.push_back(col);
        if (cols.size() < 3) {
            err = "line " + std::to_string(n) + ": a patron is id, picture, mbti";
            return false;
        }
        PatronInfo p;
        p.id = std::atoi(cols[0].c_str());
        p.picture = cols[1];
        p.mbti = cols[2];
        char buf[40];
        std::snprintf(buf, sizeof buf, "PATRON_%02d_NAME", p.id);
        p.nameKey = buf;
        std::snprintf(buf, sizeof buf, "PATRON_%02d_DESC", p.id);
        p.descKey = buf;
        list.push_back(std::move(p));
    }
    table() = std::move(list);
    return true;
}

const std::vector<PatronInfo>& patrons() { return table(); }

int patronOf(World& w, int faction) {
    const auto& by = w.resource<Patrons>().byFaction;
    const auto it = by.find(faction);
    return it == by.end() ? -1 : it->second;
}

int patronHolder(World& w, int patron) {
    for (const auto& [f, held] : w.resource<Patrons>().byFaction) {
        if (held == patron) return f;
    }
    return -1;
}

void newPatrons(World& w) {
    w.resource<PatronTalk>().out.clear();
    fill(w);
}

void ensurePatrons(World& w) { fill(w); }

void patronEvent(World& w, PatronEvent event, int caller, int responder) {
    if (caller == responder || patronOf(w, caller) < 0 || patronOf(w, responder) < 0) return;
    std::vector<PatronCue>& out = w.resource<PatronTalk>().out;
    if (out.size() < 64) out.push_back(PatronCue{ event, caller, responder });
}

const char* patronEventKey(PatronEvent event) {
    switch (event) {
    case PatronEvent::Raid: return "PATRON_RAID";
    case PatronEvent::Hostile: return "PATRON_HOSTILE";
    case PatronEvent::Allied: return "PATRON_ALLIED";
    case PatronEvent::Trade: return "PATRON_TRADE";
    case PatronEvent::Rescue: return "PATRON_RESCUE";
    case PatronEvent::Attack: return "PATRON_ATTACK";
    case PatronEvent::Kill: return "PATRON_KILL";
    case PatronEvent::Trespass: return "PATRON_TRESPASS";
    case PatronEvent::Damage: return "PATRON_DAMAGE";
    case PatronEvent::Caged: return "PATRON_CAGED";
    case PatronEvent::Idle: return "PATRON_IDLE1";
    default: return "PATRON_RAID";
    }
}
