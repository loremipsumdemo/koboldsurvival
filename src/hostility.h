#pragma once
#include "ecs.h"

// Which units fight which: their factions (factions.h), provocations (Provoked) and the Hostile and Neutral markers.

// A unit's faction id (factions.h); the Player Faction for one with none
int factionOf(World& w, Entity e);
// Whether two units fight each other: their factions are hostile, or one was provoked by the other's faction. Units of
// the same faction never fight.
bool unitsHostile(World& w, Entity a, Entity b);
// Whether a unit that is not yours fights your units: the Hostile marker it carries (and Neutral when not)
bool hostileToPlayer(World& w, Entity e);
// Makes a unit treat a faction as hostile until it has seen none of its units for PROVOKED_SECONDS (Provoked)
void provoke(World& w, Entity unit, int faction);
// Each frame: counts down each provocation while the unit sees none of that faction's units (and starts it over while
// it sees one), and keeps every other unit's Hostile or Neutral marker in step with its faction's reputation with yours
// and with Provoked. A unit that turns hostile, or calms down again, says so in the log.
void factionSystem(World& w, float dt);
