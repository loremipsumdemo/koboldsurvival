#pragma once
#include <string>
#include <vector>
#include "components.h"
#include "ecs.h"
#include "props.h"

namespace play {
constexpr float MOVE_MARK_SECONDS = 3.0f;
constexpr float TALK_COOLDOWN = 1.0f;

enum class MenuAction { Talk, Shop, Trade, Follow, Defend, Attack, AttackUnprovoked, Inventory, Use, Occupy, Place,
                        Unlock, Lock, PickUp, PutDown, AutoHarvest, AutoHaul, Disband, Destroy, Evacuate };
struct ContextMenu {
    bool open = false;
    float x = 0.0f, y = 0.0f;
    Entity target = NULL_ENTITY;
    std::vector<MenuAction> actions;
};

struct QuantityPrompt {
    bool open = false;
    bool npcSide = false;
    Entity owner = NULL_ENTITY;
    int slot = -1;
    int item = 0;
    std::string name;
    int chosen = 1;
    int available = 0;
    bool toStorage = false;
};

// UI state that lasts as long as one game and names entities of its world. Entity ids restart with each world, so all
// of it is replaced whenever a level starts (resetPlayState).
struct PlaySession {
    Entity dialogueUnit = NULL_ENTITY;
    Entity dialoguePlayer = NULL_ENTITY;
    std::vector<SpokenLine> dialogueConvo;
    bool tradeOpen = false;
    bool freeTrade = false;
    float talkCooldown = 0.0f;
    bool paused = false;
    bool autoPaused = false;
    BuildPick buildPick;
    std::vector<TradeGhost> tradeNpc;
    std::vector<TradeGhost> tradePlayer;
    int tradeNpcDrag = -1;
    Entity storageProp = NULL_ENTITY;
    Entity storageUnit = NULL_ENTITY;
    int storageDrag = -1;
    ContextMenu contextMenu;
    Entity disbandTarget = NULL_ENTITY;
    unsigned long long disbandHeldAt = 0;
    Entity lootUnit = NULL_ENTITY;
    Entity lootBy = NULL_ENTITY;
    Entity inspectUnit = NULL_ENTITY;
    QuantityPrompt quantity;
};
extern PlaySession session;

inline bool inConversation(Entity e) {
    return session.dialogueUnit != NULL_ENTITY && (e == session.dialogueUnit || e == session.dialoguePlayer);
}

void resetPlayState();
void closeStorage();
void closeLoot();
void clearTradeTable();
}
