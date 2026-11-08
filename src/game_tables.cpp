#include "game_tables.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <functional>
#include <map>
#include <sstream>
#include "factions.h"
#include "items.h"
#include "mobs.h"
#include "stats.h"
#include "pak.h"
#include "sounds.h"

namespace {
struct Row {
    int line = 0;
    std::string key;
    std::vector<std::string> cols;
};

const std::string NO_FILE;

std::array<SfxInfo, (size_t)Sfx::Count> sfxTable;
std::vector<MobInfo> mobTable;
std::vector<MobKind> mobOfFaction;
std::vector<PropInfo> propTable;
std::vector<PropKind> propsListed;
std::vector<PropKind> propsScattered;
std::array<std::string, UNIT_SPRITE_COUNT> unitModels;
std::map<int, std::string> emoteFiles, iconFiles;
std::map<std::string, std::pair<std::string, std::string>> soundtracks;
std::vector<std::string> fontOrder;
std::map<std::string, std::string> languageFonts;

const char* const SFX_KEYS[] = {
    "UiClick", "Hit", "Miss", "ItemMove", "UiHover", "UiBack", "UiError", "UiTab", "UiToggle", "UiSlider", "UiMenuOpen",
    "UiSaved", "UiLoaded", "UiPause", "UiResume", "UiSelect", "UiOrder", "Equip", "Unequip", "Drop", "Pickup", "Eat",
    "Drink", "Poison", "WorkMining", "WorkWoodcutting", "WorkHerbalism", "WorkCooking", "WorkPump", "WorkTraining",
    "WorkLockpick", "ForgeBurning", "DoneOre", "DoneTree", "DoneMushroom", "DoneCooking", "DoneWater", "DoneTraining",
    "DoneForge", "Unlocked", "LockpickFailed", "TradeDone", "TradeFailed", "Store", "Take", "TurnHostile", "LockedIn",
    "Occupy", "GearChanged", "Downed", "LevelUp", "TalkOpen", "CarryUp", "PutDown", "Dies", "Recovers",
};
static_assert(std::size(SFX_KEYS) == (size_t)Sfx::Count);

const char* const MOB_KEYS[] = {
    "Bear", "Cow", "Chicken", "Boar", "Wolf", "Deer", "Rabbit", "Fish", "Crab", "Lizard",
    "Snake", "Pig", "Sheep", "Duck", "Turtle", "Bat", "Frog", "Gorlicorn", "Griffin", "Manticore",
};
static_assert(std::size(MOB_KEYS) == MOB_NAMED_KIND_COUNT);

const char* const PROP_KEYS[] = {
    "Wagon", "Campfire", "Bedroll", "Bed", "Barrel", "Ore", "Tree", "Boulder", "Mushroom", "Bones", "Wall", "Stall",
    "LockedChest", "PrisonerCage", "Chair", "Bench", "Stool", "Table", "EndTable", "RoundTable", "MetalTable",
    "StandingTorch", "SupportBeam", "TrainingDummy", "RawFoodStorage", "FoodStorage", "WeaponRack", "ArmorStand",
    "WaterTank", "WaterPump", "Forge", "Altar", "Debris", "Shop", "Farm", "Turbine", "Stove", "Airship",
    "Foundation", "WoodWall", "WoodDoor", "Ladder", "UpperFloor", "DragonCrystal", "WoodStockpile", "StoneStockpile",
};
static_assert(std::size(PROP_KEYS) == PROP_NAMED_KIND_COUNT);

const char* const UNIT_KEYS[] = { "Unit1", "Unit2", "Unit3" };
static_assert(std::size(UNIT_KEYS) == UNIT_SPRITE_COUNT);

const char* const SCREEN_KEYS[] = { "Title", "Settings", "ScenarioSelect", "PartySetup", "Pause", "Playing" };

struct Numbered {
    const char* key;
    int id;
};

const std::vector<Numbered> EMOTE_KEYS = {
    { "Mining", 1 }, { "Woodcutting", 2 }, { "Cooking", 3 }, { "Encumbered", 4 }, { "Sleeping", 5 }, { "Hungry", 6 },
    { "Lockpicking", 7 }, { "LockedIn", 8 }, { "Herbalism", 9 }, { "Unconscious", 10 }, { "PlayingDead", 12 },
    { "CuePutDown", 13 }, { "CueTalk", 14 }, { "NoUpkeep", 15 }, { "Decaying", 16 }, { "Hauling", 17 },
    { "Building", 18 },
};

const std::vector<Numbered> ICON_KEYS = {
    { "Equipment", 1 }, { "Inventory", 2 }, { "Behavior", 3 }, { "GameLog", 4 }, { "SquadPanel", 5 },
    { "StatStrength", 6 }, { "StatDexterity", 7 }, { "StatVitality", 8 }, { "StatAgility", 10 },
    { "StatIntelligence", 11 }, { "StatWisdom", 12 }, { "StatCharisma", 13 }, { "StatMelee", 17 },
    { "StatProjectiles", 18 }, { "StatArcane", 19 }, { "StatBlock", 20 }, { "StatMining", 21 }, { "StatHerbalism", 22 },
    { "StatWoodcutting", 23 }, { "StatSkinning", 24 }, { "StatFarming", 25 }, { "StatCooking", 26 },
    { "StatSmelting", 27 }, { "StatForging", 28 }, { "StatCarpentry", 29 }, { "StatStoneworking", 30 },
    { "StatLockpicking", 31 }, { "StatThievery", 32 }, { "StatMedical", 33 }, { "PartyTabIdentity", 34 },
    { "PartyTabStats", 35 }, { "PartyTabGear", 36 }, { "PartyTabItems", 37 }, { "SettingsTabGraphics", 38 },
    { "SettingsTabInterface", 39 }, { "SettingsTabControls", 40 }, { "SettingsTabAudio", 41 }, { "UnitPanel", 42 },
    { "SettingsTabLanguage", 43 }, { "GearHead", 44 }, { "GearTorso", 45 }, { "GearMainHand", 46 }, { "GearOffHand", 47 },
    { "GearLegs", 48 }, { "GearFeet", 49 }, { "GearAccessory1", 50 }, { "GearAccessory2", 51 },
};

std::string at(const char* file, const Row& r, const std::string& what) {
    return std::string(file) + " line " + std::to_string(r.line) + ": " + what;
}

bool parseRows(const char* file, const std::string& text, size_t columns, std::vector<Row>& rows, std::string& err) {
    std::istringstream in(text);
    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        Row r;
        r.line = lineNo;
        std::istringstream ls(line);
        std::string cell;
        std::getline(ls, r.key, '\t');
        while (std::getline(ls, cell, '\t')) r.cols.push_back(cell);
        if (r.cols.size() < columns) {
            err = at(file, r, "needs " + std::to_string(columns) + " columns after the key");
            return false;
        }
        rows.push_back(std::move(r));
    }
    return true;
}

bool readTable(const char* file, size_t columns, std::vector<Row>& rows, std::string& err) {
    const unsigned char* bytes = nullptr;
    size_t size = 0;
    if (!pakFind(file, bytes, size)) {
        err = std::string(file) + " is not in data.pak";
        return false;
    }
    return parseRows(file, std::string((const char*)bytes, size), columns, rows, err);
}

// Calls apply with each key's place in keys. A key not in the list, a key listed twice and a key left out are all errors.
bool applyKeys(const char* file, const std::vector<Row>& rows, const std::vector<std::string>& keys,
               const std::function<bool(size_t, const Row&, std::string&)>& apply, std::string& err) {
    std::vector<bool> seen(keys.size(), false);
    for (const Row& r : rows) {
        const auto it = std::find(keys.begin(), keys.end(), r.key);
        if (it == keys.end()) {
            err = at(file, r, "unknown key " + r.key);
            return false;
        }
        const size_t i = (size_t)(it - keys.begin());
        if (seen[i]) {
            err = at(file, r, "key " + r.key + " is listed twice");
            return false;
        }
        seen[i] = true;
        std::string why;
        if (!apply(i, r, why)) {
            err = at(file, r, why);
            return false;
        }
    }
    for (size_t i = 0; i < keys.size(); ++i) {
        if (!seen[i]) {
            err = std::string(file) + ": key " + keys[i] + " is missing";
            return false;
        }
    }
    return true;
}

template <size_t N>
std::vector<std::string> keyList(const char* const (&keys)[N]) {
    return std::vector<std::string>(keys, keys + N);
}

std::vector<std::string> keyList(const std::vector<Numbered>& keys) {
    std::vector<std::string> out;
    for (const Numbered& k : keys) out.push_back(k.key);
    return out;
}

bool loadSfx(std::string& err) {
    const char* file = "data/sfx.txt";
    std::vector<Row> rows;
    if (!readTable(file, 2, rows, err)) return false;
    return applyKeys(file, rows, keyList(SFX_KEYS), [](size_t i, const Row& r, std::string& why) {
        const std::string& slider = r.cols[1];
        if (slider != "interface" && slider != "effects") {
            why = "the slider must be interface or effects, not " + slider;
            return false;
        }
        sfxTable[i] = SfxInfo{ r.cols[0], slider == "interface" };
        return true;
    }, err);
}

bool loadBgm(std::string& err) {
    const char* file = "data/bgm.txt";
    std::vector<Row> rows;
    if (!readTable(file, 2, rows, err)) return false;
    return applyKeys(file, rows, keyList(SCREEN_KEYS), [](size_t, const Row& r, std::string&) {
        soundtracks[r.key] = { r.cols[0], r.cols[1] };
        return true;
    }, err);
}

bool loadNumberedFiles(const char* file, const std::vector<Numbered>& keys, std::map<int, std::string>& out, std::string& err) {
    std::vector<Row> rows;
    if (!readTable(file, 1, rows, err)) return false;
    return applyKeys(file, rows, keyList(keys), [&](size_t i, const Row& r, std::string&) {
        out[keys[i].id] = r.cols[0];
        return true;
    }, err);
}

bool loadModels(std::string& err) {
    const char* file = "data/models.txt";
    std::vector<Row> rows;
    if (!readTable(file, 1, rows, err)) return false;
    return applyKeys(file, rows, keyList(UNIT_KEYS), [](size_t i, const Row& r, std::string&) {
        unitModels[i] = r.cols[0];
        return true;
    }, err);
}

bool readInt(const std::string& s, int& out) {
    char* end = nullptr;
    const long v = std::strtol(s.c_str(), &end, 10);
    if (s.empty() || *end != '\0' || v < 0) return false;
    out = (int)v;
    return true;
}

bool readFloat(const std::string& s, float& out) {
    char* end = nullptr;
    const float v = std::strtof(s.c_str(), &end);
    if (s.empty() || *end != '\0' || v < 0.0f) return false;
    out = v;
    return true;
}

bool readYesNo(const std::string& s, bool& out) {
    if (s != "yes" && s != "no") return false;
    out = s == "yes";
    return true;
}

bool readTemperament(const std::string& s, MobTemperament& out) {
    if (s == "avoidant") out = MobTemperament::Avoidant;
    else if (s == "passive") out = MobTemperament::Passive;
    else if (s == "neutral") out = MobTemperament::Neutral;
    else if (s == "aggressive") out = MobTemperament::Aggressive;
    else return false;
    return true;
}

bool readCategory(const std::string& s, MobCategory& out) {
    if (s == "regular") out = MobCategory::Regular;
    else if (s == "boss") out = MobCategory::Boss;
    else return false;
    return true;
}

// Gives every key its number: the named ones their place in names plus first, any others the numbers after them in the
// order of the rows. A key listed twice and a named key left out are errors.
template <size_t N>
bool numberKeys(const char* file, const std::vector<Row>& rows, const char* const (&names)[N], int first,
                std::map<std::string, int>& out, std::string& err) {
    out.clear();
    for (size_t i = 0; i < N; ++i) out[names[i]] = first + (int)i;
    int next = first + (int)N;
    std::map<std::string, bool> seen;
    for (const Row& r : rows) {
        if (seen[r.key]) {
            err = at(file, r, "key " + r.key + " is listed twice");
            return false;
        }
        seen[r.key] = true;
        if (!out.count(r.key)) out[r.key] = next++;
    }
    for (const char* name : names) {
        if (!seen[name]) {
            err = std::string(file) + ": key " + name + " is missing";
            return false;
        }
    }
    return true;
}

bool loadMobs(std::string& err) {
    const char* file = "data/mobs.txt";
    constexpr size_t COLUMNS = 25;
    std::vector<Row> rows;
    if (!readTable(file, COLUMNS, rows, err)) return false;
    std::map<std::string, int> numbers;
    if (!numberKeys(file, rows, MOB_KEYS, 1, numbers, err)) return false;
    std::vector<MobInfo> table(rows.size());
    std::vector<MobKind> ofFaction;
    for (const Row& r : rows) {
        const std::vector<std::string>& c = r.cols;
        MobInfo m;
        m.key = r.key;
        m.nameKey = c[0];
        m.picture = c[15];
        m.model = c[16] == "none" ? std::string() : c[16];
        const bool ok = readInt(c[1], m.meat) && readInt(c[2], m.meatCount) && readInt(c[3], m.strength) &&
                        readInt(c[4], m.dexterity) && readInt(c[5], m.vitality) && readInt(c[6], m.agility) &&
                        readInt(c[7], m.melee) && readInt(c[8], m.block) && readInt(c[9], m.wild) &&
                        readInt(c[10], m.bones) && readInt(c[11], m.boneCount) && readYesNo(c[12], m.maxStats) &&
                        readInt(c[13], m.hp) && readFloat(c[14], m.modelHeight) && readYesNo(c[17], m.patrolSpawn) &&
                        readCategory(c[18], m.category) && readTemperament(c[19], m.temperament) &&
                        readFloat(c[20], m.spriteScale) && readFloat(c[21], m.hover) && readInt(c[22], m.faction) &&
                        readInt(c[24], m.escortCount);
        if (!ok) {
            err = at(file, r, "a value for " + r.key + " is not valid");
            return false;
        }
        if (c[23] != "none") {
            const auto escort = numbers.find(c[23]);
            if (escort == numbers.end()) {
                err = at(file, r, "no mob has the key " + c[23]);
                return false;
            }
            m.escort = (MobKind)escort->second;
        }
        const int number = numbers[r.key];
        if ((size_t)m.faction >= ofFaction.size()) ofFaction.resize((size_t)m.faction + 1, MobKind::None);
        if (ofFaction[(size_t)m.faction] != MobKind::None) {
            err = at(file, r, "another mob has the faction " + c[22]);
            return false;
        }
        ofFaction[(size_t)m.faction] = (MobKind)number;
        table[(size_t)number - 1] = m;
    }
    mobTable = std::move(table);
    mobOfFaction = std::move(ofFaction);
    return true;
}

// A table whose first row names its columns
struct NamedColumns {
    std::map<std::string, size_t> at;
    const std::string& operator()(const Row& r, const char* name) const { return name == KEY ? r.key : r.cols[at.find(name)->second]; }
    static constexpr const char* KEY = "key";
};

bool readDouble(const std::string& s, double& out) {
    float f = 0.0f;
    if (!readFloat(s, f)) return false;
    out = std::strtod(s.c_str(), nullptr);
    return true;
}

template <size_t N>
int placeIn(const char* const (&names)[N], const std::string& s) {
    for (size_t i = 0; i < N; ++i) {
        if (s == names[i]) return (int)i;
    }
    return -1;
}

const char* const PROP_USE_KEYS[] = { "none", "storage", "occupy", "resource", "crafting", "passive" };
const char* const ITEM_FILTER_KEYS[] = { "any", "raw_food", "food", "weapon", "armor", "water", "wood", "stone" };

bool readPropFlags(const std::string& text, PropInfo& p, std::string& why) {
    if (text == "none") return true;
    std::istringstream in(text);
    std::string flag;
    while (std::getline(in, flag, ',')) {
        if (flag == "built") p.built = true;
        else if (flag == "build_anywhere") p.buildAnywhere = true;
        else if (flag == "scatter") p.scatter = true;
        else if (flag == "claims_land") p.claimsLand = true;
        else if (flag == "burns_corpses") p.burnsCorpses = true;
        else if (flag == "destroyed_by_hand") p.byHand = true;
        else if (flag == "debris") p.debris = true;
        else if (flag == "seat") p.seat = true;
        else if (flag == "cage") p.cage = true;
        else if (flag == "airship") p.airship = true;
        else if (flag == "foundation") p.foundation = true;
        else if (flag == "on_foundation") p.onFoundation = true;
        else if (flag == "wall_rule") p.wallRule = true;
        else if (flag == "door") p.door = true;
        else if (flag == "ladder") p.ladder = true;
        else if (flag == "upper_floor") p.upperFloor = true;
        else if (flag == "rare") p.rare = true;
        else {
            why = "unknown flag " + flag;
            return false;
        }
    }
    return true;
}

// What one row says, or why it can't be read
bool readPropRow(const NamedColumns& cell, const Row& r, PropInfo& p, std::string& why) {
    p.key = r.key;
    p.nameKey = cell(r, "name_key");
    p.model = cell(r, "model");
    const std::string& input = cell(r, "input");
    if (input == "cookable") p.input = INPUT_COOKABLE;
    else if (input == "bones") p.input = INPUT_BONES;
    else if (!readInt(input, p.input)) { why = "input must be a number, cookable or bones"; return false; }
    if (!readFloat(cell(r, "size"), p.size) || !readInt(cell(r, "item"), p.item) || !readInt(cell(r, "workers"), p.workers) ||
        !readInt(cell(r, "lock_level"), p.lockLevel) || !readDouble(cell(r, "fuel_hours"), p.fuelHours) ||
        !readInt(cell(r, "alt_item"), p.altItem) || !readInt(cell(r, "alt_one_in"), p.altOneIn) ||
        !readDouble(cell(r, "speed"), p.speed) || !readDouble(cell(r, "rested"), p.restedPerHour) ||
        !readDouble(cell(r, "comfort"), p.comfortPerHour) || !readDouble(cell(r, "materials"), p.materials) ||
        !readDouble(cell(r, "build_seconds"), p.buildSeconds)) {
        why = "a number for " + r.key + " is not valid";
        return false;
    }
    const int use = placeIn(PROP_USE_KEYS, cell(r, "use"));
    if (use < 0) { why = "unknown use " + cell(r, "use"); return false; }
    p.use = (PropUse)use;
    const int accepts = placeIn(ITEM_FILTER_KEYS, cell(r, "accepts"));
    if (accepts < 0) { why = "unknown accepts " + cell(r, "accepts"); return false; }
    p.accepts = (ItemFilter)accepts;
    if (cell(r, "stat") != "none") {
        p.stat = statNamed(cell(r, "stat"));
        if (p.stat == StatId::None) { why = "unknown stat " + cell(r, "stat"); return false; }
    }
    if (cell(r, "work_sound") != "none") {
        const int sound = placeIn(SFX_KEYS, cell(r, "work_sound"));
        if (sound < 0) { why = "unknown sound " + cell(r, "work_sound"); return false; }
        p.workSound = (Sfx)sound;
    }
    const int done = placeIn(SFX_KEYS, cell(r, "done_sound"));
    if (done < 0) { why = "unknown sound " + cell(r, "done_sound"); return false; }
    p.doneSound = (Sfx)done;
    if (cell(r, "emote") != "none") {
        bool found = false;
        for (const Numbered& e : EMOTE_KEYS) {
            if (cell(r, "emote") == e.key) {
                p.emote = (EmoteId)e.id;
                found = true;
            }
        }
        if (!found) { why = "unknown emote " + cell(r, "emote"); return false; }
    }
    return readPropFlags(cell(r, "flags"), p, why);
}

bool parseProps(const std::string& text, std::string& err) {
    const char* file = "data/props.txt";
    static const char* const NEEDED[] = { "name_key", "model", "size", "use", "stat", "item", "input", "workers", "lock_level",
        "accepts", "fuel_hours", "alt_item", "alt_one_in", "speed", "rested", "comfort", "work_sound", "done_sound", "emote", "flags",
        "materials", "build_seconds" };
    std::vector<Row> rows;
    if (!parseRows(file, text, 0, rows, err)) return false;
    if (rows.empty() || rows[0].key != NamedColumns::KEY) {
        err = std::string(file) + ": the first row must name the columns, starting with key";
        return false;
    }
    NamedColumns cell;
    for (size_t i = 0; i < rows[0].cols.size(); ++i) cell.at[rows[0].cols[i]] = i;
    for (const char* name : NEEDED) {
        if (!cell.at.count(name)) {
            err = std::string(file) + ": no column named " + name;
            return false;
        }
    }
    const size_t columns = rows[0].cols.size();
    rows.erase(rows.begin());
    std::map<std::string, int> numbers;
    if (!numberKeys(file, rows, PROP_KEYS, 0, numbers, err)) return false;
    std::vector<PropInfo> table(rows.size());
    std::vector<PropKind> listed;
    for (const Row& r : rows) {
        if (r.cols.size() < columns) {
            err = at(file, r, "needs " + std::to_string(columns) + " columns after the key");
            return false;
        }
        const int number = numbers[r.key];
        std::string why;
        if (!readPropRow(cell, r, table[(size_t)number], why)) {
            err = at(file, r, why);
            return false;
        }
        listed.push_back((PropKind)number);
    }
    propTable = std::move(table);
    propsListed = std::move(listed);
    propsScattered.clear();
    for (size_t i = 0; i < propTable.size(); ++i) {
        if (propTable[i].scatter) propsScattered.push_back((PropKind)i);
    }
    return true;
}

bool loadFonts(std::string& err) {
    const char* fontsFile = "data/fonts.txt";
    std::vector<Row> fonts;
    if (!readTable(fontsFile, 1, fonts, err)) return false;
    if (fonts.empty()) {
        err = std::string(fontsFile) + " lists no fonts";
        return false;
    }
    std::map<std::string, std::string> fileOf;
    fontOrder.clear();
    for (const Row& r : fonts) {
        if (!fileOf.emplace(r.key, r.cols[0]).second) {
            err = at(fontsFile, r, "key " + r.key + " is listed twice");
            return false;
        }
        fontOrder.push_back(r.cols[0]);
    }

    const char* languagesFile = "data/languages.txt";
    std::vector<Row> languages;
    if (!readTable(languagesFile, 1, languages, err)) return false;
    languageFonts.clear();
    for (const Row& r : languages) {
        const auto font = fileOf.find(r.cols[0]);
        if (font == fileOf.end()) {
            err = at(languagesFile, r, "no font has the key " + r.cols[0]);
            return false;
        }
        if (!languageFonts.emplace("language_" + r.key, font->second).second) {
            err = at(languagesFile, r, "key " + r.key + " is listed twice");
            return false;
        }
    }
    return true;
}

const std::string& fileIn(const std::map<int, std::string>& files, int id) {
    const auto it = files.find(id);
    return it != files.end() ? it->second : NO_FILE;
}
}

bool loadPropTableText(const std::string& text, std::string& err) { return parseProps(text, err); }

static bool loadPropsFile(std::string& err) {
    const unsigned char* bytes = nullptr;
    size_t size = 0;
    if (!pakFind("data/props.txt", bytes, size)) {
        err = "data/props.txt is not in data.pak";
        return false;
    }
    return parseProps(std::string((const char*)bytes, size), err);
}

static bool loadWholeFile(const char* file, bool (*load)(const std::string&, std::string&), std::string& err) {
    const unsigned char* bytes = nullptr;
    size_t size = 0;
    if (!pakFind(file, bytes, size)) {
        err = std::string(file) + " is not in data.pak";
        return false;
    }
    std::string why;
    if (!load(std::string((const char*)bytes, size), why)) {
        err = std::string(file) + ": " + why;
        return false;
    }
    return true;
}

bool loadGameTables(std::string& err) {
    return loadWholeFile("data/itemdb.txt", loadItemDbText, err) && loadWholeFile("data/factions.txt", loadFactions, err) && loadSfx(err) && loadBgm(err) && loadNumberedFiles("data/emotes.txt", EMOTE_KEYS, emoteFiles, err) &&
           loadNumberedFiles("data/icons.txt", ICON_KEYS, iconFiles, err) && loadModels(err) && loadPropsFile(err) && loadMobs(err) && loadFonts(err);
}

const SfxInfo& sfxInfo(Sfx s) {
    const int i = (int)s;
    return sfxTable[(i >= 0 && i < (int)Sfx::Count) ? (size_t)i : 0];
}

const MobInfo& mobInfo(MobKind kind) {
    static const MobInfo none;
    const int i = idx(kind) - 1;
    if (mobTable.empty()) return none;
    return mobTable[(i >= 0 && i < (int)mobTable.size()) ? (size_t)i : 0];
}

int mobKindCount() { return (int)mobTable.size(); }

MobKind mobKindOfFaction(int faction) {
    return faction >= 0 && (size_t)faction < mobOfFaction.size() ? mobOfFaction[(size_t)faction] : MobKind::None;
}

const PropInfo& propInfo(PropKind kind) {
    static const PropInfo none;
    const int k = idx(kind);
    if (propTable.empty()) return none;
    return propTable[(k >= 0 && k < (int)propTable.size()) ? (size_t)k : 0];
}

int propKindCount() { return (int)propTable.size(); }

bool propKindNamed(const std::string& key, PropKind& out) {
    for (size_t i = 0; i < propTable.size(); ++i) {
        if (propTable[i].key == key) {
            out = (PropKind)i;
            return true;
        }
    }
    return false;
}

bool mobKindNamed(const std::string& key, MobKind& out) {
    for (size_t i = 0; i < mobTable.size(); ++i) {
        if (mobTable[i].key == key) {
            out = (MobKind)(i + 1);
            return true;
        }
    }
    return false;
}

const std::vector<PropKind>& propKindsListed() { return propsListed; }
const std::vector<PropKind>& scatteredPropKinds() { return propsScattered; }

Soundtrack soundtrackNamed(const std::string& screenKey) {
    Soundtrack out;
    const auto it = soundtracks.find(screenKey);
    if (it == soundtracks.end()) return out;
    auto pick = [](const std::string& cell) -> const char* {
        if (cell == "keep") return nullptr;
        return cell == "none" ? "" : cell.c_str();
    };
    out.music = pick(it->second.first);
    out.ambient = pick(it->second.second);
    return out;
}

const std::string& emoteFile(int emote) { return fileIn(emoteFiles, emote); }
const std::string& iconFile(int icon) { return fileIn(iconFiles, icon); }

const std::string& propModelFile(PropKind kind) { return propInfo(kind).model; }

const std::string& unitModelFile(int model) {
    return unitModels[(model >= 1 && model <= UNIT_SPRITE_COUNT) ? (size_t)model - 1 : 0];
}

std::vector<std::string> fontFilesFor(const std::string& languageId) {
    std::vector<std::string> order;
    const auto own = languageFonts.find(languageId);
    if (own != languageFonts.end()) order.push_back(own->second);
    for (const std::string& file : fontOrder) {
        if (std::find(order.begin(), order.end(), file) == order.end()) order.push_back(file);
    }
    return order;
}
