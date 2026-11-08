#pragma once
#include <map>
#include <string>
#include <vector>

class World;

constexpr int FACTION_PLAYER = 0;
constexpr int FACTION_NOMAD = 6;

constexpr int REP_MIN = -100;
constexpr int REP_MAX = 100;
constexpr int REP_HOSTILE = -25;
constexpr int REP_ALLIED = 50;
constexpr int REP_KO_PENALTY = 5;
constexpr int REP_DEATH_PENALTY = 5;

struct FactionColor {
    float r = 1.0f, g = 1.0f, b = 1.0f;
};

struct FactionDef {
    int id = 0;
    std::string nameKey;
    FactionColor color;
    int units = 0;
};

bool loadFactions(const std::string& text, std::string& err);

const std::vector<FactionDef>& factions();
const FactionDef* findFaction(int id);
std::string factionName(int id);
FactionColor factionColor(int id);

int defaultReputation(int a, int b);
int reputation(World& w, int a, int b);
bool factionsHostile(World& w, int a, int b);
bool factionsAllied(World& w, int a, int b);
// Whether a faction may hold an Altar and the claim around it: all but the Nomads and the mobs (boss and wild alike)
bool factionCanClaim(int faction);

std::map<int, int> playerReputation(World& w);
const std::vector<FactionDef>& reputationFactions();
int reputationFactionCount();
void setPlayerReputation(World& w, const std::map<int, int>& standings);
int changePlayerReputation(World& w, int faction, int delta);
