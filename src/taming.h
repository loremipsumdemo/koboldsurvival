#pragma once
#include <algorithm>
#include "components.h"
#include "ecs.h"

// Taming: an animal (a mob, a boss among them; never a Kobold) knocked out and carried into a Taming Cage (props.txt flag
// "taming") is tamed by time alone: about one game day for every 100 of its most health. Once the time is up it joins
// the cage's faction (the cage's owner, or the faction whose claim it stands in), and the cage lets it out. Taken out
// before then, it loses what it had.
inline constexpr double TAME_HOURS_PER_100_HEALTH = 24.0;
inline constexpr double TAME_HOURS_LEAST = 1.0;

// On an animal in a Taming Cage of another faction's: game hours it has been there
struct Taming {
    double hours = 0.0;
};

// Game hours a mob of this most health takes to tame
inline double tameHoursFor(int maxHp) { return std::max(TAME_HOURS_LEAST, TAME_HOURS_PER_100_HEALTH * (double)std::max(1, maxHp) / 100.0); }
// Game hours this animal takes to tame
double tameHoursNeeded(World& w, ObjectId animal);
// How far along its taming is, 0 to 1 (0 when it isn't being tamed)
float tamingProgress(World& w, ObjectId animal);

bool isRawMeat(int item);
// The Taming Cage this unit is locked in, or NO_OBJECT
ObjectId tamingCageOf(World& w, ObjectId animal);
// The faction taming the animal in this cage, or -1
int tamingFaction(World& w, ObjectId cage);
// Moves an animal into a faction as tamed: out of its cage, and yours to order if the faction is yours
void tameAnimal(World& w, ObjectId animal, int faction);
// Counts the hours of each caged animal, and tames those whose time is up
void tamingSystem(World& w, double gameSeconds);
