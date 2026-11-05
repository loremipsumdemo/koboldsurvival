#include "play_state.h"

namespace play {

Entity dialogueUnit = NULL_ENTITY;
bool levelPlayerCamp = true;
Entity dialoguePlayer = NULL_ENTITY;
std::vector<SpokenLine> dialogueConvo;
bool tradeOpen = false;
bool freeTrade = false;
float talkCooldown = 0.0f;
bool paused = false;
BuildPick buildPick;
float buildYaw = 0.0f;
float buildPrevX = -1.0f;
std::vector<TradeGhost> tradeNpc;
std::vector<TradeGhost> tradePlayer;
int tradeNpcDrag = -1;

Entity storageProp = NULL_ENTITY;
Entity storageUnit = NULL_ENTITY;
int storageDrag = -1;

void closeStorage() {
    storageProp = NULL_ENTITY;
    storageUnit = NULL_ENTITY;
    storageDrag = -1;
}

void clearTradeTable() {
    tradeNpc.clear();
    tradePlayer.clear();
    tradeNpcDrag = -1;
}

ContextMenu contextMenu;

Entity disbandTarget = NULL_ENTITY;
unsigned long long disbandHeldAt = 0;

Entity lootUnit = NULL_ENTITY;
Entity lootBy = NULL_ENTITY;
void closeLoot() {
    lootUnit = NULL_ENTITY;
    lootBy = NULL_ENTITY;
}

Entity inspectUnit = NULL_ENTITY;

QuantityPrompt quantity;

} // namespace play
