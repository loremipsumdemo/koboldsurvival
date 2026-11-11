#pragma once
#include <string>
#include <vector>
#include "behavior.h"
#include "components.h"
#include "ecs.h"
#include "props.h"
#include "terrain.h"

// Crafting Recipes, and the Advanced Crafting props that make them (props.txt flag "advanced": the Stove and the
// Weapon, Armor and Ammunition Workbenches).
//  - A recipe (data/recipes.txt) is made at one kind of prop, from items put into that prop's store, in some hours of
//    work. It makes one batch (one sword, ten arrows) into the prop's output.
//  - A recipe that costs Gizmos is locked until a faction researches it: Gizmos held in a Research Table are spent
//    there, and the recipe is then that whole faction's. One that costs none is everyone's from the start. Every
//    cookable item is also a Stove recipe, of no cost.
//  - Each Advanced Crafting prop has one Crafting Order: the recipe picked for it, and how many of what it makes to
//    keep in stock. It makes one batch at a time: nothing more is made until its output has been carried out, which
//    Auto-Haul Ground Items does as it does for items lying on the ground.
//  - With a stock to keep above 0, units with the Auto-Craft task fill the order by themselves: while the faction's
//    stores (its whole claim's) hold fewer than that of the item and hold what the recipe takes, one fetches the
//    ingredients to the prop and works it. A unit told to Use the prop works it whatever the stock, once its
//    ingredients are in it.
struct RecipeInput {
    int item = 0;
    int count = 1;
};

struct Recipe {
    std::string key;         // its name in saves and in recipes.txt
    PropKind station = PropKind::Stove;
    int item = 0;            // what it makes
    int count = 1;           // how many, in one batch
    double hours = 1.0;      // game hours of work at skill level 1's pace times the prop's speed
    int gizmos = 0;          // Gizmos to research it. 0: known to everyone.
    std::vector<RecipeInput> inputs;
};

// Reads data/recipes.txt. Run after the items and the props are loaded: it also adds the Stove's cooking recipes.
bool loadRecipes(const std::string& text, std::string& err);
const std::vector<Recipe>& recipes();
// -1 if no recipe has this key
int recipeNamed(const std::string& key);
// "Short Sword", or "Wood Arrows x10"
std::string recipeName(const Recipe& r);
// "2 Iron Ingot, 1 Wood Log"
std::string recipeInputsText(const Recipe& r);
// Whether any recipe made at this kind of prop takes this item
bool stationTakesItem(PropKind station, int item);

bool isGizmo(int item);
// How many Gizmos this Research Table holds
int gizmosIn(World& w, ObjectId table);

bool recipeUnlocked(World& w, int faction, int recipe);
// The recipes a faction has researched, by key, for the save
std::vector<std::string> researchedRecipes(World& w, int faction);
void setResearched(World& w, int faction, const std::string& key);
// Spends the table's Gizmos to unlock a recipe for a faction. False if it is unlocked already or the table holds too few.
bool researchRecipe(World& w, ObjectId table, int recipe, int faction);

// The Crafting Order of an Advanced Crafting prop
struct CraftOrder {
    int recipe = -1;   // into recipes(), or -1 for none
    int keep = 0;      // Auto-Craft makes more while the faction's stores hold fewer than this. 0: only when told.
};

// The faction an Advanced Crafting prop works for: its owner's, or yours
int craftFaction(World& w, ObjectId prop);
// How many of an item a faction's stores hold: every store, crafting input and output that is the faction's or stands
// in its claim. The Total Stockpile the Crafting Order's stock is counted against.
int factionStock(World& w, int faction, int item);
// Why this Advanced Crafting prop can't be worked now, as a language key, or nullptr if it can: no recipe picked, the
// recipe not researched, its output not carried out yet, or ingredients missing
const char* craftBlock(World& w, ObjectId prop);
bool outputEmpty(World& w, ObjectId prop);
// A batch is done: its ingredients leave the prop's store and what it makes goes into its output
void finishCraft(World& w, ObjectId prop, ObjectId worker);
// Sets the prop's order; a recipe of another kind of prop, or one not researched, is refused
bool setCraftOrder(World& w, ObjectId prop, int recipe, int keep);

// Auto-Craft: a trip under way, fetching an ingredient from a store, taking it to the prop, or working the prop
struct CraftJob {
    enum class Phase { Fetch, Deliver, Work };
    Phase phase = Phase::Work;
    ObjectId prop = NO_OBJECT;
    ObjectId source = NO_OBJECT;
    int item = 0;
    bool working = false;   // it has begun the work, so the job ends when the work does
    float stuck = 0.0f;
};
bool startCraftStep(World& w, Terrain& t, ObjectId u, Behaviors& b, BehaviorRule& r);
void craftJobSystem(World& w, Terrain& t, float dt);
// An Advanced Crafting prop of this unit's faction whose output waits to be carried out, nearest first, or NO_OBJECT
ObjectId craftOutputToHaul(World& w, ObjectId u, const std::vector<ObjectId>& taken);
// Takes what the unit can carry of the prop's output. How many items it took.
int takeCraftOutput(World& w, ObjectId u, ObjectId prop);
