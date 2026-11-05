#pragma once
// The trade popout: the ghosts on each side (play::tradeNpc, play::tradePlayer), checking the offer, and making the trade
#include <vector>
#include "components.h"
#include "ecs.h"
#include "ui_layout.h"
#include "ui_types.h"

// How many of an item an inventory holds that are not already in one side of a trade
int tradeAvailable(const Inventory& inv, const std::vector<TradeGhost>& side, int item);
// Puts up to `want` of the clicked stack's item in a side of the trade: the clicked stack first, then the other stacks
// of that item in the inventory. Stacks already in the trade only give what is still free. Returns how many went in.
int tradeAddItems(std::vector<TradeGhost>& side, const Inventory& inv, int clickedSlot, int want);
// Takes some of a stack's items out of an inventory; the slot empties when nothing is left in it
void takeFromSlot(Inventory& inv, int slot, int count);
// Whether an inventory still holds everything a side of the trade says it gives
bool tradeStillThere(const Inventory* inv, const std::vector<TradeGhost>& side);
bool tradeItemsPresent(World& w, Entity npc, Entity player);
// Takes back out the ghost shown in one of the cells. True if one was there.
bool takeTradeGhostAt(std::vector<TradeGhost>& side, const std::vector<Rect>& cells, float x, float y);
// The distance in tiles between two units on the ground
float gapBetween(World& w, Entity a, Entity b);
// Whether a side's inventory has room for what it receives, once what it gives is taken out of it
bool tradeFits(Inventory inv, const std::vector<TradeGhost>& gives, const std::vector<TradeGhost>& receives);
// Whether the trade on the table can be confirmed: both sides have items, each inventory has room for what it
// receives, and the offer covers the NPC's price with its fee. Also how close the offer is, for the value bar.
TradeStatus checkTrade(World& w, Entity npc, Entity player);
// Makes the trade: each side's items move to the other, and the player's Charisma gains XP for the fee paid
void completeTrade(World& w, Entity npc, Entity player);
