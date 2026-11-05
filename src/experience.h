#pragma once
#include <string>
#include "ecs.h"
#include "game_log.h"
#include "stats.h"

// Experience: the XP a unit earns in a stat, and the stats it has now.

// Gives a unit fractional XP in one stat. Whole points go to the stat, and the rest waits in the unit's
// XpBuffer until it adds up to a point. Only player units gain XP.
// why names the action the XP came from, for the log; nullptr writes no message. kind is the Game tab filter its
// message falls under: Xp sorts it by the stat (Athletics and Endurance have their own), or a kind given here.
void grantXp(World& w, Entity e, StatId stat, float amount, const std::string& why = std::string(), LogKind kind = LogKind::Xp);
Stats statsOf(World& w, Entity e);   // base stats plus the gear and buffs on the unit
