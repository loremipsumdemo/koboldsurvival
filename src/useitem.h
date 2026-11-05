#pragma once
#include <string>
#include <vector>
#include "components.h"
#include "ecs.h"
#include "items.h"

// What a consumable does when it is right-clicked in the inventory. Its script is a few statements,
// separated by ';' or new lines. Each names a stat, then a sign and a whole number:
//
//     health +50              restores 50 health
//     hunger +30; health +15  restores 30 hunger and 15 health
//     health -5               takes 5 health away
//     attack +10 for 60s      adds 10 attack for 60 seconds (the only effect that takes a time)
//
// The stats are health, hunger and attack. Words are not case-sensitive. Health and hunger stay between
// zero and the unit's maximum.
struct UseEffect {
    enum class Stat { Health, Hunger, Attack };
    Stat stat = Stat::Health;
    int amount = 0;      // negative takes the stat down
    float seconds = 0.0f; // attack only: how long the boost lasts
};

// Reads a script into its effects. An empty script has none. Returns false, with a message in err,
// for a statement it can't read.
bool parseUseScript(const std::string& script, std::vector<UseEffect>& out, std::string& err);

// Where effects land: the unit's needs, its combat state for attack boosts, and how much health the
// unit's Medical skill lets it restore (1 is no change)
struct UseTarget {
    Needs* needs = nullptr;
    Combat* combat = nullptr;
    float healScale = 1.0f;
};

// Applies every effect to a target. Returns the health actually restored.
int applyUse(const std::vector<UseEffect>& effects, const UseTarget& target);

enum class UseResult {
    Empty,       // the slot is empty, so nothing happened
    NotUsable,   // the item is not a consumable with a working script, so nothing happened
    Used         // one of the stack was used up
};

// Right-click on an inventory slot. A consumable with a script applies it, and one item leaves the stack.
// Health restored by a unit's own item counts as Medical XP.
UseResult useStackInSlot(World& w, Entity unit, int slot);

// Uses a stack from the consumer's inventory on a target unit: the effects land on the target, and one item
// leaves the consumer's stack. The consumer's Medical skill scales the healing and earns the XP.
UseResult useStackOn(World& w, Entity consumer, Entity target, int slot);

// How much of a stat a consumable restores: the total of its positive effects on it. Zero for anything that is
// not a consumable with a working script.
int restoreAmount(const ItemDef& def, UseEffect::Stat stat);
