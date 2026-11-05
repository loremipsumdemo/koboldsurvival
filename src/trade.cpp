#include "trade.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include "language.h"
#include "components.h"
#include "ecs.h"
#include "items.h"
#include "inventory.h"
#include "equip.h"
#include "combat.h"
#include "stats.h"
#include "props.h"
#include "systems.h"
#include "textcase.h"
#include "ui_layout.h"
#include "play_state.h"

using namespace play;

// Shows a stack on one side of a trade, in the next free cell. Only a picture: the inventory is not changed.
// How many of an item an inventory holds that are not already in one side of a trade
int tradeAvailable(const Inventory& inv, const std::vector<TradeGhost>& side, int item) {
    int total = 0;
    for (int i = 0; i < INVENTORY_SLOTS; ++i) {
        if (inv.slots[i].item == item) total += inv.slots[i].count;
    }
    for (const TradeGhost& g : side) {
        if (g.stack.item == item) total -= g.stack.count;
    }
    return std::max(0, total);
}

// Puts up to `want` of the clicked stack's item in a side of the trade: the clicked stack first, then the other stacks
// of that item in the inventory. Stacks already in the trade only give what is still free. Returns how many went in.
int tradeAddItems(std::vector<TradeGhost>& side, const Inventory& inv, int clickedSlot, int want) {
    if (clickedSlot < 0 || clickedSlot >= INVENTORY_SLOTS || inv.slots[clickedSlot].item == 0 || want <= 0) return 0;
    const int item = inv.slots[clickedSlot].item;
    std::vector<int> order{ clickedSlot };
    for (int i = 0; i < INVENTORY_SLOTS; ++i) {
        if (i != clickedSlot && inv.slots[i].item == item) order.push_back(i);
    }
    int added = 0;
    for (int slot : order) {
        if (added >= want) break;
        TradeGhost* ghost = nullptr;
        for (TradeGhost& g : side) {
            if (g.slot == slot) ghost = &g;
        }
        const int inTrade = ghost ? ghost->stack.count : 0;
        const int take = std::min(inv.slots[slot].count - inTrade, want - added);
        if (take <= 0) continue;
        if (ghost) {
            ghost->stack.count += take;
        } else {
            if ((int)side.size() >= TRADE_SLOTS) break;
            side.push_back(TradeGhost{ slot, ItemStack{ item, take } });
        }
        added += take;
    }
    return added;
}

// Takes some of a stack's items out of an inventory; the slot empties when nothing is left in it
void takeFromSlot(Inventory& inv, int slot, int count) {
    if (slot < 0 || slot >= INVENTORY_SLOTS) return;
    ItemStack& st = inv.slots[slot];
    st.count -= count;
    if (st.count <= 0) st = ItemStack{};
}

// Whether an inventory still holds everything a side of the trade says it gives
bool tradeStillThere(const Inventory* inv, const std::vector<TradeGhost>& side) {
    if (!inv) return side.empty();
    for (const TradeGhost& g : side) {
        if (g.slot < 0 || g.slot >= INVENTORY_SLOTS) return false;
        int needed = 0;
        for (const TradeGhost& other : side) {
            if (other.slot == g.slot) needed += other.stack.count;
        }
        const ItemStack& st = inv->slots[g.slot];
        if (st.item != g.stack.item || st.count < needed) return false;
    }
    return true;
}

bool tradeItemsPresent(World& w, Entity npc, Entity player) {
    return tradeStillThere(w.get<Inventory>(npc), tradeNpc) && tradeStillThere(w.get<Inventory>(player), tradePlayer);
}

// Takes back out the ghost shown in one of the cells. True if one was there.
bool takeTradeGhostAt(std::vector<TradeGhost>& side, const std::vector<Rect>& cells, float x, float y) {
    for (size_t i = 0; i < side.size() && i < cells.size(); ++i) {
        if (cells[i].contains(x, y)) {
            side.erase(side.begin() + (int)i);
            return true;
        }
    }
    return false;
}

// The distance in tiles between two units on the ground
float gapBetween(World& w, Entity a, Entity b) {
    const Transform* ta = w.get<Transform>(a);
    const Transform* tb = w.get<Transform>(b);
    if (!ta || !tb) return 1e9f;
    const float dx = ta->x - tb->x, dz = ta->z - tb->z;
    return std::sqrt(dx * dx + dz * dz);
}

// Whether a side's inventory has room for what it receives, once what it gives is taken out of it
bool tradeFits(Inventory inv, const std::vector<TradeGhost>& gives, const std::vector<TradeGhost>& receives) {
    for (const TradeGhost& g : gives) {
        if (g.slot >= 0 && g.slot < INVENTORY_SLOTS) takeFromSlot(inv, g.slot, g.stack.count);
    }
    for (const TradeGhost& g : receives) {
        if (addToInventory(inv, g.stack.item, g.stack.count) != 0) return false;
    }
    return true;
}

// Whether the trade on the table can be confirmed: both sides have items, each inventory has room for what it
// receives, and the offer covers the NPC's price with its fee. Also how close the offer is, for the value bar.
TradeStatus checkTrade(World& w, Entity npc, Entity player) {
    TradeStatus st;
    const Inventory* ni = npc != NULL_ENTITY ? w.get<Inventory>(npc) : nullptr;
    const Inventory* pi = player != NULL_ENTITY ? w.get<Inventory>(player) : nullptr;
    if (!ni || !pi) {
        st.text = lang("MISC_NO_UNIT_TO_TRADE");
        return st;
    }
    if (freeTrade) {
        // Between your own units: anything may go either way, so long as each side has room for what it receives
        st.progress = 1.0f;
        if (tradeNpc.empty() && tradePlayer.empty()) st.text = lang("MISC_FREE_TRADE_ADD");
        else if (!tradeFits(*pi, tradePlayer, tradeNpc)) st.text = lang("MISC_TRADE_NO_ROOM_YOURS");
        else if (!tradeFits(*ni, tradeNpc, tradePlayer)) st.text = lang("MISC_TRADE_NO_ROOM_THEIRS");
        else {
            st.ok = true;
            st.text = lang("MISC_FREE_TRADE_READY");
        }
        return st;
    }
    const int npcValue = tradeValueOf(tradeNpc);
    const int offer = tradeValueOf(tradePlayer);
    const int needed = requiredOffer(npcValue, levelOf(statsOf(w, player), StatId::Charisma));
    st.progress = needed > 0 ? (float)offer / (float)needed : 1.0f;
    if (tradeNpc.empty() || tradePlayer.empty()) st.text = lang("MISC_TRADE_ADD_BOTH");
    else if (!tradeFits(*pi, tradePlayer, tradeNpc)) st.text = lang("MISC_TRADE_NO_ROOM_YOURS");
    else if (!tradeFits(*ni, tradeNpc, tradePlayer)) st.text = lang("MISC_TRADE_NO_ROOM_THEIRS");
    else if (offer < needed) st.text = langf("MISC_NEED_MORE_VALUE", { std::to_string(needed - offer) });
    else {
        st.ok = true;
        st.text = langf("MISC_TRADE_READY_FEE", { std::to_string(needed - npcValue) });
    }
    return st;
}

// Makes the trade: each side's items move to the other, and the player's Charisma gains XP for the fee paid
void completeTrade(World& w, Entity npc, Entity player) {
    Inventory* ni = w.get<Inventory>(npc);
    Inventory* pi = w.get<Inventory>(player);
    const int npcValue = tradeValueOf(tradeNpc);
    const int fee = freeTrade ? 0 : requiredOffer(npcValue, levelOf(statsOf(w, player), StatId::Charisma)) - npcValue;
    for (const TradeGhost& g : tradeNpc) takeFromSlot(*ni, g.slot, g.stack.count);
    for (const TradeGhost& g : tradePlayer) takeFromSlot(*pi, g.slot, g.stack.count);
    // The trade was checked to fit, but should anything not, it falls at the receiver's feet
    for (const TradeGhost& g : tradeNpc) giveOrDrop(w, player, g.stack.item, g.stack.count);
    for (const TradeGhost& g : tradePlayer) giveOrDrop(w, npc, g.stack.item, g.stack.count);
    // Over each side, what it received ("+3x Iron Ore") and what it gave ("-Gold Coin"), one label per kind of item
    auto totals = [](const std::vector<TradeGhost>& side) {
        std::vector<std::pair<int, int>> out;   // item, count, in the order first offered
        for (const TradeGhost& g : side) {
            auto it = std::find_if(out.begin(), out.end(), [&](const std::pair<int, int>& p) { return p.first == g.stack.item; });
            if (it == out.end()) out.push_back({ g.stack.item, g.stack.count });
            else it->second += g.stack.count;
        }
        return out;
    };
    for (const auto& [item, count] : totals(tradeNpc)) {
        queueUnitLabel(player, itemGainLabelFor(count, item));
        queueUnitLabel(npc, itemLossLabelFor(count, item), true);
    }
    for (const auto& [item, count] : totals(tradePlayer)) {
        queueUnitLabel(npc, itemGainLabelFor(count, item));
        queueUnitLabel(player, itemLossLabelFor(count, item), true);
    }
    if (fee > 0) grantXp(w, player, StatId::Charisma, fee, lang("MISC_REASON_FEES"));
    tradeOpen = false;
    clearTradeTable();
}
