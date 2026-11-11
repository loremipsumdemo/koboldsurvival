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
                        Unlock, Lock, PickUp, PutDown, AutoHarvest, AutoHaul, Disband, Destroy, Evacuate,
                        ManageUnits, Rename, AutoHaulBones, AppeaseGhosts, BehaviorGroups, StoreFilter, CancelBuild, AutoConstruct, Climb, ClimbDown,
                        BedPublic, BedPrivate, StockStall, ManStall, DealWith, GetOut, CraftOrder, Research };
struct ContextMenu {
    bool open = false;
    float x = 0.0f, y = 0.0f;
    ObjectId target = NO_OBJECT;
    std::vector<MenuAction> actions;
};

struct QuantityPrompt {
    bool open = false;
    bool npcSide = false;
    ObjectId owner = NO_OBJECT;
    int slot = -1;
    int item = 0;
    std::string name;
    int chosen = 1;
    int available = 0;
    bool toStorage = false;
};

// UI state that lasts as long as one game and names entities of its world. ObjectId ids restart with each world, so all
// of it is replaced whenever a level starts (resetPlayState).
struct PlaySession {
    ObjectId dialogueUnit = NO_OBJECT;
    ObjectId dialoguePlayer = NO_OBJECT;
    std::vector<SpokenLine> dialogueConvo;
    bool tradeOpen = false;
    bool freeTrade = false;
    float talkCooldown = 0.0f;
    bool paused = false;
    bool autoPaused = false;
    BuildPick buildPick;
    // A foundation being dragged out from this tile, and the tile last painted with a piece
    bool foundationDrag = false;
    bool floorDrag = false;   // upper floors being dragged out, from dragTileX, dragTileZ
    int dragTileX = 0, dragTileZ = 0;
    bool paintPiece = false;
    long long lastPaintTile = -1;
    // What a foundation, wall or door is made of (an int of BuildMaterial), and the kind waiting for that choice
    int buildMaterial = 0;
    // The floor looked at: 0 the ground floor, 1 the first upper floor, up to STOREY_MAX. Auto: the floor of the unit the
    // camera follows; Manual: Q and E step it down and up, and Z goes back to Auto.
    int floorLevel = 0;
    bool floorManual = false;
    // The floor what is being placed goes on: the highest finished floor seen under the pointer (upper floors: the one
    // looked at)
    int buildLevel = 0;
    int buildDir = 0;   // stairs being placed: the way they rise (0 north, 1 east, 2 south, 3 west), turned with R
    bool askMaterial = false;
    PropKind askKind = PropKind::Wagon;
    std::vector<TradeGhost> tradeNpc;
    std::vector<TradeGhost> tradePlayer;
    int tradeNpcDrag = -1;
    ObjectId storageProp = NO_OBJECT;
    ObjectId storageUnit = NO_OBJECT;
    int storageDrag = -1;
    int storageScroll = 0;   // rows scrolled down in a big store's window
    ContextMenu contextMenu;
    ObjectId disbandTarget = NO_OBJECT;
    unsigned long long disbandHeldAt = 0;
    ObjectId lootUnit = NO_OBJECT;
    ObjectId lootBy = NO_OBJECT;
    ObjectId inspectUnit = NO_OBJECT;
    QuantityPrompt quantity;
};
extern PlaySession session;

void resetPlayState();
void closeStorage();
void closeLoot();
void clearTradeTable();
}
