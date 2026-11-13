#include "play_state.h"

namespace play {
PlaySession session;

void resetPlayState() {
    session = PlaySession{};
}

void closeStorage() {
    session.storageProp = NO_OBJECT;
    session.storageScroll = 0;
    session.storageUnit = NO_OBJECT;
    session.storageDrag = -1;
    session.storageDragProp = NO_OBJECT;
    session.storageDragAt = -1;
    session.storageDragExpect = ItemStack{};
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
