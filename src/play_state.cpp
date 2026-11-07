#include "play_state.h"

namespace play {
PlaySession session;

void resetPlayState() {
    session = PlaySession{};
}

void closeStorage() {
    session.storageProp = NO_OBJECT;
    session.storageUnit = NO_OBJECT;
    session.storageDrag = -1;
}

void closeLoot() {
    session.lootUnit = NO_OBJECT;
    session.lootBy = NO_OBJECT;
}

void clearTradeTable() {
    session.tradeNpc.clear();
    session.tradePlayer.clear();
    session.tradeNpcDrag = -1;
}
}
