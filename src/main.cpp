#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>   // the WinMain entry point for a Windows GUI program (no console window)
#include "language.h"
#include <SDL3_ttf/SDL_ttf.h>
#include <GL/glew.h>
#include <SDL3/SDL_opengl.h>
#include <SDL3_image/SDL_image.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "build_info.h"
#include "camera.h"
#include "gui.h"
#include "sky.h"
#include "components.h"
#include "ecs.h"
#include "renderer.h"
#include "resources.h"
#include "map.h"
#include "audio.h"
#include "pak.h"
#include "fonts.h"
#include "savegame.h"
#include "settings.h"
#include "settings_menu.h"
#include "display.h"
#include "inventory.h"
#include "party.h"
#include "party_ui.h"
#include "behavior.h"
#include "combat.h"
#include "equip.h"
#include "textcase.h"
#include "items.h"
#include "systems.h"
#include "terrain.h"
#include "ui.h"
#include "useitem.h"
#include "world_format.h"

namespace {

constexpr size_t HOVER_NAME_LIMIT = 10;      // the hover list shows up to this many names
constexpr size_t MIN_SAVE_ROWS = 5;          // the save list shows between these many rows,
constexpr size_t MAX_SAVE_ROWS = 10;         // depending on how tall the window is
constexpr Uint64 DELETE_HOLD_MS = 3000;      // holding Delete this long deletes the save
constexpr size_t NAME_LIMIT = 32;            // longest save name, in bytes
constexpr Uint64 UNSAVED_WARN_MS = 30000;    // loading over a game not saved this recently asks first
constexpr const char* SETTINGS_PATH = "settings.cfg";

// Save list columns, as fractions of the list width. The name column starts at the left.
constexpr float COL_TYPE = 0.54f;
constexpr float COL_TIME = 0.70f;

// What the save list is sorted by. Autosaves always sit above manual saves, whatever the sort.
enum class SortKey { Name, Time };

// Which question the popout is asking, if any
enum class Ask { None, Overwrite, LoadUnsaved, Delete };

// Ray through a pixel, intersected with the terrain surface. Returns world (x, z).
std::optional<std::pair<float, float>> pickGround(const Camera& cam, const Terrain& t,
                                                  float mx, float my, int W, int H) {
    Mat4 vp = cam.viewProj((float)W / (float)H);
    float inv[16];
    if (!invert4x4(vp.m, inv)) return std::nullopt;

    float ndcX = 2.0f * mx / (float)W - 1.0f;
    float ndcY = 1.0f - 2.0f * my / (float)H;

    auto unproject = [&](float ndcZ, float out[3]) {
        float p[4] = { ndcX, ndcY, ndcZ, 1.0f };
        float r[4];
        for (int i = 0; i < 4; ++i) {
            r[i] = inv[0 * 4 + i] * p[0] + inv[1 * 4 + i] * p[1]
                 + inv[2 * 4 + i] * p[2] + inv[3 * 4 + i] * p[3];
        }
        out[0] = r[0] / r[3];
        out[1] = r[1] / r[3];
        out[2] = r[2] / r[3];
    };

    float n[3], f[3];
    unproject(-1.0f, n);
    unproject(1.0f, f);

    // Iterate: intersect with the plane at the terrain height under the last hit
    float groundY = 0.0f, hx = 0.0f, hz = 0.0f;
    for (int iter = 0; iter < 6; ++iter) {
        float dy = f[1] - n[1];
        if (std::fabs(dy) < 1e-6f) return std::nullopt;
        float s = (groundY - n[1]) / dy;
        hx = n[0] + (f[0] - n[0]) * s;
        hz = n[2] + (f[2] - n[2]) * s;
        groundY = t.heightAt(hx, hz);
    }

    if (hx < 0.0f || hz < 0.0f || hx >= t.size() || hz >= t.size()) return std::nullopt;
    return std::make_pair(hx, hz);
}

// World point to pixel coordinates. Returns false if it's behind the camera.
bool worldToScreen(const Mat4& vp, float x, float y, float z, int W, int H, float& sx, float& sy) {
    float c[4];
    for (int i = 0; i < 4; ++i) {
        c[i] = vp.m[0 * 4 + i] * x + vp.m[1 * 4 + i] * y + vp.m[2 * 4 + i] * z + vp.m[3 * 4 + i];
    }
    if (c[3] <= 0.0f) return false;
    sx = (c[0] / c[3] * 0.5f + 0.5f) * (float)W;
    sy = (1.0f - (c[1] / c[3] * 0.5f + 0.5f)) * (float)H;
    return true;
}

void clearSelection(World& w) {
    for (Entity e : w.view<Selected>()) w.remove<Selected>(e);
}

std::vector<Entity> selectedUnits(World& w) {
    std::vector<Entity> out;
    for (Entity u : w.view<Unit, Selected, Commandable>()) out.push_back(u);
    return out;
}

// The unit the sidebar shows: the lone one of yours that is selected, or, when none of yours is, the lone red
// unit. NULL when nothing is selected or more than one of yours is.
// The neutral unit being talked to, or NULL_ENTITY when no talk box is open
Entity dialogueUnit = NULL_ENTITY;
Entity dialoguePlayer = NULL_ENTITY;      // the player unit a talk is with, who speaks the player's lines
std::vector<SpokenLine> dialogueConvo;    // the lines of the talk so far
bool tradeOpen = false;                   // the trade popout is up, in place of the talk box
// A free trade between two of your own units, from the right-click menu's Trade: no value or fee, only room counts
bool freeTrade = false;
constexpr float MOVE_MARK_SECONDS = 3.0f;  // how long the green X stays on the world map after a move order, fading in its last second
constexpr float TALK_COOLDOWN = 1.0f;     // seconds after a talk ends before another one can start
float talkCooldown = 0.0f;                // seconds of that cooldown left
bool paused = false;                      // paused with Space, until Space is pressed again
std::vector<TradeGhost> tradeNpc;         // items shown on the NPC's side of the trade
std::vector<TradeGhost> tradePlayer;      // items shown on your side of the trade
int tradeNpcDrag = -1;                    // the NPC's inventory slot being dragged into the trade, or -1

// The storage panel: the storage prop it shows (a barrel) and the unit using it. It stays open while that unit is
// within STORAGE_CLOSE_RANGE of the prop. NULL_ENTITY when closed.
Entity storageProp = NULL_ENTITY;
Entity storageUnit = NULL_ENTITY;
// The panel slot being dragged out of the panel, or -1. Panel slots 0 to STORAGE_SLOTS - 1 are the prop's Storage (its
// input, at a crafting prop); the next STORAGE_SLOTS are a passive crafting prop's Output.
int storageDrag = -1;

void closeStorage() {
    storageProp = NULL_ENTITY;
    storageUnit = NULL_ENTITY;
    storageDrag = -1;
}

// The right-click menu on a unit or a prop: the options for that target, done by your selected units. It opens at
// the pointer, and a click on an option picks it; a click anywhere else closes it.
enum class MenuAction { Talk, Shop, Trade, Follow, Defend, Attack, AttackUnprovoked, Inventory, Use, Unlock, PickUp,
                        PutInBed, PutDown };
struct ContextMenu {
    bool open = false;
    float x = 0.0f, y = 0.0f;
    Entity target = NULL_ENTITY;
    std::vector<MenuAction> actions;
};
ContextMenu contextMenu;

// The looting panel: the Unconscious unit or corpse it shows and the unit of yours taking from it. It stays open while
// the looter is within LOOT_RANGE of it. NULL_ENTITY when closed.
Entity lootUnit = NULL_ENTITY;
Entity lootBy = NULL_ENTITY;
void closeLoot() {
    lootUnit = NULL_ENTITY;
    lootBy = NULL_ENTITY;
}

// The unit the Unit Panel shows when it is not one of your squad: an NPC or a corpse clicked on. Clicking one of your
// squad, or empty ground, goes back to showing your selection. Selecting your squad is separate: see selectedUnits.
Entity inspectUnit = NULL_ENTITY;

// The name of a prop in the language in use
std::string propNameOf(World& w, Entity prop) {
    const Prop* p = w.get<Prop>(prop);
    return p ? lang(propInfo(p->kind).nameKey) : std::string();
}

// What the open storage panel's prop is used for: PropUse::Storage for a barrel, PropUse::Crafting for a campfire
PropUse storageUse(World& w) {
    const Prop* p = w.get<Prop>(storageProp);
    return p ? propInfo(p->kind).use : PropUse::None;
}

// The storage panel's layout: one grid for a barrel or a campfire, an input and an output grid for a forge
StorageLayout storageLayout(World& w, int W, int H) {
    return layoutStorage(W, H, w.has<Output>(storageProp));
}

// A panel slot of the open storage (see storageDrag), or null if there is no such slot
ItemStack* storageSlot(World& w, int slot) {
    if (slot < 0) return nullptr;
    if (slot < STORAGE_SLOTS) {
        Storage* st = w.get<Storage>(storageProp);
        return st ? &st->slots[(size_t)slot] : nullptr;
    }
    Output* out = w.get<Output>(storageProp);
    return out && slot < 2 * STORAGE_SLOTS ? &out->slots[(size_t)(slot - STORAGE_SLOTS)] : nullptr;
}

// Moves a whole stack from the storage into its unit's inventory, if all of it fits. Otherwise nothing moves.
bool takeFromStorage(World& w, int slot, std::string& why) {
    ItemStack* from = storageSlot(w, slot);
    Inventory* inv = w.get<Inventory>(storageUnit);
    if (!from || !inv || from->item == 0) return false;
    const ItemStack stack = *from;
    if (!slotsCanTake(inv->slots.data(), INVENTORY_SLOTS, stack.item, stack.count)) {
        const ItemDef* def = findItem(stack.item);
        why = langf("MISC_NO_ROOM_INV", { def ? itemDisplayName(*def) : std::string(lang("MISC_AN_ITEM")) });
        return false;
    }
    *from = ItemStack{};
    giveOrDrop(w, storageUnit, stack.item, stack.count);   // checked to fit above; anything that didn't would fall
    queueSound(Sfx::Take);
    propStorageChanged(w, storageProp);   // a campfire that lost its input stops its work
    const ItemDef* got = findItem(stack.item);
    queueUnitLabel(storageUnit, itemGainLabel(stack.count, got ? itemDisplayName(*got) : std::string(lang("MISC_AN_ITEM"))));
    return true;
}

// Drops a whole stack from the storage on the ground where its unit stands, not where the prop is
bool dropFromStorage(World& w, int slot) {
    ItemStack* from = storageSlot(w, slot);
    const Transform* ut = w.get<Transform>(storageUnit);
    if (!from || !ut || from->item == 0) return false;
    const ItemStack stack = *from;
    const ItemDef* def = findItem(stack.item);
    queueLog(langf("MISC_DROPS", { nameOf(w, storageUnit), std::to_string(stack.count),
                                   def ? itemDisplayName(*def) : std::string(lang("MISC_AN_ITEM")) }), LogKind::Drop);
    const Entity e = w.create();
    w.add(e, Transform{ ut->x, ut->y, ut->z });
    w.add(e, GroundItem{ stack.item, stack.count });
    *from = ItemStack{};
    queueSound(Sfx::Drop);
    propStorageChanged(w, storageProp);   // a campfire that lost its input stops its work
    return true;
}

// Why the open storage won't take an item: a crafting prop takes only its input, a filtered storage only its sort
// (props.h: ItemFilter). Empty if it takes it.
std::string refusalFor(World& w, int item) {
    const Prop* prop = w.get<Prop>(storageProp);
    if (!prop || propAcceptsItem(prop->kind, item)) return std::string();
    const PropInfo& info = propInfo(prop->kind);
    if (info.use == PropUse::Storage) return langf("MISC_STORAGE_ONLY", { lang(itemFilterKey(info.accepts)) });
    const ItemDef* def = findItem(item);
    return langf("MISC_CRAFT_WRONG_ITEM", { def ? itemDisplayName(*def) : std::string(lang("MISC_AN_ITEM")) });
}

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
QuantityPrompt quantity;

void takeFromSlot(Inventory& inv, int slot, int count);

// How many of an item the storage unit carries, over all its stacks
int carriedCount(const Inventory& inv, int item) {
    int total = 0;
    for (const ItemStack& s : inv.slots) {
        if (s.item == item) total += s.count;
    }
    return total;
}

// Moves `count` of the clicked stack's item from the storage unit into the storage: the clicked stack first, then the
// unit's other stacks of that item. Nothing moves unless all of them fit.
bool storeCountInStorage(World& w, int clickedSlot, int count, std::string& why) {
    Storage* st = w.get<Storage>(storageProp);
    Inventory* inv = w.get<Inventory>(storageUnit);
    if (!st || !inv || clickedSlot < 0 || clickedSlot >= INVENTORY_SLOTS || count <= 0) return false;
    const int item = inv->slots[clickedSlot].item;
    if (item == 0 || carriedCount(*inv, item) < count) return false;
    // A barrel takes anything into any slot. A campfire or a forge takes only its input item, and a weapon rack and the
    // like only their sort of item.
    if (const std::string no = refusalFor(w, item); !no.empty()) {
        why = no;
        return false;
    }
    if (!slotsCanTake(st->slots.data(), STORAGE_SLOTS, item, count)) {
        why = lang("MISC_STORAGE_FULL");
        return false;
    }
    int left = count;
    std::vector<int> order{ clickedSlot };
    for (int i = 0; i < INVENTORY_SLOTS; ++i) {
        if (i != clickedSlot && inv->slots[i].item == item) order.push_back(i);
    }
    for (int slot : order) {
        if (left <= 0) break;
        const int take = std::min(left, inv->slots[slot].count);
        takeFromSlot(*inv, slot, take);
        left -= take;
    }
    addToSlots(st->slots.data(), STORAGE_SLOTS, item, count);
    propStorageChanged(w, storageProp);
    queueUnitLabel(storageUnit, itemLossLabelFor(count, item), true);   // "-3x Iron Ore" over the unit that stored it
    queueSound(Sfx::Store);
    return true;
}

// Shows a stack on one side of a trade, in the next free cell. Only a picture: the inventory is not changed.
// How many of an item an inventory holds that are not already in one side of a trade
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

// Puts up to `want` of the clicked stack's item in a side of the trade: the clicked stack first, then the other stacks
// of that item in the inventory. Stacks already in the trade only give what is still free. Returns how many went in.
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

// Takes some of a stack's items out of an inventory; the slot empties when nothing is left in it
void takeFromSlot(Inventory& inv, int slot, int count) {
    if (slot < 0 || slot >= INVENTORY_SLOTS) return;
    ItemStack& st = inv.slots[slot];
    st.count -= count;
    if (st.count <= 0) st = ItemStack{};
}

// Whether an inventory still holds everything a side of the trade says it gives
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
    return tradeStillThere(w.get<Inventory>(npc), tradeNpc) && tradeStillThere(w.get<Inventory>(player), tradePlayer);
}

// Takes back out the ghost shown in one of the cells. True if one was there.
bool takeTradeGhostAt(std::vector<TradeGhost>& side, const std::vector<Rect>& cells, float x, float y) {
    for (size_t i = 0; i < side.size() && i < cells.size(); ++i) {
        if (cells[i].contains(x, y)) {
            side.erase(side.begin() + (int)i);
            return true;
        }
    }
    return false;
}

// The distance in tiles between two units on the ground
float gapBetween(World& w, Entity a, Entity b) {
    const Transform* ta = w.get<Transform>(a);
    const Transform* tb = w.get<Transform>(b);
    if (!ta || !tb) return 1e9f;
    const float dx = ta->x - tb->x, dz = ta->z - tb->z;
    return std::sqrt(dx * dx + dz * dz);
}

// Whether a side's inventory has room for what it receives, once what it gives is taken out of it
bool tradeFits(Inventory inv, const std::vector<TradeGhost>& gives, const std::vector<TradeGhost>& receives) {
    for (const TradeGhost& g : gives) {
        if (g.slot >= 0 && g.slot < INVENTORY_SLOTS) takeFromSlot(inv, g.slot, g.stack.count);
    }
    for (const TradeGhost& g : receives) {
        if (addToInventory(inv, g.stack.item, g.stack.count) != 0) return false;
    }
    return true;
}

// Whether the trade on the table can be confirmed: both sides have items, each inventory has room for what it
// receives, and the offer covers the NPC's price with its fee. Also how close the offer is, for the value bar.
TradeStatus checkTrade(World& w, Entity npc, Entity player) {
    TradeStatus st;
    const Inventory* ni = npc != NULL_ENTITY ? w.get<Inventory>(npc) : nullptr;
    const Inventory* pi = player != NULL_ENTITY ? w.get<Inventory>(player) : nullptr;
    if (!ni || !pi) {
        st.text = lang("MISC_NO_UNIT_TO_TRADE");
        return st;
    }
    if (freeTrade) {
        // Between your own units: anything may go either way, so long as each side has room for what it receives
        st.progress = 1.0f;
        if (tradeNpc.empty() && tradePlayer.empty()) st.text = lang("MISC_FREE_TRADE_ADD");
        else if (!tradeFits(*pi, tradePlayer, tradeNpc)) st.text = lang("MISC_TRADE_NO_ROOM_YOURS");
        else if (!tradeFits(*ni, tradeNpc, tradePlayer)) st.text = lang("MISC_TRADE_NO_ROOM_THEIRS");
        else {
            st.ok = true;
            st.text = lang("MISC_FREE_TRADE_READY");
        }
        return st;
    }
    const int npcValue = tradeValueOf(tradeNpc);
    const int offer = tradeValueOf(tradePlayer);
    const int needed = requiredOffer(npcValue, levelOf(statsOf(w, player), STAT_CHARISMA));
    st.progress = needed > 0 ? (float)offer / (float)needed : 1.0f;
    if (tradeNpc.empty() || tradePlayer.empty()) st.text = lang("MISC_TRADE_ADD_BOTH");
    else if (!tradeFits(*pi, tradePlayer, tradeNpc)) st.text = lang("MISC_TRADE_NO_ROOM_YOURS");
    else if (!tradeFits(*ni, tradeNpc, tradePlayer)) st.text = lang("MISC_TRADE_NO_ROOM_THEIRS");
    else if (offer < needed) st.text = langf("MISC_NEED_MORE_VALUE", { std::to_string(needed - offer) });
    else {
        st.ok = true;
        st.text = langf("MISC_TRADE_READY_FEE", { std::to_string(needed - npcValue) });
    }
    return st;
}

// Makes the trade: each side's items move to the other, and the player's Charisma gains XP for the fee paid
void completeTrade(World& w, Entity npc, Entity player) {
    Inventory* ni = w.get<Inventory>(npc);
    Inventory* pi = w.get<Inventory>(player);
    const int npcValue = tradeValueOf(tradeNpc);
    const int fee = freeTrade ? 0 : requiredOffer(npcValue, levelOf(statsOf(w, player), STAT_CHARISMA)) - npcValue;
    for (const TradeGhost& g : tradeNpc) takeFromSlot(*ni, g.slot, g.stack.count);
    for (const TradeGhost& g : tradePlayer) takeFromSlot(*pi, g.slot, g.stack.count);
    // The trade was checked to fit, but should anything not, it falls at the receiver's feet
    for (const TradeGhost& g : tradeNpc) giveOrDrop(w, player, g.stack.item, g.stack.count);
    for (const TradeGhost& g : tradePlayer) giveOrDrop(w, npc, g.stack.item, g.stack.count);
    // Over each side, what it received ("+3x Iron Ore") and what it gave ("-Gold Coin"), one label per kind of item
    auto totals = [](const std::vector<TradeGhost>& side) {
        std::vector<std::pair<int, int>> out;   // item, count, in the order first offered
        for (const TradeGhost& g : side) {
            auto it = std::find_if(out.begin(), out.end(), [&](const std::pair<int, int>& p) { return p.first == g.stack.item; });
            if (it == out.end()) out.push_back({ g.stack.item, g.stack.count });
            else it->second += g.stack.count;
        }
        return out;
    };
    for (const auto& [item, count] : totals(tradeNpc)) {
        queueUnitLabel(player, itemGainLabelFor(count, item));
        queueUnitLabel(npc, itemLossLabelFor(count, item), true);
    }
    for (const auto& [item, count] : totals(tradePlayer)) {
        queueUnitLabel(npc, itemGainLabelFor(count, item));
        queueUnitLabel(player, itemLossLabelFor(count, item), true);
    }
    if (fee > 0) grantXp(w, player, STAT_CHARISMA, fee, lang("MISC_REASON_FEES"));
    tradeOpen = false;
    tradeNpc.clear();
    tradePlayer.clear();
    tradeNpcDrag = -1;
}

// The unit the Unit Panel shows: the talking unit during a trade, the unit using a storage prop while its panel is
// open, otherwise the lone selected unit
Entity sidebarUnitOf(World& w) {
    if (tradeOpen && dialoguePlayer != NULL_ENTITY && w.has<Unit>(dialoguePlayer)) return dialoguePlayer;
    if (storageProp != NULL_ENTITY && w.has<Unit>(storageUnit)) return storageUnit;
    if (lootUnit != NULL_ENTITY && w.has<Unit>(lootBy)) return lootBy;
    if (inspectUnit != NULL_ENTITY && w.has<Unit>(inspectUnit)) return inspectUnit;
    const std::vector<Entity> mine = selectedUnits(w);
    return mine.size() == 1 ? mine[0] : NULL_ENTITY;
}

// The units the Unit Panel acts on: the talking unit during a trade, the storage panel's unit, otherwise the selected units
std::vector<Entity> panelUnits(World& w) {
    if (tradeOpen && dialoguePlayer != NULL_ENTITY && w.has<Unit>(dialoguePlayer)) return { dialoguePlayer };
    if (storageProp != NULL_ENTITY && w.has<Unit>(storageUnit)) return { storageUnit };
    if (lootUnit != NULL_ENTITY && w.has<Unit>(lootBy)) return { lootBy };
    return selectedUnits(w);
}

// The unit a click lands on: the one whose picture is under the pointer (yours first), else the nearest within one
// tile of the clicked ground point. NULL_ENTITY for none.
Entity unitClicked(World& w, const Terrain& t, const Camera& cam, const std::vector<Entity>& underPointer,
                   float mx, float my, int W, int H) {
    for (Entity u : underPointer) {
        if (w.has<Commandable>(u)) return u;
    }
    if (!underPointer.empty()) return underPointer[0];
    auto hit = pickGround(cam, t, mx, my, W, H);
    if (!hit) return NULL_ENTITY;
    Entity best = NULL_ENTITY;
    float bestD2 = 1.0f;
    for (Entity u : w.view<Unit, Transform>()) {
        Transform* tr = w.get<Transform>(u);
        float dx = tr->x - hit->first, dz = tr->z - hit->second;
        float d2 = dx * dx + dz * dz;
        if (d2 < bestD2) { bestD2 = d2; best = u; }
    }
    return best;
}

// Click selection. One of your squad is selected (added, with Shift or Alt; else in place of the others), and the Unit
// Panel goes back to your selection. Any other unit (an NPC, a corpse) is shown in the Unit Panel without touching your
// selection. Empty ground changes nothing: your squad stays selected.
void clickSelect(World& w, Entity hit, bool additive) {
    if (hit == NULL_ENTITY) {
        inspectUnit = NULL_ENTITY;
        return;
    }
    if (!w.has<Commandable>(hit)) {
        inspectUnit = hit;
        return;
    }
    inspectUnit = NULL_ENTITY;
    if (!additive) clearSelection(w);
    if (!w.has<Selected>(hit)) w.add(hit, Selected{});
}

// Box selection: every unit of your squad whose screen position falls inside the rectangle. A box with none of them in
// it changes nothing, so your squad is never left with nobody selected.
void boxSelect(World& w, const Camera& cam, float x0, float y0, float x1, float y1,
               int W, int H, bool additive) {
    float minX = std::min(x0, x1), maxX = std::max(x0, x1);
    float minY = std::min(y0, y1), maxY = std::max(y0, y1);
    Mat4 vp = cam.viewProj((float)W / (float)H);

    std::vector<Entity> inBox;
    for (Entity u : w.view<Unit, Transform, Commandable>()) {
        Transform* tr = w.get<Transform>(u);
        if (!withinDetail(cam, tr->x, tr->y, tr->z)) continue;
        float sx, sy;
        if (worldToScreen(vp, tr->x, tr->y + 0.5f, tr->z, W, H, sx, sy)
            && sx >= minX && sx <= maxX && sy >= minY && sy <= maxY) {
            inBox.push_back(u);
        }
    }
    if (inBox.empty()) return;
    inspectUnit = NULL_ENTITY;
    if (!additive) clearSelection(w);
    for (Entity u : inBox) {
        if (!w.has<Selected>(u)) w.add(u, Selected{});
    }
}

// Right click: order the selected units to a ground point.
// Returns true, with the ground point in gx and gz, if an order was given.
bool rightClickOrder(World& w, Terrain& t, const Camera& cam, float mx, float my, int W, int H,
                     float& gx, float& gz) {
    if (selectedUnits(w).empty()) return false;
    auto hit = pickGround(cam, t, mx, my, W, H);
    if (!hit) return false;
    gx = hit->first;
    gz = hit->second;
    return true;
}

// Draws one loading frame and keeps the window responsive while things load
void loadingFrame(SDL_Window* window, Renderer& renderer, float progress, const std::string& label = std::string()) {
    SDL_PumpEvents();
    int winW, winH;
    SDL_GetWindowSizeInPixels(window, &winW, &winH);
    renderer.drawLoadingScreen(progress, winW, winH, label);
    SDL_GL_SwapWindow(window);
}

// Game worlds are kept in this folder, beside the game.
std::string mapsDir() {
    const char* base = SDL_GetBasePath();
    return std::string(base ? base : "") + "maps";
}

// The folder beside the game where F12 screenshots are saved
std::string screenshotsDir() {
    const char* base = SDL_GetBasePath();
    return std::string(base ? base : "") + "screenshots";
}

// Reads back the frame that is about to be shown and saves it as a PNG in the screenshots folder.
// Returns the file's name, or an empty string if it could not be saved.
std::string saveScreenshot(SDL_Window* window) {
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    if (w <= 0 || h <= 0) return "";

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    std::vector<unsigned char> pixels((size_t)w * h * 4);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    // OpenGL counts rows from the bottom, the picture from the top. The window's alpha is not part of the picture.
    const size_t rowBytes = (size_t)w * 4;
    std::vector<unsigned char> flipped(pixels.size());
    for (int y = 0; y < h; ++y) {
        std::copy_n(pixels.data() + (size_t)(h - 1 - y) * rowBytes, rowBytes, flipped.data() + (size_t)y * rowBytes);
    }
    for (size_t i = 3; i < flipped.size(); i += 4) flipped[i] = 255;

    std::error_code ec;
    const std::filesystem::path dir = screenshotsDir();
    std::filesystem::create_directories(dir, ec);
    if (ec) return "";

    // Named by the time it was taken. A number is added if that name is already used.
    const std::time_t now = std::time(nullptr);
    const std::tm* tm = std::localtime(&now);
    char stamp[32] = "screenshot";
    if (tm) std::strftime(stamp, sizeof(stamp), "screenshot_%Y%m%d_%H%M%S", tm);
    std::string name = std::string(stamp) + ".png";
    for (int n = 2; std::filesystem::exists(dir / name, ec); ++n) {
        name = std::string(stamp) + "_" + std::to_string(n) + ".png";
    }

    SDL_Surface* surface = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_ABGR8888, flipped.data(), (int)rowBytes);
    if (!surface) return "";
    const bool ok = IMG_SavePNG(surface, (dir / name).string().c_str());
    SDL_DestroySurface(surface);
    return ok ? name : "";
}

// A folder left as custom_maps by an earlier version moves to maps the first time the game starts
void moveOldMapsFolder() {
    const char* base = SDL_GetBasePath();
    const std::filesystem::path oldDir = std::string(base ? base : "") + "custom_maps";
    const std::filesystem::path newDir = mapsDir();
    std::error_code ec;
    if (!std::filesystem::is_directory(oldDir, ec) || std::filesystem::exists(newDir, ec)) return;
    std::filesystem::rename(oldDir, newDir, ec);
    if (ec) SDL_Log("Could not move custom_maps to maps: %s", ec.message().c_str());
}

// A map the New Game list can start: a premade map from data.pak ("data"), a game world folder in
// maps ("world"), or an older .map file in maps ("custom")
struct MapChoice {
    std::string source;      // "data", "world" or "custom"
    std::string stem;        // file or folder name without the .map extension. Saves refer to it.
    std::string name;        // what the list shows: the saved name of a world, or the stem
    long long created = 0;   // when a world was saved, milliseconds since 1970; 0 if unknown
    std::string path;        // where a world folder or .map file is on disk

    std::string pakName() const { return "data/map/" + stem + ".map"; }
};

// The main map: a world folder packed into data.pak at data/map/main. New Game always starts on it.
constexpr const char* MAIN_MAP_STEM = "main";

bool mainMapPacked() {
    const unsigned char* bytes = nullptr;
    size_t size = 0;
    return pakFind(std::string("data/map/") + MAIN_MAP_STEM + "/world.txt", bytes, size);
}

// Every map the New Game list can start, not yet sorted: the main map, the premade maps, then worlds and .map files
std::vector<MapChoice> listMaps() {
    std::vector<MapChoice> out;
    if (mainMapPacked()) {
        out.push_back(MapChoice{ "packed", MAIN_MAP_STEM, MAIN_MAP_STEM, 0,
                                 std::string(PAK_WORLD_PREFIX) + "data/map/" + MAIN_MAP_STEM });
    }
    const std::string prefix = "data/map/", suffix = ".map";
    for (const std::string& pak : pakNames(prefix, suffix)) {
        const std::string stem = pak.substr(prefix.size(), pak.size() - prefix.size() - suffix.size());
        out.push_back(MapChoice{ "data", stem, stem, 0, "" });
    }

    const std::string dir = mapsDir();
    for (const WorldEntry& e : listWorlds(dir)) {
        const std::string stem = std::filesystem::path(e.folder).filename().string();
        out.push_back(MapChoice{ "world", stem, e.name, e.created, e.folder });
    }

    std::error_code ec;
    if (std::filesystem::is_directory(dir, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (entry.path().extension() != ".map") continue;
            const std::string stem = entry.path().stem().string();
            out.push_back(MapChoice{ "custom", stem, stem, 0, entry.path().string() });
        }
    }
    return out;
}

// Puts a map's ground into the terrain: a premade or custom map whole, or a world folder to be streamed
bool loadTerrain(const MapChoice& c, Terrain& terrain, std::string& err) {
    if (c.source == "world" || c.source == "packed") return terrain.openWorld(c.path, err);

    MapData data;
    bool ok = false;
    if (c.source == "data") {
        const unsigned char* bytes = nullptr;
        size_t size = 0;
        ok = pakFind(c.pakName(), bytes, size) && parseMap(bytes, size, data);
    } else {
        ok = readMapFile(c.path, data);
    }
    if (!ok) {
        err = langf("MISC_CANNOT_READ", { c.name });
        return false;
    }
    terrain.load(data);
    return true;
}

// Every item in the database is put on the ground this many times when a level starts
constexpr int ITEM_COPIES_PER_LEVEL = 10;

// Builds a fresh level on a map: the ground, then the entities from the seed, then the items on the
// ground if spawnItems is set. progress, if given, is called as the level loads (0..1), for the loading screen.
bool prepareLevel(const MapChoice& c, World& world, Terrain& terrain, Renderer& renderer, Rng& rng,
                  unsigned seed, bool spawnItems, std::string& err, const std::function<void(float)>& progress = {}) {
    auto report = [&](float p) { if (progress) progress(p); };
    if (!loadTerrain(c, terrain, err)) return false;
    report(0.35f);
    renderer.uploadTerrain(terrain);
    report(0.7f);
    world = World{};
    dialogueUnit = NULL_ENTITY;   // entity ids restart with the new world, so a talk box can't keep pointing at one
    dialogueConvo.clear();
    discardQueuedLog();
    tradeOpen = false;
    tradeNpc.clear();
    tradePlayer.clear();
    tradeNpcDrag = -1;
    closeStorage();
    contextMenu = ContextMenu{};
    freeTrade = false;
    paused = false;
    rng.gen.seed(seed);
    setupWorld(world, terrain, rng);
    // log.txt: how many of each prop kind went down, and how far the nearest is from the first unit
    {
        std::array<int, PROP_KIND_COUNT> placed{};
        std::array<float, PROP_KIND_COUNT> nearest{};
        nearest.fill(1e9f);
        Vec2 home{ 0.0f, 0.0f };
        for (Entity u : world.view<Unit, Transform, Commandable>()) {
            home = Vec2{ world.get<Transform>(u)->x, world.get<Transform>(u)->z };
            break;
        }
        for (Entity e : world.view<Prop, Transform>()) {
            const int kind = world.get<Prop>(e)->kind;
            if (kind < 0 || kind >= PROP_KIND_COUNT) continue;
            ++placed[(size_t)kind];
            const Transform* pt = world.get<Transform>(e);
            nearest[(size_t)kind] = std::min(nearest[(size_t)kind], std::hypot(pt->x - home.x, pt->z - home.z));
        }
        std::string line;
        for (int k = 0; k < PROP_KIND_COUNT; ++k) {
            char buf[96];
            std::snprintf(buf, sizeof(buf), "%s%s %d (%.0f)", k ? ", " : "", lang(propInfo(k).nameKey).c_str(),
                          placed[(size_t)k], placed[(size_t)k] ? nearest[(size_t)k] : -1.0f);
            line += buf;
        }
        SDL_Log("Props placed, with the nearest one's distance in tiles: %s", line.c_str());
    }
    if (spawnItems) spawnGroundItems(world, terrain, rng, ITEM_COPIES_PER_LEVEL);
    report(0.9f);
    return true;
}

// Open worlds keep the ground loaded around the camera and every unit the player commands
constexpr int STREAM_RADIUS = 4;            // chunks kept loaded around each focus point
constexpr int STREAM_LOADS_PER_FRAME = 4;   // chunks read in per frame while playing, so play never stalls

std::vector<Vec2> streamFocuses(World& w, const Camera& cam) {
    std::vector<Vec2> out{ Vec2{ cam.focusX, cam.focusZ } };
    for (Entity u : w.view<Unit, Transform, Commandable>()) {
        const Transform* t = w.get<Transform>(u);
        out.push_back(Vec2{ t->x, t->z });
    }
    return out;
}

// Reads in the ground around the camera and the units straight away, so a level starts with its ground in place
void primeStreaming(World& w, Terrain& t, const Camera& cam) {
    if (t.streamed()) t.stream(streamFocuses(w, cam), STREAM_RADIUS, 0);
}

// Rebuilds a level from a save. The ground comes from the map, and the props from the seed.
// The units are then swapped for the saved ones.
bool restoreLevel(World& world, Terrain& terrain, Renderer& renderer, Rng& rng, const SaveData& d,
                  const MapChoice& map, std::string& err) {
    // A save from before items existed has none, so its items are placed from the seed as on a new level.
    // A save that has items gets exactly those, and nothing is placed from the seed.
    if (!prepareLevel(map, world, terrain, renderer, rng, d.seed, !d.hasItems, err)) return false;

    // Drop the units that setupWorld generated
    for (Entity e : world.view<Unit>()) world.destroy(e);

    std::vector<Entity> restoredUnits;   // in the save's order
    for (const SaveUnit& u : d.units) {
        Entity e = world.create();
        restoredUnits.push_back(e);
        world.add(e, Transform{ u.x, u.y, u.z });
        // Hostiles walk at their own pace, as on a new level. Every unit fights, so each one gets Combat.
        Unit unit;
        if (u.hostile || u.neutral) unit.speed = HOSTILE_WALK_SPEED;
        world.add(e, unit);
        world.add(e, Combat{});
        if (u.hostile) {
            world.add(e, Hostile{});
        } else if (u.neutral) {
            world.add(e, Neutral{});
        } else if (!u.corpse) {
            world.add(e, Commandable{});   // one of yours; a corpse is nobody's
        }
        if (u.unconscious) {
            world.add(e, Unconscious{ u.corpse ? 100.0f : u.doom });
            if (u.corpse) world.add(e, Corpse{});
        }
        if (u.hostile || u.neutral) world.add(e, Dialogue{});
        // Every unit carries items and has behaviors, whichever side it is on
        Inventory inv;
        for (const SaveStack& s : u.stacks) {
            if (s.slot >= 0 && s.slot < INVENTORY_SLOTS && s.stack.item > 0 && s.stack.count > 0) {
                inv.slots[s.slot] = s.stack;
            }
        }
        world.add(e, inv);
        Equipment worn;
        for (const SaveGear& g : u.gear) worn.items[g.slot] = g.item;
        world.add(e, worn);
        world.add(e, u.hasNeeds ? u.needs : Needs{});
        Behaviors behaviors;   // older saves have none, so their units get the defaults
        if (u.hasBehaviors) behaviors.rules = u.behaviors;
        world.add(e, behaviors);
        world.add(e, u.hasStats ? Stats{ u.stats } : defaultStats());
        world.add(e, Name{ u.name });
        world.add(e, UnitSprite{ u.sprite });
        world.add(e, Body{ u.hasBody ? u.body : bodyForSavedName(u.name) });
        // Older saves have no faction: it comes from the side the unit is on
        world.add(e, Faction{ u.hasFaction ? u.faction : u.hostile ? FACTION_RED : u.neutral ? FACTION_GRAY : FACTION_PLAYER });
    }

    // Who was carrying whom: the carrier is named by its place in the save
    for (size_t i = 0; i < d.units.size() && i < restoredUnits.size(); ++i) {
        const int by = d.units[i].carriedBy;
        if (by < 0 || by >= (int)restoredUnits.size() || by == (int)i) continue;
        const Entity carrier = restoredUnits[(size_t)by];
        if (world.has<Carrying>(carrier) || world.has<Unconscious>(carrier) || world.has<Carried>(carrier)) continue;
        world.add(restoredUnits[i], Carried{ carrier });
        if (d.units[i].playingDead && !d.units[i].unconscious) world.add(restoredUnits[i], PlayingDead{});
        world.add(carrier, Carrying{ restoredUnits[i] });
    }

    if (d.hasItems) {
        for (const SaveGround& g : d.ground) {
            if (g.item <= 0 || g.count <= 0) continue;
            Entity e = world.create();
            world.add(e, Transform{ g.x, g.y, g.z });
            world.add(e, GroundItem{ g.item, g.count });
        }
    }
    // The props' states below name each prop by its serial. A save made when the level laid its props out another way
    // (other kinds, or another number of each) would put them on the wrong props, so they are left out.
    if (d.propLayout != PROP_LAYOUT) {
        SDL_Log("Save has prop layout %d, the game has %d: prop contents, locks and seats start fresh", d.propLayout, PROP_LAYOUT);
        return true;
    }
    // Which locks were open. The props were placed again from the seed, locked, so each is found by its serial.
    for (Entity e : world.view<Prop, Lock>()) {
        for (const SaveLock& l : d.locks) {
            if (l.prop == world.get<Prop>(e)->serial) world.get<Lock>(e)->locked = l.locked;
        }
    }
    // What the barrels held. The props were placed again from the seed, so each is found by its serial.
    for (Entity e : world.view<Prop, Storage>()) {
        const int serial = world.get<Prop>(e)->serial;
        Storage* st = world.get<Storage>(e);
        for (const SaveStorage& s : d.storage) {
            if (s.prop != serial || s.slot < 0 || s.slot >= STORAGE_SLOTS || s.item <= 0 || s.count <= 0) continue;
            st->slots[(size_t)s.slot] = ItemStack{ s.item, s.count };
        }
    }
    // A forge's output, and its fuel and progress
    for (Entity e : world.view<Prop, Output>()) {
        const int serial = world.get<Prop>(e)->serial;
        Output* out = world.get<Output>(e);
        for (const SaveStorage& s : d.output) {
            if (s.prop != serial || s.slot < 0 || s.slot >= STORAGE_SLOTS || s.item <= 0 || s.count <= 0) continue;
            out->slots[(size_t)s.slot] = ItemStack{ s.item, s.count };
        }
        for (const SaveFuel& f : d.fuel) {
            if (f.prop != serial) continue;
            if (Fuel* fuel = world.get<Fuel>(e)) fuel->hours = std::max(0.0, f.hours);
            if (PropWork* pw = world.get<PropWork>(e)) pw->progress = std::clamp(f.progress, 0.0, 1.0);
        }
    }
    // Units on beds, chairs and in cages, put back on them. Two saved on one prop: the first keeps it.
    for (size_t i = 0; i < d.units.size() && i < restoredUnits.size(); ++i) {
        if (d.units[i].occupying < 0) continue;
        for (Entity p : world.view<Prop, Transform>()) {
            if (world.get<Prop>(p)->serial != d.units[i].occupying) continue;
            if (propInfo(world.get<Prop>(p)->kind).use != PropUse::Occupy || propOccupant(world, p) != NULL_ENTITY) break;
            const Transform* pt = world.get<Transform>(p);
            *world.get<Transform>(restoredUnits[i]) = *pt;
            world.add(restoredUnits[i], Occupying{ p });
            break;
        }
    }
    return true;
}

// Captures what a save needs from the running world, including the camera
SaveData captureWorld(World& w, const GameState& gs, const Camera& cam, unsigned seed,
                      long long ts, const std::string& name, const MapChoice& map) {
    SaveData d;
    d.name = name;
    d.timestamp = ts;
    d.seed = seed;
    d.mapSource = map.source;
    d.mapStem = map.stem;
    d.gs = gs;
    d.propLayout = PROP_LAYOUT;

    d.cam.present = true;
    d.cam.fx = cam.focusX;
    d.cam.fy = cam.focusY;
    d.cam.fz = cam.focusZ;
    d.cam.yaw = cam.yaw;
    d.cam.pitch = cam.pitch;
    d.cam.distance = cam.distance;

    const std::vector<Entity> saved = w.view<Unit, Transform>();
    auto indexOf = [&](Entity e) {
        const auto it = std::find(saved.begin(), saved.end(), e);
        return it == saved.end() ? -1 : (int)(it - saved.begin());
    };
    for (Entity u : saved) {
        Transform* t = w.get<Transform>(u);
        Name* n = w.get<Name>(u);
        UnitSprite* us = w.get<UnitSprite>(u);
        SaveUnit su;
        if (const Unconscious* un = w.get<Unconscious>(u)) {
            su.unconscious = true;
            su.doom = un->doom;
            su.corpse = w.has<Corpse>(u);
        }
        if (const Carried* cd = w.get<Carried>(u)) {
            su.carriedBy = indexOf(cd->by);
            su.playingDead = w.has<PlayingDead>(u);
        }
        su.x = t->x;
        su.y = t->y;
        su.z = t->z;
        su.hostile = w.has<Hostile>(u);
        su.neutral = w.has<Neutral>(u);
        if (const Faction* f = w.get<Faction>(u)) {
            su.hasFaction = true;
            su.faction = f->id;
        }
        su.sprite = us ? us->id : 1;
        su.name = n ? n->text : lang("UILABEL_UNNAMED_UNIT");
        if (const Body* bd = w.get<Body>(u)) {
            su.hasBody = true;
            su.body = bd->type;
        }
        if (const Occupying* oc = w.get<Occupying>(u)) {
            if (const Prop* p = w.get<Prop>(oc->prop)) su.occupying = p->serial;
        }
        if (const Needs* nd = w.get<Needs>(u)) {
            su.hasNeeds = true;
            su.needs = *nd;
        }
        if (const Stats* st = w.get<Stats>(u)) {
            su.hasStats = true;
            su.stats = *st;
        }
        if (const Behaviors* b = w.get<Behaviors>(u)) {
            su.hasBehaviors = true;
            su.behaviors = b->rules;
        }
        if (const Inventory* inv = w.get<Inventory>(u)) {
            for (int i = 0; i < INVENTORY_SLOTS; ++i) {
                const ItemStack& st = inv->slots[i];
                if (st.item > 0 && st.count > 0) su.stacks.push_back(SaveStack{ i, st });
            }
        }
        if (const Equipment* worn = w.get<Equipment>(u)) {
            for (int g = 0; g < GEAR_SLOTS; ++g) {
                if (worn->items[g] > 0) su.gear.push_back(SaveGear{ g, worn->items[g] });
            }
        }
        d.units.push_back(su);
    }

    d.hasItems = true;
    for (Entity e : w.view<GroundItem, Transform>()) {
        const GroundItem* g = w.get<GroundItem>(e);
        const Transform* t = w.get<Transform>(e);
        d.ground.push_back(SaveGround{ t->x, t->y, t->z, g->item, g->count });
    }
    for (Entity e : w.view<Prop, Lock>()) d.locks.push_back(SaveLock{ w.get<Prop>(e)->serial, w.get<Lock>(e)->locked });
    for (Entity e : w.view<Prop, Storage>()) {
        const int serial = w.get<Prop>(e)->serial;
        const Storage* st = w.get<Storage>(e);
        for (int i = 0; i < STORAGE_SLOTS; ++i) {
            const ItemStack& s = st->slots[(size_t)i];
            if (s.item > 0 && s.count > 0) d.storage.push_back(SaveStorage{ serial, i, s.item, s.count });
        }
    }
    for (Entity e : w.view<Prop, Output>()) {
        const int serial = w.get<Prop>(e)->serial;
        const Output* out = w.get<Output>(e);
        for (int i = 0; i < STORAGE_SLOTS; ++i) {
            const ItemStack& s = out->slots[(size_t)i];
            if (s.item > 0 && s.count > 0) d.output.push_back(SaveStorage{ serial, i, s.item, s.count });
        }
        const Fuel* fuel = w.get<Fuel>(e);
        const PropWork* pw = w.get<PropWork>(e);
        d.fuel.push_back(SaveFuel{ serial, fuel ? fuel->hours : 0.0, pw ? pw->progress : 0.0 });
    }
    return d;
}

// Puts the camera on the tile under the lowest-numbered unit, so the game starts there
void focusCameraOnFirstUnit(World& w, Camera& cam) {
    Entity first = NULL_ENTITY;
    for (Entity u : w.view<Unit, Transform, Commandable>()) {
        if (first == NULL_ENTITY || u < first) first = u;
    }
    if (first == NULL_ENTITY) return;

    const Transform* t = w.get<Transform>(first);
    cam.focusX = std::floor(t->x) + 0.5f;
    cam.focusY = t->y;
    cam.focusZ = std::floor(t->z) + 0.5f;
    cam.clampState();
}

// Moves the camera's focus onto a unit, wherever it is on the map
void focusCameraOnUnit(World& w, Camera& cam, Entity u) {
    const Transform* t = w.get<Transform>(u);
    if (!t) return;
    cam.focusX = t->x;
    cam.focusY = t->y;
    cam.focusZ = t->z;
    cam.clampState();
}

// Holds the camera's focus on a unit as it moves. False once the unit is gone, so following stops.
bool followUnitCamera(World& w, Camera& cam, Entity u) {
    const Transform* t = w.get<Transform>(u);
    if (!t || !w.has<Unit>(u)) return false;
    cam.focusX = t->x;
    cam.focusZ = t->z;
    cam.clampState();
    return true;
}

// Puts the camera back where it was saved. Saves from before the camera was stored keep
// the default focus set by focusCameraOnFirstUnit.
void applySaveCamera(const SaveCamera& c, Camera& cam) {
    if (!c.present) return;
    cam.focusX = c.fx;
    cam.focusY = c.fy;
    cam.focusZ = c.fz;
    cam.yaw = c.yaw;
    cam.pitch = c.pitch;
    cam.distance = c.distance;
    cam.clampState();
}

// Current time in milliseconds since 1970
long long nowMs() {
    return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

// The name with leading and trailing spaces removed
std::string trimmedName(const std::string& s) {
    size_t a = s.find_first_not_of(' ');
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(' ');
    return s.substr(a, b - a + 1);
}

// Removes the last character. Names can hold UTF-8, so skip its continuation bytes too.
void popUtf8Char(std::string& s) {
    if (s.empty()) return;
    s.pop_back();
    while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0x80) s.pop_back();
}

// The save with exactly this name, if there is one
std::optional<SaveSummary> findSaveByName(const std::vector<SaveSummary>& saves, const std::string& name) {
    for (const SaveSummary& s : saves) {
        if (s.name == name) return s;
    }
    return std::nullopt;
}

std::string toLower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// Sorts the save list. Autosaves always come first, then manual saves. Each group is sorted
// by the chosen column; ties fall back to the other column.
void sortSaves(std::vector<SaveSummary>& saves, SortKey key, bool ascending) {
    std::stable_sort(saves.begin(), saves.end(), [&](const SaveSummary& a, const SaveSummary& b) {
        if (a.autosave != b.autosave) return a.autosave;
        if (key == SortKey::Name) {
            std::string la = toLower(a.name), lb = toLower(b.name);
            if (la != lb) return ascending ? la < lb : lb < la;
            return a.timestamp > b.timestamp;
        }
        if (a.timestamp != b.timestamp) {
            return ascending ? a.timestamp < b.timestamp : a.timestamp > b.timestamp;
        }
        return a.name < b.name;
    });
}

// What a map is, as the list's Type column shows it
std::string mapTypeLabel(const MapChoice& m) {
    if (m.source == "data" || m.source == "packed") return lang("UILABEL_BUILT_IN");
    return m.source == "world" ? lang("UILABEL_WORLD") : lang("UILABEL_MAP_FILE");
}

// Sorts the New Game list. The premade map always comes first. The rest sort by name, or by the date
// they were saved, newest first unless ascending.
void sortMaps(std::vector<MapChoice>& maps, bool byName, bool ascending) {
    std::stable_sort(maps.begin(), maps.end(), [&](const MapChoice& a, const MapChoice& b) {
        const bool aPremade = a.source == "data", bPremade = b.source == "data";
        if (aPremade != bPremade) return aPremade;
        if (byName) {
            const std::string la = toLower(a.name), lb = toLower(b.name);
            if (la != lb) return ascending ? la < lb : lb < la;
            return a.created > b.created;
        }
        if (a.created != b.created) return ascending ? a.created < b.created : a.created > b.created;
        return a.name < b.name;
    });
}

// The map a save was made on. Saves made before the premade maps were cut down to text_map name map_01.
std::optional<MapChoice> findMap(const std::vector<MapChoice>& maps, const std::string& source, std::string stem) {
    if (source == "data" && stem == "map_01") stem = "text_map";
    for (const MapChoice& m : maps) {
        if (m.source == source && m.stem == stem) return m;
    }
    return std::nullopt;
}

// How many save rows fit the window height. The rows take whatever space is left under the fixed
// parts of the popout, kept between MIN_SAVE_ROWS and MAX_SAVE_ROWS.
size_t visibleSaveRows(int H) {
    const float s = H / 720.0f;
    const float fixed = 246.0f * s;      // heading, name field, headers, "..." rows, Close, margins
    const float pitch = 32.0f * s;       // one row plus its gap
    const float space = H - 16.0f * s - fixed;
    size_t rows = space > 0.0f ? (size_t)(space / pitch) : 0;
    return std::clamp(rows, MIN_SAVE_ROWS, MAX_SAVE_ROWS);
}

// The save/load popout, in pixels. It is drawn over the screen underneath and leaves it in place.
struct PanelGeometry {
    Rect box;
    float pad;
    Rect nameBox;     // the save name field
    Rect saveBtn;     // Save button, beside the name field
    float headerY;    // column headers
    float headerH;
    Rect aboveBand;   // "..." row for saves hidden above, between the headers and the first row
    float listTop;    // first save row
    Rect belowBand;   // "..." row for saves hidden below, between the last row and Close
    float closeY;     // Close button
    float btnH;       // height of the name field, Save, and Close
    float rowH;       // height of one save row
    float rowPitch;   // distance from one save row to the next
};

PanelGeometry panelGeometry(int W, int H) {
    // Compact sizes so that LOAD_LIST_LIMIT rows fit on a 720p window
    const float s = H / 720.0f;
    const float btnH = 44.0f * s, gap = 8.0f * s, rowGap = 4.0f * s;
    const float pad = 16.0f * s, hdrH = 30.0f * s, band = 24.0f * s;
    const float rowH = 28.0f * s, pitch = rowH + rowGap;
    const float pw = 1100.0f * s, btnW = 180.0f * s;
    // The panel is centerd vertically, and never closer than 8px to the top
    const float panelH = 56.0f * s + btnH + gap + hdrH + band
                       + (float)visibleSaveRows(H) * pitch + band + btnH + pad;
    const float px = (W - pw) * 0.5f, py = std::max(8.0f * s, (H - panelH) * 0.5f);
    const float nameY = py + 56.0f * s;
    const float headerY = nameY + btnH + gap;
    const float aboveY = headerY + hdrH;
    const float listTop = aboveY + band;
    const float belowY = listTop + (float)visibleSaveRows(H) * pitch;
    const float closeY = belowY + band;
    const float bottom = closeY + btnH + pad;
    const float nameW = pw - 2.0f * pad - btnW - gap;
    const float innerX = px + pad, innerW = pw - 2.0f * pad;

    PanelGeometry g;
    g.box = Rect{ px, py, pw, bottom - py };
    g.pad = pad;
    g.nameBox = Rect{ px + pad, nameY, nameW, btnH };
    g.saveBtn = Rect{ px + pad + nameW + gap, nameY, btnW, btnH };
    g.headerY = headerY;
    g.headerH = hdrH;
    g.aboveBand = Rect{ innerX, aboveY, innerW, band };
    g.listTop = listTop;
    g.belowBand = Rect{ innerX, belowY, innerW, band };
    g.closeY = closeY;
    g.btnH = btnH;
    g.rowH = rowH;
    g.rowPitch = pitch;
    return g;
}

// The question shown in the middle of the popout
struct DialogGeometry {
    Rect box;
    Rect yes;
    Rect no;
};

DialogGeometry questionGeometry(int W, int H) {
    const float s = H / 720.0f;
    const float bh = 56.0f * s, bw = 200.0f * s;
    const float dw = 760.0f * s, dh = 190.0f * s;
    const float dx = (W - dw) * 0.5f, dy = H * 0.40f;
    const float by = dy + dh - bh - 28.0f * s;

    DialogGeometry d;
    d.box = Rect{ dx, dy, dw, dh };
    d.yes = Rect{ dx + dw * 0.5f - bw - 10.0f * s, by, bw, bh };
    d.no = Rect{ dx + dw * 0.5f + 10.0f * s, by, bw, bh };
    return d;
}

// Buttons for the popout, in pixels. Drawing and clicking both use this, so they always match.
// While a question is up, only its Yes and No buttons are returned.
// Save rows carry their index in the list as param. Their text is drawn in columns by the renderer code.
std::vector<Button> layoutPanel(int W, int H, bool saveMode, Ask ask,
                                const std::vector<SaveSummary>& saves, size_t listOffset, bool fromTitle) {
    std::vector<Button> out;
    if (ask != Ask::None) {
        DialogGeometry d = questionGeometry(W, H);
        if (ask == Ask::Delete) {
            out.push_back(Button{ lang("UILABEL_DELETE"), d.yes, Action::DeleteHold });
            out.push_back(Button{ lang("UILABEL_CANCEL"), d.no, Action::DeleteNo });
            return out;
        }
        Action yes = Action::OverwriteYes, no = Action::OverwriteNo;
        if (ask == Ask::LoadUnsaved) { yes = Action::LoadYes; no = Action::LoadNo; }
        out.push_back(Button{ lang("UILABEL_YES"), d.yes, yes });
        out.push_back(Button{ lang("UILABEL_NO"), d.no, no });
        return out;
    }

    PanelGeometry g = panelGeometry(W, H);
    const float x = g.box.x + g.pad, w = g.box.w - 2.0f * g.pad;

    if (saveMode) out.push_back(Button{ lang("UILABEL_SAVE"), g.saveBtn, Action::ConfirmName });

    // Column headers. Type is not sortable, since autosaves always come first anyway.
    out.push_back(Button{ lang("UILABEL_NAME"), Rect{ x, g.headerY, COL_TYPE * w, g.headerH }, Action::SortName });
    out.push_back(Button{ lang("UILABEL_TIME"), Rect{ x + COL_TIME * w, g.headerY, (1.0f - COL_TIME) * w, g.headerH }, Action::SortTime });

    const size_t rows = visibleSaveRows(H);
    size_t first = std::min(listOffset, saves.size());
    size_t last = std::min(first + rows, saves.size());

    // "..." rows: clickable, and shown only when saves are hidden above or below
    if (first > 0) out.push_back(Button{ "", g.aboveBand, Action::ScrollUp });
    const float uiScale = H / 720.0f;
    const float delW = 84.0f * uiScale, delGap = 8.0f * uiScale;
    float y = g.listTop;
    for (size_t i = first; i < last; ++i) {
        // Each row: the save itself, then its Delete button in the right-hand column
        out.push_back(Button{ "", Rect{ x, y, w - delW - delGap, g.rowH }, Action::LoadSlot, true, (int)i });
        out.push_back(Button{ lang("UILABEL_DELETE"), Rect{ x + w - delW, y, delW, g.rowH }, Action::DeleteSave, true, (int)i });
        y += g.rowPitch;
    }
    if (last < saves.size()) out.push_back(Button{ "", g.belowBand, Action::ScrollDown });

    // Opened from the title screen, the button goes back there. Opened from the Esc menu, it closes to that menu.
    out.push_back(Button{ fromTitle ? lang("UILABEL_RETURN_TO_TITLE") : lang("UILABEL_CLOSE"), Rect{ x, g.closeY, w, g.btnH },
                          Action::CancelName });
    return out;
}

// Buttons for the New Game list, in the same popout as the load list: the column headers, one row
// per visible map, the "..." rows at the ends, and Close.
std::vector<Button> layoutMapPanel(int W, int H, const std::vector<MapChoice>& maps, size_t listOffset) {
    std::vector<Button> out;
    PanelGeometry g = panelGeometry(W, H);
    const float x = g.box.x + g.pad, w = g.box.w - 2.0f * g.pad;

    out.push_back(Button{ lang("UILABEL_NAME"), Rect{ x, g.headerY, COL_TYPE * w, g.headerH }, Action::MapSortName });
    out.push_back(Button{ lang("UILABEL_CREATED"), Rect{ x + COL_TIME * w, g.headerY, (1.0f - COL_TIME) * w, g.headerH },
                          Action::MapSortTime });

    const size_t rows = visibleSaveRows(H);
    const size_t first = std::min(listOffset, maps.size());
    const size_t last = std::min(first + rows, maps.size());
    if (first > 0) out.push_back(Button{ "", g.aboveBand, Action::MapScrollUp });
    float y = g.listTop;
    for (size_t i = first; i < last; ++i) {
        out.push_back(Button{ "", Rect{ x, y, w, g.rowH }, Action::PickMap, true, (int)i });
        y += g.rowPitch;
    }
    if (last < maps.size()) out.push_back(Button{ "", g.belowBand, Action::MapScrollDown });

    out.push_back(Button{ lang("UILABEL_RETURN_TO_TITLE"), Rect{ x, g.closeY, w, g.btnH }, Action::ReturnToTitle });
    return out;
}

// Buttons for the main menus, in pixels
std::vector<Button> layoutButtons(Screen screen, int W, int H, const std::vector<SaveSummary>& saves) {
    const float s = H / 720.0f;
    const float bh = 56.0f * s, gap = 14.0f * s;
    const float bw = 380.0f * s;
    std::vector<Button> out;
    auto add = [&](const std::string& label, Action action, float x, float y, bool enabled = true) {
        out.push_back(Button{ label, Rect{ x, y, bw, bh }, action, enabled });
    };

    const bool hasSave = !saves.empty();

    switch (screen) {
    case Screen::Title: {
        // A column down the right of the screen, centered on its height, its right edge on the screen's margin. The
        // Language item is taller: it shows the language in use on a second line (titleItemSub).
        const TitleLayout tl = layoutTitle(W, H);
        const float iw = titleClamp(240.0f * s, W * 0.25f, 380.0f * s);
        const float ih = 44.0f * s, langH = 60.0f * s, ig = titleClamp(5.0f * s, H * 0.008f, 10.0f * s);
        const float total = 5.0f * ih + langH + 5.0f * ig;
        const float x = W - tl.side - iw;
        float y = (H - total) * 0.5f;
        auto item = [&](const std::string& label, Action action, float h, bool enabled = true) {
            out.push_back(Button{ label, Rect{ x, y, iw, h }, action, enabled });
            y += h + ig;
        };
        item(lang("UILABEL_CONTINUE"), Action::Continue, ih, hasSave);
        item(lang("UILABEL_NEW_GAME"), Action::NewGame, ih);
        item(lang("UILABEL_LOAD_GAME"), Action::LoadGame, ih, hasSave);
        item(lang("UILABEL_SETTINGS"), Action::Settings, ih);
        item(lang("UILABEL_LANGUAGE_LABEL"), Action::Language, langH);
        item(lang("UILABEL_QUIT"), Action::Quit, ih);
        break;
    }
    case Screen::Pause: {
        float x = (W - bw) * 0.5f, y = H * 0.36f;
        add(lang("UILABEL_RESUME"), Action::Resume, x, y);                     y += bh + gap;
        add(lang("UILABEL_SAVE_GAME"), Action::Save, x, y);                    y += bh + gap;
        add(lang("UILABEL_LOAD_GAME"), Action::Load, x, y, hasSave);           y += bh + gap;
        add(lang("UILABEL_SETTINGS"), Action::Settings, x, y);                 y += bh + gap;
        add(lang("UILABEL_RETURN_TO_TITLE"), Action::ReturnToTitle, x, y);
        break;
    }
    case Screen::Playing:
    case Screen::Settings:
    case Screen::MapSelect:
    case Screen::ScenarioSelect:
    case Screen::PartySetup:
        break;
    }
    return out;
}

// The tracks a screen plays, as file names in the BGM folder. A null track keeps whatever is playing on its bus,
// so the menus opened from the title (New Game, Load Game, Settings) do not restart it. "" is silence. Both
// tracks are matched to the current screen once per frame.
struct Soundtrack {
    const char* music = nullptr;     // the music bus
    const char* ambient = nullptr;   // the ambient bus
};

// The game's name is in the language files, in two words: GAME_TITLE_FIRST (shown green on the title screen) and
// GAME_TITLE_SECOND. gameTitle() is the two together, for the window's title bar.
std::string gameTitle() {
    return lang("GAME_TITLE_FIRST") + " " + lang("GAME_TITLE_SECOND");
}

// The title screen's notice board: the lines of data/motd.txt (the Message of the Day) in the pak, read once. Edit that
// file for update notes. Missing, the board is empty.
const std::vector<std::string>& messageOfTheDay() {
    static std::vector<std::string> lines;
    static bool read = false;
    if (read) return lines;
    read = true;
    const unsigned char* bytes = nullptr;
    size_t size = 0;
    if (!pakFind("data/motd.txt", bytes, size)) return lines;
    std::string line;
    for (size_t i = 0; i <= size; ++i) {
        const char c = i < size ? (char)bytes[i] : '\n';
        if (c == '\r') continue;
        if (c == '\n') {
            lines.push_back(line);
            line.clear();
        } else {
            line += c;
        }
    }
    while (!lines.empty() && lines.back().empty()) lines.pop_back();
    return lines;
}

Soundtrack soundtrackFor(Screen screen) {
    switch (screen) {
    case Screen::Title:
        return { "title.mp3", "" };
    case Screen::Settings:
    case Screen::MapSelect:
    case Screen::ScenarioSelect:
    case Screen::PartySetup:
    case Screen::Pause:
        return {};
    case Screen::Playing:
        return { "bgm_01.mp3", "amb_01.mp3" };
    }
    return {};
}

// Plays a sound effect now, on the slider it follows (sounds.h lists them all, with their files)
// A sound effect is not played again within this long of its last start: a click that lands on several things at once,
// or an order given to many units, plays it once
constexpr Uint64 SFX_REPEAT_GAP_MS = 50;
void playSfx(Sfx s) {
    static Uint64 lastStart[(int)Sfx::Count] = {};
    static bool started[(int)Sfx::Count] = {};
    const int i = (int)s;
    if (i >= 0 && i < (int)Sfx::Count) {
        const Uint64 now = SDL_GetTicks();
        if (started[i] && now - lastStart[i] < SFX_REPEAT_GAP_MS) return;
        started[i] = true;
        lastStart[i] = now;
    }
    const SfxInfo& info = sfxInfo(s);
    audio::playEffect(info.file, info.ui ? audio::Bus::Interface : audio::Bus::Effects);
}

// The size the title screen's name is drawn in before it is shrunk to fit: large enough that it is never enlarged,
// even on a 4K screen
constexpr float TITLE_NAME_POINTS = 200.0f;
// The menus' text (the title, New Game, the scenarios, the party menu, Settings and the Esc menu) is this much larger
// than the game's. The stat rows of the party menu's Stats tab stay at the game's size. The menus are laid out for a
// 720 pixel high window and grow with it, so in a smaller window the text grows less, to keep fitting its boxes:
// the full 1.5 times from 1080 pixels high, and the game's own size at 720.
constexpr float MENU_FONT_SCALE = 1.5f;
int menuFontPercent(int uiScale, int windowH) {
    const float grow = std::clamp(windowH / 720.0f, 1.0f, MENU_FONT_SCALE);
    return (int)std::lround(uiScale * grow);
}

// Working sounds: how often each plays while a prop is worked (real seconds), and how far from the camera's focus a
// worked prop is still heard, in tiles
constexpr float WORK_SOUND_SECONDS = 1.6f;
constexpr float FORGE_SOUND_SECONDS = 3.0f;
constexpr float WORK_SOUND_RANGE = 30.0f;

} // namespace

// The text fonts, opened at the Font size setting and for the selected language. Each main font has the other fonts
// as fallbacks (see fonts.h). owned holds every font opened, so they can all be closed.
struct UiFonts {
    TTF_Font* hud = nullptr;
    TTF_Font* ui = nullptr;
    TTF_Font* list = nullptr;
    TTF_Font* statsTitle = nullptr;
    TTF_Font* statsRow = nullptr;
    std::vector<TTF_Font*> owned;
    bool valid() const { return hud && ui && list && statsTitle && statsRow; }
};

void closeUiFonts(UiFonts& f);

UiFonts openUiFonts(int pct, const std::string& fontPath, const std::string& languageId) {
    // 100% is the default size: 20 points at 100%
    const float size = 20.0f * pct / 100.0f;
    UiFonts f;
    // The HUD, the UI, the lists and the title are bold. The Stats popout is regular.
    f.hud = openFontChain(size, true, fontPath, languageId, f.owned);
    f.ui = openFontChain(size, true, fontPath, languageId, f.owned);
    f.list = openFontChain(size, true, fontPath, languageId, f.owned);
    f.statsTitle = openFontChain(size, false, fontPath, languageId, f.owned);
    f.statsRow = openFontChain(size, false, fontPath, languageId, f.owned);
    return f;
}

void closeUiFonts(UiFonts& f) {
    closeFonts(f.owned);
    f.hud = f.ui = f.list = f.statsTitle = f.statsRow = nullptr;
}

// Every SDL_Log line also goes to log.txt beside the game, flushed at once, so a game that closes on its
// own leaves the reason behind. The lines still go to the console or debugger output as before.
std::FILE* gLogFile = nullptr;
SDL_LogOutputFunction gDefaultLog = nullptr;
void* gDefaultLogData = nullptr;

void SDLCALL logToFile(void*, int category, SDL_LogPriority priority, const char* message) {
    if (gLogFile) {
        std::fprintf(gLogFile, "%s\n", message);
        std::fflush(gLogFile);
    }
    if (gDefaultLog) gDefaultLog(gDefaultLogData, category, priority, message);
}

// log.txt goes next to the exe, whatever folder the game was started from
void startLogFile() {
    const char* base = SDL_GetBasePath();   // owned by SDL; works before SDL_Init
    const std::string path = std::string(base ? base : "") + "log.txt";
    gLogFile = std::fopen(path.c_str(), "w");
    if (!gLogFile) gLogFile = std::fopen("log.txt", "w");
    SDL_GetLogOutputFunction(&gDefaultLog, &gDefaultLogData);
    SDL_SetLogOutputFunction(logToFile, nullptr);
}

// Logs why the game has to close and shows it in a message box. Returns the exit code.
int fatalError(SDL_Window* window, const std::string& message) {
    SDL_Log("FATAL: %s", message.c_str());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Kobold Survival", message.c_str(), window);
    return 1;
}

// An exception nothing caught would end the game without a word: say what it was first
void onTerminate() {
    std::string what = "an unknown error";
    if (std::exception_ptr ex = std::current_exception()) {
        try { std::rethrow_exception(ex); }
        catch (const std::exception& e) { what = e.what(); }
        catch (...) {}
    }
    fatalError(nullptr, "The game stopped on an error: " + what);
    std::abort();
}

int main(int, char**) {
    startLogFile();
    std::set_terminate(onTerminate);
    SDL_Log("Kobold Game starting. Working folder: %s", std::filesystem::current_path().string().c_str());

    if (!SDL_Init(SDL_INIT_VIDEO)) return fatalError(nullptr, std::string("SDL_Init failed: ") + SDL_GetError());
    if (!TTF_Init()) return fatalError(nullptr, std::string("TTF_Init failed: ") + SDL_GetError());
    // Every asset comes from data.pak next to the executable, except the music and sound effects: those are loose
    // files in the BGM and SFX folders beside it, read by the audio module.
    if (!pakOpen("data.pak")) return fatalError(nullptr, "Could not open data.pak. It must sit next to the game executable.");
    SDL_Log("data.pak open");
    // Settings come first: the saved text size decides the fonts
    GameSettings settings = loadSettings(SETTINGS_PATH);
    UiFonts fonts = openUiFonts(settings.fontScale, settings.fontPath, settings.language);
    UiFonts menuFonts;            // opened in the loop, once the window's height is known (menuFontPercent)
    int appliedMenuPercent = 0;   // the size they were opened at; 0 to open them again
    int appliedFontScale = settings.fontScale;
    std::string appliedFontPath = settings.fontPath;
    std::string appliedLanguage = settings.language;
    {
        // A saved language whose file can't be found goes back to the default one, and the setting is saved
        std::string langErr;
        if (!loadLanguage(settings.language, langErr)) {
            SDL_Log("Language: %s. Using the default language.", langErr.c_str());
            settings.language = LANGUAGE_DEFAULT_ID;
            loadLanguage(settings.language, langErr);
            saveSettings(settings, SETTINGS_PATH);
        }
    }
    // The large title font, for the selected language, with the same fallbacks. It keeps the language it started with.
    std::vector<TTF_Font*> titleFontOwned;
    TTF_Font* font = openFontChain(64.0f, true, settings.fontPath, settings.language, titleFontOwned);
    if (!font || !fonts.valid()) return fatalError(nullptr, std::string("Could not open a font from data.pak: ") + SDL_GetError());
    SDL_Log("Settings, language and fonts loaded (language %s, UI scale %d%%)", settings.language.c_str(), settings.fontScale);

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    // 24 bits: the depth every graphics card offers for a window. Asking for 32 made Windows fall back to its
    // software renderer ("GDI Generic", OpenGL 1.1) on cards without it, and the game could not start.
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    // One window, made once. Destroying an OpenGL window and making another left the new one black on Windows.
    SDL_Window* window = SDL_CreateWindow("Kobold Survival", 1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!window) return fatalError(nullptr, std::string("Could not create the game window: ") + SDL_GetError());
    SDL_GLContext ctx = SDL_GL_CreateContext(window);
    if (!ctx || !SDL_GL_MakeCurrent(window, ctx))
        return fatalError(window, std::string("Could not start OpenGL 3.3: ") + SDL_GetError() +
                                  "\nUpdating the graphics driver may help.");
    {
        // The game needs OpenGL 3.3 on the graphics card. Windows' own software renderer only has 1.1.
        const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
        const char* card = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
        int major = 0, minor = 0;
        if (version) std::sscanf(version, "%d.%d", &major, &minor);
        const bool software = card && std::string(card).find("GDI Generic") != std::string::npos;
        if (major < 3 || (major == 3 && minor < 3) || software)
            return fatalError(window, std::string("The game needs OpenGL 3.3 on the graphics card, and got OpenGL ") +
                                      (version ? version : "?") + " on " + (card ? card : "?") +
                                      ".\nUpdating the graphics driver may help.");
    }
    {
        int gotDepth = 0;
        SDL_GL_GetAttribute(SDL_GL_DEPTH_SIZE, &gotDepth);
        SDL_Log("Depth buffer: %d bits", gotDepth);
    }

    glewExperimental = GL_TRUE;
    GLenum glewErr = glewInit();
    if (glewErr != GLEW_OK)
        return fatalError(window, std::string("GLEW init failed: ") + reinterpret_cast<const char*>(glewGetErrorString(glewErr)));
    glGetError();
    SDL_Log("OpenGL %s on %s", reinterpret_cast<const char*>(glGetString(GL_VERSION)),
            reinterpret_cast<const char*>(glGetString(GL_RENDERER)));

    moveOldMapsFolder();
    DisplayInfo displayInfo = queryDisplays(settings.monitor);
    applyDisplay(window, settings, displayInfo);
    SDL_GL_SetSwapInterval(settings.vsync ? 1 : 0);

    // ---- Loading phase: textures and models load before the title screen appears ----
    Renderer renderer;
    if (!renderer.init(font, fonts.hud, fonts.ui, [&](float p) { loadingFrame(window, renderer, p); }))
        return fatalError(window, "Loading the game's textures, models or shaders failed. log.txt beside the game has the details.");
    SDL_Log("Renderer loaded");
    renderer.setListFont(fonts.list);
    // The game's name on the title screen: a very large font, drawn shrunk so it stays sharp
    std::vector<TTF_Font*> nameFontOwned;
    renderer.setTitleFont(openFontChain(TITLE_NAME_POINTS, true, settings.fontPath, settings.language, nameFontOwned));
    renderer.setStatsFonts(fonts.statsTitle, fonts.statsRow);

    Terrain terrain;
    World world;
    Rng rng;
    GameState gs;
    Selection sel;
    Camera cam;
    unsigned levelSeed = 0;
    std::vector<MapChoice> maps;          // premade and custom maps, for New Game
    MapChoice currentMap;                 // the map the game in progress is played on
    size_t mapOffset = 0;                 // first map shown in the New Game list
    bool mapSortByName = false;           // New Game list sorted by name, else by date saved
    bool mapSortAsc = false;              // ascending order; the list opens newest first
    SkyTuning sky;                        // atmosphere settings
    int unitPanelRow = 0;                 // first visible row of the bottom unit panel
    float unitPanelWheel = 0.0f;          // wheel movement over the squad panel not yet turned into a row of scroll
    // Walk or run for the squad. Run moves faster, and each unit that moves at run pace in an in-game hour pays
    // double hunger at the next hour. Not saved: a game starts walking.
    bool runMode = false;
    bool combatCollapsed = false, squadCollapsed = false;   // the title bars' [-] buttons
    bool unitOpen = false;          // the Unit Panel: open shows its tabs, closed shows its bar alone
    int unitTab = 0;                // the tab shown: Inventory, Equipment, Profile, Stats or Behavior
    Entity lastLone = NULL_ENTITY;  // the lone unit the Unit Panel showed last frame
    // Where the Squad Panel, the Unit Panel and the Combat Log were dragged. 0 is the default size.
    PanelPlace squadPlace, unitPlace, logPlace;
    // The panel being dragged (0 squad, 1 Unit Panel, 2 combat log) and what the drag does: 1 its top edge (height),
    // 2 its right edge (width), 3 its title bar (move). The mouse, and the panel's rectangle, where the drag began.
    int grabWhich = -1, grabKind = 0;
    float grabMouseX = 0.0f, grabMouseY = 0.0f;
    float grabX = 0.0f, grabY = 0.0f, grabW = 0.0f, grabH = 0.0f;
    float mapCx = 0.0f, mapCz = 0.0f, mapSpan = 0.0f;   // the world map's view: its centre and height in tiles, 0 while closed
    float moveMarkX = 0.0f, moveMarkZ = 0.0f, moveMarkTime = 0.0f;   // the world map's green X: the last move order
    bool mapOpen = false;          // the world map covers the ground: M or the View Map button
    bool rightHold = false;        // the right button is held over a point for the selected units
    // The Optimize All dialog, from the button beside Walk/Run: open, and whether its slider is being dragged
    bool optimizeAllOpen = false;
    bool optimizeSliderDrag = false;
    float holdX = 0.0f, holdZ = 0.0f;
    // A stack being dragged out of the inventory sidebar. Released outside the sidebar, it drops on the ground.
    bool itemDrag = false;
    Entity dragUnit = NULL_ENTITY;        // the unit whose inventory it came from
    int dragSlot = -1;                    // the inventory slot it came from, or -1 when dragging gear
    int dragGear = -1;                    // the gear slot it came from, or -1 when dragging from the inventory
    // The stats popout for the lone selected unit, opened from the sidebar's Stats button
    // The right-click menu for an inventory item: Use and Drop, opened at the mouse
    struct ItemMenu {
        bool open = false;
        Entity unit = NULL_ENTITY;
        int slot = -1;
        float x = 0.0f, y = 0.0f;
    };
    ItemMenu itemMenu;
    // How far the sidebar is scrolled, in pixels, and the unit that scroll was for. A different unit starts at the top.
    float sidebarScroll = 0.0f;
    Entity sidebarScrollUnit = NULL_ENTITY;
    // The condition editor for one behavior of a unit, opened from the behavior's Condition cell at (x, y)
    struct BehaviorEditor {
        bool open = false;
        Entity unit = NULL_ENTITY;
        size_t row = 0;
        float x = 0.0f, y = 0.0f;
    };
    BehaviorEditor behaviorEditor;

    Screen screen = Screen::Title;
    // The title and pause menus: the item lit by the arrow keys or the mouse, the screen it is for, and when that screen
    // opened (its items slide in)
    int menuSel = -1;
    // Sounds: each worked prop's time to its next working sound, the selection last frame, and how many units were down
    std::map<Entity, float> workSoundTimers;
    size_t lastSelectionKey = 0;
    Screen menuScreen = Screen::Playing;
    unsigned long long menuShownAt = 0;
    float menuMouseX = -1.0f, menuMouseY = -1.0f;
    Screen backScreen = Screen::Title;    // where Back goes from Settings
    SettingsMenuState settingsUi;         // the Settings screen's tab, open dropdown and dragged slider

    // The starting party: the units a new game begins with, edited in the party menu. It is rolled the
    // first time that menu opens, then kept for the session, so edits survive a trip back to the map list.
    Party party{};
    PartyUi partyUi;
    // The scenario page: the scenario picked (the top one, Vagabond, each time the page opens) and how far its list is
    // scrolled
    ScenarioId scenarioPick = ScenarioId::Vagabond;
    int scenarioScroll = 0;
    MapChoice pendingMap;                 // the map picked, which starts once the party is confirmed
    Rng partyRng;
    partyRng.gen.seed((unsigned)std::time(nullptr));

    // Whether a game is loaded. Loading over one may need a confirmation first.
    bool gameActive = false;
    // When the current game was last saved. Empty until it's saved, so a new or loaded game counts as unsaved.
    std::optional<Uint64> lastSaveTicks;

    // Save/load popout. It sits over the current screen.
    bool panelOpen = false;
    bool panelSaveMode = false;           // true: Save Game, false: Load Game
    Screen panelFrom = Screen::Title;     // the screen the popout opened over
    Ask ask = Ask::None;                  // the question the popout is asking, if any
    std::optional<SaveSummary> pendingOverwrite;   // the save an overwrite question is about
    std::optional<SaveSummary> pendingLoad;        // the save an unsaved-progress question is about
    std::optional<SaveSummary> pendingDelete;      // the save the delete question is about
    bool deleteHolding = false;           // the Delete button is being held down
    Uint64 deleteHoldStart = 0;           // when that hold began, in SDL ticks
    Rect deleteHoldRect{ 0.0f, 0.0f, 0.0f, 0.0f };   // the Delete button being held
    std::string saveName;                 // text typed in the popout for a manual save
    size_t listOffset = 0;                // first save shown in the popout
    int viewH = 720;                      // window height, for how many save rows fit

    // Sort for the save list. The popout always opens sorted by time, newest first.
    SortKey sortKey = SortKey::Time;
    bool sortAsc = false;

    std::vector<SaveSummary> saves = listSaves();
    sortSaves(saves, sortKey, sortAsc);

    bool leftDown = false, middleDown = false;
    bool running = true;
    bool screenshotPending = false;   // set by F12, and the next frame is saved before it is shown
    float pressX = 0.0f, pressY = 0.0f;
    float lastX = 0.0f, lastY = 0.0f;
    std::string lastTitle;

    // The unit the camera follows, set by a double-click on its panel card. A manual pan clears it.
    Entity followUnit = NULL_ENTITY;

    // The combat log and the damage numbers over the units. Neither is saved, so each new or loaded level starts empty.
    CombatLog combatLog;
    std::vector<FloatingNumber> floatingNumbers;
    std::vector<TradeLabel> tradeLabels;   // short labels over units for a second: trade values, and "+item" from work
    int infoTab = 1;        // the Information Panel's tab: 0 Message, 1 Game (shown first), 2 Time
    LogFilter logFilter;    // which game message kinds the Game tab shows
    bool logFilterOpen = false;
    int logOffset = 0;      // lines the combat log is scrolled up from its newest line; 0 follows new lines
    size_t logSeen = 0;     // lines the log held last frame, so a view scrolled up stays on the same lines
    auto clearCombatText = [&]() {
        combatLog = CombatLog{};
        floatingNumbers.clear();
        tradeLabels.clear();
        discardQueuedLabels();
        logOffset = 0;
        logSeen = 0;
    };

    // Autosave countdown, in seconds of play
    float autoTimer = 0.0f;

    // Pivot ring under the camera's orbit point: fades in while the middle button is held
    float pivotAlpha = 0.0f;

    // Action crystal: appears where the last right-click order went, then fades out
    float actionAlpha = 0.0f;
    float actionX = 0.0f, actionZ = 0.0f;

    // Short message (lang("MISC_GAME_SAVED"), and so on)
    std::string notice;
    float noticeSec = 0.0f;

    Uint64 lastTicks = SDL_GetTicks();
    SDL_Event e;

    auto refreshSaves = [&]() {
        saves = listSaves();
        sortSaves(saves, sortKey, sortAsc);
    };

    auto refreshMaps = [&]() {
        maps = listMaps();
        sortMaps(maps, mapSortByName, mapSortAsc);
    };

    auto showNotice = [&](const std::string& text, float seconds = 2.5f) {
        notice = text;
        noticeSec = seconds;
    };
    // A notice that something was refused (locked, taken, no room, out of reach...), with the refusal sound
    auto refuse = [&](const std::string& text) {
        showNotice(text);
        playSfx(Sfx::UiError);
    };

    // Closes the popout and drops any pending question
    auto closePanel = [&]() {
        panelOpen = false;
        ask = Ask::None;
        pendingOverwrite.reset();
        pendingLoad.reset();
        pendingDelete.reset();
        deleteHolding = false;
        SDL_StopTextInput(window);
    };

    // Opens the popout in save mode (with an empty name) or load mode. The sort goes back to the default.
    auto openPanel = [&](bool saveMode) {
        panelOpen = true;
        panelSaveMode = saveMode;
        panelFrom = screen;
        ask = Ask::None;
        pendingOverwrite.reset();
        pendingLoad.reset();
        pendingDelete.reset();
        deleteHolding = false;
        saveName.clear();
        listOffset = 0;
        sortKey = SortKey::Time;
        sortAsc = false;
        refreshSaves();
        if (saveMode) SDL_StartTextInput(window);
        else SDL_StopTextInput(window);
    };

    // True if loading now would throw away progress: a game is running and it hasn't been saved
    // in the last UNSAVED_WARN_MS
    auto wouldLoseProgress = [&]() -> bool {
        if (!gameActive) return false;
        if (!lastSaveTicks) return true;
        return SDL_GetTicks() - *lastSaveTicks > UNSAVED_WARN_MS;
    };

    // Loads a map and starts a new game on it, showing the level-start image
    auto startMapLevel = [&](const MapChoice& choice) {
        std::string err;
        levelSeed = (unsigned)std::time(nullptr);
        // The title music ends as a new game starts, before its level loads
        audio::stopTracks();
        if (!prepareLevel(choice, world, terrain, renderer, rng, levelSeed, true, err,
                          [&](float p) { loadingFrame(window, renderer, p); })) {
            refuse(langf("MISC_LOAD_FAILED", { choice.name, err }));
            return;
        }
        // The party from the menu replaces the three units that the seed made
        ensurePlayerUnits(world, terrain, rng, (int)party.size());
        applyParty(world, party);
        clearCombatText();
        closePanel();
        currentMap = choice;
        // A new game starts with the default camera, not the zoom and angle the last game was left at. The focus is
        // clamped to the map, so the map's size has to be known before it is set.
        cam = Camera{};
        cam.mapSize = (float)terrain.size();
        focusCameraOnFirstUnit(world, cam);
        primeStreaming(world, terrain, cam);
        gs = GameState{};
        sel = Selection{};
        leftDown = middleDown = false;
        itemDrag = false;
        followUnit = NULL_ENTITY;
        autoTimer = 0.0f;
        gameActive = true;
        lastSaveTicks.reset();
        screen = Screen::Playing;
    };

    // Opens the scenario page for a picked map, with the top scenario (Vagabond) picked. The party menu comes after it.
    auto openParty = [&](const MapChoice& choice) {
        pendingMap = choice;
        scenarioPick = ScenarioId::Vagabond;
        scenarioScroll = 0;
        screen = Screen::ScenarioSelect;
    };
    // From the scenario page to the party menu, with a party rolled for the scenario. A new party is rolled every time,
    // so its names come from the language in use now. Back in the party menu returns to the scenario page.
    auto openPartyMenu = [&]() {
        party = scenarioParty(scenarioPick, partyRng);
        partyUi = PartyUi{};
        partyUi.resizable = scenarioResizable(scenarioPick);
        partyUi.editable = scenarioEditable(scenarioPick);
        backScreen = Screen::ScenarioSelect;
        screen = Screen::PartySetup;
    };

    // Stops typing into the name box, if it is taking text
    auto stopNameEdit = [&]() {
        if (partyUi.editingName) SDL_StopTextInput(window);
        partyUi.editingName = false;
        partyUi.composition.clear();
    };

    // Closes the open dropdown, if there is one
    auto closeDropdown = [&]() {
        partyUi.dropKind = DropKind::None;
        partyUi.dropScroll = 0;
    };

    // Writes the current game to a file. Used for manual saves and autosaves alike.
    auto writeSave = [&](const std::string& path, const std::string& name, bool autosave) -> bool {
        SaveData d = captureWorld(world, gs, cam, levelSeed, nowMs(), name, currentMap);
        d.autosave = autosave;
        bool ok = writeSaveFile(path, d);
        if (ok) lastSaveTicks = SDL_GetTicks();
        refreshSaves();
        return ok;
    };

    // Loads a save and starts play. The popout closes.
    auto loadSave = [&](std::string path) {
        SaveData d;
        if (!readSaveFile(path, d)) {
            refuse(lang("MISC_READ_FAILED"));
            refreshSaves();
            return;
        }
        if (d.mapStem.empty()) {
            showNotice(lang("MISC_SAVE_OLD"));
            refreshSaves();
            return;
        }
        std::optional<MapChoice> found = findMap(listMaps(), d.mapSource, d.mapStem);
        if (!found) {
            refuse(langf("MISC_MAP_MISSING", { d.name }));
            refreshSaves();
            return;
        }
        const MapChoice choice = *found;
        std::string err;
        // The title music ends as a saved game starts, before its level loads
        audio::stopTracks();
        if (!restoreLevel(world, terrain, renderer, rng, d, choice, err)) {
            refuse(langf("MISC_MAP_LOAD_FAILED", { choice.name, err }));
            refreshSaves();
            return;
        }
        currentMap = choice;
        levelSeed = d.seed;
        clearCombatText();
        gs = d.gs;
        // Older saves have no camera, so they start from the default one, not the last game's view
        cam = Camera{};
        cam.mapSize = (float)terrain.size();
        focusCameraOnFirstUnit(world, cam);
        applySaveCamera(d.cam, cam);
        primeStreaming(world, terrain, cam);
        sel = Selection{};
        leftDown = middleDown = false;
        itemDrag = false;
        followUnit = NULL_ENTITY;
        autoTimer = 0.0f;
        gameActive = true;
        lastSaveTicks.reset();
        screen = Screen::Playing;
        closePanel();
        showNotice(langf("MISC_LOADED", { d.name }));
        playSfx(Sfx::UiLoaded);
    };

    // Writes an autosave into the next slot. Slots rotate: an empty one first, otherwise the oldest.
    auto autosaveNow = [&]() -> bool {
        std::string path = nextAutosavePath();
        std::string name = std::filesystem::path(path).stem().string();   // autosave0, and so on
        return writeSave(path, name, true);
    };

    // Deletes the save the question is about, then closes the question
    auto performDelete = [&]() {
        if (!pendingDelete) {
            ask = Ask::None;
            return;
        }
        const std::string name = pendingDelete->name;
        std::error_code ec;
        std::filesystem::remove(pendingDelete->path, ec);
        ask = Ask::None;
        pendingDelete.reset();
        deleteHolding = false;
        refreshSaves();
        showNotice(ec ? langf("MISC_DELETE_FAILED", { name }) : langf("MISC_DELETED", { name }));
    };

    // Pans the camera with WASD or the arrow keys. Edge scrolling pans when the mouse is at a window edge.
    auto panWithKeys = [&](float dt, bool edgeScroll) {
        const bool* keys = SDL_GetKeyboardState(nullptr);
        float right = 0.0f, forward = 0.0f;   // edge scrolling adds to these below
        if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) right += 1.0f;
        if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT]) right -= 1.0f;
        if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP]) forward += 1.0f;
        if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]) forward -= 1.0f;

        if (edgeScroll) {
            int winW = 0, winH = 0;
            SDL_GetWindowSizeInPixels(window, &winW, &winH);
            float mx = 0.0f, my = 0.0f;
            SDL_GetMouseState(&mx, &my);
            if (mx < 6.0f) right -= 1.0f;
            if (mx > winW - 6.0f) right += 1.0f;
            if (my < 6.0f) forward += 1.0f;
            if (my > winH - 6.0f) forward -= 1.0f;
        }

        // Holding shift pans twice as fast
        const bool fast = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
        float panSpeed = cam.distance * 0.9f * dt * (fast ? 2.0f : 1.0f);
        // Any manual move takes the camera off the unit it was following
        if (right != 0.0f || forward != 0.0f) followUnit = NULL_ENTITY;
        cam.pan(right * panSpeed, forward * panSpeed);
    };

    // ---- Settings: the audio buses and the window follow the settings as they change ----
    auto applyAudio = [&]() {
        audio::setGlobal(settings.globalVolume, settings.globalOn);
        audio::setBus(audio::Bus::Music, settings.musicVolume, settings.musicOn);
        audio::setBus(audio::Bus::Ambient, settings.ambientVolume, settings.ambientOn);
        audio::setBus(audio::Bus::Effects, settings.effectsVolume, settings.effectsOn);
        audio::setBus(audio::Bus::Interface, settings.interfaceVolume, settings.interfaceOn);
    };
    // Reads the monitors again, then makes the window show the settings, which are normalized to what it can show
    auto applyVideo = [&]() {
        displayInfo = queryDisplays(settings.monitor);
        applyDisplay(window, settings, displayInfo);
        SDL_GL_SetSwapInterval(settings.vsync ? 1 : 0);
    };
    auto saveSettingsFile = [&]() { saveSettings(settings, SETTINGS_PATH); };
    // How Optimize and Optimize All choose, from the settings the Optimize All dialog sets
    auto gearPrefs = [&]() {
        GearPrefs p;
        p.attackShare = settings.optimizeAttackShare;
        p.accessories = settings.optimizeAccessories;
        p.twoHanded = settings.optimizeTwoHanded;
        p.valueTies = settings.optimizeValueTies;
        return p;
    };
    // Applies what a change touched. It is saved when the change is final: a slider saves when released.
    // The Font size setting: every text font is reopened at the new size, and the text drawn with the old ones is dropped
    auto applyFontScale = [&]() {
        if (settings.fontScale == appliedFontScale && settings.fontPath == appliedFontPath
            && settings.language == appliedLanguage) return;
        UiFonts next = openUiFonts(settings.fontScale, settings.fontPath, settings.language);
        if (!next.valid()) {
            closeUiFonts(next);
            return;
        }
        renderer.setFonts(next.hud, next.ui, next.list, next.statsTitle, next.statsRow);
        closeUiFonts(fonts);
        fonts = next;
        appliedMenuPercent = 0;   // the menu fonts follow at the top of the next frame
        // The name's font follows the language and the font picked, too
        std::vector<TTF_Font*> nextName;
        if (TTF_Font* f = openFontChain(TITLE_NAME_POINTS, true, settings.fontPath, settings.language, nextName)) {
            renderer.setTitleFont(f);
            closeFonts(nameFontOwned);
            nameFontOwned = nextName;
        }
        appliedFontScale = settings.fontScale;
        appliedFontPath = settings.fontPath;
        appliedLanguage = settings.language;
    };
    // The Language setting: its text file is reloaded. A file that can't be found goes back to the default language,
    // and that choice is saved.
    auto applyLanguage = [&]() {
        std::string err;
        if (loadLanguage(settings.language, err)) return;
        showNotice(err + ". Using the default language.");
        settings.language = LANGUAGE_DEFAULT_ID;
        loadLanguage(settings.language, err);
        saveSettingsFile();
    };
    auto settingsCommit = [&](const SettingsChange& c, bool save) {
        if (!c.any) return;
        if (c.display) applyVideo();
        else if (c.vsync) SDL_GL_SetSwapInterval(settings.vsync ? 1 : 0);
        if (c.audio) applyAudio();
        if (c.language) {
            applyLanguage();
            // The party's names came from the old language, so the party is rolled again in the new one
            party = scenarioParty(scenarioPick, partyRng, (int)party.size());
        }
        if (c.fonts || c.language) applyFontScale();
        if (save) saveSettingsFile();
    };
    auto leaveSettings = [&]() {
        settingsUi.openRow = -1;
        settingsUi.dragRow = -1;
        screen = backScreen;
    };
    // A left click on the Settings screen: a tab, a dropdown or one of its options, a toggle, a slider or a button
    // Puts the Squad Panel, the Combat Log and the Unit Panel back in their default places and sizes
    auto resetPanels = [&]() {
        squadPlace = PanelPlace{};
        logPlace = PanelPlace{};
        unitPlace = PanelPlace{};
    };
    auto settingsClick = [&](float x, float y, int W, int H) {
        const std::vector<RowSpec> rows = settingsRows(settingsUi.tab, settings, displayInfo);
        const SettingsClick c = hitSettings(layoutSettings(W, H, rows, settingsUi), rows, x, y);
        // Every control that does something clicks. Closing a dropdown, and a click on nothing, do not.
        // A tab, a switch, a dropdown and Back each have their own sound. Closing a dropdown, a slider drag (it sounds as
        // it is let go) and a click on nothing are silent.
        switch (c.kind) {
        case ClickKind::None: case ClickKind::CloseDropdown: case ClickKind::LevelDrag: break;
        case ClickKind::Tab: playSfx(Sfx::UiTab); break;
        case ClickKind::ToggleRow: case ClickKind::LevelToggle: playSfx(Sfx::UiToggle); break;
        case ClickKind::OpenDropdown: playSfx(Sfx::UiMenuOpen); break;
        case ClickKind::Back: playSfx(Sfx::UiBack); break;
        default: playSfx(Sfx::UiClick); break;
        }
        switch (c.kind) {
        case ClickKind::Tab:
            settingsSelectTab(settingsUi, static_cast<SettingsTab>(c.index));
            break;
        case ClickKind::OpenDropdown:
            settingsOpenDropdown(settingsUi, rows, c.row);
            break;
        case ClickKind::CloseDropdown:
            settingsCloseDropdown(settingsUi);
            break;
        case ClickKind::PickOption:
            settingsCloseDropdown(settingsUi);
            settingsCommit(pickOption(rows[c.row].id, rows[c.row].options[c.index], settings), true);
            break;
        case ClickKind::ToggleRow:
        case ClickKind::LevelToggle:
            settingsCommit(setRowOn(rows[c.row].id, !rows[c.row].on, settings), true);
            break;
        case ClickKind::LevelDrag:
            settingsUi.dragRow = c.row;
            settingsCommit(setRowLevel(rows[c.row].id, c.index, settings), false);
            break;
        case ClickKind::Defaults:
            settingsCloseDropdown(settingsUi);
            settingsCommit(resetAllSettings(settings), true);
            break;
        case ClickKind::Action:
            if (rows[c.row].id == RowId::ResetPanels) resetPanels();
            break;
        case ClickKind::Back:
            leaveSettings();
            break;
        case ClickKind::None:
            break;
        }
    };

    // Runs a button's action
    auto performAction = [&](Action action, int param) {
        // Going back or saying no has its own sound, and so does resuming the game
        const bool back = action == Action::Back || action == Action::ReturnToTitle || action == Action::CancelName ||
                          action == Action::OverwriteNo || action == Action::LoadNo || action == Action::DeleteNo;
        playSfx(back ? Sfx::UiBack : action == Action::Resume ? Sfx::UiResume : Sfx::UiClick);
        switch (action) {
        case Action::Continue:
            if (!saves.empty()) loadSave(saves.front().path);
            return;
        case Action::NewGame: {
            // New Game always starts on the main map, so there is no map to pick
            const std::optional<MapChoice> mainMap = findMap(listMaps(), "packed", MAIN_MAP_STEM);
            if (!mainMap) {
                showNotice("The main map is missing from data.pak");
                return;
            }
            openParty(*mainMap);
            return;
        }
        case Action::PickMap:
            if (param >= 0 && param < (int)maps.size()) openParty(maps[param]);
            return;
        case Action::MapSortName:
        case Action::MapSortTime: {
            // Clicking the active column flips the order. A new column starts ascending.
            const bool byName = action == Action::MapSortName;
            if (mapSortByName == byName) {
                mapSortAsc = !mapSortAsc;
            } else {
                mapSortByName = byName;
                mapSortAsc = true;
            }
            sortMaps(maps, mapSortByName, mapSortAsc);
            mapOffset = 0;
            return;
        }
        case Action::MapScrollUp: {
            const size_t page = visibleSaveRows(viewH);
            mapOffset = (mapOffset > page) ? mapOffset - page : 0;
            return;
        }
        case Action::MapScrollDown: {
            const size_t page = visibleSaveRows(viewH);
            const size_t maxOffset = maps.size() > page ? maps.size() - page : 0;
            mapOffset = std::min(mapOffset + page, maxOffset);
            return;
        }
        case Action::PartyPick:
            partyUi.selected = std::clamp(param, 0, (int)party.size() - 1);
            stopNameEdit();
            closeDropdown();
            return;
        case Action::PartyTabPick:
            partyUi.tab = (PartyTab)std::clamp(param, 0, PARTY_TAB_COUNT - 1);
            stopNameEdit();
            closeDropdown();
            return;
        case Action::PartyNameField:
            closeDropdown();
            if (!partyUi.editingName) SDL_StartTextInput(window);
            partyUi.editingName = true;
            return;
        case Action::PartyBodyBox:
            stopNameEdit();
            partyUi.dropKind = DropKind::Body;
            partyUi.dropScroll = 0;
            return;
        case Action::PartySpriteBox:
            // The list opens scrolled so the unit's current sprite is near the middle
            stopNameEdit();
            partyUi.dropKind = DropKind::Sprite;
            partyUi.dropScroll = clampDropScroll(pickedMember(party, partyUi).sprite - 1 - DROP_VISIBLE_ROWS / 2,
                                                 UNIT_SPRITE_COUNT);
            return;
        case Action::PartyRandomUnit: {
            // A new member for the picked unit, with a name that no other unit in the party has
            const int picked = std::clamp(partyUi.selected, 0, (int)party.size() - 1);
            std::vector<std::string> others;
            for (int i = 0; i < (int)party.size(); ++i) {
                if (i != picked) others.push_back(party[i].name);
            }
            PartyMember fresh = randomMember(partyRng, others);
            if (!partyUi.editable) {
                // Outside Custom only who the unit is changes: its stats, gear, items and hunger stay as the scenario made them
                const PartyMember& old = party[picked];
                fresh.stats = old.stats;
                fresh.gear = old.gear;
                fresh.items = old.items;
                fresh.hunger = old.hunger;
            }
            party[picked] = fresh;
            stopNameEdit();
            closeDropdown();
            return;
        }
        case Action::PartyGearSlot:
            if (!partyUi.editable) return;
            stopNameEdit();
            partyUi.dropKind = DropKind::Gear;
            partyUi.dropSlot = std::clamp(param, 0, GEAR_SLOTS - 1);
            partyUi.dropScroll = 0;
            return;
        case Action::PartyStep: {
            // Minus is the even parameter and plus the odd one. Holding Shift moves a level by ten.
            if (!partyUi.editable) return;
            const int stat = param / 2;
            const int step = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0 ? 10 : 1;
            Stats& st = pickedMember(party, partyUi).stats;
            setStatLevel(st, stat, levelOf(st, stat) + ((param % 2) != 0 ? step : -step));
            return;
        }
        case Action::PartyInvSlot:
            stopNameEdit();
            partyUi.stackSlot = std::clamp(param, 0, INVENTORY_SLOTS - 1);
            if (!partyUi.editable) return;   // the slot is picked to read about, with no list to change it
            partyUi.dropKind = DropKind::Item;
            partyUi.dropSlot = partyUi.stackSlot;
            partyUi.dropScroll = 0;
            return;
        case Action::PartyCount: {
            if (!partyUi.editable) return;
            ItemStack& st = pickedMember(party, partyUi).items[partyUi.stackSlot];
            if (st.item > 0) st.count = std::clamp(st.count + param, 1, stackLimit(st.item));
            return;
        }
        case Action::PartyDropPick: {
            const std::vector<DropEntry> entries = dropdownEntries(party, partyUi);
            if (param < 0 || param >= (int)entries.size()) return;
            PartyMember& m = pickedMember(party, partyUi);
            const int value = entries[param].value;
            switch (partyUi.dropKind) {
            case DropKind::Sprite:
                m.sprite = value;
                break;
            case DropKind::Body:
                m.body = value;
                break;
            case DropKind::Gear:
                if (partyUi.editable) m.gear[partyUi.dropSlot] = value;
                break;
            case DropKind::Item: {
                if (!partyUi.editable) break;
                ItemStack& st = m.items[partyUi.dropSlot];
                if (value == 0) {
                    st = ItemStack{};
                } else {
                    // Choosing the same item again keeps its count. Anything else starts at one.
                    const int count = st.item == value ? st.count : 1;
                    st = ItemStack{ value, std::clamp(count, 1, stackLimit(value)) };
                }
                break;
            }
            case DropKind::None:
                break;
            }
            closeDropdown();
            return;
        }
        case Action::PartyDropScroll: {
            const int count = (int)dropdownEntries(party, partyUi).size();
            partyUi.dropScroll = clampDropScroll(partyUi.dropScroll + param, count);
            return;
        }
        case Action::PartyDropBack:
        case Action::PartyHeading:
        case Action::PartyNote:
        case Action::PartyStatLabel:
        case Action::PartyMessage:
            return;
        case Action::PartyAddUnit:
            // A random unit at the end of the party. The list is scrolled to the end, and the layout clamps it.
            if (partyUi.resizable && (int)party.size() < PARTY_MAX_UNITS) {
                addRandomMember(party, partyRng);
                partyUi.selected = (int)party.size() - 1;
                partyUi.listScroll = (int)party.size();
                stopNameEdit();
                closeDropdown();
            }
            return;
        case Action::PartyRemoveUnit:
            // The picked unit leaves the party. The party can be emptied, but a game needs at least one unit to start.
            if (partyUi.resizable && !party.empty()) {
                const int picked = std::clamp(partyUi.selected, 0, (int)party.size() - 1);
                party.erase(party.begin() + picked);
                partyUi.selected = std::clamp(picked, 0, (int)party.size() - 1);
                stopNameEdit();
                closeDropdown();
            }
            return;
        case Action::PartyRandomAll:
            party = scenarioParty(scenarioPick, partyRng, (int)party.size());
            stopNameEdit();
            closeDropdown();
            return;
        case Action::PartyConfirm:
            if (party.empty() || !partyNamesValid(party)) return;
            stopNameEdit();
            closeDropdown();
            startMapLevel(pendingMap);
            return;
        case Action::LoadGame:
        case Action::Load:
            openPanel(false);
            return;
        case Action::Save:
            openPanel(true);
            return;
        case Action::LoadSlot:
            if (param < 0 || param >= (int)saves.size()) return;
            if (panelSaveMode) {
                // Save mode: a click copies that save's name into the name field
                saveName = saves[param].name;
            } else if (wouldLoseProgress()) {
                // Load mode, and the current game would be lost: ask first
                pendingLoad = saves[param];
                ask = Ask::LoadUnsaved;
            } else {
                loadSave(saves[param].path);
            }
            return;
        case Action::LoadYes: {
            if (!pendingLoad) {
                ask = Ask::None;
                return;
            }
            SaveSummary target = *pendingLoad;
            loadSave(target.path);
            return;
        }
        case Action::LoadNo:
            ask = Ask::None;
            pendingLoad.reset();
            return;
        case Action::DeleteSave:
            if (param < 0 || param >= (int)saves.size()) return;
            pendingDelete = saves[param];
            ask = Ask::Delete;
            return;
        case Action::DeleteHold:
            // Handled by the hold timer, not by clicks
            return;
        case Action::DeleteNo:
            ask = Ask::None;
            pendingDelete.reset();
            deleteHolding = false;
            return;
        case Action::ScrollUp: {
            // Jump up one page of saves
            const size_t page = visibleSaveRows(viewH);
            listOffset = (listOffset > page) ? listOffset - page : 0;
            return;
        }
        case Action::ScrollDown: {
            const size_t page = visibleSaveRows(viewH);
            size_t maxOffset = saves.size() > page ? saves.size() - page : 0;
            listOffset = std::min(listOffset + page, maxOffset);
            return;
        }
        case Action::SortName:
        case Action::SortTime: {
            // Clicking the active column flips the direction. A new column starts ascending.
            SortKey k = (action == Action::SortName) ? SortKey::Name : SortKey::Time;
            if (sortKey == k) {
                sortAsc = !sortAsc;
            } else {
                sortKey = k;
                sortAsc = true;
            }
            sortSaves(saves, sortKey, sortAsc);
            listOffset = 0;
            return;
        }
        case Action::ConfirmName: {
            // Save button. A name that matches an existing save asks before overwriting it.
            std::string name = trimmedName(saveName);
            if (name.empty()) return;
            if (std::optional<SaveSummary> existing = findSaveByName(saves, name)) {
                pendingOverwrite = existing;
                ask = Ask::Overwrite;
                return;
            }
            bool ok = writeSave(manualSavePath(nowMs()), name, false);
            closePanel();
            showNotice(ok ? lang("MISC_GAME_SAVED") : lang("MISC_SAVE_FAILED"));
            playSfx(ok ? Sfx::UiSaved : Sfx::UiError);
            return;
        }
        case Action::OverwriteYes: {
            if (!pendingOverwrite) {
                ask = Ask::None;
                return;
            }
            SaveSummary target = *pendingOverwrite;
            // Keep the save's file and its AUTO or MANUAL type; only the contents change
            bool ok = writeSave(target.path, target.name, target.autosave);
            closePanel();
            showNotice(ok ? lang("MISC_GAME_SAVED") : lang("MISC_SAVE_FAILED"));
            playSfx(ok ? Sfx::UiSaved : Sfx::UiError);
            return;
        }
        case Action::OverwriteNo:
            ask = Ask::None;
            pendingOverwrite.reset();
            return;
        case Action::CancelName:
            // Close button
            closePanel();
            return;
        case Action::Settings:
            backScreen = screen;
            screen = Screen::Settings;
            settingsUi.openRow = -1;
            settingsUi.dragRow = -1;
            rescanLanguages();
            return;
        case Action::Language:
            // The start menu's Language button: the same Settings screen, opened on its Language tab
            backScreen = screen;
            screen = Screen::Settings;
            settingsUi.tab = SettingsTab::Language;
            settingsUi.openRow = -1;
            settingsUi.dragRow = -1;
            rescanLanguages();
            return;
        case Action::Back:
            stopNameEdit();
            closeDropdown();
            screen = backScreen;
            return;
        case Action::Resume:
            screen = Screen::Playing;
            return;
        case Action::ReturnToTitle:
            screen = Screen::Title;
            gameActive = false;
            refreshSaves();
            return;
        case Action::Quit:
            running = false;
            return;
        }
    };

    // Without an audio output the game runs silent, so this result is not checked
    audio::init();
    applyAudio();

    SDL_Log("Start-up done, showing the title screen");
    while (running) {
        // Taken at the top of the frame, for the frame rate limit at its end
        const auto frameStart = std::chrono::steady_clock::now();

        // Music and ambient sound follow the screen. A screen that names a track starts it, nullptr keeps what is
        // playing, and "" is silence. Each request is tried once, so a missing track is not reloaded every frame.
        const Soundtrack sound = soundtrackFor(screen);
        if (sound.music) audio::playTrack(audio::Bus::Music, sound.music);
        if (sound.ambient) audio::playTrack(audio::Bus::Ambient, sound.ambient);

        int winW, winH;
        SDL_GetWindowSizeInPixels(window, &winW, &winH);
        viewH = winH;

        // The menu fonts, opened again when the window's height or the text settings call for another size
        if (const int pct = menuFontPercent(settings.fontScale, winH); pct != appliedMenuPercent) {
            UiFonts next = openUiFonts(pct, settings.fontPath, settings.language);
            if (next.valid()) {
                renderer.setMenuFonts(next.hud, next.ui, next.list, next.statsTitle, next.statsRow);
                closeUiFonts(menuFonts);
                menuFonts = next;
            } else {
                closeUiFonts(next);
            }
            appliedMenuPercent = pct;
        }

        // Own units by entity number, lowest first. The bottom panel shows them in this order, and the
        // camera starts on the first of them, which is also the first party member.
        auto ownUnitList = [&]() {
            std::vector<Entity> units = world.view<Unit, Transform, Commandable>();
            std::sort(units.begin(), units.end());
            return units;
        };
        // The sidebar is up for the lone selected unit, yours or a red one
        auto sidebarUp = [&]() { return sidebarUnitOf(world) != NULL_ENTITY; };
        // The sidebar's layout for a window of W by H, for the unit it shows, scrolled by the given pixels
        auto sidebarFor = [&](int W, int H, float scroll) {
            const Entity shown = sidebarUnitOf(world);
            const Behaviors* b = shown != NULL_ENTITY ? world.get<Behaviors>(shown) : nullptr;
            return layoutInventorySidebar(W, H, b ? (int)b->rules.size() : 0, scroll, unitOpen, unitTab, unitPlace,
                                          renderer.profileCardHeight(H));
        };
        // Only your own units can be changed from the sidebar. A red unit's sidebar is for looking at.
        auto shownIsYours = [&]() {
            const Entity shown = sidebarUnitOf(world);
            return shown != NULL_ENTITY && world.has<Commandable>(shown);
        };
        // Sorts a unit's inventory by item type
        auto sortInventory = [&](Entity unit) {
            Inventory* inv = world.get<Inventory>(unit);
            if (!inv) return;
            sortInventoryByType(*inv);
            playSfx(Sfx::UiClick);
        };
        // Adds a behavior with the default rule, unless the list is full
        auto addBehavior = [&](Entity unit) {
            Behaviors* b = world.get<Behaviors>(unit);
            if (!b || (int)b->rules.size() >= BEHAVIOR_LIMIT) return;
            b->rules.push_back(BehaviorRule{});
            b->pending = true;   // the new rule is checked on the next behavior pass
            playSfx(Sfx::UiClick);
        };
        // A click on one part of a behavior row. Toggles and choices change the rule at once, the priority and delete
        // buttons move or remove it, and the Condition cell opens the editor for it.
        auto behaviorClick = [&](Entity unit, const BehaviorHit& hit, float mx, float my) {
            Behaviors* b = world.get<Behaviors>(unit);
            if (!b || hit.row < 0 || hit.row >= (int)b->rules.size()) return;
            const size_t row = (size_t)hit.row;
            BehaviorRule& r = b->rules[row];
            switch (hit.part) {
            case BehaviorPart::Toggle:
                r.on = !r.on;
                break;
            case BehaviorPart::ConditionTarget:
                r.conditionTarget = (BehaviorTarget)(((int)r.conditionTarget + 1) % (int)BehaviorTarget::Count);
                break;
            case BehaviorPart::Action:
                r.action = (BehaviorAction)(((int)r.action + 1) % (int)BehaviorAction::Count);
                break;
            case BehaviorPart::ActionTarget:
                r.actionTarget = (BehaviorTarget)(((int)r.actionTarget + 1) % (int)BehaviorTarget::Count);
                break;
            case BehaviorPart::Condition:
                itemMenu.open = false;
                behaviorEditor = BehaviorEditor{ true, unit, row, mx, my };
                return;
            case BehaviorPart::Up:
                if (!moveBehavior(b->rules, row, -1)) return;
                behaviorEditor.open = false;   // the rule's row has changed, so the editor is closed
                break;
            case BehaviorPart::Down:
                if (!moveBehavior(b->rules, row, +1)) return;
                behaviorEditor.open = false;
                break;
            case BehaviorPart::Remove:
                if (!removeBehavior(b->rules, row)) return;
                behaviorEditor.open = false;
                break;
            case BehaviorPart::None:
                return;
            }
            b->pending = true;   // every rule is checked again, so the change takes effect at once
            playSfx(Sfx::UiClick);
        };
        // A click on the open condition editor: a kind, an operator, or a step of the percent
        auto editorClick = [&](const ConditionEditorLayout& ed, float mx, float my) {
            Behaviors* b = world.get<Behaviors>(behaviorEditor.unit);
            if (!b || behaviorEditor.row >= b->rules.size()) {
                behaviorEditor.open = false;
                return;
            }
            BehaviorRule& r = b->rules[behaviorEditor.row];
            const int kind = rectIndexAt(ed.kinds, mx, my);
            const int op = rectIndexAt(ed.ops, mx, my);
            const int step = rectIndexAt(ed.steps, mx, my);
            static const int stepSizes[4] = { -10, -1, 1, 10 };
            if (kind >= 0) r.condition = (BehaviorCondition)kind;
            else if (op >= 0) r.op = (BehaviorOp)op;
            else if (step >= 0) r.percent = std::clamp(r.percent + stepSizes[step], 0, 100);
            else return;
            b->pending = true;
            playSfx(Sfx::UiClick);
        };
        // Whether the mouse is over the bottom unit panel
        auto unitPanelHovered = [&](int W, int H) {
            float mx = 0.0f, my = 0.0f;
            SDL_GetMouseState(&mx, &my);
            return layoutUnitPanel(W, H, (int)ownUnitList().size(), unitPanelRow, squadCollapsed, squadPlace).panel.contains(mx, my);
        };
        // Whether a point is over the bottom unit panel or the inventory sidebar. Those belong to the
        // interface: clicks and wheel turns there never reach the world behind them.
        // The buttons of the right-click menu for the item it was opened on: Use for a consumable with an
        // effect, then Drop. Empty when the menu is closed or its item is gone.
        auto menuLabels = [&]() {
            std::vector<std::string> labels;
            const Inventory* inv = world.get<Inventory>(itemMenu.unit);
            if (!itemMenu.open || !inv || itemMenu.slot < 0 || itemMenu.slot >= INVENTORY_SLOTS) return labels;
            const ItemDef* def = findItem(inv->slots[itemMenu.slot].item);
            if (!def) return labels;
            if (def->typeId == "consumable" && !def->onUse.empty()) labels.push_back(lang("UILABEL_USE"));
            labels.push_back(lang("UILABEL_DROP"));
            return labels;
        };
        // Uses the stack in a slot of a unit, and says what happened
        auto useSlot = [&](Entity unit, int slot) {
            const Inventory* inv = world.get<Inventory>(unit);
            if (!inv || slot < 0 || slot >= INVENTORY_SLOTS) return;
            const ItemDef* def = findItem(inv->slots[slot].item);
            const std::string itemName = def ? itemDisplayName(*def) : std::string(lang("MISC_THAT_ITEM"));
            switch (useStackInSlot(world, unit, slot)) {
            case UseResult::Used:
                showNotice(langf("MISC_USED", { itemName }));
                break;
            case UseResult::NotUsable:
                refuse(itemName + " can't be used");
                break;
            case UseResult::Empty:
                break;
            }
        };
        // The combat log in the bottom left, stopping short of the unit panel. Its offset is kept in range.
        auto combatLogPanel = [&](int W, int H) {
            const float s = H / 720.0f;
            const float unitLeft = layoutUnitPanel(W, H, (int)ownUnitList().size(), unitPanelRow, squadCollapsed, squadPlace).panel.x;
            return renderer.combatLogLayout(W, H, infoTab, logFilterOpen, logOffset,
                                            (int)infoLines(combatLog, infoTab, logFilter).size(),
                                            unitLeft - 8.0f * s, combatCollapsed, logPlace);
        };
        // The talk: each line said goes into the talk's lines and the Message tab. The player's side speaks as the
        // first player unit that is selected, or else the first one there is.
        auto sayAs = [&](Entity who, const std::string& text, bool player) {
            SpokenLine ln;
            ln.text = text;
            ln.player = player;
            const bool known = who != NULL_ENTITY && world.has<Unit>(who);
            ln.speaker = known ? nameOf(world, who) : std::string(player ? lang("MISC_YOU") : lang("UILABEL_UNNAMED_UNIT"));
            const UnitSprite* us = known ? world.get<UnitSprite>(who) : nullptr;
            ln.sprite = us ? us->id : 1;
            dialogueConvo.push_back(ln);
            combatLog.say(ln.speaker + ": " + text);
        };
        // Ends the talk and the trade with it
        auto endTalk = [&]() {
            talkCooldown = TALK_COOLDOWN;
            tradeOpen = false;
            freeTrade = false;
            tradeNpc.clear();
            tradePlayer.clear();
            tradeNpcDrag = -1;
            quantity.open = false;
            dialogueUnit = NULL_ENTITY;
            dialogueConvo.clear();
        };
        // An item put in the trade from a stack: a single item goes straight in, a stack asks how many first
        auto requestTrade = [&](bool npcSide, Entity owner, int slot) {
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
            const ItemDef* def = findItem(item);
            quantity = QuantityPrompt{ true, npcSide, owner, slot, item, def ? itemDisplayName(*def) : std::string(lang("MISC_ITEM_FALLBACK")), 1, avail };
        };
        auto confirmQuantity = [&]() {
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
        };
        // A click on the how-many prompt, for a trade or for a storage: the -100 to +100 steps, Single and Max set the
        // number, Confirm moves that many, Cancel closes it
        auto quantityClick = [&](float qx, float qy) {
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
        };
        // An item from the storage unit's inventory going into the storage: a single item goes straight in, a stack
        // asks how many first, as a trade does. Max is what the unit carries of the item, or what the storage has room
        // for if that is less.
        auto requestStore = [&](int slot) {
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
            const ItemDef* def = findItem(item);
            quantity = QuantityPrompt{ true, false, storageUnit, slot, item,
                                       def ? itemDisplayName(*def) : std::string(lang("MISC_ITEM_FALLBACK")), 1, cap, true };
        };
        auto startTalk = [&](Entity npc, Entity talker, bool shop) {
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
                    tradeOpen = true;
                    freeTrade = false;
                    tradeNpc.clear();
                    tradePlayer.clear();
                    tradeNpcDrag = -1;
                    break;
                }
            }
        };
        // Picking an option: the player says its prompt, and the unit answers with its reply
        // Starts a talk, unless one ended too recently. Then no dialogue starts, but the units still walk to the NPC.
        auto tryStartTalk = [&](Entity npc, Entity talker, std::vector<Entity> units, bool shop = false) {
            if (talkCooldown <= 0.0f) {
                startTalk(npc, talker, shop);
                return;
            }
            for (Entity u : world.view<TalkOrder>()) units.push_back(u);
            std::sort(units.begin(), units.end());
            units.erase(std::unique(units.begin(), units.end()), units.end());
            const Transform* nt = world.get<Transform>(npc);
            if (nt && !units.empty()) issueMoveOrder(world, terrain, units, Vec2{ nt->x, nt->z });
        };
        // A free trade between two of your own units: the trade screen, with the other unit's items on the left
        auto startFreeTrade = [&](Entity other, Entity unit) {
            clearTalkOrders(world);
            closeStorage();
            contextMenu.open = false;
            itemMenu.open = false;
            quantity.open = false;
            dialogueUnit = other;
            dialoguePlayer = unit;
            dialogueConvo.clear();
            tradeOpen = true;
            freeTrade = true;
            tradeNpc.clear();
            tradePlayer.clear();
            tradeNpcDrag = -1;
        };
        // Tells the player when a unit given an order can't follow it because it is locked in a cage. True if one is.
        auto noteLockedIn = [&](const std::vector<Entity>& units) {
            // Unconscious ones can't do anything at all; say so when they are all there is
            bool anyAwake = false;
            for (Entity u : units) anyAwake = anyAwake || !world.has<Unconscious>(u);
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
        };
        // ---- The right-click menu ----
        // What is under the pointer: a unit (one that is not selected, if there is one), or else the nearest prop
        // that can be used
        auto menuTargetAt = [&](float mx, float my) {
            const std::vector<Entity> under = renderer.unitsAtPixel(world, cam, mx, my, winW, winH);
            // A unit locked in a cage stands for the cage: the menu offers to unlock it
            for (Entity u : under) {
                if (lockedIn(world, u)) return occupiedProp(world, u);
            }
            for (Entity u : under) {
                if (!world.has<Selected>(u)) return u;
            }
            if (!under.empty()) return under[0];
            float ex = 0.0f, ey = 0.0f, ez = 0.0f;
            cam.eye(ex, ey, ez);
            Entity prop = NULL_ENTITY;
            float best = 0.0f;
            for (Entity p : renderer.propsAtPixel(world, cam, mx, my, winW, winH)) {
                if (propInfo(world.get<Prop>(p)->kind).use == PropUse::None) continue;
                const Transform* pt = world.get<Transform>(p);
                const float d2 = (pt->x - ex) * (pt->x - ex) + (pt->y - ey) * (pt->y - ey) + (pt->z - ez) * (pt->z - ez);
                if (prop == NULL_ENTITY || d2 < best) {
                    prop = p;
                    best = d2;
                }
            }
            return prop;
        };
        // Your selected units that act on a target, nearest first. The target itself never acts on itself.
        // A unit locked in a cage acts only on that cage.
        auto menuActors = [&](Entity target) {
            std::vector<Entity> units;
            for (Entity u : selectedUnits(world)) {
                if (world.has<Unconscious>(u)) continue;   // out cold: it does nothing
                if (world.has<Carried>(u) && !world.has<PlayingDead>(u)) continue;   // carried: it does nothing
                if (u != target && (!lockedIn(world, u) || occupiedProp(world, u) == target)) units.push_back(u);
            }
            std::sort(units.begin(), units.end(), [&](Entity a, Entity b) {
                return gapBetween(world, a, target) < gapBetween(world, b, target);
            });
            return units;
        };
        // The options for a target, in the order the menu lists them
        // Whether an NPC can be talked to, and whether its talk has a trade (Show me your wares)
        auto talkable = [&](Entity u) {
            return world.has<Unit>(u) && world.has<Dialogue>(u) && !world.has<Commandable>(u) && !world.has<Hostile>(u) &&
                   !world.has<Unconscious>(u) && !world.has<Carried>(u);
        };
        auto sells = [&](Entity u) {
            const Dialogue* dg = world.get<Dialogue>(u);
            if (!dg || !talkable(u)) return false;
            for (const DialogueOption& o : dg->options) {
                if (o.opensTrade) return true;
            }
            return false;
        };
        // The selected unit that could put a unit down: the one carrying it, or the unit itself if it is the carrier.
        // NULL_ENTITY when neither is one of your selected units.
        auto putDownCarrier = [&](Entity target) {
            if (world.has<Carrying>(target) && world.has<Selected>(target) && !cannotAct(world, target)) return target;
            if (const Carried* c = world.get<Carried>(target)) {
                if (world.has<Selected>(c->by) && !cannotAct(world, c->by)) return c->by;
            }
            return NULL_ENTITY;
        };
        auto menuActionsFor = [&](Entity target) {
            std::vector<MenuAction> out;
            // A selected unit carrying someone, or the unit it carries: Put down comes first
            if (world.has<Unit>(target) && putDownCarrier(target) != NULL_ENTITY) out.push_back(MenuAction::PutDown);
            if (world.has<Unconscious>(target)) {
                // Out cold or dead: carried off (unless someone has it already) and looted
                if (canBeCarried(world, target)) out.push_back(MenuAction::PickUp);
                out.push_back(MenuAction::Inventory);
                return out;
            }
            if (world.has<Unit>(target)) {
                if (world.has<Commandable>(target)) {
                    // One of your squad: trade, follow, defend, and pick it up (unless it carries or is carried)
                    out.insert(out.end(), { MenuAction::Trade, MenuAction::Follow, MenuAction::Defend });
                    if (canBeCarried(world, target)) out.push_back(MenuAction::PickUp);
                } else if (world.has<Hostile>(target)) {
                    out.push_back(MenuAction::Attack);   // a hostile unit is only fought, never followed
                } else {
                    if (talkable(target)) out.push_back(MenuAction::Talk);
                    if (sells(target)) out.push_back(MenuAction::Shop);
                    out.insert(out.end(), { MenuAction::Follow, MenuAction::Defend, MenuAction::AttackUnprovoked });
                }
            } else if (const Prop* p = world.get<Prop>(target)) {
                const PropUse use = propInfo(p->kind).use;
                // A locked prop offers only Unlock, until it is open
                if (const Lock* lock = world.get<Lock>(target); lock && lock->locked) return std::vector<MenuAction>{ MenuAction::Unlock };
                // A bed or bedroll, with a selected unit carrying someone: put them in it
                bool carrying = false;
                for (Entity u : selectedUnits(world)) carrying = carrying || world.has<Carrying>(u);
                if (carrying && doomRecoveryPerHour(p->kind) > 0.0f) out.push_back(MenuAction::PutInBed);
                if (hasStorage(propInfo(p->kind))) out.push_back(MenuAction::Inventory);
                if (isWorkProp(use) || use == PropUse::Occupy) out.push_back(MenuAction::Use);
            }
            return out;
        };
        auto contextLabels = [&]() {
            std::vector<std::string> labels;
            for (MenuAction a : contextMenu.actions) {
                switch (a) {
                case MenuAction::Talk: labels.push_back(lang("UILABEL_TALK")); break;
                case MenuAction::Shop: labels.push_back(lang("UILABEL_SHOP")); break;
                case MenuAction::Trade: labels.push_back(lang("UILABEL_TRADE")); break;
                case MenuAction::Follow: labels.push_back(lang("UILABEL_FOLLOW")); break;
                case MenuAction::Defend: labels.push_back(lang("UILABEL_DEFEND")); break;
                case MenuAction::Attack: labels.push_back(lang("UILABEL_ATTACK")); break;
                case MenuAction::AttackUnprovoked: labels.push_back(lang("UILABEL_ATTACK")); break;   // it turns them hostile
                case MenuAction::Inventory: labels.push_back(lang("UILABEL_INVENTORY")); break;
                case MenuAction::Use: labels.push_back(lang("UILABEL_USE")); break;
                case MenuAction::Unlock: labels.push_back(lang("UILABEL_UNLOCK")); break;
                case MenuAction::PickUp: labels.push_back(lang("UILABEL_PICK_UP")); break;
                case MenuAction::PutInBed: labels.push_back(lang("UILABEL_PUT_IN_BED")); break;
                case MenuAction::PutDown: labels.push_back(lang("UILABEL_PUT_DOWN")); break;
                }
            }
            return labels;
        };
        auto contextMenuLayout = [&]() {
            return renderer.itemMenuLayout(contextMenu.x, contextMenu.y, (int)contextMenu.actions.size(), winW, winH);
        };
        // Opens the menu for what is under the pointer. False if nothing there can be acted on, so the right-click is
        // a move order instead.
        auto openContextMenuOn = [&](Entity target, float mx, float my) {
            if (target == NULL_ENTITY) return false;
            std::vector<MenuAction> actions = menuActionsFor(target);
            if (actions.empty()) return false;
            if (menuActors(target).empty()) {
                // Only Put down needs no other unit: the carrier does it
                if (actions[0] == MenuAction::PutDown) {
                    contextMenu = ContextMenu{ true, mx, my, target, { MenuAction::PutDown } };
                    playSfx(Sfx::UiMenuOpen);
                    itemMenu.open = false;
                    return true;
                }
                // Your lone selected unit clicked on itself: a move order, as on the ground under it
                if (world.has<Selected>(target) && world.has<Commandable>(target)) return false;
                if (!noteLockedIn(selectedUnits(world))) refuse(lang("MISC_SELECT_UNIT_FIRST"));
                return true;
            }
            contextMenu = ContextMenu{ true, mx, my, target, actions };
            playSfx(Sfx::UiMenuOpen);
            itemMenu.open = false;
            return true;
        };
        auto openContextMenu = [&](float mx, float my) { return openContextMenuOn(menuTargetAt(mx, my), mx, my); };
        // Does the picked option with the selected units
        auto pickMenuAction = [&](MenuAction a) {
            const Entity target = contextMenu.target;
            contextMenu.open = false;
            if (a == MenuAction::PutDown) {
                // Put down at once, where the carrier stands
                const Entity carrier = putDownCarrier(target);
                if (carrier != NULL_ENTITY) putDown(world, carrier);
                return;
            }
            const std::vector<Entity> actors = menuActors(target);
            if (actors.empty()) {
                refuse(lang("MISC_SELECT_UNIT_FIRST"));
                return;
            }
            if (!world.has<Unit>(target) && !world.has<Prop>(target)) return;
            switch (a) {
            case MenuAction::Talk: {
                // A selected unit already in talk range talks now; otherwise they walk up, and the first there talks
                Entity talker = NULL_ENTITY;
                for (Entity u : actors) {
                    if (gapBetween(world, u, target) <= TALK_RANGE) {
                        talker = u;
                        break;
                    }
                }
                if (talker != NULL_ENTITY) tryStartTalk(target, talker, actors);
                else issueTalkOrder(world, actors, target);
                break;
            }
            case MenuAction::Trade:
                // The nearest selected unit trades: now if it is close enough, or once it has walked up
                if (gapBetween(world, actors[0], target) <= TALK_RANGE) startFreeTrade(target, actors[0]);
                else issueTalkOrder(world, { actors[0] }, target, true);
                break;
            case MenuAction::Follow:
                issueFollowOrder(world, actors, target);
                break;
            case MenuAction::Defend:
                issueDefendOrder(world, actors, target);
                break;
            case MenuAction::AttackUnprovoked:
                makeHostile(world, target);
                issueAttackOrder(world, actors, target);
                break;
            case MenuAction::Attack:
                issueAttackOrder(world, actors, target);
                break;
            case MenuAction::Inventory:
                if (world.has<Unconscious>(target)) {
                    // Looting: the nearest unit that can goes, and the panel opens when it gets there (or now, if it is)
                    if (gapBetween(world, actors[0], target) <= CARRY_REACH && !cannotAct(world, actors[0])) {
                        closeStorage();
                        lootUnit = target;
                        lootBy = actors[0];
                        playSfx(Sfx::UiMenuOpen);
                    } else if (!issueLootOrder(world, terrain, actors, target)) {
                        refuse(lang("MISC_SELECT_UNIT_FIRST"));
                    }
                    break;
                }
                issuePropOrder(world, terrain, { actors[0] }, target, PropAction::Inventory);
                break;
            case MenuAction::PickUp:
                if (!issueCarryOrder(world, terrain, actors, target)) refuse(lang("MISC_ALREADY_CARRYING"));
                break;
            case MenuAction::PutInBed: {
                // The nearest selected unit that carries someone takes them there
                Entity carrier = NULL_ENTITY;
                for (Entity u : actors) {
                    if (world.has<Carrying>(u)) {
                        carrier = u;
                        break;
                    }
                }
                if (carrier != NULL_ENTITY) issuePropOrder(world, terrain, { carrier }, target, PropAction::Use);
                break;
            }
            case MenuAction::PutDown:
                break;   // done above
            case MenuAction::Shop: {
                // As Talk, then Show me your wares at once
                Entity talker = NULL_ENTITY;
                for (Entity u : actors) {
                    if (gapBetween(world, u, target) <= TALK_RANGE) {
                        talker = u;
                        break;
                    }
                }
                if (talker != NULL_ENTITY) tryStartTalk(target, talker, actors, true);
                else issueTalkOrder(world, actors, target, false, true);
                break;
            }
            case MenuAction::Unlock: {
                // The selected unit best at Lockpicking goes (the nearest, if several are as good), if it is good enough
                const Lock* lock = world.get<Lock>(target);
                if (!lock || !lock->locked) return;
                Entity best = NULL_ENTITY;
                int bestLevel = 0;
                for (Entity u : actors) {   // nearest first
                    const int lv = levelOf(statsOf(world, u), STAT_LOCKPICKING);
                    if (lv > bestLevel) {
                        best = u;
                        bestLevel = lv;
                    }
                }
                if (best == NULL_ENTITY || bestLevel < lock->level) {
                    refuse(langf("MISC_LOCKPICK_NEEDS", { std::to_string(lock->level) }));
                    return;
                }
                issuePropOrder(world, terrain, { best }, target, PropAction::Unlock);
                break;
            }
            case MenuAction::Use: {
                const PropInfo& info = propInfo(world.get<Prop>(target)->kind);
                if (info.use != PropUse::Resource) {
                    issuePropOrder(world, terrain, { actors[0] }, target, PropAction::Use);
                    break;
                }
                // A resource prop takes the nearest selected units, as many as it has room for. Units already on it
                // keep their places; the rest share what is left.
                std::vector<Entity> units = actors;
                int others = 0;
                for (Entity u : world.view<Working>()) {
                    if (world.get<Working>(u)->prop == target && std::find(units.begin(), units.end(), u) == units.end()) ++others;
                }
                for (Entity u : world.view<PropOrder>()) {
                    if (world.get<PropOrder>(u)->prop == target && std::find(units.begin(), units.end(), u) == units.end()) ++others;
                }
                const int room = std::max(0, info.workers - others);
                if (room == 0) {
                    refuse(langf("MISC_PROP_WORKERS_FULL", { std::to_string(info.workers) }));
                    return;
                }
                if ((int)units.size() > room) {
                    units.resize((size_t)room);
                    refuse(langf("MISC_PROP_WORKERS_FULL", { std::to_string(info.workers) }));
                }
                issuePropOrder(world, terrain, units, target, PropAction::Use);
                break;
            }
            }
            if (const Transform* tt = world.get<Transform>(target)) {
                actionX = tt->x;
                actionZ = tt->z;
                actionAlpha = 1.0f;
            }
            playSfx(Sfx::UiClick);
        };
        auto chooseOption = [&](size_t i) {
            const Dialogue* dg = world.get<Dialogue>(dialogueUnit);
            if (!dg || i >= dg->options.size()) return;
            const std::string prompt = dg->options[i].prompt, reply = dg->options[i].reply;
            const bool trade = dg->options[i].opensTrade;
            sayAs(dialoguePlayer, prompt, true);
            sayAs(dialogueUnit, reply, false);
            if (trade) {
                tradeOpen = true;
                freeTrade = false;
                tradeNpc.clear();
                tradePlayer.clear();
                tradeNpcDrag = -1;
            }
        };
        // The talk box for the lines so far and the options of the unit it is with
        auto dialogueBox = [&](int W, int H) {
            const Dialogue* dg = world.get<Dialogue>(dialogueUnit);
            const int lines = (int)std::min<size_t>(2, dialogueConvo.size());
            return layoutDialogue(W, H, lines, dg ? (int)dg->options.size() : 0);
        };
        // A click on the Information Panel's tabs, its Filter button or its menu. True if the click was used.
        auto infoPanelClick = [&](int W, int H, float tx, float ty) {
            const CombatLogLayout cl = combatLogPanel(W, H);
            if (cl.collapsed) return false;
            if (cl.tabMessage.contains(tx, ty)) {
                if (infoTab != 0) { infoTab = 0; logOffset = 0; }
                logFilterOpen = false;
                return true;
            }
            if (cl.tabGame.contains(tx, ty)) {
                if (infoTab != 1) { infoTab = 1; logOffset = 0; }
                logFilterOpen = false;
                return true;
            }
            if (cl.tabTime.contains(tx, ty)) {
                if (infoTab != 2) { infoTab = 2; logOffset = 0; }
                logFilterOpen = false;
                return true;
            }
            if (infoTab == 1 && cl.filterBtn.contains(tx, ty)) {
                logFilterOpen = !logFilterOpen;
                return true;
            }
            if (cl.filterOpen) {
                for (const CombatLogLayout::FilterRow& fr : cl.filterRows) {
                    if (fr.heading || !fr.r.contains(tx, ty)) continue;
                    logFilter.on[(int)fr.kind] = !logFilter.on[(int)fr.kind];
                    return true;
                }
                if (cl.filterMenu.contains(tx, ty)) return true;
                logFilterOpen = false;   // a click elsewhere closes the menu, and still does its own job
            }
            return false;
        };
        // The rectangle of each bottom panel: 0 squad, 1 Unit Panel, 2 combat log
        auto resizablePanel = [&](int which, int W, int H) -> Rect {
            if (which == 0) return layoutUnitPanel(W, H, (int)ownUnitList().size(), unitPanelRow, squadCollapsed, squadPlace).panel;
            if (which == 1) return sidebarFor(W, H, sidebarScroll).panel;
            return combatLogPanel(W, H).panel;
        };
        // What a press or the mouse over a point would grab: the panel, and the kind of grab. Kind 1 is the top edge
        // (height), 2 the right edge (width), 3 the title bar (move). A folded panel can only be moved. Panel -1 means none.
        auto grabAt = [&](int W, int H, float x, float y) -> std::pair<int, int> {
            const float barH = 22.0f * H / 720.0f;
            for (int which = 0; which < 3; ++which) {
                const bool folded = which == 0 ? squadCollapsed : which == 1 ? !unitOpen : combatCollapsed;
                const Rect r = resizablePanel(which, W, H);
                const bool inX = x >= r.x && x < r.x + r.w;
                if (inX && !folded && y >= r.y - 5.0f && y <= r.y + 3.0f) return { which, 1 };
                if (!folded && y >= r.y && y < r.y + r.h && x >= r.x + r.w - 4.0f && x <= r.x + r.w + 4.0f) return { which, 2 };
                if (inX && y >= r.y + 3.0f && y < r.y + barH) return { which, 3 };
            }
            return { -1, 0 };
        };
        // The up-and-down or left-and-right arrow cursor shows over a top or right edge
        auto setGrabCursor = [&](int kind) {
            static SDL_Cursor* upDown = nullptr;
            static SDL_Cursor* leftRight = nullptr;
            static int shown = 0;
            const int want = kind == 1 || kind == 2 ? kind : 0;
            if (want == shown) return;
            shown = want;
            if (want == 1 && !upDown) upDown = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NS_RESIZE);
            if (want == 2 && !leftRight) leftRight = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_EW_RESIZE);
            SDL_Cursor* c = want == 1 ? upDown : want == 2 ? leftRight : nullptr;
            SDL_SetCursor(c ? c : SDL_GetDefaultCursor());
        };
        // The place of a panel by its number: 0 squad, 1 Unit Panel, 2 combat log
        auto placeOf = [&](int which) -> PanelPlace& {
            return which == 0 ? squadPlace : which == 1 ? unitPlace : logPlace;
        };
        // Moves the grabbed panel to follow the mouse, from where the press began. It stays on the screen.
        auto applyGrab = [&](int W, int H, float x, float y) {
            const float s = H / 720.0f;
            PanelPlace& p = placeOf(grabWhich);
            if (grabKind == 1) {
                p.h = std::clamp(grabH + (grabMouseY - y), 0.0f, (float)H);
            } else if (grabKind == 2) {
                const float minW = grabWhich == 0 ? squadMinWidth(H) : grabWhich == 1 ? sidebarMinWidth(H) : 120.0f * s;
                p.w = std::clamp(grabW + (x - grabMouseX), minW, std::max(minW, W - grabX));
            } else if (grabKind == 3) {
                const float left = std::clamp(grabX + (x - grabMouseX), 0.0f, std::max(0.0f, W - grabW));
                const float top = std::clamp(grabY + (y - grabMouseY), 0.0f, std::max(0.0f, H - grabH));
                const float defaultLeft = grabWhich == 0 ? squadDefaultX(W, H) : grabWhich == 1 ? sidebarDefaultX(W, H) : 8.0f * s;
                p.dx = left - defaultLeft;
                p.dy = top + grabH - (H - 8.0f * s);
            }
        };
        // Lets go of the grab. A panel whose top edge was dragged under 64 pixels folds and goes back to its default size.
        auto endGrab = [&](int W, int H) {
            const int which = grabWhich, kind = grabKind;
            grabWhich = -1;
            grabKind = 0;
            if (kind != 1 || which < 0 || resizablePanel(which, W, H).h >= 64.0f) return;
            placeOf(which).w = placeOf(which).h = 0.0f;
            if (which == 0) squadCollapsed = true;
            else if (which == 1) { unitOpen = false; unitTab = 0; }
            else combatCollapsed = true;
        };
        // Whether the mouse is over the combat log
        auto combatLogHovered = [&](int W, int H) {
            float mx = 0.0f, my = 0.0f;
            SDL_GetMouseState(&mx, &my);
            return combatLogPanel(W, H).panel.contains(mx, my);
        };
        // The world map window, 800 by 600 in the middle of the screen
        auto worldMapFor = [&](int W, int H) {
            return layoutWorldMap(W, H);
        };
        // The ground point under the mouse on the world map, while it is open
        auto mapGroundAt = [&](int W, int H, float x, float y, float& gx, float& gz) {
            if (!mapOpen) return false;
            return mapGround(worldMapFor(W, H).map, mapCx, mapCz, mapSpan, (float)terrain.size(), x, y, gx, gz);
        };
        // The green X on the world map: where the last move order went, and how long it has left to show
        auto markMove = [&](float x, float z) {
            moveMarkX = x;
            moveMarkZ = z;
            moveMarkTime = MOVE_MARK_SECONDS;
        };
        // A move order to a ground point for the selected units, as a right click on the ground gives
        // The right button pressed on a point: the selected units walk straight at it while it is held, with the
        // crystal showing it. The route is planned when the button is let go.
        auto beginRightHold = [&](float x, float z) {
            const std::vector<Entity> units = selectedUnits(world);
            if (units.empty()) return;
            noteLockedIn(units);   // a unit locked in a cage stays put
            rightHold = true;
            holdX = x;
            holdZ = z;
            actionX = x;
            actionZ = z;
            actionAlpha = 1.0f;
            markMove(x, z);
            steerUnits(world, units, Vec2{ x, z });
            playSfx(Sfx::UiOrder);
        };
        auto moveSelectedTo = [&](float x, float z) {
            const std::vector<Entity> units = selectedUnits(world);
            if (units.empty()) return;
            issueMoveOrder(world, terrain, units, Vec2{ x, z });
            actionX = x;
            actionZ = z;
            actionAlpha = 1.0f;
            markMove(x, z);
        };
        auto overPanels = [&](float x, float y, int W, int H) {
            const std::vector<std::string> labels = menuLabels();
            return overGamePanels(layoutUnitPanel(W, H, (int)ownUnitList().size(), unitPanelRow, squadCollapsed, squadPlace),
                                  sidebarFor(W, H, sidebarScroll), true, x, y)
                || combatLogPanel(W, H).panel.contains(x, y)
                || (lootUnit != NULL_ENTITY && layoutLoot(W, H).panel.contains(x, y))
                || (mapOpen && worldMapFor(W, H).window.contains(x, y))
                || (behaviorEditor.open
                    && renderer.conditionEditorLayout(behaviorEditor.x, behaviorEditor.y, W, H).box.contains(x, y))
                || (!labels.empty() && renderer.itemMenuLayout(itemMenu.x, itemMenu.y, (int)labels.size(), W, H).box.contains(x, y));
        };
        // The point a held right button is over: the world map's ground where the mouse is on it, or else the ground
        // under the mouse. Over the panels, or over the world map window away from its ground, there is none.
        auto holdTarget = [&](int W, int H, float x, float y, float& gx, float& gz) {
            if (mapGroundAt(W, H, x, y, gx, gz)) return true;
            if (overPanels(x, y, W, H)) return false;
            auto hit = pickGround(cam, terrain, x, y, W, H);
            if (!hit) return false;
            gx = hit->first;
            gz = hit->second;
            return true;
        };
        auto pointerOverPanels = [&](int W, int H) {
            float mx = 0.0f, my = 0.0f;
            SDL_GetMouseState(&mx, &my);
            return overPanels(mx, my, W, H);
        };

        // The menus measure and draw their text in the larger menu fonts, the game in its own: set before the input,
        // so a click is matched against the same layout that was drawn
        renderer.setMenuText(screen != Screen::Playing);

        // ---- Input ----
        while (SDL_PollEvent(&e)) {
            // F12 takes a screenshot from any screen, whatever menu or text box has the keys
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_F12 && !e.key.repeat) {
                screenshotPending = true;
                continue;
            }
            switch (e.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;

            case SDL_EVENT_TEXT_INPUT:
                if (screen == Screen::PartySetup) {
                    // Typed text goes into the name being edited, up to the limit in characters. Any text an input
                    // method was composing is now committed, so it leaves the name box.
                    partyUi.composition.clear();
                    if (partyUi.editingName) appendUtf8Limited(pickedMember(party, partyUi).name, e.text.text, PARTY_NAME_CHARS);
                    break;
                }
                if (panelOpen && panelSaveMode && ask == Ask::None && saveName.size() < NAME_LIMIT) {
                    saveName += e.text.text;
                }
                break;

            case SDL_EVENT_TEXT_EDITING:
                // An input method is composing text (for example Chinese or Japanese): it shows in the name box
                if (screen == Screen::PartySetup && partyUi.editingName) partyUi.composition = e.edit.text;
                break;

            case SDL_EVENT_KEY_DOWN:
                if (screen == Screen::PartySetup) {
                    // Escape backs out one step at a time: the dropdown, then the name box, then the map list
                    if (e.key.key == SDLK_ESCAPE) {
                        if (partyUi.dropKind != DropKind::None) {
                            closeDropdown();
                        } else if (partyUi.editingName) {
                            stopNameEdit();
                        } else {
                            screen = backScreen;
                        }
                    } else if (partyUi.editingName) {
                        std::string& name = pickedMember(party, partyUi).name;
                        if (e.key.key == SDLK_BACKSPACE) {
                            popUtf8Char(name);
                        } else if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) {
                            stopNameEdit();
                        }
                    }
                    break;
                }
                if (panelOpen) {
                    if (e.key.key == SDLK_ESCAPE) {
                        // Escape backs out of a question first, then closes the popout
                        if (ask != Ask::None) {
                            ask = Ask::None;
                            pendingOverwrite.reset();
                            pendingLoad.reset();
                            pendingDelete.reset();
                            deleteHolding = false;
                        } else {
                            closePanel();
                        }
                    } else if (panelSaveMode && ask == Ask::None) {
                        if (e.key.key == SDLK_BACKSPACE) {
                            popUtf8Char(saveName);
                        } else if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) {
                            performAction(Action::ConfirmName, 0);
                        }
                    }
                    break;
                }
                // The title and pause menus: up and down (or W and S) light the next item, Enter (or Space on the title)
                // picks it. Items that can't be used are passed over.
                if (screen == Screen::Title || screen == Screen::Pause) {
                    const std::vector<Button> items = layoutButtons(screen, winW, winH, saves);
                    const SDL_Keycode k = e.key.key;
                    const bool up = k == SDLK_UP || k == SDLK_W, down = k == SDLK_DOWN || k == SDLK_S;
                    if ((up || down) && !items.empty()) {
                        const int n = (int)items.size();
                        int i = menuSel;
                        for (int tries = 0; tries < n; ++tries) {
                            i = ((i < 0 ? (down ? -1 : 0) : i) + (down ? 1 : -1) + n) % n;
                            if (items[(size_t)i].enabled) break;
                        }
                        if (items[(size_t)i].enabled && i != menuSel) menuSel = i;   // the hover sound plays as it lights
                        break;
                    }
                    const bool pick = k == SDLK_RETURN || k == SDLK_KP_ENTER || (k == SDLK_SPACE && screen == Screen::Title);
                    if (pick && !e.key.repeat && menuSel >= 0 && menuSel < (int)items.size() && items[(size_t)menuSel].enabled) {
                        performAction(items[(size_t)menuSel].action, items[(size_t)menuSel].param);
                        break;
                    }
                }
                if (screen == Screen::Playing && !e.key.repeat && e.key.key == SDLK_M) {
                    mapOpen = !mapOpen;   // M opens and closes the world map
                    break;
                }
                // Space pauses and resumes the game, but not while a talk or a trade is open
                if (screen == Screen::Playing && e.key.key == SDLK_SPACE && !e.key.repeat && dialogueUnit == NULL_ENTITY) {
                    paused = !paused;
                    playSfx(paused ? Sfx::UiPause : Sfx::UiResume);
                }
                if (e.key.key == SDLK_ESCAPE) {
                    // A talk only ends with its End Dialogue button, so Escape does nothing while one is open
                    if (screen == Screen::Playing && dialogueUnit != NULL_ENTITY) break;
                    if (screen == Screen::Playing && optimizeAllOpen) {
                        optimizeAllOpen = false;   // Escape is Cancel on the Optimize All dialog
                        optimizeSliderDrag = false;
                        playSfx(Sfx::UiBack);
                    } else if (screen == Screen::Playing && mapOpen) {
                        mapOpen = false;  // Escape closes the world map first
                    } else if (screen == Screen::Playing && (itemMenu.open || behaviorEditor.open)) {
                        // Escape closes the open menu, editor or popout first, and only then pauses the game
                        itemMenu.open = false;
                        behaviorEditor.open = false;
                    } else if (screen == Screen::Playing) {
                        leftDown = middleDown = false;
                        sel.dragging = false;
                        itemDrag = false;
                        itemMenu.open = false;
                        behaviorEditor.open = false;
                        screen = Screen::Pause;
                        refreshSaves();
                        playSfx(Sfx::UiPause);
                    } else if (screen == Screen::Pause) {
                        screen = Screen::Playing;
                        playSfx(Sfx::UiResume);
                    } else if (screen == Screen::Settings) {
                        // Escape closes an open dropdown first, then leaves the menu
                        if (settingsUi.openRow >= 0) settingsCloseDropdown(settingsUi);
                        else leaveSettings();
                    } else if (screen == Screen::MapSelect || screen == Screen::ScenarioSelect) {
                        performAction(Action::ReturnToTitle, 0);
                    }
                }
                break;

            case SDL_EVENT_MOUSE_WHEEL: {
                // The world map popout takes the wheel over everything under it
                const bool wheelOverMap = mapOpen && screen == Screen::Playing
                                          && worldMapFor(winW, winH).window.contains(e.wheel.mouse_x, e.wheel.mouse_y);
                if (panelOpen) {
                    // Scroll the save list, keeping the offset within range
                    int maxOffset = std::max(0, (int)saves.size() - (int)visibleSaveRows(winH));
                    int next = (int)listOffset - (int)e.wheel.y;
                    listOffset = (size_t)std::clamp(next, 0, maxOffset);
                } else if (screen == Screen::MapSelect) {
                    int maxOffset = std::max(0, (int)maps.size() - (int)visibleSaveRows(winH));
                    int next = (int)mapOffset - (int)e.wheel.y;
                    mapOffset = (size_t)std::clamp(next, 0, maxOffset);
                } else if (screen == Screen::ScenarioSelect) {
                    // The scenario list scrolls a row at a time; the layout keeps it in range
                    scenarioScroll = layoutScenario(winW, winH, (int)ScenarioId::Count,
                                                    scenarioScroll - (int)e.wheel.y).first;
                } else if (screen == Screen::PartySetup) {
                    // Scrolls an open dropdown. The rest of the menu does not scroll.
                    if (partyUi.dropKind != DropKind::None) {
                        const int count = (int)dropdownEntries(party, partyUi).size();
                        partyUi.dropScroll = clampDropScroll(partyUi.dropScroll - (int)e.wheel.y, count);
                    } else if (partyPanels(winW, winH).left.contains(e.wheel.mouse_x, e.wheel.mouse_y)) {
                        // The unit list scrolls, a unit at a time
                        const int maxScroll = partyListMaxScroll((int)party.size(), winH);
                        partyUi.listScroll = std::clamp(std::min(partyUi.listScroll, maxScroll) - (int)e.wheel.y, 0, maxScroll);
                    }
                } else if (screen == Screen::Settings) {
                    // Scrolls an open dropdown. Nothing else on the menu scrolls.
                    if (settingsUi.openRow >= 0) {
                        const std::vector<RowSpec> rows = settingsRows(settingsUi.tab, settings, displayInfo);
                        settingsScrollDropdown(settingsUi, layoutSettings(winW, winH, rows, settingsUi), (int)e.wheel.y);
                    }
                } else if (screen == Screen::Playing && !wheelOverMap && combatLogHovered(winW, winH)) {
                    // Scrolls the combat log, three lines a notch. Wheel up goes back in time.
                    const int step = e.wheel.y > 0.0f ? 3 : e.wheel.y < 0.0f ? -3 : 0;
                    logOffset = std::clamp(logOffset + step, 0, combatLogPanel(winW, winH).maxOffset());
                } else if (wheelOverMap) {
                    // Zooms the world map about the point under the mouse: wheel up zooms in, down zooms out
                    const Rect box = worldMapFor(winW, winH).map;
                    const float px = std::clamp(e.wheel.mouse_x, box.x, box.x + box.w);
                    const float py = std::clamp(e.wheel.mouse_y, box.y, box.y + box.h);
                    const float oldK = mapTilesPerPixel(box, mapSpan);
                    const float offX = box.x + box.w * 0.5f - px, offY = box.y + box.h * 0.5f - py;
                    const float gx = mapCx + offX * oldK;   // the ground under the mouse
                    const float gz = mapCz + offY * oldK;
                    mapSpan = e.wheel.y > 0.0f ? mapSpan * 0.8f : mapSpan * 1.25f;
                    float cx = mapCx, cz = mapCz;
                    clampMapView(box, (float)terrain.size(), cx, cz, mapSpan);
                    const float k = mapTilesPerPixel(box, mapSpan);
                    mapCx = gx - offX * k;
                    mapCz = gz - offY * k;
                    clampMapView(box, (float)terrain.size(), mapCx, mapCz, mapSpan);
                } else if (screen == Screen::Playing && unitPanelHovered(winW, winH)) {
                    // Scrolls the unit list very slowly: a row for every SQUAD_WHEEL_NOTCHES_PER_ROW notches of the
                    // wheel. Turning the wheel the other way starts the count again.
                    if (unitPanelWheel * e.wheel.y < 0.0f) unitPanelWheel = 0.0f;
                    unitPanelWheel += e.wheel.y;
                    const int rows = (int)(unitPanelWheel / SQUAD_WHEEL_NOTCHES_PER_ROW);
                    if (rows != 0) {
                        unitPanelWheel -= rows * SQUAD_WHEEL_NOTCHES_PER_ROW;
                        unitPanelRow = layoutUnitPanel(winW, winH, (int)ownUnitList().size(),
                                                       unitPanelRow - rows, squadCollapsed, squadPlace).scrollRow;
                    }
                } else if (screen == Screen::Playing
                           && sidebarFor(winW, winH, sidebarScroll).panel.contains(e.wheel.mouse_x, e.wheel.mouse_y)) {
                    // Scrolls the sidebar, 40 pixels a notch. Wheel up goes back toward the top.
                    const float notch = 40.0f * (winH / 720.0f);
                    sidebarScroll = sidebarFor(winW, winH, sidebarScroll - e.wheel.y * notch).scroll;
                } else if (screen == Screen::Playing && !pointerOverPanels(winW, winH)) {
                    // Over the panels the wheel does nothing, so the camera doesn't zoom behind them
                    cam.distance *= (e.wheel.y > 0.0f) ? 0.9f : 1.1f;
                    cam.clampState();
                }
                break;
            }

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (panelOpen) {
                    // The popout takes all clicks while it's open
                    if (e.button.button == SDL_BUTTON_LEFT) {
                        for (const Button& b : layoutPanel(winW, winH, panelSaveMode, ask, saves, listOffset,
                                                           panelFrom == Screen::Title)) {
                            if (b.enabled && b.rect.contains(e.button.x, e.button.y)) {
                                if (b.action == Action::DeleteHold) {
                                    // Held, not clicked: the timer deletes once it has been held long enough
                                    deleteHolding = true;
                                    deleteHoldStart = SDL_GetTicks();
                                    deleteHoldRect = b.rect;
                                } else {
                                    performAction(b.action, b.param);
                                }
                                break;
                            }
                        }
                    }
                    break;
                }

                if (screen == Screen::ScenarioSelect) {
                    // A row picks its scenario; Continue rolls the party for it; Back returns to the title
                    if (e.button.button == SDL_BUTTON_LEFT) {
                        const float mx = e.button.x, my = e.button.y;
                        const ScenarioLayout sl = layoutScenario(winW, winH, (int)ScenarioId::Count, scenarioScroll);
                        scenarioScroll = sl.first;
                        for (size_t i = 0; i < sl.rows.size(); ++i) {
                            if (sl.rows[i].contains(mx, my)) {
                                scenarioPick = (ScenarioId)(sl.first + (int)i);
                                playSfx(Sfx::UiClick);
                            }
                        }
                        if (sl.next.contains(mx, my)) {
                            playSfx(Sfx::UiClick);
                            openPartyMenu();
                        } else if (sl.back.contains(mx, my)) {
                            performAction(Action::ReturnToTitle, 0);
                        }
                    }
                    break;
                }

                if (screen == Screen::PartySetup) {
                    // The unit list's scroll bar: the thumb is taken where it was clicked; a click on the track moves
                    // the thumb's middle there, and it can be dragged on from that point
                    const PartyListBar bar = partyListBar(winW, winH, (int)party.size(), partyUi.listScroll);
                    const Rect barGrab{ bar.track.x - 4.0f, bar.track.y, bar.track.w + 8.0f, bar.track.h };
                    if (e.button.button == SDL_BUTTON_LEFT && partyUi.dropKind == DropKind::None && bar.track.h > 0.0f
                        && barGrab.contains(e.button.x, e.button.y)) {
                        partyUi.listGrab = bar.thumb.contains(bar.thumb.x, e.button.y) ? e.button.y - bar.thumb.y : bar.thumb.h * 0.5f;
                        partyUi.listScroll = partyListScrollAt(winW, winH, (int)party.size(), e.button.y - partyUi.listGrab);
                        partyUi.listDrag = true;
                        break;
                    }
                    if (e.button.button == SDL_BUTTON_LEFT) {
                        const float mx = e.button.x, my = e.button.y;
                        const std::vector<DropEntry> drop = dropdownEntries(party, partyUi);
                        const std::vector<Button> buttons = layoutPartySetup(winW, winH, party, partyUi, drop);
                        // The last enabled button under the pointer is the one on top, so it gets the click
                        const Button* hit = nullptr;
                        for (auto it = buttons.rbegin(); it != buttons.rend(); ++it) {
                            if (it->enabled && it->rect.contains(mx, my)) {
                                hit = &*it;
                                break;
                            }
                        }
                        const bool onDropdown = hit && (hit->action == Action::PartyDropPick
                                                        || hit->action == Action::PartyDropScroll
                                                        || hit->action == Action::PartyDropBack);
                        if (partyUi.dropKind != DropKind::None && !onDropdown) {
                            // A click outside an open dropdown only closes it
                            closeDropdown();
                        } else if (hit) {
                            if (partyUi.editingName && hit->action != Action::PartyNameField) stopNameEdit();
                            performAction(hit->action, hit->param);
                        } else {
                            stopNameEdit();
                        }
                    }
                    break;
                }

                if (screen == Screen::Settings) {
                    if (e.button.button == SDL_BUTTON_LEFT) settingsClick(e.button.x, e.button.y, winW, winH);
                    break;
                }

                if (screen != Screen::Playing) {
                    // Menus: a left click runs the enabled button under the cursor
                    if (e.button.button == SDL_BUTTON_LEFT) {
                        const std::vector<Button> menuButtons = screen == Screen::MapSelect
                            ? layoutMapPanel(winW, winH, maps, mapOffset)
                            : layoutButtons(screen, winW, winH, saves);
                        for (const Button& b : menuButtons) {
                            if (b.enabled && b.rect.contains(e.button.x, e.button.y)) {
                                performAction(b.action, b.param);
                                break;
                            }
                        }
                    }
                    break;
                }

                // The Optimize All dialog is modal: only it answers while it is open
                if (screen == Screen::Playing && optimizeAllOpen) {
                    if (e.button.button == SDL_BUTTON_LEFT) {
                        const OptimizeAllLayout ol = layoutOptimizeAll(winW, winH);
                        const float x = e.button.x, y = e.button.y;
                        const Rect grab{ ol.track.x - 8.0f, ol.track.y - 10.0f, ol.track.w + 16.0f, ol.track.h + 20.0f };
                        if (grab.contains(x, y)) {
                            settings.optimizeAttackShare = levelAtX(ol.track, x);
                            optimizeSliderDrag = true;
                        } else if (ol.toggles[0].contains(x, y)) {
                            settings.optimizeAccessories = !settings.optimizeAccessories;
                            saveSettingsFile();
                            playSfx(Sfx::UiToggle);
                        } else if (ol.toggles[1].contains(x, y)) {
                            settings.optimizeTwoHanded = !settings.optimizeTwoHanded;
                            saveSettingsFile();
                            playSfx(Sfx::UiToggle);
                        } else if (ol.toggles[2].contains(x, y)) {
                            settings.optimizeValueTies = !settings.optimizeValueTies;
                            saveSettingsFile();
                            playSfx(Sfx::UiToggle);
                        } else if (ol.cancel.contains(x, y)) {
                            optimizeAllOpen = false;
                            playSfx(Sfx::UiBack);
                        } else if (ol.confirm.contains(x, y)) {
                            // Each unit of the squad, on its own: one without the room to change stays as it was
                            const std::vector<Entity> squad = ownUnitList();
                            const OptimizeAllResult r = optimizeAllGear(world, squad, gearPrefs());
                            optimizeAllOpen = false;
                            if (r.changed == 0 && r.noRoom == 0) {
                                showNotice(lang("MISC_OPTIMIZE_ALL_SAME"));
                            } else if (r.noRoom == 0) {
                                showNotice(langf("MISC_OPTIMIZE_ALL_DONE", { std::to_string(r.changed) }));
                            } else {
                                showNotice(langf("MISC_OPTIMIZE_ALL_SOME_ROOM", { std::to_string(r.changed), std::to_string(r.noRoom) }));
                            }
                            if (r.changed == 0) playSfx(r.noRoom > 0 ? Sfx::UiError : Sfx::UiClick);   // a change plays its own sound
                        }
                    }
                    break;
                }
                // The world map window is over everything, and a click inside it is its own: the X closes it, and a right
                // click on the map is a move order to that point. Outside the window, clicks go on as if it were shut.
                if (screen == Screen::Playing && mapOpen && worldMapFor(winW, winH).window.contains(e.button.x, e.button.y)) {
                    const WorldMapLayout wm = worldMapFor(winW, winH);
                    float mgx = 0.0f, mgz = 0.0f;
                    if (e.button.button == SDL_BUTTON_LEFT && wm.close.contains(e.button.x, e.button.y)) {
                        mapOpen = false;
                        playSfx(Sfx::UiClick);
                    } else if (e.button.button == SDL_BUTTON_RIGHT && mapGroundAt(winW, winH, e.button.x, e.button.y, mgx, mgz)) {
                        contextMenu.open = false;
                        itemMenu.open = false;
                        beginRightHold(mgx, mgz);
                    }
                    break;
                }

                // The talk owns the mouse while it is open. During a trade the Unit Panel still works, so a click there
                // passes on to the panel; everything else is the talk's or the trade's.
                if (dialogueUnit != NULL_ENTITY && screen == Screen::Playing) {
                    // The quantity prompt is modal over the trade: only its buttons answer
                    if (quantity.open && tradeOpen && !quantity.toStorage) {
                        if (e.button.button == SDL_BUTTON_LEFT) quantityClick(e.button.x, e.button.y);
                        break;
                    }
                    const float x = e.button.x, y = e.button.y;
                    const bool unitPanelClick = tradeOpen && sidebarFor(winW, winH, sidebarScroll).panel.contains(x, y);
                    if (!unitPanelClick) {
                        if (tradeOpen) {
                            const TradeLayout tl = layoutTrade(winW, winH);
                            const Inventory* npcInv = world.get<Inventory>(dialogueUnit);
                            const bool leftButton = e.button.button == SDL_BUTTON_LEFT;
                            if (leftButton && tl.cancelBtn.contains(x, y) && freeTrade) {
                                endTalk();   // a free trade has no talk to go back to
                            } else if (leftButton && tl.cancelBtn.contains(x, y)) {
                                tradeOpen = false;
                                tradeNpc.clear();
                                tradePlayer.clear();
                                tradeNpcDrag = -1;
                            } else if (leftButton && tl.tradeBtn.contains(x, y)) {
                                if (!tradeItemsPresent(world, dialogueUnit, dialoguePlayer)) {
                                    // Something offered is no longer in its inventory: the trade fails and the talk ends
                                    tradeLabels.push_back(TradeLabel{ dialogueUnit, lang("MISC_TRADE_FAILED"), 0.0f });
                                    tradeLabels.push_back(TradeLabel{ dialoguePlayer, lang("MISC_TRADE_FAILED"), 0.0f });
                                    playSfx(Sfx::TradeFailed);
                                    endTalk();
                                } else if (checkTrade(world, dialogueUnit, dialoguePlayer).ok) {
                                    // Each side shows its own gain or loss: what it receives minus what it gives
                                    const int theirGive = tradeValueOf(tradeNpc);
                                    const int yourGive = tradeValueOf(tradePlayer);
                                    auto signedValue = [](int v) { return (v > 0 ? "+" : "") + std::to_string(v); };
                                    if (!freeTrade) {   // a free trade between your own units has no value to show
                                        tradeLabels.push_back(TradeLabel{ dialogueUnit, langf("MISC_TRADE_CHANGE", { signedValue(yourGive - theirGive) }), 0.0f });
                                        tradeLabels.push_back(TradeLabel{ dialoguePlayer, langf("MISC_TRADE_CHANGE", { signedValue(theirGive - yourGive) }), 0.0f });
                                    }
                                    completeTrade(world, dialogueUnit, dialoguePlayer);
                                    playSfx(Sfx::TradeDone);
                                    endTalk();
                                    showNotice(lang("MISC_TRADE_COMPLETE"), TRADE_LABEL_LIFE);
                                }
                            } else if (takeTradeGhostAt(tradeNpc, tl.theirCells, x, y)
                                       || takeTradeGhostAt(tradePlayer, tl.yourCells, x, y)) {
                                // A ghost was taken back out of its cell
                            } else {
                                // An NPC item: left button drags it to the left box, right button puts it there
                                for (size_t i = 0; npcInv && i < tl.npcSlots.size(); ++i) {
                                    if (!tl.npcSlots[i].contains(x, y) || npcInv->slots[i].item == 0) continue;
                                    if (leftButton) {
                                        tradeNpcDrag = (int)i;
                                    } else if (e.button.button == SDL_BUTTON_RIGHT) {
                                        requestTrade(true, dialogueUnit, (int)i);
                                    }
                                    break;
                                }
                            }
                        } else if (e.button.button == SDL_BUTTON_LEFT) {
                            const DialogueLayout dl = dialogueBox(winW, winH);
                            if (dl.end.contains(x, y)) {
                                talkCooldown = TALK_COOLDOWN;
                                dialogueUnit = NULL_ENTITY;
                                dialogueConvo.clear();
                            } else {
                                for (size_t i = 0; i < dl.options.size(); ++i) {
                                    if (dl.options[i].contains(x, y)) {
                                        chooseOption(i);
                                        break;
                                    }
                                }
                            }
                        }
                        break;
                    }
                }
                // The right-click menu: a left click on an option picks it. A click anywhere else closes the menu, and
                // that click still does what it would have done (a right-click elsewhere opens a new menu or moves).
                if (screen == Screen::Playing && contextMenu.open) {
                    const ItemMenuLayout ml = contextMenuLayout();
                    if (ml.box.contains(e.button.x, e.button.y)) {
                        const int hit = itemMenuButtonAt(ml, e.button.x, e.button.y);
                        if (e.button.button == SDL_BUTTON_LEFT && hit >= 0 && hit < (int)contextMenu.actions.size()) {
                            pickMenuAction(contextMenu.actions[(size_t)hit]);
                        }
                        break;
                    }
                    contextMenu.open = false;
                }
                // Choosing how many of a stack to store: the prompt is modal, and only its buttons answer
                if (screen == Screen::Playing && quantity.open && quantity.toStorage) {
                    if (e.button.button == SDL_BUTTON_LEFT) quantityClick(e.button.x, e.button.y);
                    break;
                }
                // The looting panel takes every click on it: a click on an item takes it (as much as fits), Take All takes
                // everything that fits, Close closes it
                if (screen == Screen::Playing && lootUnit != NULL_ENTITY) {
                    const LootLayout ll = layoutLoot(winW, winH);
                    const float x = e.button.x, y = e.button.y;
                    if (ll.panel.contains(x, y)) {
                        if (e.button.button == SDL_BUTTON_LEFT && ll.closeBtn.contains(x, y)) {
                            closeLoot();
                            playSfx(Sfx::UiBack);
                        } else if (e.button.button == SDL_BUTTON_LEFT && ll.takeAllBtn.contains(x, y)) {
                            if (lootAll(world, lootBy, lootUnit) == 0) refuse(lang("MISC_LOOT_NOTHING"));
                        } else {
                            std::string why;
                            bool hitCell = false;
                            for (int g = 0; g < GEAR_SLOTS && !hitCell; ++g) {
                                if (!ll.gear[(size_t)g].contains(x, y)) continue;
                                hitCell = true;
                                if (!lootTake(world, lootBy, lootUnit, true, g, why) && !why.empty()) refuse(why);
                            }
                            for (int i = 0; i < INVENTORY_SLOTS && !hitCell; ++i) {
                                if (!ll.slots[(size_t)i].contains(x, y)) continue;
                                hitCell = true;
                                if (!lootTake(world, lootBy, lootUnit, false, i, why) && !why.empty()) refuse(why);
                            }
                        }
                        break;
                    }
                }
                // The storage panel takes every click on it. Left button on an item starts dragging it out; right button
                // moves its whole stack into the unit's inventory, if all of it fits.
                if (screen == Screen::Playing && storageProp != NULL_ENTITY) {
                    const StorageLayout stl = storageLayout(world, winW, winH);
                    const float x = e.button.x, y = e.button.y;
                    if (stl.panel.contains(x, y)) {
                        if (e.button.button == SDL_BUTTON_LEFT && stl.closeBtn.contains(x, y)) {
                            closeStorage();
                            playSfx(Sfx::UiBack);
                        } else {
                            // The input (or only) grid's cells are panel slots 0 and up, a forge's output cells follow
                            std::vector<Rect> cells = stl.slots;
                            cells.insert(cells.end(), stl.outSlots.begin(), stl.outSlots.end());
                            for (size_t i = 0; i < cells.size(); ++i) {
                                const ItemStack* at = storageSlot(world, (int)i);
                                if (!cells[i].contains(x, y) || !at || at->item == 0) continue;
                                if (e.button.button == SDL_BUTTON_LEFT) {
                                    storageDrag = (int)i;
                                } else if (e.button.button == SDL_BUTTON_RIGHT) {
                                    std::string why;
                                    if (takeFromStorage(world, (int)i, why)) queueSound(Sfx::ItemMove);
                                    else if (!why.empty()) refuse(why);
                                }
                                break;
                            }
                        }
                        break;
                    }
                }
                // The [-] and [+] buttons on the title bars fold their panels up or down
                if (screen == Screen::Playing && e.button.button == SDL_BUTTON_LEFT) {
                    const float tx = e.button.x, ty = e.button.y;
                    const SidebarLayout sbl = sidebarFor(winW, winH, sidebarScroll);
                    if (sbl.plus.contains(tx, ty)) {
                        unitOpen = true;
                        unitPlace.w = unitPlace.h = 0.0f;
                        break;
                    }
                    if (sbl.minus.contains(tx, ty)) {
                        unitOpen = false;
                        unitTab = 0;
                        unitPlace.w = unitPlace.h = 0.0f;
                        break;
                    }
                    if (infoPanelClick(winW, winH, tx, ty)) break;
                    if (layoutUnitPanel(winW, winH, (int)ownUnitList().size(), unitPanelRow, squadCollapsed, squadPlace).toggle.contains(tx, ty)) {
                        squadCollapsed = !squadCollapsed;
                        squadPlace.w = squadPlace.h = 0.0f;
                        break;
                    }
                    if (combatLogPanel(winW, winH).toggle.contains(tx, ty)) {
                        combatCollapsed = !combatCollapsed;
                        logPlace.w = logPlace.h = 0.0f;
                        break;
                    }
                }
                // The top edge, right edge or title bar of a bottom panel grabs it, to resize or move it
                if (screen == Screen::Playing && e.button.button == SDL_BUTTON_LEFT) {
                    const auto g = grabAt(winW, winH, e.button.x, e.button.y);
                    if (g.first >= 0) {
                        const Rect r = resizablePanel(g.first, winW, winH);
                        grabWhich = g.first;
                        grabKind = g.second;
                        grabMouseX = e.button.x;
                        grabMouseY = e.button.y;
                        grabX = r.x;
                        grabY = r.y;
                        grabW = r.w;
                        grabH = r.h;
                        break;
                    }
                }
                if (e.button.button == SDL_BUTTON_LEFT && screen == Screen::Playing) {
                    // The unit panel and sidebar take the click, so it never reaches the ground
                    const float mx = e.button.x, my = e.button.y;
                    const std::vector<Entity> own = ownUnitList();
                    const UnitPanelLayout pl = layoutUnitPanel(winW, winH, (int)own.size(), unitPanelRow, squadCollapsed, squadPlace);

                    // The right-click menu: a click on one of its buttons does that action. A click
                    // anywhere else just closes it, and the click still does what it would have done.
                    if (itemMenu.open) {
                        const std::vector<std::string> labels = menuLabels();
                        if (labels.empty()) {
                            itemMenu.open = false;
                        } else {
                            const ItemMenuLayout ml = renderer.itemMenuLayout(itemMenu.x, itemMenu.y, (int)labels.size(), winW, winH);
                            const int hit = itemMenuButtonAt(ml, mx, my);
                            const bool onMenu = ml.box.contains(mx, my);
                            const Entity unit = itemMenu.unit;
                            const int slot = itemMenu.slot;
                            itemMenu.open = false;
                            if (hit >= 0 && labels[hit] == lang("UILABEL_USE")) {
                                useSlot(unit, slot);
                                playSfx(Sfx::UiClick);
                            } else if (hit >= 0 && labels[hit] == lang("UILABEL_DROP")) {
                                // A drop from the inventory plays its own sound, in place of the menu click
                                if (!dropStack(world, unit, slot)) playSfx(Sfx::UiError);
                            }
                            if (onMenu) break;
                        }
                    }

                    // The condition editor takes the clicks on it. A click anywhere else closes it, and that click
                    // still does whatever it would have done.
                    if (behaviorEditor.open) {
                        const ConditionEditorLayout ed =
                            renderer.conditionEditorLayout(behaviorEditor.x, behaviorEditor.y, winW, winH);
                        if (ed.box.contains(mx, my)) {
                            editorClick(ed, mx, my);
                            break;
                        }
                        behaviorEditor.open = false;
                    }

                    // The Unit Panel's tabs pick what it shows
                    {
                        const SidebarLayout st = sidebarFor(winW, winH, sidebarScroll);
                        bool tabHit = false;
                        for (int t = 0; t < UNIT_TAB_COUNT && st.open; ++t) {
                            if (st.tabs[t].contains(mx, my)) {
                                if (!unitTabEnabled(world, sidebarUnitOf(world), t)) {
                                    tabHit = true;   // off for a unit not in your squad: the click does nothing
                                    break;
                                }
                                if (unitTab != t) playSfx(Sfx::UiTab);
                                unitTab = t;
                                sidebarScroll = 0.0f;
                                tabHit = true;
                                break;
                            }
                        }
                        if (tabHit) break;
                    }

                    if (overPanels(mx, my, winW, winH)) {
                        // The pace toggle and the View Map button on the bottom panel
                        if (pl.paceButton.contains(mx, my)) { runMode = !runMode; playSfx(Sfx::UiToggle); break; }
                        if (pl.optimizeAllButton.contains(mx, my)) {
                            contextMenu.open = false;
                            itemMenu.open = false;
                            optimizeAllOpen = true;
                            optimizeSliderDrag = false;
                            playSfx(Sfx::UiMenuOpen);
                            break;
                        }

                        const SidebarLayout sl = sidebarFor(winW, winH, sidebarScroll);
                        const bool onSidebar = sl.panel.contains(mx, my);
                        if (onSidebar) {
                            // Sort, add and the behavior rows change your unit, when the sidebar shows one. They
                            // come before the drags, which start on the inventory and gear.
                            if (shownIsYours()) {
                                const Entity shown = sidebarUnitOf(world);
                                if (sl.sortButton.contains(mx, my)) {
                                    itemMenu.open = false;
                                    sortInventory(shown);
                                    break;
                                }
                                // The Gear tab: Optimize wears the best gear on hand, Remove All takes it all off.
                                // Either one is checked as a whole first; if the inventory can't take what comes off,
                                // nothing changes.
                                if (sl.optimizeButton.contains(mx, my) || sl.removeAllButton.contains(mx, my)) {
                                    itemMenu.open = false;
                                    const bool optimize = sl.optimizeButton.contains(mx, my);
                                    std::string why;
                                    const GearAction done = optimize ? optimizeGear(world, shown, why, gearPrefs()) : removeAllGear(world, shown, why);
                                    if (done == GearAction::Done) {
                                        queueSound(Sfx::ItemMove);
                                    } else if (done == GearAction::NoRoom) {
                                        refuse(why);
                                    } else {
                                        showNotice(lang(optimize ? "MISC_GEAR_ALREADY_BEST" : "MISC_GEAR_NONE_WORN"));
                                    }
                                    break;
                                }
                                if (sl.addButton.contains(mx, my)) {
                                    addBehavior(shown);
                                    break;
                                }
                                const BehaviorHit bh = behaviorHitAt(sl, mx, my);
                                if (bh.row >= 0) {
                                    behaviorClick(shown, bh, mx, my);
                                    break;
                                }
                            }

                            // A filled inventory slot starts dragging its stack out of the inventory. A filled
                            // gear slot starts dragging the gear item out of the unit's gear. A red unit's
                            // sidebar has no unit of yours in it, so nothing there can be dragged.
                            const std::vector<Entity> picked = panelUnits(world);
                            const Inventory* inv = picked.size() == 1 ? world.get<Inventory>(picked[0]) : nullptr;
                            const Equipment* worn = picked.size() == 1 ? world.get<Equipment>(picked[0]) : nullptr;
                            const int slot = sidebarSlotAt(sl, mx, my);
                            const int gear = gearSlotAt(sl, mx, my);
                            if (inv && slot >= 0 && inv->slots[slot].item != 0) {
                                itemDrag = true;
                                dragUnit = picked[0];
                                dragSlot = slot;
                                dragGear = -1;
                            } else if (worn && gear >= 0 && worn->items[gear] != 0) {
                                itemDrag = true;
                                dragUnit = picked[0];
                                dragSlot = -1;
                                dragGear = gear;
                            }
                        }
                        for (const UnitCell& c : pl.cells) {
                            if (!c.rect.contains(mx, my)) continue;
                            const Entity u = own[c.index];
                            // Shift toggles the unit in the selection. Alt adds it and never clears or removes anything.
                            const SDL_Keymod mods = SDL_GetModState();
                            const bool shift = (mods & SDL_KMOD_SHIFT) != 0;
                            const bool additive = (mods & (SDL_KMOD_SHIFT | SDL_KMOD_ALT)) != 0;
                            inspectUnit = NULL_ENTITY;
                            if (shift && world.has<Selected>(u)) {
                                // Shift takes it out of the selection, but never the last one
                                if (selectedUnits(world).size() > 1) world.remove<Selected>(u);
                            } else {
                                if (!additive) clearSelection(world);
                                if (!world.has<Selected>(u)) world.add(u, Selected{});
                            }
                            // Double-click moves the camera to the unit, wherever it is, and keeps it on the unit as it moves
                            if (e.button.clicks == 2) {
                                focusCameraOnUnit(world, cam, u);
                                followUnit = u;
                            }
                            break;
                        }
                        break;
                    }

                    // A click on the visible part of an item on the ground (or, while Alt shows them, on its name)
                    // sends the selected units to pick it up
                    {
                        const bool alt = (SDL_GetModState() & SDL_KMOD_ALT) != 0;
                        const Entity item = renderer.groundItemAtPixel(world, terrain, cam, mx, my, winW, winH, alt);
                        if (item != NULL_ENTITY) {
                            const std::vector<Entity> picked = selectedUnits(world);
                            if (picked.empty()) refuse(lang("MISC_PICKUP_SELECT"));
                            else {
                                noteLockedIn(picked);
                                issuePickupOrder(world, terrain, picked, item);
                            }
                            break;
                        }
                    }
                }

                if (e.button.button == SDL_BUTTON_LEFT) {
                    leftDown = true;
                    pressX = e.button.x;
                    pressY = e.button.y;
                    sel.dragging = false;
                } else if (e.button.button == SDL_BUTTON_MIDDLE) {
                    // Pressing alone changes nothing. Dragging orbits around the current focus.
                    // Pressed over the panels, it orbits nothing.
                    if (!overPanels(e.button.x, e.button.y, winW, winH)) {
                        middleDown = true;
                        lastX = e.button.x;
                        lastY = e.button.y;
                    }
                } else if (e.button.button == SDL_BUTTON_RIGHT) {
                    float mgx = 0.0f, mgz = 0.0f;
                    if (screen == Screen::Playing && mapGroundAt(winW, winH, e.button.x, e.button.y, mgx, mgz)) {
                        // The minimap: the selected units walk to the point clicked on it
                        beginRightHold(mgx, mgz);
                    } else if (overPanels(e.button.x, e.button.y, winW, winH)) {
                        // Right-click on a unit in the Squad Panel: its menu, as on the unit in the world
                        {
                            const std::vector<Entity> own = ownUnitList();
                            const UnitPanelLayout squadL = layoutUnitPanel(winW, winH, (int)own.size(), unitPanelRow, squadCollapsed, squadPlace);
                            bool onCell = false;
                            for (const UnitCell& c : squadL.cells) {
                                if (!c.rect.contains(e.button.x, e.button.y) || c.index < 0 || c.index >= (int)own.size()) continue;
                                onCell = true;
                                openContextMenuOn(own[(size_t)c.index], e.button.x, e.button.y);
                                break;
                            }
                            if (onCell) break;
                        }
                        // Right-click on an inventory item: gear goes straight to its slot, and anything
                        // else opens the menu with Use and Drop. Right-click on a worn gear item takes it off
                        // into the inventory. Elsewhere on the panels, nothing happens. A red unit's sidebar
                        // has no unit of yours in it, so neither of those applies there.
                        itemMenu.open = false;
                        const std::vector<Entity> picked = panelUnits(world);
                        const SidebarLayout sl = sidebarFor(winW, winH, sidebarScroll);
                        const int slot = sidebarUp() ? sidebarSlotAt(sl, e.button.x, e.button.y) : -1;
                        const int gear = sidebarUp() ? gearSlotAt(sl, e.button.x, e.button.y) : -1;
                        const Inventory* inv = picked.size() == 1 ? world.get<Inventory>(picked[0]) : nullptr;
                        const Equipment* wornHere = picked.size() == 1 ? world.get<Equipment>(picked[0]) : nullptr;
                        if (wornHere && gear >= 0 && wornHere->items[gear] != 0) {
                            std::string why;
                            if (unequipToInventory(world, picked[0], gear, why)) queueSound(Sfx::ItemMove);
                            else if (!why.empty()) refuse(why);
                        } else if (inv && slot >= 0 && inv->slots[slot].item != 0 && tradeOpen) {
                            // During a trade a right-click puts the item in the trade, whatever it is
                            requestTrade(false, picked[0], slot);
                        } else if (inv && slot >= 0 && inv->slots[slot].item != 0 && storageProp != NULL_ENTITY &&
                                   picked[0] == storageUnit) {
                            // With a storage panel open, a right-click puts the item in the storage. A stack asks how many.
                            requestStore(slot);
                        } else if (inv && slot >= 0 && inv->slots[slot].item != 0) {
                            const int item = inv->slots[slot].item;
                            const ItemDef* def = findItem(item);
                            if (def && !def->equipSlot.empty()) {
                                const Equipment* worn = world.get<Equipment>(picked[0]);
                                const int target = worn ? gearSlotFor(*worn, item) : -1;
                                std::string why;
                                if (target < 0 || !equipFromInventory(world, picked[0], slot, target, why)) {
                                    refuse(why.empty() ? langf("MISC_CANT_BE_WORN", { itemDisplayName(*def) }) : why);
                                } else {
                                    queueSound(Sfx::ItemMove);
                                }
                            } else {
                                itemMenu = ItemMenu{ true, picked[0], slot, e.button.x, e.button.y };
                                playSfx(Sfx::UiMenuOpen);
                            }
                        }
                    } else {
                        // A unit or a usable prop under the pointer opens the right-click menu: Talk, Trade, Follow,
                        // Defend, Attack, Inventory or Use, as fits it. Anywhere else the selected units walk there.
                        if (openContextMenu(e.button.x, e.button.y)) {
                            rightHold = false;
                        } else {
                            float gx = 0.0f, gz = 0.0f;
                            if (rightClickOrder(world, terrain, cam, e.button.x, e.button.y, winW, winH, gx, gz)) beginRightHold(gx, gz);
                        }
                    }
                }
                break;

            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (partyUi.listDrag && e.button.button == SDL_BUTTON_LEFT) {
                    partyUi.listDrag = false;
                    break;
                }
                if (optimizeSliderDrag && e.button.button == SDL_BUTTON_LEFT) {
                    optimizeSliderDrag = false;
                    saveSettingsFile();   // its last place is kept
                    playSfx(Sfx::UiSlider);
                    break;
                }
                // Letting go of the right button plans the route to where it was held
                if (rightHold && e.button.button == SDL_BUTTON_RIGHT) {
                    rightHold = false;
                    moveSelectedTo(holdX, holdZ);
                    break;
                }
                if (grabWhich >= 0 && e.button.button == SDL_BUTTON_LEFT) {
                    endGrab(winW, winH);
                    setGrabCursor(grabAt(winW, winH, e.button.x, e.button.y).second);
                    break;
                }
                // A slider let go of: save its final level
                if (e.button.button == SDL_BUTTON_LEFT && settingsUi.dragRow >= 0) {
                    settingsUi.dragRow = -1;
                    saveSettingsFile();
                }
                // An item dragged out of the storage: let go over the panel it stays; over the Unit Panel it goes into the
                // unit's inventory if it all fits; anywhere else it drops where the unit stands, not at the prop
                if (e.button.button == SDL_BUTTON_LEFT && storageDrag >= 0) {
                    const int from = storageDrag;
                    storageDrag = -1;
                    if (storageProp != NULL_ENTITY) {
                        const float x = e.button.x, y = e.button.y;
                        if (storageLayout(world, winW, winH).panel.contains(x, y)) {
                            // back where it was
                        } else if (sidebarFor(winW, winH, sidebarScroll).panel.contains(x, y)) {
                            std::string why;
                            if (takeFromStorage(world, from, why)) queueSound(Sfx::ItemMove);
                            else if (!why.empty()) refuse(why);
                        } else if (dropFromStorage(world, from)) {
                            queueSound(Sfx::ItemMove);
                        }
                    }
                    break;
                }
                // An inventory stack let go over the storage panel goes into the storage, if all of it fits
                if (e.button.button == SDL_BUTTON_LEFT && itemDrag && storageProp != NULL_ENTITY &&
                    storageLayout(world, winW, winH).panel.contains(e.button.x, e.button.y)) {
                    if (dragGear < 0 && dragUnit == storageUnit) requestStore(dragSlot);
                    itemDrag = false;
                    dragUnit = NULL_ENTITY;
                    dragSlot = -1;
                    dragGear = -1;
                    break;
                }
                // The trade: an NPC item let go over its side becomes a ghost there. A unit's item let go over your side
                // does too, if it is the unit being traded with. Either way the drag ends with nothing moved.
                if (e.button.button == SDL_BUTTON_LEFT && tradeOpen && dialogueUnit != NULL_ENTITY) {
                    const float x = e.button.x, y = e.button.y;
                    const TradeLayout tl = layoutTrade(winW, winH);
                    if (tradeNpcDrag >= 0) {
                        if (tl.theirBox.contains(x, y)) requestTrade(true, dialogueUnit, tradeNpcDrag);
                        tradeNpcDrag = -1;
                    }
                    if (itemDrag && !sidebarFor(winW, winH, sidebarScroll).panel.contains(x, y)) {
                        if (tl.yourBox.contains(x, y) && dragGear < 0) {
                            if (dragUnit == dialoguePlayer) requestTrade(false, dragUnit, dragSlot);
                            else refuse(lang("MISC_TRADE_ONLY_TALKING"));
                        }
                        itemDrag = false;
                        dragUnit = NULL_ENTITY;
                        dragSlot = -1;
                        dragGear = -1;
                    }
                }
                if (e.button.button == SDL_BUTTON_LEFT && itemDrag) {
                    itemDrag = false;
                    if (world.has<Unit>(dragUnit)) {
                        const SidebarLayout sl = sidebarFor(winW, winH, sidebarScroll);
                        const float rx = e.button.x, ry = e.button.y;
                        const bool overSidebar = sl.panel.contains(rx, ry);
                        const int gear = gearSlotAt(sl, rx, ry);
                        const bool onGrid = sidebarSlotAt(sl, rx, ry) >= 0;
                        std::string why;
                        if (dragGear < 0) {
                            // An inventory stack: onto a gear slot it equips, out of the sidebar it drops
                            // under the unit. Released on the rest of the sidebar, nothing happens.
                            if (gear >= 0) {
                                if (equipFromInventory(world, dragUnit, dragSlot, gear, why)) queueSound(Sfx::ItemMove);
                                else if (!why.empty()) refuse(why);
                            } else if (!overSidebar) {
                                if (dropStack(world, dragUnit, dragSlot)) queueSound(Sfx::ItemMove);
                            }
                        } else {
                            // A gear item: onto another gear slot it moves, onto the inventory it comes off,
                            // out of the sidebar it drops. Released on the rest of the sidebar, nothing happens.
                            if (gear >= 0 && gear != dragGear) {
                                if (!moveEquipped(world, dragUnit, dragGear, gear, why) && !why.empty()) refuse(why);
                            } else if (onGrid) {
                                if (unequipToInventory(world, dragUnit, dragGear, why)) queueSound(Sfx::ItemMove);
                                else if (!why.empty()) refuse(why);
                            } else if (!overSidebar) {
                                dropEquipped(world, dragUnit, dragGear);
                            }
                        }
                    }
                    dragUnit = NULL_ENTITY;
                    dragSlot = -1;
                    dragGear = -1;
                }
                if (e.button.button == SDL_BUTTON_LEFT && leftDown) {
                    leftDown = false;
                    // Shift adds to the selection. Alt never clears it, so Alt-click on empty ground does nothing.
                    const SDL_Keymod mods = SDL_GetModState();
                    const bool additive = (mods & (SDL_KMOD_SHIFT | SDL_KMOD_ALT)) != 0;
                    if (sel.dragging) {
                        boxSelect(world, cam, sel.x0, sel.y0, sel.x1, sel.y1, winW, winH, additive);
                    } else {
                        const std::vector<Entity> under = renderer.unitsAtPixel(world, cam, e.button.x, e.button.y, winW, winH);
                        clickSelect(world, unitClicked(world, terrain, cam, under, e.button.x, e.button.y, winW, winH), additive);
                    }
                    sel.dragging = false;
                }
                if (e.button.button == SDL_BUTTON_MIDDLE) middleDown = false;
                break;

            case SDL_EVENT_MOUSE_MOTION:
                // The party menu's unit list follows its scroll bar thumb while it is held
                if (screen == Screen::PartySetup && partyUi.listDrag) {
                    if (e.motion.state & SDL_BUTTON_LMASK) {
                        partyUi.listScroll = partyListScrollAt(winW, winH, (int)party.size(), e.motion.y - partyUi.listGrab);
                    } else {
                        partyUi.listDrag = false;
                    }
                    break;
                }
                // The Optimize All slider follows the mouse while its button is held
                if (screen == Screen::Playing && optimizeAllOpen && optimizeSliderDrag) {
                    if (e.motion.state & SDL_BUTTON_LMASK) {
                        settings.optimizeAttackShare = levelAtX(layoutOptimizeAll(winW, winH).track, e.motion.x);
                    } else {
                        optimizeSliderDrag = false;
                        saveSettingsFile();
                    }
                    break;
                }
                if (grabWhich >= 0) {
                    applyGrab(winW, winH, e.motion.x, e.motion.y);
                    break;
                }
                if (screen == Screen::Playing) {
                    const bool onMap = mapOpen && worldMapFor(winW, winH).window.contains(e.motion.x, e.motion.y);
                    setGrabCursor(onMap ? 0 : grabAt(winW, winH, e.motion.x, e.motion.y).second);
                }
                // A slider follows the mouse while its button is held
                if (screen == Screen::Settings && settingsUi.dragRow >= 0) {
                    if (e.motion.state & SDL_BUTTON_LMASK) {
                        const std::vector<RowSpec> rows = settingsRows(settingsUi.tab, settings, displayInfo);
                        const SettingsLayout L = layoutSettings(winW, winH, rows, settingsUi);
                        if (settingsUi.dragRow < (int)L.rows.size()) {
                            const int level = levelAtX(L.rows[settingsUi.dragRow].track, e.motion.x);
                            settingsCommit(setRowLevel(rows[settingsUi.dragRow].id, level, settings), false);
                        }
                    } else {
                        // The button came up outside the window, so no button-up arrived: save here
                        settingsUi.dragRow = -1;
                        saveSettingsFile();
                    }
                }
                if (screen == Screen::Playing) {
                    if (screen == Screen::Playing && leftDown) {
                        float dx = e.motion.x - pressX, dy = e.motion.y - pressY;
                        if (!sel.dragging && dx * dx + dy * dy > 36.0f) sel.dragging = true;
                        if (sel.dragging) {
                            sel.x0 = pressX; sel.y0 = pressY;
                            sel.x1 = e.motion.x; sel.y1 = e.motion.y;
                        }
                    }
                    if (middleDown) {
                        cam.yaw += (e.motion.x - lastX) * 0.005f;
                        cam.pitch += (e.motion.y - lastY) * 0.005f;
                        cam.clampState();
                        lastX = e.motion.x;
                        lastY = e.motion.y;
                    }
                }
                break;
            }
        }

        // ---- Delete hold: the save is deleted once the button has been held for DELETE_HOLD_MS ----
        if (deleteHolding) {
            float hx = 0.0f, hy = 0.0f;
            const bool leftHeld = (SDL_GetMouseState(&hx, &hy) & SDL_BUTTON_LMASK) != 0;
            if (!leftHeld || !deleteHoldRect.contains(hx, hy)) {
                deleteHolding = false;          // let go early, or moved off the button: start over
            } else if (SDL_GetTicks() - deleteHoldStart >= DELETE_HOLD_MS) {
                performDelete();
            }
        }

        // ---- Timing ----
        Uint64 now = SDL_GetTicks();
        float dt = (now - lastTicks) / 1000.0f;
        lastTicks = now;
        if (dt > 0.25f) dt = 0.25f;
        if (noticeSec > 0.0f) noticeSec -= dt;
        // The stats popout and the item menu belong to one lone selected unit. They close when that changes.
        if (screen == Screen::Playing) {
            const Entity shown = sidebarUnitOf(world);
            const bool lone = shown != NULL_ENTITY;
            // The Unit Panel opens when one unit is selected on its own, and folds when there is none
            if (!lone) {
                unitOpen = false;
                unitTab = 0;
                unitPlace.w = unitPlace.h = 0.0f;
            } else if (shown != lastLone) {
                unitOpen = true;
                unitPlace.w = unitPlace.h = 0.0f;
                sidebarScroll = 0.0f;
            }
            lastLone = shown;
            if (itemMenu.open) {
                const Inventory* inv = world.get<Inventory>(itemMenu.unit);
                const bool same = lone && shown == itemMenu.unit;
                const bool hasItem = inv && itemMenu.slot >= 0 && itemMenu.slot < INVENTORY_SLOTS
                                  && inv->slots[itemMenu.slot].item != 0;
                if (!same || !hasItem) itemMenu.open = false;
            }
            // The sidebar starts at the top for a unit it was not scrolled for. The editor closes when its rule is gone.
            if (shown != sidebarScrollUnit) {
                sidebarScrollUnit = shown;
                sidebarScroll = 0.0f;
            }
            if (behaviorEditor.open) {
                const Behaviors* b = world.get<Behaviors>(behaviorEditor.unit);
                if (!b || shown != behaviorEditor.unit || behaviorEditor.row >= b->rules.size()) {
                    behaviorEditor.open = false;
                }
            }
        }
        pivotAlpha = std::clamp(pivotAlpha + (middleDown ? 1.0f : -1.0f) * dt * 4.0f, 0.0f, 1.0f);
        if (rightHold && screen == Screen::Playing) {
            // The hold's point follows the mouse, and the units walk straight at it: no route is planned until release
            float hx = holdX, hz = holdZ, mhx = 0.0f, mhy = 0.0f;
            SDL_GetMouseState(&mhx, &mhy);
            if (holdTarget(winW, winH, mhx, mhy, hx, hz)) {
                holdX = hx;
                holdZ = hz;
            }
            steerUnits(world, selectedUnits(world), Vec2{ holdX, holdZ });
            actionX = holdX;
            actionZ = holdZ;
            actionAlpha = 1.0f;
            markMove(holdX, holdZ);
        } else {
            rightHold = false;
            actionAlpha = std::max(0.0f, actionAlpha - dt);   // fades out over about a second
        }
        moveMarkTime = std::max(0.0f, moveMarkTime - dt);

        // Game messages written outside a fight (XP, pickups, drops) join the log every frame
        flushQueuedLog(combatLog);
        cam.mapSize = (float)terrain.size();

        // ---- Game update: only while playing ----
        // The game stands still while a talk is open, and only End Dialogue starts it again
        talkCooldown = std::max(0.0f, talkCooldown - dt);
        // A talk order ends the first time one of its units is in talk range of its NPC
        if (screen == Screen::Playing && dialogueUnit == NULL_ENTITY && !paused) {
            Entity talkNpc = NULL_ENTITY, talker = NULL_ENTITY;
            bool tradeTalk = false, shopTalk = false;
            for (Entity u : world.view<TalkOrder>()) {
                const Entity npcOf = world.get<TalkOrder>(u)->npc;
                if (!world.has<Unit>(npcOf)) {
                    clearTalkOrders(world);
                    break;
                }
                if (gapBetween(world, u, npcOf) <= TALK_RANGE) {
                    talkNpc = npcOf;
                    talker = u;
                    tradeTalk = world.get<TalkOrder>(u)->trade;
                    shopTalk = world.get<TalkOrder>(u)->shop;
                    break;
                }
            }
            if (talker != NULL_ENTITY && tradeTalk) startFreeTrade(talkNpc, talker);
            else if (talker != NULL_ENTITY) tryStartTalk(talkNpc, talker, {}, shopTalk);
        }
        if (screen == Screen::Playing && dialogueUnit == NULL_ENTITY && !paused) {
            panWithKeys(dt, settings.edgeScroll && !leftDown && !middleDown);
            if (followUnit != NULL_ENTITY && !followUnitCamera(world, cam, followUnit)) followUnit = NULL_ENTITY;
            cam.updateClearance(terrain);

            // Fighting first, so a unit that dies this frame is gone before the others move. One sound covers the
            // swings of the frame: a hit if any landed, otherwise a miss if any were missed or dodged.
            const size_t hitsBefore = combatLog.hits;
            const size_t missesBefore = combatLog.misses;
            defendSystem(world);   // defenders pick the units attacking what they defend, before the fight
            combatSystem(world, rng, dt, combatLog, floatingNumbers);
            // Behaviors after the fight, so a unit hit this frame can drink in the same frame
            behaviorSystem(world, combatLog);
            // Anyone left at 0 health falls Unconscious
            downedSystem(world);
            if (combatLog.hits > hitsBefore) playSfx(Sfx::Hit);
            else if (combatLog.misses > missesBefore) playSfx(Sfx::Miss);
            // A log scrolled up keeps its place as lines arrive. One at its newest line follows them.
            const size_t logCount = combatLog.lines.size();
            if (logCount > logSeen && logOffset > 0) logOffset += (int)(logCount - logSeen);
            logSeen = logCount;
            squadSystem(world, dt, runMode);
            steeringSystem(world, terrain, dt, runMode);
            pickupSystem(world);   // a pickup plays its own sound
            // Units that reached the prop they were sent to. Inventory opens the panel. Use says if the bed was taken, the
            // prop had all its workers, or a campfire had nothing to cook or no room.
            for (const PropArrival& a : propOrderSystem(world)) {
                const Prop* p = world.get<Prop>(a.prop);
                if (!p) continue;
                const PropInfo& info = propInfo(p->kind);
                if (a.locked || a.craft == WorkStart::Locked) {
                    refuse(lang("MISC_PROP_LOCKED"));   // locked: it can't be opened or used until it is unlocked
                    continue;
                }
                if (a.action == PropAction::Unlock) {
                    if (a.craft == WorkStart::SkillTooLow) {
                        const Lock* lock = world.get<Lock>(a.prop);
                        refuse(langf("MISC_LOCKPICK_NEEDS", { std::to_string(lock ? lock->level : 1) }));
                    } else if (a.craft == WorkStart::Full) {
                        refuse(lang("MISC_PROP_TAKEN"));
                    }
                    continue;
                }
                if (a.action == PropAction::Inventory) {
                    if (!(storageProp == a.prop && storageUnit == a.unit)) {
                        closeStorage();
                        storageProp = a.prop;
                        storageUnit = a.unit;
                        itemMenu.open = false;
                    }
                    continue;
                }
                if (a.corpse) {
                    refuse(lang("MISC_CORPSE_NO_BED"));
                    continue;
                }
                if (a.putInBed) continue;
                if (a.unreachable) {
                    refuse(lang("MISC_PROP_UNREACHABLE"));
                } else if (a.taken && info.use == PropUse::Occupy) {
                    refuse(lang("MISC_PROP_TAKEN"));
                } else if (a.taken) {
                    refuse(langf("MISC_PROP_WORKERS_FULL", { std::to_string(info.workers) }));
                } else if (info.use == PropUse::Crafting && a.craft == WorkStart::NoInput) {
                    refuse(langf("MISC_CRAFT_NEEDS", { info.input == INPUT_COOKABLE ? std::string(lang("MISC_FOOD_TO_COOK"))
                                                                                         : itemNameOf(info.input) }));
                }
            }
            // Picking up, putting down and carrying Unconscious units; looters that got there open the panel
            const CarryEvents carried = carrySystem(world);
            for (const auto& [looter, looted] : carried.loot) {
                closeStorage();
                lootUnit = looted;
                lootBy = looter;
                playSfx(Sfx::UiMenuOpen);
            }
            cleanupSquads(world);
            // Open worlds: read in the ground near the camera and the units, a few chunks each frame
            if (terrain.streamed()) terrain.stream(streamFocuses(world, cam), STREAM_RADIUS, STREAM_LOADS_PER_FRAME);
            // The in-game clock runs while it is not stopped. Hunger ticks once for each in-game hour it passes,
            // and health comes back by the in-game seconds that passed.
            int hoursPassed = 0;
            double gameSecs = 0.0;
            if (sky.clockRunning) {
                gameSecs = dt * GAME_SECONDS_PER_REAL_SECOND;
                hoursPassed = advanceClock(gs.clockSeconds, dt);
            }
            needsSystem(world, hoursPassed, gameSecs);
            downedSystem(world);   // starving to 0 knocks a unit out too
            // Doom: up while Unconscious, down in a bed or bedroll; 0 there wakes the unit, 100 makes it a corpse
            const DoomChanges doom = doomSystem(world, gameSecs);
            for (Entity u : doom.woke) {
                tradeLabels.push_back(TradeLabel{ u, langf("MISC_EFFECT_LABEL", { lang("UILABEL_HEALTH"), "+" + std::to_string(RECOVERY_HEALTH) }), 0.0f });
                queueSound(Sfx::Recovers);
            }
            if (!doom.died.empty()) queueSound(Sfx::Dies);
            // Resource and crafting props: the units working them make items by the in-game time that passed
            // Each item a worker receives shows over it, as "+Iron Ore", like a trade's label
            for (const WorkGain& g : workSystem(world, gameSecs, rng)) {
                switch (g.kind) {
                case WorkGain::Kind::Item:
                    tradeLabels.push_back(TradeLabel{ g.unit, itemGainLabel(1, itemNameOf(g.item)), 0.0f });
                    // Each prop has its own sound for an item finished
                    switch (g.propKind) {
                    case PROP_ORE: queueSound(Sfx::DoneOre); break;
                    case PROP_TREE: queueSound(Sfx::DoneTree); break;
                    case PROP_MUSHROOM: queueSound(Sfx::DoneMushroom); break;
                    case PROP_CAMPFIRE: queueSound(Sfx::DoneCooking); break;
                    case PROP_WATER_PUMP: queueSound(Sfx::DoneWater); break;
                    default: queueSound(Sfx::ItemMove); break;
                    }
                    break;
                case WorkGain::Kind::Unlocked:
                    tradeLabels.push_back(TradeLabel{ g.unit, lang("MISC_UNLOCKED"), 0.0f });
                    queueSound(Sfx::Unlocked);
                    break;
                case WorkGain::Kind::LockpickFailed:
                    tradeLabels.push_back(TradeLabel{ g.unit, lang("MISC_LOCKPICK_FAILED"), 0.0f });
                    queueSound(Sfx::LockpickFailed);
                    break;
                case WorkGain::Kind::Trained:
                    queueSound(Sfx::DoneTraining);
                    tradeLabels.push_back(TradeLabel{ g.unit, langf("MISC_XP_GAINED", { std::to_string((int)std::lround(g.xp)), statName(g.item) }), 0.0f });
                    break;
                }
            }
            // Passive crafting props (the forge) burn their fuel and fill their output by themselves
            passiveCraftSystem(world, gameSecs);
            // While a prop is worked, its working sound now and then: the pick on the ore, the axe on the tree, the
            // lockpick in the lock. Only props near the camera are heard, and each kind of sound plays at most once
            // at a time.
            if (sky.clockRunning) {
                std::map<Entity, float> nextTicks;
                for (Entity e : world.view<PropWork, Prop, Transform>()) {
                    const PropWork* pw = world.get<PropWork>(e);
                    if (pw->workers <= 0) continue;
                    const Transform* pt = world.get<Transform>(e);
                    if (std::hypot(pt->x - cam.focusX, pt->z - cam.focusZ) > WORK_SOUND_RANGE) continue;
                    const int kind = world.get<Prop>(e)->kind;
                    bool picking = false;
                    for (Entity u : world.view<Working>()) {
                        const Working* wk = world.get<Working>(u);
                        if (wk->prop == e && wk->unlocking) picking = true;
                    }
                    Sfx sound = Sfx::Count;
                    if (picking) sound = Sfx::WorkLockpick;
                    else if (kind == PROP_ORE) sound = Sfx::WorkMining;
                    else if (kind == PROP_TREE) sound = Sfx::WorkWoodcutting;
                    else if (kind == PROP_MUSHROOM) sound = Sfx::WorkHerbalism;
                    else if (kind == PROP_CAMPFIRE) sound = Sfx::WorkCooking;
                    else if (kind == PROP_WATER_PUMP) sound = Sfx::WorkPump;
                    else if (kind == PROP_TRAINING_DUMMY) sound = Sfx::WorkTraining;
                    else if (kind == PROP_FORGE) sound = Sfx::ForgeBurning;
                    if (sound == Sfx::Count) continue;
                    const float every = sound == Sfx::ForgeBurning ? FORGE_SOUND_SECONDS : WORK_SOUND_SECONDS;
                    float t = workSoundTimers.count(e) ? workSoundTimers[e] : every * 0.5f;
                    t += dt;
                    if (t >= every) {
                        t = 0.0f;
                        queueSound(sound);
                    }
                    nextTicks[e] = t;
                }
                workSoundTimers.swap(nextTicks);
            }

            // Autosave every N minutes of play
            autoTimer += dt;
            if (settings.autosaveMinutes > 0 && autoTimer >= settings.autosaveMinutes * 60.0f) {
                autoTimer = 0.0f;
                if (autosaveNow()) {
                    showNotice(lang("MISC_AUTOSAVED"));
                    playSfx(Sfx::UiSaved);
                }
            }
        }

        // The storage panel closes once its unit is out of reach of the prop, is downed, or either is gone, or when the
        // selection becomes one unit other than the panel's. A how-many prompt for it closes with it.
        if (storageProp != NULL_ENTITY) {
            const Needs* sn = world.get<Needs>(storageUnit);
            const std::vector<Entity> picked = selectedUnits(world);
            if (!world.has<Storage>(storageProp) || !world.has<Unit>(storageUnit) ||
                (sn && sn->hp <= 0) || gapBetween(world, storageUnit, storageProp) > STORAGE_CLOSE_RANGE ||
                (picked.size() == 1 && picked[0] != storageUnit)) {
                closeStorage();
            }
        }
        if (quantity.open && quantity.toStorage && storageProp == NULL_ENTITY) quantity.open = false;
        // The looting panel closes once its looter is out of reach, is knocked out, or either is gone, or the unit it
        // shows wakes up
        if (lootUnit != NULL_ENTITY) {
            if (!world.has<Unconscious>(lootUnit) || !world.has<Unit>(lootBy) || world.has<Unconscious>(lootBy) ||
                gapBetween(world, lootBy, lootUnit) > LOOT_RANGE) {
                closeLoot();
            }
        }
        // A unit of your squad is always selected while there is one: when none is (a new game, the selected unit
        // died), the first in the Squad Panel is, preferring one that is awake. With none left, nobody is.
        if (screen == Screen::Playing && selectedUnits(world).empty()) {
            const std::vector<Entity> squad = world.view<Unit, Commandable>();   // creation order, as the Squad Panel
            Entity first = NULL_ENTITY;
            for (Entity u : squad) {
                if (!world.has<Unconscious>(u)) {
                    first = u;
                    break;
                }
            }
            if (first == NULL_ENTITY && !squad.empty()) first = squad[0];
            if (first != NULL_ENTITY) world.add(first, Selected{});
        }
        if (inspectUnit != NULL_ENTITY && !world.has<Unit>(inspectUnit)) inspectUnit = NULL_ENTITY;
        // A unit that is not in your squad shows only its Profile
        if (!unitTabEnabled(world, sidebarUnitOf(world), unitTab)) unitTab = UNIT_TAB_PROFILE;
        // The right-click menu closes if its target is gone or a talk or trade opens
        if (contextMenu.open && ((!world.has<Unit>(contextMenu.target) && !world.has<Prop>(contextMenu.target)) ||
                                 dialogueUnit != NULL_ENTITY)) {
            contextMenu.open = false;
        }

        // ---- Sounds the systems asked for this frame. The plain item sound gives way to any more particular one. ----
        {
            const std::vector<Sfx> queued = takeQueuedSounds();
            const bool particular = std::any_of(queued.begin(), queued.end(), [](Sfx q) {
                return q == Sfx::Equip || q == Sfx::Unequip || q == Sfx::Drop || q == Sfx::Pickup || q == Sfx::Store ||
                       q == Sfx::Take || q == Sfx::GearChanged || q == Sfx::Eat || q == Sfx::Drink || q == Sfx::Poison;
            });
            for (Sfx q : queued) {
                if (q == Sfx::ItemMove && particular) continue;
                playSfx(q);
            }
            // Units selected: a sound when the selection changes to some units
            size_t selKey = 0, selCount = 0;
            for (Entity u : world.view<Selected>()) {
                selKey = selKey * 31u + (size_t)u + 1u;
                ++selCount;
            }
            if (screen == Screen::Playing && selKey != lastSelectionKey && selCount > 0) playSfx(Sfx::UiSelect);
            lastSelectionKey = selKey;
            // (A unit falling Unconscious plays its own sound, from knockOut)
        }

        // ---- HUD: shown in the window title, since there's no text rendering there ----
        std::string title = gameTitle() + "   " + formatDate(gs.clockSeconds);
        if (title != lastTitle) {
            SDL_SetWindowTitle(window, title.c_str());
            lastTitle = title;
        }

        // ---- Hover list: what is under the mouse, in the top-left corner. Units first (white), then items on the
        // ground (orange-yellow), then props (gray), up to the limit in all, then "..." ----
        std::vector<HudEntry> hud;
        float mouseX = 0.0f, mouseY = 0.0f;
        SDL_GetMouseState(&mouseX, &mouseY);
        renderer.setMouse(mouseX, mouseY);
        if (screen == Screen::Playing) {
            std::vector<HudEntry> under;
            for (Entity u : renderer.unitsAtPixel(world, cam, mouseX, mouseY, winW, winH))
                under.push_back({ world.get<Name>(u)->text, HUD_UNIT_COLOR });

            std::vector<Entity> items = renderer.groundItemsAtPixel(world, terrain, cam, mouseX, mouseY, winW, winH);
            // While Alt shows the name boxes, the name line under the mouse counts too
            if ((SDL_GetModState() & SDL_KMOD_ALT) != 0) {
                const Entity labelled = renderer.groundItemAtPixel(world, terrain, cam, mouseX, mouseY, winW, winH);
                if (labelled != NULL_ENTITY && std::find(items.begin(), items.end(), labelled) == items.end())
                    items.insert(items.begin(), labelled);
            }
            for (Entity e : items) {
                const std::string name = itemNameOf(world.get<GroundItem>(e)->item);
                under.push_back({ name.empty() ? std::string(lang("MISC_UNKNOWN_ITEM")) : name, HUD_ITEM_COLOR });
            }

            for (Entity e : renderer.propsAtPixel(world, cam, mouseX, mouseY, winW, winH)) {
                // A locked prop says so: "Locked Chest (Locked)"
                const std::string name = lang(propInfo(world.get<Prop>(e)->kind).nameKey);
                const Lock* lock = world.get<Lock>(e);
                under.push_back({ lock && lock->locked ? langf("MISC_LOCKED_NAME", { name }) : name, HUD_PROP_COLOR });
            }

            for (size_t i = 0; i < under.size() && i < HOVER_NAME_LIMIT; ++i) hud.push_back(under[i]);
            if (under.size() > HOVER_NAME_LIMIT) hud.push_back({ "...", HUD_UNIT_COLOR });
        }

        // ---- Render ----
        renderer.setDayMinutes(timeOfDayMinutes(gs.clockSeconds));
        renderer.setSky(sky);
        auto drawButtons = [&](const std::vector<Button>& buttons) {
            for (const Button& b : buttons) {
                renderer.drawButton(b.rect, b.label, b.enabled, b.rect.contains(mouseX, mouseY));
            }
        };
        // The title and pause menus' items. A new screen lights its first usable item, and its items slide in one after
        // another. Moving the mouse onto an item lights it.
        auto drawMenuItems = [&](Screen which, const std::vector<Button>& buttons) {
            const unsigned long long now = SDL_GetTicks();
            if (menuScreen != which) {
                menuScreen = which;
                menuShownAt = now;
                menuSel = -1;
                for (size_t i = 0; i < buttons.size(); ++i) {
                    if (buttons[i].enabled) { menuSel = (int)i; break; }
                }
            }
            const bool moved = mouseX != menuMouseX || mouseY != menuMouseY;
            menuMouseX = mouseX;
            menuMouseY = mouseY;
            for (size_t i = 0; i < buttons.size(); ++i) {
                if (moved && buttons[i].enabled && buttons[i].rect.contains(mouseX, mouseY)) menuSel = (int)i;
            }
            const float since = (float)(now - menuShownAt) / 1000.0f;
            for (size_t i = 0; i < buttons.size(); ++i) {
                const Button& b = buttons[i];
                const float appear = std::clamp((since - (0.055f * (float)i + 0.1f)) / 0.34f, 0.0f, 1.0f);
                const std::string sub = b.action == Action::Language ? languageDisplayName() : std::string();
                renderer.drawMenuItem(b.rect, b.label, sub, (int)i == menuSel && b.enabled, b.enabled, appear);
            }
        };
        // Notices sit at the top middle of the window
        auto drawNotice = [&]() {
            if (noticeSec > 0.0f) renderer.drawNotice(notice, winW, winH);
        };
        auto dim = [&](float alpha) {
            renderer.drawRect(Rect{ 0.0f, 0.0f, (float)winW, (float)winH }, RGBA{ 0.0f, 0.0f, 0.0f, alpha });
        };

        if (screen != Screen::Title && screen != Screen::Pause) menuScreen = screen;   // the next menu opens fresh
        renderer.setMenuText(screen != Screen::Playing);
        renderer.hoverFrameStart();
        switch (screen) {
        case Screen::Title:
            renderer.beginUi(winW, winH);
            renderer.drawTitleBackground();
            {
                // The game's name and the notice board, top left; the menu down the right; along the bottom, the build
                // stamp and the keys
                const TitleLayout tl = layoutTitle(winW, winH);
                renderer.drawTitleHeader(tl, lang("GAME_TITLE_FIRST"), lang("GAME_TITLE_SECOND"), lang("UILABEL_EARLY_ACCESS"),
                                         messageOfTheDay());
                drawMenuItems(Screen::Title, layoutButtons(Screen::Title, winW, winH, saves));
                renderer.drawTitleFooter(tl.footer, BUILD_STAMP, lang("MISC_TITLE_KEYS"));
            }
            renderer.endUi();
            break;

        case Screen::Settings: {
            renderer.beginUi(winW, winH);
            renderer.drawTitleBackground();
            dim(0.45f);
            const std::vector<RowSpec> rows = settingsRows(settingsUi.tab, settings, displayInfo);
            drawSettingsMenu(renderer, layoutSettings(winW, winH, rows, settingsUi), rows, settingsUi, mouseX, mouseY);
            renderer.endUi();
            break;
        }

        case Screen::Playing: {
            // The world map's view starts out showing the whole level, and starts over each time it opens
            if (!mapOpen) {
                mapSpan = 0.0f;
            } else {
                const float size = (float)terrain.size();
                const Rect box = worldMapFor(winW, winH).map;
                if (mapSpan <= 0.0f) {
                    mapSpan = mapMaxSpan(box, size);
                    mapCx = size * 0.5f;
                    mapCz = size * 0.5f;
                }
                // From one chunk across to the whole level
                clampMapView(box, size, mapCx, mapCz, mapSpan);
            }
            renderer.drawFrame(world, terrain, cam, sel, hud, winW, winH);
            renderer.drawPivotRing(cam, terrain, pivotAlpha, winW, winH);
            renderer.drawActionCrystal(cam, terrain, actionX, actionZ, actionAlpha, winW, winH);
            // In-game panels, drawn over the world: item names while Alt is held, the unit list along the
            // bottom, the inventory of a lone selected unit, and the stack being dragged under the mouse
            renderer.beginUi(winW, winH);
            // The world map's marks: every prop of any kind as a purple square, every player unit as a green dot and
            // the other units as red or gray dots
            std::vector<MapDot> mapDots;
            for (Entity e : world.view<Prop, Transform>()) {
                const Transform* t = world.get<Transform>(e);
                mapDots.push_back(MapDot{ Vec2{ t->x, t->z }, MapDotKind::Prop });
            }
            for (Entity e : world.view<Unit, Transform>()) {
                const Transform* t = world.get<Transform>(e);
                MapDotKind kind = MapDotKind::Player;
                if (world.has<Hostile>(e)) kind = MapDotKind::Hostile;
                else if (world.has<Neutral>(e)) kind = MapDotKind::Neutral;
                else if (!world.has<Commandable>(e)) continue;
                mapDots.push_back(MapDot{ Vec2{ t->x, t->z }, kind });
            }
            if (settings.showPropNames) renderer.drawPropNames(world, cam, winW, winH);
            if (settings.showUnitNames || settings.showNpcNames) {
                renderer.drawUnitNames(world, cam, winW, winH, settings.showUnitNames, settings.showNpcNames);
            }
            // What each unit is doing: mining, woodcutting, cooking, sleeping, encumbered
            renderer.drawEmotes(world, cam, winW, winH, settings.showUnitNames, settings.showNpcNames);
            // What a right-click would do to the unit under the pointer: put down a unit your selected unit carries, or
            // talk to an NPC
            if (!overPanels(mouseX, mouseY, winW, winH) && !contextMenu.open && !itemMenu.open && dialogueUnit == NULL_ENTITY) {
                for (Entity u : renderer.unitsAtPixel(world, cam, mouseX, mouseY, winW, winH)) {
                    const Carried* cd = world.get<Carried>(u);
                    const bool names = world.has<Commandable>(u) ? settings.showUnitNames : settings.showNpcNames;
                    if (cd && world.has<Selected>(cd->by)) {
                        renderer.drawHoverCue(world, cam, winW, winH, u, EMOTE_CUE_PUT_DOWN, names);
                        break;
                    }
                    if (talkable(u)) {
                        renderer.drawHoverCue(world, cam, winW, winH, u, EMOTE_CUE_TALK, names);
                        break;
                    }
                }
            }
            // Damage numbers rise over the units they hit
            renderer.drawFloatingNumbers(floatingNumbers, cam, winW, winH);
            // Labels the systems queued this frame: items gained, and what used items did
            for (const TradeLabel& l : takeQueuedLabels()) tradeLabels.push_back(l);
            for (TradeLabel& l : tradeLabels) l.age += dt;
            tradeLabels.erase(std::remove_if(tradeLabels.begin(), tradeLabels.end(),
                                             [](const TradeLabel& l) { return l.age >= TRADE_LABEL_LIFE; }),
                              tradeLabels.end());
            renderer.drawTradeLabels(tradeLabels, world, cam, winW, winH);
            renderer.drawPropProgress(world, cam, winW, winH, settings.showPropNames);
            if ((SDL_GetModState() & SDL_KMOD_ALT) != 0) {
                renderer.drawGroundLabels(world, terrain, cam, winW, winH, mouseX, mouseY);
            } else if (!overPanels(mouseX, mouseY, winW, winH) && !contextMenu.open && !itemMenu.open) {
                // The item under the pointer (its visible part) shows its name over it; a click picks it up
                const Entity hovered = renderer.groundItemAtPixel(world, terrain, cam, mouseX, mouseY, winW, winH, false);
                if (hovered != NULL_ENTITY) renderer.drawGroundItemName(world, cam, winW, winH, hovered);
            }
            {
                const std::vector<Entity> own = ownUnitList();
                const UnitPanelLayout L = layoutUnitPanel(winW, winH, (int)own.size(), unitPanelRow, squadCollapsed, squadPlace);
                unitPanelRow = L.scrollRow;
                renderer.drawUnitPanel(world, L, own, runMode);
                // The combat log, bottom left. Its offset is written back, so it stays in range.
                const CombatLogLayout cl = combatLogPanel(winW, winH);
                logOffset = cl.offset;
                InfoTime timeInfo;
                if (infoTab == 2) {
                    timeInfo.clock = formatTime(gs.clockSeconds);
                    timeInfo.date = formatDate(gs.clockSeconds);
                    timeInfo.ymd = formatYmd(gs.clockSeconds);
                    timeInfo.days = langf("UILABEL_DAYS_PASSED", { std::to_string(dayIndexOf(gs.clockSeconds)) });
                }
                renderer.drawCombatLog(cl, infoLines(combatLog, infoTab, logFilter), logFilter, mouseX, mouseY, timeInfo);
                // The sidebar shows the lone selected unit, yours or a red one. A red one's gear and inventory
                // are only shown: the mouse handling never starts a drag or a menu on them.
                const Entity shown = sidebarUnitOf(world);
                {
                    // The item being dragged, so the gear slots can show where it fits
                    int draggedId = 0;
                    if (itemDrag && dragGear >= 0) {
                        const Equipment* worn = world.get<Equipment>(dragUnit);
                        if (worn) draggedId = worn->items[dragGear];
                    } else if (itemDrag) {
                        const Inventory* inv = world.get<Inventory>(dragUnit);
                        if (inv && dragSlot >= 0 && dragSlot < INVENTORY_SLOTS) draggedId = inv->slots[dragSlot].item;
                    }
                    const SidebarLayout sl = sidebarFor(winW, winH, sidebarScroll);
                    sidebarScroll = sl.scroll;   // kept within range as the list grows and shrinks
                    renderer.drawInventorySidebar(sl, world, shown, mouseX, mouseY,
                                                  itemDrag && dragGear < 0 ? dragSlot : -1,
                                                  itemDrag && dragGear >= 0 ? dragGear : -1,
                                                  draggedId, shownIsYours());
                    // The full text of the rule under the mouse, when nothing else is open over it
                    if (shown != NULL_ENTITY && !itemDrag && !behaviorEditor.open) renderer.drawBehaviorTip(sl, world, shown, mouseX, mouseY);
                }
                if (itemMenu.open) {
                    const std::vector<std::string> labels = menuLabels();
                    if (!labels.empty()) {
                        renderer.drawItemMenu(renderer.itemMenuLayout(itemMenu.x, itemMenu.y, (int)labels.size(), winW, winH),
                                              labels, mouseX, mouseY);
                    }
                }
                if (behaviorEditor.open && shown == behaviorEditor.unit) {
                    const Behaviors* b = world.get<Behaviors>(shown);
                    if (b && behaviorEditor.row < b->rules.size()) {
                        renderer.drawConditionEditor(
                            renderer.conditionEditorLayout(behaviorEditor.x, behaviorEditor.y, winW, winH),
                            b->rules[behaviorEditor.row], mouseX, mouseY);
                    }
                }
                if (itemDrag) {
                    // The item under the mouse: a gear item, or a stack from the inventory
                    if (dragGear >= 0) {
                        const Equipment* worn = world.get<Equipment>(dragUnit);
                        if (worn && worn->items[dragGear] != 0) renderer.drawDraggedStack(mouseX, mouseY, worn->items[dragGear], 1);
                    } else {
                        const Inventory* inv = world.get<Inventory>(dragUnit);
                        if (inv && dragSlot >= 0 && dragSlot < INVENTORY_SLOTS && inv->slots[dragSlot].item != 0) {
                            renderer.drawDraggedStack(mouseX, mouseY, inv->slots[dragSlot].item, inv->slots[dragSlot].count);
                        }
                    }
                }
            }
            // The looting panel over an Unconscious unit or a corpse
            if (lootUnit != NULL_ENTITY) {
                const std::string who = world.has<Corpse>(lootUnit) ? langf("UILABEL_CORPSE_OF", { nameOf(world, lootUnit) })
                                                                   : nameOf(world, lootUnit);
                renderer.drawLoot(layoutLoot(winW, winH), who, world.get<Equipment>(lootUnit), world.get<Inventory>(lootUnit),
                                  mouseX, mouseY);
            }
            // The storage panel, and the item being dragged out of it
            if (storageProp != NULL_ENTITY) {
                const Storage* st = world.get<Storage>(storageProp);
                const Prop* sp = world.get<Prop>(storageProp);
                const PropInfo& sinfo = propInfo(sp ? sp->kind : 0);
                std::string hint, status;
                if (sinfo.use == PropUse::Crafting) {
                    hint = langf("UILABEL_CRAFT_HINT", { propNameOf(world, storageProp) });
                } else if (sinfo.use == PropUse::PassiveCrafting) {
                    hint = langf("UILABEL_PASSIVE_HINT", { itemNameOf(sinfo.input),
                                                           std::to_string((int)std::lround(sinfo.fuelHours * 60.0)),
                                                           itemNameOf(sinfo.item) });
                    const Fuel* fuel = world.get<Fuel>(storageProp);
                    switch (passiveStateOf(world, storageProp)) {
                    case PassiveState::OutputFull: status = lang("MISC_OUTPUT_FULL"); break;
                    case PassiveState::NoFuel: status = lang("MISC_NO_FUEL"); break;
                    case PassiveState::Burning:
                        status = langf("MISC_FUEL_LEFT", { std::to_string((int)std::ceil((fuel ? fuel->hours : 0.0) * 60.0 - 1e-6)) });
                        break;
                    }
                } else if (sinfo.accepts != ItemFilter::Any) {
                    hint = langf("UILABEL_STORAGE_TAKES", { lang(itemFilterKey(sinfo.accepts)) }) + " " + lang("UILABEL_STORAGE_HINT");
                } else {
                    hint = lang("UILABEL_STORAGE_HINT");
                }
                renderer.drawStorage(storageLayout(world, winW, winH), propNameOf(world, storageProp), hint, st,
                                     world.get<Output>(storageProp), status, storageDrag, mouseX, mouseY);
                if (const ItemStack* dragged = storageSlot(world, storageDrag); dragged && dragged->item != 0) {
                    renderer.drawDraggedStack(mouseX, mouseY, dragged->item, dragged->count);
                }
                if (quantity.open && quantity.toStorage) {
                    renderer.drawQuantityPrompt(layoutQuantity(winW, winH), quantity.name, quantity.chosen,
                                                quantity.available, mouseX, mouseY);
                }
            }
            if (contextMenu.open) renderer.drawItemMenu(contextMenuLayout(), contextLabels(), mouseX, mouseY);
            if (optimizeAllOpen) {
                renderer.drawOptimizeAll(layoutOptimizeAll(winW, winH), (int)ownUnitList().size(), settings.optimizeAttackShare,
                                         { settings.optimizeAccessories, settings.optimizeTwoHanded, settings.optimizeValueTies },
                                         mouseX, mouseY);
            }
            // The world map popout is over everything, the panels included
            if (mapOpen) {
                WorldMapMarks marks;
                marks.camX = cam.focusX;
                marks.camZ = cam.focusZ;
                marks.camYaw = cam.yaw;
                marks.moveX = moveMarkX;
                marks.moveZ = moveMarkZ;
                marks.moveAlpha = std::clamp(moveMarkTime, 0.0f, 1.0f);   // the last second fades it out
                renderer.drawWorldMap(worldMapFor(winW, winH), mapDots, mapCx, mapCz, mapSpan, marks);
            }
            // The talk box, over everything. It ends if the unit it is with is gone.
            if (dialogueUnit != NULL_ENTITY) {
                if (!world.has<Unit>(dialogueUnit)) {
                    dialogueUnit = NULL_ENTITY;
                    dialogueConvo.clear();
                    tradeOpen = false;
                } else {
                    float mhx = 0.0f, mhy = 0.0f;
                    SDL_GetMouseState(&mhx, &mhy);
                    if (dialogueUnit == NULL_ENTITY) quantity.open = false;
                    if (tradeOpen) {
                        const Inventory* npcInv = world.get<Inventory>(dialogueUnit);
                        renderer.drawTrade(layoutTrade(winW, winH), nameOf(world, dialogueUnit), nameOf(world, dialoguePlayer),
                                           npcInv, tradeNpc, tradePlayer, tradeNpcDrag,
                                           checkTrade(world, dialogueUnit, dialoguePlayer), !freeTrade, mhx, mhy);
                        if (npcInv && tradeNpcDrag >= 0 && npcInv->slots[tradeNpcDrag].item != 0) {
                            renderer.drawDraggedStack(mhx, mhy, npcInv->slots[tradeNpcDrag].item, npcInv->slots[tradeNpcDrag].count);
                        }
                        if (quantity.open) {
                            renderer.drawQuantityPrompt(layoutQuantity(winW, winH), quantity.name, quantity.chosen,
                                                        quantity.available, mhx, mhy);
                        }
                    } else {
                        const Dialogue* dg = world.get<Dialogue>(dialogueUnit);
                        renderer.drawDialogue(dialogueBox(winW, winH), dialogueConvo,
                                              dg ? dg->options : std::vector<DialogueOption>{}, mhx, mhy);
                    }
                }
            }
            // The PAUSED bar, over everything in the game and its popouts, while the game stands still
            if (paused || dialogueUnit != NULL_ENTITY) renderer.drawPauseBar(paused && dialogueUnit == NULL_ENTITY);
            renderer.endUi();
            }
            break;

        case Screen::MapSelect: {
            // The same popout as the load list: a name and date for each map, sorted, scrolled in a window
            const float s = winH / 720.0f;
            PanelGeometry g = panelGeometry(winW, winH);
            const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f };
            const RGBA gray{ 0.6f, 0.6f, 0.65f, 1.0f };
            const RGBA hover{ 0.624f, 0.902f, 0.0f, 1.0f };
            const RGBA hint{ 0.91f, 0.925f, 0.949f, 1.0f };
            const RGBA builtIn{ 1.0f, 0.55f, 0.80f, 1.0f };
            const float cx = g.box.x + g.box.w * 0.5f;

            renderer.beginUi(winW, winH);
            renderer.drawTitleBackground();
            dim(0.45f);
            renderer.drawThemedPanel(g.box);
            renderer.drawUiTextCentered(uiUpper(lang("UILABEL_NEW_GAME")), cx, g.box.y + g.pad, white);
            renderer.drawUiTextCentered(maps.empty() ? lang("UILABEL_NO_MAPS")
                                                     : lang("UILABEL_CLICK_MAP"),
                                        cx, g.nameBox.y + g.nameBox.h * 0.22f, gray);

            // Column headers. The active sort column shows its direction.
            const float innerX = g.box.x + g.pad, innerW = g.box.w - 2.0f * g.pad;
            const float textInset = 12.0f * s;
            const float listH = renderer.listTextHeight("Ag");
            const float hy = g.headerY + (g.headerH - listH) * 0.5f;
            auto headerLabel = [&](const std::string& label, bool active) {
                if (!active) return label;
                return label + (mapSortAsc ? "  ^" : "  v");
            };
            renderer.drawListText(headerLabel(lang("UILABEL_NAME"), mapSortByName), innerX + textInset, hy,
                                  mapSortByName ? white : gray);
            renderer.drawListText(lang("UILABEL_TYPE"), innerX + COL_TYPE * innerW, hy, gray);
            renderer.drawListText(headerLabel(lang("UILABEL_CREATED"), !mapSortByName), innerX + COL_TIME * innerW, hy,
                                  !mapSortByName ? white : gray);

            // "..." rows: how many maps are hidden above and below the visible rows
            const size_t total = maps.size();
            const size_t shownFirst = std::min(mapOffset, total);
            const size_t shownLast = std::min(shownFirst + visibleSaveRows(winH), total);
            if (shownFirst > 0) {
                const bool hot = g.aboveBand.contains(mouseX, mouseY);
                renderer.drawListText("...   " + std::to_string(shownFirst) + " more above",
                                      innerX + textInset, g.aboveBand.y + (g.aboveBand.h - listH) * 0.5f,
                                      hot ? hover : hint);
            }
            if (shownLast < total) {
                const bool hot = g.belowBand.contains(mouseX, mouseY);
                renderer.drawListText("...   " + std::to_string(total - shownLast) + " more below",
                                      innerX + textInset, g.belowBand.y + (g.belowBand.h - listH) * 0.5f,
                                      hot ? hover : hint);
            }

            for (const Button& b : layoutMapPanel(winW, winH, maps, mapOffset)) {
                const bool isHover = b.rect.contains(mouseX, mouseY);
                if (b.action == Action::PickMap) {
                    // A map row: background, then its name, type and date in the columns
                    const MapChoice& m = maps[b.param];
                    renderer.drawButton(b.rect, "", true, isHover);
                    const float ry = b.rect.y + (b.rect.h - listH) * 0.5f;
                    renderer.drawListText(m.name, b.rect.x + textInset, ry, isHover ? hover : white);
                    renderer.drawListText(mapTypeLabel(m), b.rect.x + COL_TYPE * b.rect.w, ry,
                                          (m.source == "data" || m.source == "packed") ? builtIn : white);
                    const std::string when = (m.source == "data" || m.source == "packed") ? std::string(lang("UILABEL_BUILT_IN_WHEN"))
                                           : m.created > 0 ? timeString(m.created) : std::string(lang("UILABEL_UNKNOWN"));
                    renderer.drawListText(when, b.rect.x + COL_TIME * b.rect.w, ry, isHover ? hover : white);
                } else if (b.action == Action::MapScrollUp || b.action == Action::MapScrollDown
                           || b.action == Action::MapSortName || b.action == Action::MapSortTime) {
                    // Drawn above as text: the "..." rows and the headers only need their click areas
                    continue;
                } else {
                    drawButtons({ b });
                }
            }
            renderer.endUi();
            break;
        }

        case Screen::ScenarioSelect: {
            // The picked scenario's name and description at the top, the list of scenarios at the bottom
            const float s = winH / 720.0f;
            const ScenarioLayout sl = layoutScenario(winW, winH, (int)ScenarioId::Count, scenarioScroll);
            scenarioScroll = sl.first;
            const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f };
            const RGBA gray{ 0.72f, 0.74f, 0.80f, 1.0f };
            const RGBA accent{ 1.0f, 0.78f, 0.30f, 1.0f };
            renderer.beginUi(winW, winH);
            renderer.drawTitleBackground();
            dim(0.45f);
            renderer.drawThemedPanel(sl.box);
            renderer.drawListText(lang("UILABEL_SCENARIO"), sl.heading.x, sl.heading.y, gray);
            const std::string name = lang(scenarioNameKey(scenarioPick));
            renderer.drawUiTextInBox(name, Rect{ sl.name.x, sl.name.y, sl.name.w, sl.name.h }, accent);
            float ty = sl.desc.y;
            const float lh = renderer.listTextHeight("Ag");
            for (const std::string& ln : renderer.wrapListText(lang(scenarioDescKey(scenarioPick)), sl.desc.w)) {
                if (ty + lh > sl.desc.y + sl.desc.h + 0.5f) break;
                renderer.drawListText(ln, sl.desc.x, ty, white);
                ty += lh;
            }
            renderer.drawRect(Rect{ sl.list.x, sl.list.y - 8.0f * s, sl.list.w, 1.0f * s }, RGBA{ 0.478f, 0.043f, 0.278f, 1.0f });
            for (size_t i = 0; i < sl.rows.size(); ++i) {
                const ScenarioId id = (ScenarioId)(sl.first + (int)i);
                const Rect& r = sl.rows[i];
                renderer.drawButton(r, lang(scenarioNameKey(id)), true, r.contains(mouseX, mouseY));
                if (id == scenarioPick) {
                    // The picked one: a bar down its left edge and a frame
                    renderer.drawRect(Rect{ r.x, r.y, 5.0f * s, r.h }, accent);
                    renderer.drawRect(Rect{ r.x, r.y, r.w, 2.0f * s }, accent);
                    renderer.drawRect(Rect{ r.x, r.y + r.h - 2.0f * s, r.w, 2.0f * s }, accent);
                    renderer.drawRect(Rect{ r.x + r.w - 2.0f * s, r.y, 2.0f * s, r.h }, accent);
                }
            }
            if (sl.track.h > 0.0f) {
                renderer.drawRect(sl.track, RGBA{ 0.13f, 0.08f, 0.17f, 0.9f });
                renderer.drawRect(sl.thumb, RGBA{ 0.878f, 0.129f, 0.541f, 0.9f });
            }
            renderer.drawButton(sl.back, lang("UILABEL_BACK"), true, sl.back.contains(mouseX, mouseY));
            renderer.drawButton(sl.next, lang("UILABEL_CONTINUE"), true, sl.next.contains(mouseX, mouseY));
            renderer.endUi();
            break;
        }

        case Screen::PartySetup: {
            const std::vector<DropEntry> drop = dropdownEntries(party, partyUi);
            renderer.beginUi(winW, winH);
            renderer.drawTitleBackground();
            dim(0.45f);
            renderer.drawPartySetup(layoutPartySetup(winW, winH, party, partyUi, drop), party, partyUi,
                                    partyPanels(winW, winH), mouseX, mouseY);
            renderer.endUi();
            break;
        }

        case Screen::Pause:
            // The world and its PAUSED bar as in the game, then the menu over them in the menu fonts
            renderer.setMenuText(false);
            renderer.drawFrame(world, terrain, cam, sel, {}, winW, winH);
            renderer.beginUi(winW, winH);
            dim(0.55f);
            renderer.drawPauseBar(false);
            renderer.setMenuText(true);
            drawMenuItems(Screen::Pause, layoutButtons(Screen::Pause, winW, winH, saves));
            renderer.endUi();
            break;
        }

        // ---- Save/load popout, drawn over whichever screen is showing ----
        if (panelOpen) {
            const float s = winH / 720.0f;
            PanelGeometry g = panelGeometry(winW, winH);
            const RGBA white{ 1.0f, 1.0f, 1.0f, 1.0f };
            const RGBA gray{ 0.6f, 0.6f, 0.65f, 1.0f };
            const RGBA hover{ 0.624f, 0.902f, 0.0f, 1.0f };
            const RGBA hint{ 0.91f, 0.925f, 0.949f, 1.0f };
            const float cx = g.box.x + g.box.w * 0.5f;

            renderer.beginUi(winW, winH);
            dim(0.25f);
            renderer.drawThemedPanel(g.box);
            renderer.drawUiTextCentered(uiUpper(panelSaveMode ? lang("UILABEL_SAVE_GAME") : lang("UILABEL_LOAD_GAME")), cx, g.box.y + g.pad, white);

            if (ask != Ask::None) {
                DialogGeometry d = questionGeometry(winW, winH);
                renderer.drawThemedPanel(d.box);
                if (ask == Ask::Overwrite) {
                    std::string who = pendingOverwrite ? pendingOverwrite->name : std::string();
                    renderer.drawUiTextCentered(langf("MISC_OVERWRITE_Q", { who }),
                                                d.box.x + d.box.w * 0.5f, d.box.y + 36.0f * s, white);
                } else if (ask == Ask::LoadUnsaved) {
                    std::string who = pendingLoad ? pendingLoad->name : std::string();
                    renderer.drawUiTextCentered(langf("MISC_LOAD_Q", { who }),
                                                d.box.x + d.box.w * 0.5f, d.box.y + 30.0f * s, white);
                    renderer.drawUiTextCentered(lang("UILABEL_UNSAVED_WARNING"),
                                                d.box.x + d.box.w * 0.5f, d.box.y + 36.0f * s + 48.0f * s, white);
                } else {
                    // Delete: the name is shown so it's clear which save goes
                    const std::string who = pendingDelete ? pendingDelete->name : std::string();
                    renderer.drawUiTextCentered(langf("MISC_DELETE_Q", { who }),
                                                d.box.x + d.box.w * 0.5f, d.box.y + 30.0f * s, white);
                    renderer.drawUiTextCentered(lang("UILABEL_HOLD_DELETE"),
                                                d.box.x + d.box.w * 0.5f, d.box.y + 36.0f * s + 48.0f * s, white);
                }
                for (const Button& b : layoutPanel(winW, winH, panelSaveMode, ask, saves, listOffset,
                                                   panelFrom == Screen::Title)) {
                    if (b.action == Action::DeleteHold) {
                        const float fill = deleteHolding
                            ? std::min(1.0f, (float)(SDL_GetTicks() - deleteHoldStart) / (float)DELETE_HOLD_MS)
                            : 0.0f;
                        renderer.drawHoldButton(b.rect, b.label, fill);
                    } else {
                        drawButtons({ b });
                    }
                }
            } else {
                if (panelSaveMode) {
                    // Name field with a blinking cursor, shown even while the name is empty
                    // so it's clear the field has focus as soon as the popout opens
                    renderer.drawRect(g.nameBox, RGBA{ 0.09f, 0.06f, 0.12f, 1.0f });
                    float textY = g.nameBox.y + g.nameBox.h * 0.22f;
                    bool cursorOn = (SDL_GetTicks() / 500) % 2 == 0;
                    renderer.drawUiText(saveName + (cursorOn ? "|" : ""), g.nameBox.x + 12.0f, textY, white);
                } else {
                    renderer.drawUiTextCentered(lang("UILABEL_CLICK_SAVE"),
                                                cx, g.nameBox.y + g.nameBox.h * 0.22f, gray);
                }

                // Column headers. The active sort column shows its direction.
                const float innerX = g.box.x + g.pad, innerW = g.box.w - 2.0f * g.pad;
                const float textInset = 12.0f * s;
                const float listH = renderer.listTextHeight("Ag");
                const float hy = g.headerY + (g.headerH - listH) * 0.5f;
                auto headerLabel = [&](const std::string& label, bool active) {
                    if (!active) return label;
                    return label + (sortAsc ? "  ^" : "  v");
                };
                renderer.drawListText(headerLabel(lang("UILABEL_NAME"), sortKey == SortKey::Name),
                                      innerX + textInset, hy, sortKey == SortKey::Name ? white : gray);
                renderer.drawListText(lang("UILABEL_TYPE"), innerX + COL_TYPE * innerW, hy, gray);
                renderer.drawListText(headerLabel(lang("UILABEL_TIME"), sortKey == SortKey::Time),
                                      innerX + COL_TIME * innerW, hy, sortKey == SortKey::Time ? white : gray);

                if (saves.empty()) {
                    renderer.drawUiTextCentered(lang("UILABEL_NO_SAVES"), cx, g.listTop + g.nameBox.h * 0.22f, gray);
                }

                // "..." rows: how many saves are hidden above and below the visible rows
                const size_t total = saves.size();
                const size_t shownFirst = std::min(listOffset, total);
                const size_t shownLast = std::min(shownFirst + visibleSaveRows(winH), total);
                if (shownFirst > 0) {
                    const bool hot = g.aboveBand.contains(mouseX, mouseY);
                    renderer.drawListText("...   " + std::to_string(shownFirst) + " more above",
                                          innerX + textInset, g.aboveBand.y + (g.aboveBand.h - listH) * 0.5f,
                                          hot ? hover : hint);
                }
                if (shownLast < total) {
                    const bool hot = g.belowBand.contains(mouseX, mouseY);
                    renderer.drawListText("...   " + std::to_string(total - shownLast) + " more below",
                                          innerX + textInset, g.belowBand.y + (g.belowBand.h - listH) * 0.5f,
                                          hot ? hover : hint);
                }

                for (const Button& b : layoutPanel(winW, winH, panelSaveMode, Ask::None, saves, listOffset,
                                                   panelFrom == Screen::Title)) {
                    const bool isHover = b.rect.contains(mouseX, mouseY);
                    if (b.action == Action::LoadSlot) {
                        // A save row: background, then left-aligned name and the type and time columns
                        const SaveSummary& sv = saves[b.param];
                        renderer.drawButton(b.rect, "", true, isHover);
                        const float ry = b.rect.y + (b.rect.h - listH) * 0.5f;
                        renderer.drawListText(sv.name, b.rect.x + textInset, ry, isHover ? hover : white);
                        renderer.drawListText(sv.autosave ? lang("UILABEL_AUTO") : lang("UILABEL_MANUAL"),
                                              b.rect.x + COL_TYPE * b.rect.w, ry,
                                              sv.autosave ? RGBA{ 1.0f, 0.55f, 0.80f, 1.0f } : white);
                        renderer.drawListText(timeString(sv.timestamp),
                                              b.rect.x + COL_TIME * b.rect.w, ry, isHover ? hover : white);
                    } else if (b.action == Action::ScrollUp || b.action == Action::ScrollDown
                               || b.action == Action::SortName || b.action == Action::SortTime) {
                        // Drawn above as text: the "..." rows and the headers only need their click areas
                        continue;
                    } else if (b.action == Action::DeleteSave) {
                        renderer.drawListButton(b.rect, lang("UILABEL_DELETE"), isHover);
                    } else {
                        renderer.drawButton(b.rect, b.label, b.enabled, isHover);
                    }
                }
            }
            renderer.endUi();
        }

        renderer.beginUi(winW, winH);
        drawNotice();
        renderer.endUi();

        // The frame is complete but not yet shown, so it is the one saved
        if (screenshotPending) {
            screenshotPending = false;
            const std::string saved = saveScreenshot(window);
            showNotice(saved.empty() ? std::string(lang("MISC_SCREENSHOT_FAILED")) : langf("MISC_SCREENSHOT_SAVED", { saved }));
        }

        if (renderer.hoverFrameEnd()) playSfx(Sfx::UiHover);   // the pointer came onto another button
        SDL_GL_SwapWindow(window);
        // Holds the frame to the limit, counted from the frame's start
        limitFrameRate(frameStart, settings.fpsLimit);
    }

    // ---- Shutdown ----
    audio::shutdown();
    renderer.shutdown();
    renderer.setTitleFont(nullptr);
    closeFonts(nameFontOwned);
    closeUiFonts(fonts);
    closeUiFonts(menuFonts);
    closeFonts(titleFontOwned);
    TTF_Quit();
    SDL_GL_DestroyContext(ctx);
    SDL_DestroyWindow(window);
    SDL_Log("Closed normally");
    SDL_Quit();
    return 0;
}
