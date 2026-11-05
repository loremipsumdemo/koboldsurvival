#pragma once
// The play screen's UI state that main() and the helpers in trade.cpp, storage.cpp, selection.cpp, level.cpp and save_world.cpp
// share: the talk box and trade, the storage and loot panels, the right-click menu, the build pick and the pause.
// Kept in the play namespace so the short names can't meet another file's.
#include <string>
#include <vector>
#include "components.h"
#include "ecs.h"
#include "props.h"

namespace play {

// The neutral unit being talked to, or NULL_ENTITY when no talk box is open
extern Entity dialogueUnit;
// Whether the level being built has your camp: set from the scenario for a new game, and from the save when one loads
extern bool levelPlayerCamp;
extern Entity dialoguePlayer;              // the player unit a talk is with, who speaks the player's lines
extern std::vector<SpokenLine> dialogueConvo;   // the lines of the talk so far
extern bool tradeOpen;                     // the trade popout is up, in place of the talk box
// A free trade between two of your own units, from the right-click menu's Trade: no value or fee, only room counts
extern bool freeTrade;
constexpr float MOVE_MARK_SECONDS = 3.0f;  // how long the green X stays on the world map after a move order, fading in its last second
constexpr float TALK_COOLDOWN = 1.0f;      // seconds after a talk ends before another one can start
extern float talkCooldown;                 // seconds of that cooldown left
extern bool paused;                        // paused with Space, until Space is pressed again
// The Build tab: the prop picked to place (a PropKind, or -1 for none), its facing while placed, and the mouse's x last
// frame, which turns it while R is held
extern BuildPick buildPick;
extern float buildYaw;
extern float buildPrevX;
// Radians a pixel of mouse movement turns a prop being placed
constexpr float BUILD_TURN_PER_PIXEL = 0.01f;
extern std::vector<TradeGhost> tradeNpc;   // items shown on the NPC's side of the trade
extern std::vector<TradeGhost> tradePlayer;   // items shown on your side of the trade
extern int tradeNpcDrag;                   // the NPC's inventory slot being dragged into the trade, or -1

// The storage panel: the storage prop it shows (a barrel) and the unit using it. It stays open while that unit is
// within STORAGE_CLOSE_RANGE of the prop. NULL_ENTITY when closed.
extern Entity storageProp;
extern Entity storageUnit;
// The panel slot being dragged out of the panel, or -1. Panel slots 0 to STORAGE_SLOTS - 1 are the prop's Storage (its
// input, at a crafting prop); the next STORAGE_SLOTS are a passive crafting prop's Output.
extern int storageDrag;

void closeStorage();
// Empties both sides of the trade and lets go of a dragged NPC stack
void clearTradeTable();

// The right-click menu on a unit or a prop: the options for that target, done by your selected units. It opens at
// the pointer, and a click on an option picks it; a click anywhere else closes it.
enum class MenuAction { Talk, Shop, Trade, Follow, Defend, Attack, AttackUnprovoked, Inventory, Use, Occupy, Place,
                        Unlock, Lock, PickUp, PutDown, AutoHarvest, AutoHaul, Disband };
struct ContextMenu {
    bool open = false;
    float x = 0.0f, y = 0.0f;
    Entity target = NULL_ENTITY;
    std::vector<MenuAction> actions;
};
extern ContextMenu contextMenu;

// The Disband popout (layoutDisband in ui_layout.h): the unit of yours it asks about, or NULL_ENTITY when it is shut, and when
// its Disband button was pressed (SDL milliseconds), or 0 while it is not held
extern Entity disbandTarget;
extern unsigned long long disbandHeldAt;

// The looting panel: the Unconscious unit or corpse it shows and the unit of yours taking from it. It stays open while
// the looter is within LOOT_RANGE of it. NULL_ENTITY when closed.
extern Entity lootUnit;
extern Entity lootBy;
void closeLoot();

// The unit the Unit Panel shows when it is not one of your squad: an NPC or a corpse clicked on. Clicking one of your
// squad, or empty ground, goes back to showing your selection. Selecting your squad is separate: see selectedUnits.
extern Entity inspectUnit;

// A stack's item being chosen for the trade: how many, out of how many the inventory holds that are not in the trade yet
struct QuantityPrompt {
    bool open = false;
    bool npcSide = false;
    Entity owner = NULL_ENTITY;
    int slot = -1;
    int item = 0;
    std::string name;
    int chosen = 1;
    int available = 0;
    bool toStorage = false;   // the stack goes into the open storage panel's prop, not into the trade
};
extern QuantityPrompt quantity;

} // namespace play
