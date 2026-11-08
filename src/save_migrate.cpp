#include "savegame.h"
#include <sstream>

// Each step turns the lines of one version's file into the next version's. A step describes files that already exist, so
// once written it never changes, and its tables must not be replaced by the game's own: those will move on.
namespace {
using Lines = std::vector<std::string>;

// The word at this place in the line (0 is the line's key), as where it starts and how long it is
bool wordAt(const std::string& line, size_t place, size_t& start, size_t& length) {
    start = 0;
    for (size_t i = 0; i < place; ++i) {
        start = line.find(' ', start);
        if (start == std::string::npos) return false;
        ++start;
    }
    const size_t end = line.find(' ', start);
    length = (end == std::string::npos ? line.size() : end) - start;
    return true;
}

// Replaces a word that is a number with that place in names. False if it is not one.
template <size_t N>
bool nameWord(std::string& line, size_t place, const char* const (&names)[N], bool minusOneIsNone = false) {
    size_t start = 0, length = 0;
    if (!wordAt(line, place, start, length)) return false;
    const std::string word = line.substr(start, length);
    int number = -2;
    std::istringstream in(word);
    if (!(in >> number)) return false;
    if (minusOneIsNone && number == -1) {
        line.replace(start, length, "-");
        return true;
    }
    if (number < 0 || number >= (int)N) return false;
    line.replace(start, length, names[number]);
    return true;
}

template <size_t N>
bool namePlaces(std::string& line, const char* const (&names)[N]) {
    std::istringstream in(line);
    std::string key, unit, value, out;
    in >> key >> unit;
    out = key + " " + unit;
    for (size_t i = 0; i < N; ++i) {
        if (!(in >> value)) return false;
        out += std::string(" ") + names[i] + " " + value;
    }
    line = out;
    return true;
}

// Version 6 named props, mobs, behaviors and stats by number, so a new one made every older file unreadable.
const char* const V6_PROPS[] = {
    "Wagon", "Campfire", "Bedroll", "Bed", "Barrel", "Ore", "Tree", "Boulder", "Mushroom", "Bones", "Wall", "Stall",
    "LockedChest", "PrisonerCage", "Chair", "Bench", "Stool", "Table", "EndTable", "RoundTable", "MetalTable",
    "StandingTorch", "SupportBeam", "TrainingDummy", "RawFoodStorage", "FoodStorage", "WeaponRack", "ArmorStand",
    "WaterTank", "WaterPump", "Forge", "Altar", "Debris", "Shop", "Farm", "Turbine", "Stove",
};
const char* const V6_MOBS[] = {
    "-", "Bear", "Cow", "Chicken", "Boar", "Wolf", "Deer", "Rabbit", "Fish", "Crab", "Lizard",
    "Snake", "Pig", "Sheep", "Duck", "Turtle", "Bat", "Frog", "Gorlicorn", "Griffin", "Manticore",
};
const char* const V6_BEHAVIORS[] = {
    "Eat", "Heal", "Share", "AutoHarvest", "AutoHaul", "Patrol", "Roam", "Rest", "LeaveBed", "Rescue", "PatrolSpawn",
};
const char* const V6_STATS[] = {
    "Strength", "Dexterity", "Vitality", "Agility", "Intelligence", "Wisdom", "Charisma", "Melee", "Projectiles", "Arcane",
    "Block", "Mining", "Herbalism", "Woodcutting", "Skinning", "Farming", "Cooking", "Smelting", "Forging", "Carpentry",
    "Stoneworking", "Lockpicking", "Thievery", "Medical",
};
constexpr const char* V6_SIZES = "sizes 37 20 24 11 8 32 12";

bool upgrade6to7(Lines& lines) {
    Lines out;
    out.reserve(lines.size());
    for (std::string& line : lines) {
        std::string key;
        std::istringstream(line) >> key;
        bool ok = true;
        if (key == "sizes") {
            if (line != V6_SIZES) return false;   // from a version 6 game with other lists
            continue;
        }
        if (key == "prop") ok = nameWord(line, 1, V6_PROPS);
        else if (key == "unit") ok = nameWord(line, 6, V6_MOBS);
        else if (key == "stats" || key == "xpparts") ok = namePlaces(line, V6_STATS);
        else if (key == "task") ok = nameWord(line, 3, V6_BEHAVIORS) && nameWord(line, 4, V6_PROPS, true);
        else if (key == "step") ok = nameWord(line, 2, V6_BEHAVIORS);
        if (!ok) return false;
        out.push_back(std::move(line));
    }
    lines = std::move(out);
    return true;
}

// Version 8 added the squad, citizen, squadname and squadat lines. A version 7 file has none of them, so it needs no
// change: its units are put in squads once it loads.
bool upgrade7to8(Lines&) { return true; }
// Version 9 added the ghost lines, for the Graveyard. A version 8 file has none: nobody has died yet, as far as it knows.
bool upgrade8to9(Lines&) { return true; }
// Version 10 added the foundation, blueprint and upstairs lines. A version 9 file has no buildings to tell of.
bool upgrade9to10(Lines&) { return true; }
// Version 11 added the material line. A version 10 piece without one is wood, as it was.
bool upgrade10to11(Lines&) { return true; }
// Version 12 added the bgroup, btask and citgroup lines, for behavior groups. A version 11 file has none: its citizens
// follow Group 1, which starts as the Citizen Default Behavior.
bool upgrade11to12(Lines&) { return true; }
// Version 13 added the storefilter line (a general store's Allowed Items) and the storeslisted line. A version 12 store
// without one allows the default, and its stores get their haul tasks in the Citizen Default Behavior on loading.
bool upgrade12to13(Lines&) { return true; }

struct Step {
    int from;
    bool (*upgrade)(Lines&);
};
constexpr Step STEPS[] = {
    { 6, upgrade6to7 },
    { 7, upgrade7to8 },
    { 8, upgrade8to9 },
    { 9, upgrade9to10 },
    { 10, upgrade10to11 },
    { 11, upgrade11to12 },
    { 12, upgrade12to13 },
};
}

bool upgradeSaveLines(std::vector<std::string>& lines) {
    if (lines.empty()) return false;
    std::istringstream first(lines[0]);
    std::string key;
    int version = 0;
    if (!(first >> key >> version) || key != "version" || version < SAVE_OLDEST_VERSION || version > SAVE_VERSION) return false;
    for (const Step& step : STEPS) {
        if (step.from < version) continue;
        if (!step.upgrade(lines)) return false;
    }
    lines[0] = "version " + std::to_string(SAVE_VERSION);
    return true;
}
