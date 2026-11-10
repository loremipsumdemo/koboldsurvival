#pragma once
#include <vector>
#include "behavior.h"
#include "components.h"
#include "ecs.h"
#include "rng.h"
#include "terrain.h"

// Trade with the world around your claim:
//  - Stalls: you pick items from your stores to stock one (each order hauled once, by Auto-Haul to Stalls), and a unit
//    with the Man Stall task keeps it. Units of friendly factions nearby come to buy what they need or could use, paying
//    in the kinds of item the Stall accepts.
//  - Raids: units of factions hostile to you go for your stores outside your buildings and run off with the valuables.
//    Three or more inside your claim with nothing left outside break in through a door.
//  - The Wandering Trader: a Factionless unit who may appear at the edge of your claim each day, buys from a Stall with coins made
//    on the spot, and leaves.
// Items taken from another faction's store are marked stolen from it. Selling them to that faction, or to one allied with
// it, may be noticed (more likely for a valuable item, less with a good Thievery), and costs reputation with the buyer.

inline constexpr float STALL_KEEPER_REACH = 3.5f;   // a shopkeeper this near its Stall mans it
inline constexpr float SHOP_RANGE = 80.0f;          // friendly units this near a manned Stall may come to shop
inline constexpr float RAID_RANGE = 60.0f;          // hostile units this near your claim may come to raid
inline constexpr int RAIDERS_MOST = 4;              // raiders at once
inline constexpr int RAIDERS_TO_BREAK_IN = 3;       // hostile units inside your claim before they go for a door
inline constexpr float TRADER_DAILY_CHANCE = 0.33f;
inline constexpr int TRADER_MOST_ITEMS = 20;
inline constexpr int REP_STOLEN_PENALTY = 10;

// The unit manning this Stall, or NO_OBJECT
ObjectId stallManned(World& w, ObjectId stall);
// An order for the Stall: count of item from the store from, hauled once by a unit with Auto-Haul to Stalls
bool requestStallHaul(World& w, ObjectId stall, ObjectId from, int item, int count);

// The tasks: Auto-Haul to Stalls takes the next waiting order; Man Stall goes to the Stall and keeps it
bool startStallHaulStep(World& w, Terrain& t, ObjectId u, Behaviors& b, BehaviorRule& r);
bool startManStallStep(World& w, Terrain& t, ObjectId u, Behaviors& b, BehaviorRule& r);

// A unit's item handed to someone of buyerFaction: if it was stolen from them or an ally, whether they notice, which
// costs reputation with them. seller's Thievery makes it less likely.
bool stolenSaleNoticed(World& w, Rng& rng, ObjectId seller, int buyerFaction, int item);
// An item a unit takes back that was stolen from its own faction is its own again
int reclaimStolen(World& w, ObjectId unit, int item);

// Stall orders, shoppers, raiders and the Wandering Trader
void commerceSystem(World& w, Terrain& t, Rng& rng, float dt);
