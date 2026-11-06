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

void takeFromSlot(Inventory& inv, int slot, int count) {
    if (slot < 0 || slot >= INVENTORY_SLOTS) return;
    ItemStack& st = inv.slots[slot];
    st.count -= count;
    if (st.count <= 0) st = ItemStack{};
}

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
    return tradeStillThere(w.get<Inventory>(npc), session.tradeNpc) && tradeStillThere(w.get<Inventory>(player), session.tradePlayer);
}

bool takeTradeGhostAt(std::vector<TradeGhost>& side, const std::vector<Rect>& cells, float x, float y) {
    for (size_t i = 0; i < side.size() && i < cells.size(); ++i) {
        if (cells[i].contains(x, y)) {
            side.erase(side.begin() + (int)i);
            return true;
        }
    }
    return false;
}

bool tradeFits(Inventory inv, const std::vector<TradeGhost>& gives, const std::vector<TradeGhost>& receives) {
    for (const TradeGhost& g : gives) {
        if (g.slot >= 0 && g.slot < INVENTORY_SLOTS) takeFromSlot(inv, g.slot, g.stack.count);
    }
    for (const TradeGhost& g : receives) {
        if (addToInventory(inv, g.stack.item, g.stack.count) != 0) return false;
    }
    return true;
}

TradeStatus checkTrade(World& w, Entity npc, Entity player) {
    TradeStatus st;
    const Inventory* ni = npc != NULL_ENTITY ? w.get<Inventory>(npc) : nullptr;
    const Inventory* pi = player != NULL_ENTITY ? w.get<Inventory>(player) : nullptr;
    if (!ni || !pi) {
        st.text = lang("MISC_NO_UNIT_TO_TRADE");
        return st;
    }
    if (session.freeTrade) {
        st.progress = 1.0f;
        if (session.tradeNpc.empty() && session.tradePlayer.empty()) st.text = lang("MISC_FREE_TRADE_ADD");
        else if (!tradeFits(*pi, session.tradePlayer, session.tradeNpc)) st.text = lang("MISC_TRADE_NO_ROOM_YOURS");
        else if (!tradeFits(*ni, session.tradeNpc, session.tradePlayer)) st.text = lang("MISC_TRADE_NO_ROOM_THEIRS");
        else {
            st.ok = true;
            st.text = lang("MISC_FREE_TRADE_READY");
        }
        return st;
    }
    const int npcValue = tradeValueOf(session.tradeNpc);
    const int offer = tradeValueOf(session.tradePlayer);
    const int needed = requiredOffer(npcValue, levelOf(statsOf(w, player), StatId::Charisma));
    st.progress = needed > 0 ? (float)offer / (float)needed : 1.0f;
    if (session.tradeNpc.empty() || session.tradePlayer.empty()) st.text = lang("MISC_TRADE_ADD_BOTH");
    else if (!tradeFits(*pi, session.tradePlayer, session.tradeNpc)) st.text = lang("MISC_TRADE_NO_ROOM_YOURS");
    else if (!tradeFits(*ni, session.tradeNpc, session.tradePlayer)) st.text = lang("MISC_TRADE_NO_ROOM_THEIRS");
    else if (offer < needed) st.text = langf("MISC_NEED_MORE_VALUE", { std::to_string(needed - offer) });
    else {
        st.ok = true;
        st.text = langf("MISC_TRADE_READY_FEE", { std::to_string(needed - npcValue) });
    }
    return st;
}

void completeTrade(World& w, Entity npc, Entity player) {
    Inventory* ni = w.get<Inventory>(npc);
    Inventory* pi = w.get<Inventory>(player);
    const int npcValue = tradeValueOf(session.tradeNpc);
    const int fee = session.freeTrade ? 0 : requiredOffer(npcValue, levelOf(statsOf(w, player), StatId::Charisma)) - npcValue;
    for (const TradeGhost& g : session.tradeNpc) takeFromSlot(*ni, g.slot, g.stack.count);
    for (const TradeGhost& g : session.tradePlayer) takeFromSlot(*pi, g.slot, g.stack.count);
    for (const TradeGhost& g : session.tradeNpc) giveOrDrop(w, player, g.stack.item, g.stack.count);
    for (const TradeGhost& g : session.tradePlayer) giveOrDrop(w, npc, g.stack.item, g.stack.count);
    auto totals = [](const std::vector<TradeGhost>& side) {
        std::vector<std::pair<int, int>> out;
        for (const TradeGhost& g : side) {
            auto it = std::find_if(out.begin(), out.end(), [&](const std::pair<int, int>& p) { return p.first == g.stack.item; });
            if (it == out.end()) out.push_back({ g.stack.item, g.stack.count });
            else it->second += g.stack.count;
        }
        return out;
    };
    for (const auto& [item, count] : totals(session.tradeNpc)) {
        queueUnitLabel(player, itemGainLabelFor(count, item));
        queueUnitLabel(npc, itemLossLabelFor(count, item), true);
    }
    for (const auto& [item, count] : totals(session.tradePlayer)) {
        queueUnitLabel(npc, itemGainLabelFor(count, item));
        queueUnitLabel(player, itemLossLabelFor(count, item), true);
    }
    if (fee > 0) grantXp(w, player, StatId::Charisma, fee, lang("MISC_REASON_FEES"));
    session.tradeOpen = false;
    clearTradeTable();
}
