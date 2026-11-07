#pragma once
#include <array>
#include <cstddef>
#include <string>
#include <vector>
#include "ecs.h"

// Your faction's units are of two kinds:
//  - military: the units you command (Commandable), each in one of nine numbered squads of at most 16
//  - citizens: units the game commands (Citizen), who stay near your altar
// The military can never hold more than its nine full squads. Citizens are limited by the soft and hard caps in Settings.
inline constexpr int SQUAD_COUNT = 9;
inline constexpr int SQUAD_SIZE = 16;
inline constexpr int MILITARY_MAX = SQUAD_COUNT * SQUAD_SIZE;
inline constexpr size_t SQUAD_NAME_CHARS = 24;

// A military unit's squad, 0 to SQUAD_COUNT - 1. It is shown to the player as 1 to 9.
struct SquadMember {
    int squad = 0;
};

// One of your units that the game commands
struct Citizen {};

// In the World's resources: the squads' names (empty is the default name) and which squad the Squad tab shows
struct SquadBook {
    std::array<std::string, SQUAD_COUNT> names{};
    int current = 0;
};

std::string squadLabel(World& w, int squad);
std::vector<ObjectId> squadUnits(World& w, int squad);
std::array<int, SQUAD_COUNT> squadSizes(World& w);
std::vector<ObjectId> militaryUnits(World& w);
std::vector<ObjectId> citizenUnits(World& w);
int squadOf(World& w, ObjectId u);   // -1 for a unit in no squad

// The squad a unit should join: the preferred one if it has room, or else the first with room. -1 if every squad is full.
int squadWithRoom(World& w, int preferred);

// Moves a military unit into a squad with room. False if the squad is full; the unit stays where it was.
bool moveToSquad(World& w, ObjectId u, int squad);

// Hands a unit to the game: it leaves its squad and the selection, drops its orders, and stays near your altar
void makeCitizen(World& w, ObjectId u);

// Gives a citizen back to you, in the preferred squad or the first with room. False if every squad is full.
bool makeMilitary(World& w, ObjectId u, int preferred);

// Puts every military unit in a squad, keeping each squad to SQUAD_SIZE. A unit with no room in its own squad goes to
// another squad with room, and once every squad is full, it becomes a citizen. Returns how many became citizens.
int settleSquads(World& w);
