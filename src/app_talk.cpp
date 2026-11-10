#include "app.h"

using namespace play;

namespace {
bool shiftHeld() { return (SDL_GetModState() & SDL_KMOD_SHIFT) != 0; }
}

void App::sayAs(ObjectId who, const std::string& text, bool player) {
    SpokenLine ln;
    ln.text = text;
    ln.player = player;
    const bool known = who != NO_OBJECT && world.has<Entity>(who);
    ln.speaker = known ? nameOf(world, who) : std::string(player ? lang("MISC_YOU") : lang("UILABEL_UNNAMED_UNIT"));
    const UnitSprite* us = known ? world.get<UnitSprite>(who) : nullptr;
    ln.sprite = us ? us->id : 1;
    session.dialogueConvo.push_back(ln);
    gameLog.say(ln.speaker + ": " + text);
}

void App::endTalk() {
    session.talkCooldown = TALK_COOLDOWN;
    session.tradeOpen = false;
    session.freeTrade = false;
    clearTradeTable();
    session.quantity.open = false;
    world.remove<InConversation>(session.dialogueUnit);
    world.remove<InConversation>(session.dialoguePlayer);
    session.dialogueUnit = NO_OBJECT;
    session.dialogueConvo.clear();
    if (session.autoPaused) session.paused = false;
    session.autoPaused = false;
}

// A pause the player made stays when the conversation ends. Only this one is lifted by endTalk.
void App::pauseForConversation() {
    if (!settings.conversationPause || session.paused) return;
    session.paused = true;
    session.autoPaused = true;
}

// While the game runs behind a conversation, either side can die, be knocked out, be carried off or come under attack.
void App::endBrokenConversation() {
    if (session.dialogueUnit == NO_OBJECT) return;
    const ObjectId npc = session.dialogueUnit, you = session.dialoguePlayer;
    auto cannotTalk = [&](ObjectId u) {
        if (!world.has<Entity>(u) || world.has<Corpse>(u) || outCold(world, u) || world.has<Carried>(u)) return true;
        const Combat* c = world.get<Combat>(u);
        return c && c->target != NO_OBJECT;
    };
    if (cannotTalk(npc) || cannotTalk(you) || world.has<Hostile>(npc)) {
        endTalk();
        showNotice(lang("MISC_TALK_INTERRUPTED"));
    }
}

void App::requestTrade(bool npcSide, ObjectId owner, int slot) {
    const Inventory* inv = world.get<Inventory>(owner);
    if (!inv || slot < 0 || slot >= INVENTORY_SLOTS || inv->slots[slot].item == 0) return;
    std::vector<TradeGhost>& side = npcSide ? session.tradeNpc : session.tradePlayer;
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
    if (!shiftHeld()) {
        int onTable = 0;
        for (const TradeGhost& g : side) {
            if (g.slot == slot) onTable += g.stack.count;
        }
        const int stack = inv->slots[slot].count;
        if (tradeAddItems(side, *inv, slot, onTable < stack ? stack - onTable : stack) == 0) refuse(lang("MISC_TRADE_FULL"));
        return;
    }
    session.quantity = QuantityPrompt{ true, npcSide, owner, slot, item, itemNameOf(item), 1, avail };
}

void App::confirmQuantity() {
    if (session.quantity.toStorage) {
        std::string why;
        if (session.storageProp != NO_OBJECT && session.quantity.owner == session.storageUnit &&
            storeCountInStorage(world, session.quantity.slot, session.quantity.chosen, why)) {
            queueSound(world, Sfx::ItemMove);
        } else if (!why.empty()) {
            refuse(why);
        }
        session.quantity.open = false;
        return;
    }
    const Inventory* inv = world.get<Inventory>(session.quantity.owner);
    std::vector<TradeGhost>& side = session.quantity.npcSide ? session.tradeNpc : session.tradePlayer;
    const int added = inv ? tradeAddItems(side, *inv, session.quantity.slot, session.quantity.chosen) : 0;
    if (added == 0) refuse(lang("MISC_TRADE_FULL"));
    session.quantity.open = false;
}

void App::quantityClick(float qx, float qy) {
    const QuantityLayout ql = layoutQuantity(winW, winH);
    const int cap = std::max(1, session.quantity.available);
    auto nudge = [&](int by) { session.quantity.chosen = std::clamp(session.quantity.chosen + by, 1, cap); };
    if (ql.minus100.contains(qx, qy)) nudge(-100);
    else if (ql.minus10.contains(qx, qy)) nudge(-10);
    else if (ql.minus1.contains(qx, qy)) nudge(-1);
    else if (ql.plus1.contains(qx, qy)) nudge(1);
    else if (ql.plus10.contains(qx, qy)) nudge(10);
    else if (ql.plus100.contains(qx, qy)) nudge(100);
    else if (ql.single.contains(qx, qy)) session.quantity.chosen = 1;
    else if (ql.max.contains(qx, qy)) session.quantity.chosen = cap;
    else if (ql.confirm.contains(qx, qy)) confirmQuantity();
    else if (ql.cancel.contains(qx, qy)) session.quantity.open = false;
}

void App::requestStore(int slot) {
    const Inventory* inv = world.get<Inventory>(session.storageUnit);
    const Storage* st = world.get<Storage>(session.storageProp);
    if (!inv || !st || slot < 0 || slot >= INVENTORY_SLOTS || inv->slots[slot].item == 0) return;
    const int item = inv->slots[slot].item;
    std::string why;
    if (inv->slots[slot].count <= 1) {
        if (storeCountInStorage(world, slot, 1, why)) queueSound(world, Sfx::ItemMove);
        else if (!why.empty()) refuse(why);
        return;
    }
    if (const std::string no = refusalFor(world, item); !no.empty()) {
        refuse(no);
        return;
    }
    const int cap = std::min(carriedCount(*inv, item), storeRoomFor(*st, item));
    if (cap <= 0) {
        refuse(lang("MISC_STORAGE_FULL"));
        return;
    }
    if (!shiftHeld()) {
        if (storeCountInStorage(world, slot, std::min(inv->slots[slot].count, cap), why)) queueSound(world, Sfx::ItemMove);
        else if (!why.empty()) refuse(why);
        return;
    }
    session.quantity = QuantityPrompt{ true, false, session.storageUnit, slot, item,
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

void App::startTalk(ObjectId npc, ObjectId talker, bool shop) {
    if (world.has<Mob>(npc) || world.has<Mob>(talker)) return;
    std::vector<ObjectId> others;
    for (ObjectId u : selectedUnits(world)) {
        if (u != talker) others.push_back(u);
    }
    clearTalkOrders(world);
    if (const Transform* nt = world.get<Transform>(npc); nt && !others.empty()) {
        issueMoveOrder(world, terrain, others, Vec2{ nt->x, nt->z });
    }
    session.dialogueUnit = npc;
    session.dialogueConvo.clear();
    session.dialoguePlayer = talker;
    world.add(npc, InConversation{});
    world.add(talker, InConversation{});
    pauseForConversation();
    playSfx(Sfx::TalkOpen);
    const Dialogue* dg = world.get<Dialogue>(npc);
    sayAs(npc, dg ? dg->text : std::string(lang("NPC_GREETING")), false);
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

void App::tryStartTalk(ObjectId npc, ObjectId talker, std::vector<ObjectId> units, bool shop) {
    if (session.talkCooldown <= 0.0f) {
        startTalk(npc, talker, shop);
        return;
    }
    for (ObjectId u : world.view<TalkOrder>()) units.push_back(u);
    std::sort(units.begin(), units.end());
    units.erase(std::unique(units.begin(), units.end()), units.end());
    const Transform* nt = world.get<Transform>(npc);
    if (nt && !units.empty()) issueMoveOrder(world, terrain, units, Vec2{ nt->x, nt->z });
}

void App::startFreeTrade(ObjectId other, ObjectId unit) {
    if (world.has<Mob>(other) || world.has<Mob>(unit)) return;
    clearTalkOrders(world);
    closeStorage();
    session.contextMenu.open = false;
    itemMenu.open = false;
    session.quantity.open = false;
    session.dialogueUnit = other;
    session.dialoguePlayer = unit;
    world.add(other, InConversation{});
    world.add(unit, InConversation{});
    session.dialogueConvo.clear();
    pauseForConversation();
    openTrade(true);
}

bool App::noteLockedIn(const std::vector<ObjectId>& units) {
    bool anyAwake = false;
    for (ObjectId u : units) anyAwake = anyAwake || !outCold(world, u);
    if (!units.empty() && !anyAwake) {
        refuse(langf("MISC_IS_UNCONSCIOUS", { nameOf(world, units[0]) }));
        return true;
    }
    for (ObjectId u : units) {
        if (!lockedIn(world, u)) continue;
        refuse(langf("MISC_LOCKED_IN", { nameOf(world, u), propNameOf(world, occupiedProp(world, u)) }));
        return true;
    }
    return false;
}

void App::chooseOption(size_t i) {
    const Dialogue* dg = world.get<Dialogue>(session.dialogueUnit);
    if (!dg || i >= dg->options.size()) return;
    const std::string prompt = dg->options[i].prompt, reply = dg->options[i].reply;
    const bool trade = dg->options[i].opensTrade;
    sayAs(session.dialoguePlayer, prompt, true);
    sayAs(session.dialogueUnit, reply, false);
    if (trade) {
        openTrade(false);
    }
}

void App::openTrade(bool free) {
    // Their faction's Trade rule (or yours) may rule it out: they say so, and no trade opens
    if (!free && !factionsTrade(world, factionOf(world, session.dialogueUnit), factionOf(world, session.dialoguePlayer))) {
        sayAs(session.dialogueUnit, lang("NPC_WONT_TRADE"), false);
        return;
    }
    session.tradeOpen = true;
    showTraderInventory();
    session.freeTrade = free;
    clearTradeTable();
}
