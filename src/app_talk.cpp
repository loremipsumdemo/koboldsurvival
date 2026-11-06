#include "app.h"

using namespace play;

void App::sayAs(Entity who, const std::string& text, bool player) {
    SpokenLine ln;
    ln.text = text;
    ln.player = player;
    const bool known = who != NULL_ENTITY && world.has<Unit>(who);
    ln.speaker = known ? nameOf(world, who) : std::string(player ? lang("MISC_YOU") : lang("UILABEL_UNNAMED_UNIT"));
    const UnitSprite* us = known ? world.get<UnitSprite>(who) : nullptr;
    ln.sprite = us ? us->id : 1;
    dialogueConvo.push_back(ln);
    gameLog.say(ln.speaker + ": " + text);
}

void App::endTalk() {
    talkCooldown = TALK_COOLDOWN;
    tradeOpen = false;
    freeTrade = false;
    clearTradeTable();
    quantity.open = false;
    dialogueUnit = NULL_ENTITY;
    dialogueConvo.clear();
}

void App::requestTrade(bool npcSide, Entity owner, int slot) {
    const Inventory* inv = world.get<Inventory>(owner);
    if (!inv || slot < 0 || slot >= INVENTORY_SLOTS || inv->slots[slot].item == 0) return;
    std::vector<TradeGhost>& side = npcSide ? tradeNpc : tradePlayer;
    const int item = inv->slots[slot].item;
    if (inv->slots[slot].count <= 1) {
        if (tradeAddItems(side, *inv, slot, 1) == 0) refuse(lang("MISC_TRADE_ALREADY"));
        return;
    }
    const int avail = tradeAvailable(*inv, side, item);
    if (avail <= 0) {
        refuse(lang("MISC_TRADE_NO_MORE"));
        return;
    }
    quantity = QuantityPrompt{ true, npcSide, owner, slot, item, itemNameOf(item), 1, avail };
}

void App::confirmQuantity() {
    if (quantity.toStorage) {
        std::string why;
        if (storageProp != NULL_ENTITY && quantity.owner == storageUnit &&
            storeCountInStorage(world, quantity.slot, quantity.chosen, why)) {
            queueSound(Sfx::ItemMove);
        } else if (!why.empty()) {
            refuse(why);
        }
        quantity.open = false;
        return;
    }
    const Inventory* inv = world.get<Inventory>(quantity.owner);
    std::vector<TradeGhost>& side = quantity.npcSide ? tradeNpc : tradePlayer;
    const int added = inv ? tradeAddItems(side, *inv, quantity.slot, quantity.chosen) : 0;
    if (added == 0) refuse(lang("MISC_TRADE_FULL"));
    quantity.open = false;
}

void App::quantityClick(float qx, float qy) {
    const QuantityLayout ql = layoutQuantity(winW, winH);
    const int cap = std::max(1, quantity.available);
    auto nudge = [&](int by) { quantity.chosen = std::clamp(quantity.chosen + by, 1, cap); };
    if (ql.minus100.contains(qx, qy)) nudge(-100);
    else if (ql.minus10.contains(qx, qy)) nudge(-10);
    else if (ql.minus1.contains(qx, qy)) nudge(-1);
    else if (ql.plus1.contains(qx, qy)) nudge(1);
    else if (ql.plus10.contains(qx, qy)) nudge(10);
    else if (ql.plus100.contains(qx, qy)) nudge(100);
    else if (ql.single.contains(qx, qy)) quantity.chosen = 1;
    else if (ql.max.contains(qx, qy)) quantity.chosen = cap;
    else if (ql.confirm.contains(qx, qy)) confirmQuantity();
    else if (ql.cancel.contains(qx, qy)) quantity.open = false;
}

void App::requestStore(int slot) {
    const Inventory* inv = world.get<Inventory>(storageUnit);
    const Storage* st = world.get<Storage>(storageProp);
    if (!inv || !st || slot < 0 || slot >= INVENTORY_SLOTS || inv->slots[slot].item == 0) return;
    const int item = inv->slots[slot].item;
    std::string why;
    if (inv->slots[slot].count <= 1) {
        if (storeCountInStorage(world, slot, 1, why)) queueSound(Sfx::ItemMove);
        else if (!why.empty()) refuse(why);
        return;
    }
    if (const std::string no = refusalFor(world, item); !no.empty()) {
        refuse(no);
        return;
    }
    const int cap = std::min(carriedCount(*inv, item), slotsRoomFor(st->slots.data(), STORAGE_SLOTS, item));
    if (cap <= 0) {
        refuse(lang("MISC_STORAGE_FULL"));
        return;
    }
    quantity = QuantityPrompt{ true, false, storageUnit, slot, item,
                               itemNameOf(item), 1, cap, true };
}

void App::showTraderInventory() {
    if (!unitOpen) {
        unitPlace = settlePanel(PanelId::Unit, unitPlace, winW, winH, true);
        unitOpen = true;
    }
    unitTab = UnitTab::Inventory;
    sidebarScroll = 0.0f;
}

void App::startTalk(Entity npc, Entity talker, bool shop) {
    if (world.has<Mob>(npc) || world.has<Mob>(talker)) return;   // mobs have no dialogue (mobs.h)
    // Only the talker starts the dialogue. The other selected units still walk to the NPC's spot.
    std::vector<Entity> others;
    for (Entity u : selectedUnits(world)) {
        if (u != talker) others.push_back(u);
    }
    clearTalkOrders(world);
    if (const Transform* nt = world.get<Transform>(npc); nt && !others.empty()) {
        issueMoveOrder(world, terrain, others, Vec2{ nt->x, nt->z });
    }
    dialogueUnit = npc;
    dialogueConvo.clear();
    dialoguePlayer = talker;
    playSfx(Sfx::TalkOpen);
    const Dialogue* dg = world.get<Dialogue>(npc);
    sayAs(npc, dg ? dg->text : std::string(lang("NPC_GREETING")), false);
    // Shop: the trade option is picked at once, as if "Show me your wares" were clicked
    if (shop && dg) {
        for (const DialogueOption& o : dg->options) {
            if (!o.opensTrade) continue;
            sayAs(talker, o.prompt, true);
            sayAs(npc, o.reply, false);
            openTrade(false);
            break;
        }
    }
}

void App::tryStartTalk(Entity npc, Entity talker, std::vector<Entity> units, bool shop) {
    if (talkCooldown <= 0.0f) {
        startTalk(npc, talker, shop);
        return;
    }
    for (Entity u : world.view<TalkOrder>()) units.push_back(u);
    std::sort(units.begin(), units.end());
    units.erase(std::unique(units.begin(), units.end()), units.end());
    const Transform* nt = world.get<Transform>(npc);
    if (nt && !units.empty()) issueMoveOrder(world, terrain, units, Vec2{ nt->x, nt->z });
}

void App::startFreeTrade(Entity other, Entity unit) {
    if (world.has<Mob>(other) || world.has<Mob>(unit)) return;   // mobs don't trade (mobs.h)
    clearTalkOrders(world);
    closeStorage();
    contextMenu.open = false;
    itemMenu.open = false;
    quantity.open = false;
    dialogueUnit = other;
    dialoguePlayer = unit;
    dialogueConvo.clear();
    openTrade(true);
}

bool App::noteLockedIn(const std::vector<Entity>& units) {
    // Unconscious ones can't do anything at all; say so when they are all there is
    bool anyAwake = false;
    for (Entity u : units) anyAwake = anyAwake || !outCold(world, u);
    if (!units.empty() && !anyAwake) {
        refuse(langf("MISC_IS_UNCONSCIOUS", { nameOf(world, units[0]) }));
        return true;
    }
    for (Entity u : units) {
        if (!lockedIn(world, u)) continue;
        refuse(langf("MISC_LOCKED_IN", { nameOf(world, u), propNameOf(world, occupiedProp(world, u)) }));
        return true;
    }
    return false;
}

void App::chooseOption(size_t i) {
    const Dialogue* dg = world.get<Dialogue>(dialogueUnit);
    if (!dg || i >= dg->options.size()) return;
    const std::string prompt = dg->options[i].prompt, reply = dg->options[i].reply;
    const bool trade = dg->options[i].opensTrade;
    sayAs(dialoguePlayer, prompt, true);
    sayAs(dialogueUnit, reply, false);
    if (trade) {
        openTrade(false);
    }
}

void App::openTrade(bool free) {
    tradeOpen = true;
    showTraderInventory();
    freeTrade = free;
    clearTradeTable();
}
