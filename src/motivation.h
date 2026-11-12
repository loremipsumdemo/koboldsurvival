#pragma once
#include <map>
#include <vector>
#include "claims.h"
#include "ecs.h"

// Motivation: what a faction's units dancing round its Altars and Campfires build up (Dance task, automation.cpp).
//  - Each unit dancing gives its faction 1 point for every in-game hour it dances.
//  - It ticks down by about 10 an hour. The most it holds is 125, and 25 more for every complete building of the faction.
//  - Over 100 (MOTIVATION_FAST), the faction's units walk 50% faster inside the faction's own claims, and its Altar's fire burns
//    bigger and brighter.
// Hovering the faction's Altar shows it as a bar with a mark at the threshold (more thresholds, for other bonuses, may be added). Its
// fire burns much higher over the threshold, and a chime plays as the player's Motivation rises up to it.
inline constexpr double MOTIVATION_BASE_MAX = 125.0;
inline constexpr double MOTIVATION_PER_BUILDING = 25.0;
inline constexpr double MOTIVATION_FAST = 100.0;
inline constexpr double MOTIVATION_DECAY_PER_HOUR = 10.0;
inline constexpr float MOTIVATION_SPEED_BONUS = 0.5f;
inline constexpr double MOTIVATION_POINTS_PER_DANCER_HOUR = 1.0;
inline constexpr float MOTIVATION_LOOK_SECONDS = 2.0f;   // how often claims and buildings are counted again

struct Motivation {
    std::map<int, double> value;       // by faction. Saved.
    std::map<int, int> buildings;      // each faction's complete buildings, counted every MOTIVATION_LOOK_SECONDS
    std::vector<Claim> claims;         // the claims as last seen
    float lookIn = 0.0f;
    std::map<int, bool> over;          // whether each faction was over the threshold at the last look: a chime plays on the way up. Not saved.
};

double motivationOf(World& w, int faction);
double motivationMax(World& w, int faction);
void setMotivation(World& w, int faction, double value);
// Whether the faction is over the threshold
bool isMotivated(World& w, int faction);
// 1.5 for a unit of a motivated faction inside its faction's own claim, else 1
float motivationSpeedFactor(World& w, ObjectId unit);
// Dancers add, everyone decays; dt in real seconds, gameSeconds the in-game time that went by
void motivationSystem(World& w, float dt, double gameSeconds);
