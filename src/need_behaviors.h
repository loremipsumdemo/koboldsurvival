#pragma once
#include <cstddef>
#include "combat.h"
#include "ecs.h"

// Share hands items over at trading range: a unit this close to another can take its food or healing item. One
// in need with no sharer that close walks to one within SHARE_SEEK_RANGE (automationSystem in systems.h does the
// walk), stopping SHARE_WALK_STOP from it.
constexpr float SHARE_RANGE = TALK_RANGE;
constexpr float SHARE_SEEK_RANGE = 10.0f;
constexpr float SHARE_WALK_STOP = 0.8f;
// Whether g can share an item for the stat (food: hunger, else health) with taker now: Share on, of an allied
// faction, able to act, carrying such an item and needing it less than the taker, whose share of the stat is
// takerValue. Distance is not checked.
bool canShareWith(World& w, Entity g, Entity taker, bool food, int takerValue);

// Runs every unit's Eat and Heal behaviors (behavior_rules.h), from the top of each list down. One is checked when the
// stat it watches has changed, when the unit's list was edited, or when the wait after a failed try (no item to use,
// none to share) has run out. dt is the real seconds since the last call, for those waits. The automated behaviors are
// worked by automationSystem (systems.h). Returns how many behaviors were checked.
size_t behaviorSystem(World& w, CombatLog& log, float dt);
