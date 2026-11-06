#include "play_state.h"

namespace play {
PlaySession session;

void resetPlayState() {
    session = PlaySession{};
}

void closeStorage() {
    session.storageProp = NULL_ENTITY;
    session.storageUnit = NULL_ENTITY;
    session.storageDrag = -1;
}

void closeLoot() {
    session.lootUnit = NULL_ENTITY;
    session.lootBy = NULL_ENTITY;
}

void clearTradeTable() {
    session.tradeNpc.clear();
    session.tradePlayer.clear();
    session.tradeNpcDrag = -1;
}
}
