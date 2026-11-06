#pragma once
#include <vector>
#include "components.h"
#include "ecs.h"
#include "ui_layout.h"
#include "ui_types.h"

int tradeAvailable(const Inventory& inv, const std::vector<TradeGhost>& side, int item);
int tradeAddItems(std::vector<TradeGhost>& side, const Inventory& inv, int clickedSlot, int want);
void takeFromSlot(Inventory& inv, int slot, int count);
bool tradeStillThere(const Inventory* inv, const std::vector<TradeGhost>& side);
bool tradeItemsPresent(World& w, Entity npc, Entity player);
bool takeTradeGhostAt(std::vector<TradeGhost>& side, const std::vector<Rect>& cells, float x, float y);
bool tradeFits(Inventory inv, const std::vector<TradeGhost>& gives, const std::vector<TradeGhost>& receives);
TradeStatus checkTrade(World& w, Entity npc, Entity player);
void completeTrade(World& w, Entity npc, Entity player);
