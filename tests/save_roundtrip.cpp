// Saves a level, loads it back and saves it again: both files must match, and what changed in play must stay changed.
// Runs beside data.pak. Prints each failure and returns non-zero if there were any.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include "build.h"
#include "camera.h"
#include "claims.h"
#include "combat.h"
#include "factions.h"
#include "game_tables.h"
#include "hostility.h"
#include "items.h"
#include "mobs.h"
#include "language.h"
#include "level.h"
#include "pak.h"
#include "sounds.h"
#include "squads.h"
#include "ghosts.h"
#include "airship.h"
#include "systems_internal.h"
#include "chunk_source.h"
#include "surface_tiles.h"
#include "systems.h"
#include "simulation.h"
#include "building.h"
#include "behavior_groups.h"
#include "terrain.h"
#include "encumbrance.h"
#include "spawn_cluster.h"
#include "taming.h"
#include "useitem.h"
#include "wheelbarrow.h"
#include "faction_rules.h"
#include "level_file.h"
#include "recipes.h"

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

std::string slurp(const std::string& path) {
    std::ifstream in(path);
    std::ostringstream all;
    all << in.rdbuf();
    return all.str();
}

ObjectId firstProp(World& w, PropKind kind) {
    std::vector<ObjectId> props = w.view<Prop, Transform>();
    std::sort(props.begin(), props.end());
    for (ObjectId e : props) {
        if (w.get<Prop>(e)->kind == kind) return e;
    }
    return NO_OBJECT;
}

ObjectId firstStocked(World& w) {
    for (ObjectId e : w.view<Prop, Storage, Transform>()) {
        for (const ItemStack& s : w.get<Storage>(e)->slots) {
            if (s.count > 0) return e;
        }
    }
    return NO_OBJECT;
}

// Open ground, but for a strip of closed ground from x 100 to 120 that cuts the map in two
class MoatSource : public ChunkSource {
public:
    explicit MoatSource(int size) : size_(size) {
        surface_.ground = surfaceTilePixels(GROUND_TILE_SPEC);
        surface_.water = surfaceTilePixels(WATER_TILE_SPEC);
    }
    int size() const override { return size_; }
    float floorY() const override { return -2.0f; }
    const WorldSurface& surface() const override { return surface_; }
    bool streamed() const override { return false; }
    bool fill(int cx, int cz, ChunkData& out) const override {
        out = emptyChunk(cx, cz);
        for (int j = 0; j < CHUNK_TILES; ++j) {
            for (int i = 0; i < CHUNK_TILES; ++i) {
                const int x = cx * CHUNK_TILES + i, z = cz * CHUNK_TILES + j;
                const bool open = x < size_ && z < size_ && (x < 100 || x >= 120);
                out.walk[(size_t)j * CHUNK_TILES + i] = open ? 1 : 0;
            }
        }
        return true;
    }

private:
    int size_;
    WorldSurface surface_;
};

int countBarrelLogs(World& w, ObjectId barrel) {
    int n = 0;
    for (const ItemStack& st : w.get<Storage>(barrel)->slots) n += st.item == woodLogItem() ? st.count : 0;
    return n;
}

int stacksIn(const Storage& st) {
    int n = 0;
    for (const ItemStack& s : st.slots) n += s.count > 0 ? 1 : 0;
    return n;
}

} // namespace

int main(int, char**) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);   // each line as it is printed, so a crash shows where it got to
    const char* base = SDL_GetBasePath();
    if (!pakOpen(std::string(base ? base : "") + "data.pak")) {
        std::printf("FAIL: data.pak not found\n");
        return 1;
    }
    {
        std::string err;
        if (!loadGameTables(err)) {
            std::printf("FAIL: %s\n", err.c_str());
            return 1;
        }
        check(sfxInfo(Sfx::Recovers).file == "recovers.mp3" && sfxInfo(Sfx::UiClick).ui && !sfxInfo(Sfx::Hit).ui, "sfx.txt reaches the sounds");
        check(propModelFile(PropKind::Stove) == "stove.glb" && unitModelFile(3) == "unit/a1.glb" && unitModelKey(4) == "f2", "models.txt reaches the props and units");
        check(emoteFile(16) == "decaying.png" && iconFile(51) == "gear_accessory_2.png", "emotes.txt and icons.txt give each id its picture");
        check(mobInfo(MobKind::Bat).hover > 0.0f && mobBones(MobKind::Frog) == 150 && mobTemperament(MobKind::Cow) == MobTemperament::Passive,
              "mobs.txt reaches the mobs");
        check(std::string(soundtrackNamed("Playing").music) == "playing.mp3" && !soundtrackNamed("Pause").music &&
              std::string(soundtrackNamed("Title").ambient).empty(), "bgm.txt gives each screen its tracks");
        check(fontFilesFor("language_ja").front() == "NotoSansJP-Regular.ttf" && fontFilesFor("language_xx").front() == "NotoSans-Regular.ttf" &&
              fontFilesFor("language_ja").size() == 7, "languages.txt puts a language's font first");
        loadLanguage(LANGUAGE_DEFAULT_ID, err);
    }

    Terrain terrain(512);
    World world;
    Rng rng;
    rng.gen.seed(12345);
    setupWorld(world, terrain, rng, true);
    spawnGroundItems(world, terrain, rng, 2);

    // Things a reload must not undo
    const ObjectId emptied = firstStocked(world);
    check(emptied != NO_OBJECT, "the level has a stocked container");
    if (emptied) *world.get<Storage>(emptied) = Storage{};
    const ObjectId altar = firstProp(world, PropKind::Altar);
    check(altar != NO_OBJECT, "the level has an Altar");
    const int altarOwner = altar ? world.get<Owner>(altar)->faction : -1;
    if (altar) breakBuilding(world, altar);
    size_t ownedByLoser = 0;
    for (ObjectId e : world.view<Prop, Owner>()) ownedByLoser += world.get<Owner>(e)->faction == altarOwner ? 1 : 0;

    // A resting unit and a unit with a standing order
    std::vector<ObjectId> people = world.view<Entity, Commandable>();
    std::sort(people.begin(), people.end());
    ObjectId bed = firstProp(world, PropKind::Bedroll);
    check(people.size() >= 2 && bed != NO_OBJECT, "the level has two units and a bedroll");
    Behaviors* resting = world.get<Behaviors>(people[0]);
    for (const BehaviorRule& r : resting->rules) {
        if (r.kind != BehaviorKind::Rest) continue;
        resting->busy = true;
        resting->step = r;
        resting->stepProp = bed;
    }
    world.add(people[0], Occupying{ bed });
    *world.get<Transform>(people[0]) = *world.get<Transform>(bed);
    issueFollowOrder(world, { people[1] }, people[0]);
    world.get<Needs>(people[1])->healSeconds = 12.5;
    world.add(people[1], XpBuffer{});
    world.get<XpBuffer>(people[1])->frac[0] = 0.25f;

    // The bosses
    for (MobKind kind : { MobKind::Gorlicorn, MobKind::Griffin, MobKind::Manticore }) {
        int found = 0;
        for (ObjectId e : world.view<Mob, Entity>()) {
            if (world.get<Mob>(e)->kind != kind) continue;
            ++found;
            check(world.get<Needs>(e)->maxHp == mobInfo(kind).hp, "a boss has its own health");
            check(world.get<Faction>(e)->id == mobFaction(kind) && findFaction(mobFaction(kind)), "a boss has its own listed faction");
            check(world.has<SpawnHome>(e), "a boss patrols its spawn");
            const float plain = deriveCombat(*world.get<Stats>(e), *world.get<Equipment>(e), 0.0f).attack;
            check(numbersOf(world, e).attack == plain * BOSS_DAMAGE_FACTOR, "a boss hits twice as hard");
        }
        check(found == 1, "each boss is placed once");
        const ItemDef* bones = findItem(mobBones(kind));
        const ItemDef* meat = findItem(mobInfo(kind).meat);
        check(bones && hasTag(*bones, "bones") && meat && hasTag(*meat, "raw_food") && findItem(meat->cooksInto), "a boss has bones and cookable meat");
    }
    check(mobInfo(MobKind::Griffin).hp == 10000 && mobInfo(MobKind::Manticore).hp == 5000, "the Griffin and Manticore have their health");
    int bossRabbits = 0;
    for (ObjectId e : world.view<Mob, SpawnHome>()) bossRabbits += world.get<Mob>(e)->kind == MobKind::Rabbit ? 1 : 0;
    check(bossRabbits == mobInfo(MobKind::Gorlicorn).escortCount, "only the Gorlicorn brings rabbits");

    // States a fresh level never has
    std::vector<ObjectId> npcs;
    for (ObjectId e : world.view<Entity, Dialogue>()) npcs.push_back(e);
    std::sort(npcs.begin(), npcs.end());
    check(npcs.size() >= 3 && people.size() >= 3, "the level has NPCs and three of your units");
    const ObjectId patient = npcs[0], dead = npcs[1], angry = npcs[2], rescuer = people[2];
    knockOut(world, patient);
    world.get<Unconscious>(patient)->dying = 12.5f;
    world.add(patient, Carried{ rescuer });
    world.add(rescuer, Carrying{ patient });
    Behaviors* rescuing = world.get<Behaviors>(rescuer);
    for (const BehaviorRule& r : rescuing->rules) {
        if (r.kind != BehaviorKind::Rescue) continue;
        rescuing->busy = true;
        rescuing->step = r;
    }
    knockOut(world, dead);
    finishDying(world, dead);
    world.get<Corpse>(dead)->hours = 3.5;
    provoke(world, angry, FACTION_PLAYER);

    ObjectId lockedProp = NO_OBJECT;
    for (ObjectId e : world.view<Prop, Lock, Transform>()) {
        const Lock* l = world.get<Lock>(e);
        if (l->locked && l->faction > 0) lockedProp = e;
    }
    check(lockedProp != NO_OBJECT, "a faction has a locked prop");
    const ObjectId picker = makeEntity(world, *world.get<Transform>(lockedProp), FACTION_PLAYER);
    world.add(picker, defaultStats());
    world.add(picker, Working{ lockedProp, true });

    // Squads: one renamed and shown, a unit moved into it, and a citizen
    const ObjectId resident = makeEntity(world, *world.get<Transform>(people[0]), FACTION_PLAYER);
    world.add(resident, defaultStats());
    world.get<Name>(resident)->text = "Resident";
    settleSquads(world);
    makeCitizen(world, resident);
    check(world.has<Citizen>(resident) && !world.has<Commandable>(resident) && world.has<SpawnHome>(resident),
          "a citizen is no longer yours to command, and has a home");
    check(moveToSquad(world, people[1], 4), "a unit moves into another squad");
    world.resource<SquadBook>().names[4] = "Night Watch";
    world.resource<SquadBook>().current = 4;
    world.resource<Graveyard>().ghosts.push_back(Ghost{ "Old Bones", 2, 30.0, false });
    world.resource<Graveyard>().ghosts.push_back(Ghost{ "Laid To Rest", 1, 3.0, true });
    ghostSystem(world, 0.0);

    const ObjectId bones = makeProp(world, PropKind::Bones, *world.get<Transform>(people[0]));
    world.add(bones, BonesPile{ 2.0, { ItemStack{ itemTagged(ITEM_TAG_COMMON_BONES), 3 } } });
    const ObjectId forge = firstProp(world, PropKind::Forge);
    check(forge != NO_OBJECT, "the level has a forge");
    *world.get<Fuel>(forge) = Fuel{ 1.5, propInfo(PropKind::Forge).input };
    world.get<PropWork>(forge)->progress = 0.25;
    const ObjectId lying = spawnGroundItem(world, *world.get<Transform>(people[0]), itemTagged(ITEM_TAG_COMMON_BONES), 2);
    world.get<GroundItem>(lying)->hours = 5.0;

    // Behavior groups: a second group that takes in Group 1 and adds woodcutting, with the citizen in it
    const BehaviorRule chopTrees = [] {
        BehaviorRule r;
        r.kind = BehaviorKind::AutoHarvest;
        r.propKind = idx(PropKind::Tree);
        return r;
    }();
    {
        BehaviorGroups& bg = behaviorGroups(world);
        check(bg.groups.size() == 1 && expandGroup(bg, 0).size() == bg.citizen.size(), "Group 1 starts as the Citizen Default Behavior");
        const int woodGroup = addBehaviorGroup(world);
        BehaviorRule base;
        base.kind = BehaviorKind::Group;
        base.propKind = 0;
        bg.groups[(size_t)woodGroup] = { base, chopTrees };
        behaviorGroupsChanged(world);
        check(!canIncludeGroup(bg, 0, woodGroup) && !canIncludeGroup(bg, woodGroup, woodGroup) &&
              canIncludeGroup(bg, woodGroup, GROUP_REF_MILITARY) && !canIncludeGroup(bg, GROUP_REF_CITIZEN, 0),
              "a group can't take in itself or a group that leads back to it, and a Default Behavior takes in no group");
        const std::vector<BehaviorRule> all = expandGroup(bg, woodGroup);
        check(all.size() == bg.citizen.size() + 1 && sameBehavior(all.back(), chopTrees),
              "a Group task stands for the whole of Group 1, with the group's own task below it");
        check(setCitizenGroup(world, resident, woodGroup) && !setCitizenGroup(world, resident, 9), "a citizen moves to a group that exists");
        behaviorGroupSystem(world);
        BehaviorRule home;
        home.kind = BehaviorKind::Patrol;
        check(holdsBehavior(world.get<Behaviors>(resident)->rules, chopTrees) && holdsBehavior(world.get<Behaviors>(resident)->rules, home),
              "the citizen takes its group's tasks, the Citizen Default Behavior's among them");
    }

    const size_t propsBefore = world.view<Prop, Transform>().size();
    const std::map<int, int> repBefore = playerReputation(world);
    const std::string first = "roundtrip_a.sav", second = "roundtrip_b.sav", cut = "roundtrip_cut.sav";
    const Camera cam;
    const GameState gs;
    const MapChoice map{ "packed", "test", "test", "" };
    // Your faction's rules, changed in play
    FactionRules changedRules = factionRules(world, FACTION_PLAYER);
    changedRules.downedBody = DownedBodyRule::Cage;
    changedRules.downedTake = DownedTakeRule::Weapons;
    changedRules.trespass = TrespassRule::Warn;
    changedRules.markup = 35;
    setFactionRules(world, FACTION_PLAYER, changedRules);
    check(writeSaveFile(first, captureWorld(world, gs, cam, rng, 1000, "test", map)), "the save is written");

    SaveData loaded;
    check(readSaveFile(first, loaded), "the save is read");
    Rng after;
    World restored;
    restoreWorld(restored, terrain, after, loaded);
    check(writeSaveFile(second, captureWorld(restored, gs, cam, after, 1000, "test", map)), "the reloaded game is saved");
    check(slurp(first) == slurp(second), "both saves match");
    check(factionRules(restored, FACTION_PLAYER) == changedRules && factionRules(restored, 2) == findFaction(2)->rules,
          "your faction's changed rules come back from a save, and the others keep factions.txt's");
    {
        const BehaviorGroups& bg = behaviorGroups(restored);
        int residentGroup = -1;
        for (ObjectId u : restored.view<Citizen, Name>()) {
            if (restored.get<Name>(u)->text == "Resident") residentGroup = restored.get<Citizen>(u)->group;
        }
        check(bg.groups.size() == 2 && bg.groups[1].size() == 2 && sameBehavior(bg.groups[1][1], chopTrees) && residentGroup == 1,
              "behavior groups, and the group a citizen follows, come back from a save");
    }

    // The Citizen Default Behavior runs needs, building, hauling, harvesting, then movement, and new tasks sort in
    {
        World town;
        BehaviorGroups& bg = behaviorGroups(town);
        bool ordered = true;
        for (size_t i = 1; i < bg.citizen.size(); ++i) ordered = ordered && taskCategory(bg.citizen[i - 1].kind) <= taskCategory(bg.citizen[i].kind);
        check(ordered && bg.citizen.back().kind == BehaviorKind::Patrol, "the Citizen Default Behavior is in category order, Patrol Town last");
        bool spawnPatrol = false, groundHaul = false;
        for (const BehaviorRule& r : bg.citizen) {
            spawnPatrol = spawnPatrol || r.kind == BehaviorKind::PatrolSpawn;
            groundHaul = groundHaul || r.kind == BehaviorKind::AutoHaulGround;
        }
        check(!spawnPatrol && groundHaul, "citizens haul ground items and don't Patrol Spawn (a wild mob's task)");
        bool offered = false;
        for (BehaviorKind k : addableKinds(false)) offered = offered || k == BehaviorKind::PatrolSpawn;
        check(!offered, "Patrol Spawn isn't offered for your units");
        bool sleeps = false, builds = false;
        for (const BehaviorRule& r : bg.citizen) {
            sleeps = sleeps || r.kind == BehaviorKind::Sleep;
            builds = builds || r.kind == BehaviorKind::AutoConstruct;
        }
        check(sleeps && builds, "citizens sleep at night and build by default");
        std::vector<BehaviorRule> list = bg.citizen;
        BehaviorRule chop, haul;
        chop.kind = BehaviorKind::AutoHarvest;
        chop.propKind = idx(PropKind::Tree);
        haul.kind = BehaviorKind::AutoHaul;
        haul.prop = (ObjectId)4242;
        addBehavior(list, chop, BEHAVIOR_GROUP_TASKS);
        addBehavior(list, haul, BEHAVIOR_GROUP_TASKS);
        const size_t n = list.size();
        check(n >= 7 && list[n - 1].kind == BehaviorKind::Patrol && list[n - 2].kind == BehaviorKind::AutoHarvest &&
              list[n - 3].kind == BehaviorKind::AutoCraft && list[n - 4].kind == BehaviorKind::AutoHaulGround &&
              list[n - 5].kind == BehaviorKind::AutoHaul && list[n - 6].kind == BehaviorKind::AutoHaulStall &&
              list[n - 7].kind == BehaviorKind::AutoConstruct,
              "hauling (to stores, then off the ground) and harvesting sort in after building and before movement");
        check(!isNightAt(START_CLOCK_SECONDS) && isNightAt(18.0 * SECONDS_PER_HOUR) && isNightAt(3.0 * SECONDS_PER_HOUR) &&
              !isNightAt(17.9 * SECONDS_PER_HOUR), "night runs from 18:00 to 06:00");
    }
    // Awake, rested empties over the daylight hours less one; at 0, harvesting and crafting slow right down
    {
        World camp;
        const ObjectId worker = makeEntity(camp, Transform{ 0.0f, 0.0f, 0.0f }, FACTION_PLAYER);
        camp.add(worker, defaultStats());
        needsSystem(camp, 0, 0.0);
        Needs* n = camp.get<Needs>(worker);
        n->rested = n->maxRested;
        n->hunger = n->maxHunger;
        for (int i = 0; i < 10 * 60; ++i) needsSystem(camp, 0, 60.0);
        const bool leftAfterTen = n->rested > 0;
        for (int i = 0; i < 61; ++i) needsSystem(camp, 0, 60.0);
        check(leftAfterTen && n->rested == 0, "rested lasts eleven hours awake, from full to 0");
        check(restedWorkFactor(n) == EXHAUSTED_WORK_FACTOR && EXHAUSTED_WORK_FACTOR < 0.5, "work at 0 rested goes far slower");
        n->rested = 1;
        check(restedWorkFactor(n) == 1.0, "with any rest left, work goes at the usual speed");
    }

    // Removing a group: its citizens go back to Group 1, and Group tasks naming it go
    {
        World hall;
        Rng hallRng;
        const ObjectId walker = makeEntity(hall, Transform{ 0.0f, 0.0f, 0.0f }, FACTION_PLAYER);
        hall.add(walker, defaultStats());
        makeCitizen(hall, walker);
        const int a = addBehaviorGroup(hall), b = addBehaviorGroup(hall);
        BehaviorRule takeA;
        takeA.kind = BehaviorKind::Group;
        takeA.propKind = a;
        behaviorGroups(hall).groups[(size_t)b] = { takeA };
        setCitizenGroup(hall, walker, a);
        // A group's list holds up to 256 tasks, past a unit's 16
        {
            std::vector<BehaviorRule> big;
            int added = 0;
            for (int i = 0; i < 300; ++i) {
                BehaviorRule haul;
                haul.kind = BehaviorKind::AutoHaul;
                haul.prop = (ObjectId)(1000 + i);
                added += addBehavior(big, haul, BEHAVIOR_GROUP_TASKS) ? 1 : 0;
            }
            check(added == BEHAVIOR_GROUP_TASKS && BEHAVIOR_GROUP_TASKS == 256, "a group's list takes 256 tasks and no more");
            check(taskCategory(BehaviorKind::AutoHaul) == TaskCategory::Hauling && taskCategory(BehaviorKind::AutoHaulBones) == TaskCategory::Hauling &&
                  taskCategory(BehaviorKind::AutoHarvest) == TaskCategory::Harvesting && taskCategory(BehaviorKind::Group) == TaskCategory::Groups &&
                  taskCategory(BehaviorKind::Eat) == TaskCategory::Needs, "tasks are sorted into kinds, hauling among them");
        }
        check(removeBehaviorGroup(hall, a) && !removeBehaviorGroup(hall, 0), "a group is removed, but never Group 1");
        check(hall.get<Citizen>(walker)->group == 0 && behaviorGroups(hall).groups.size() == 2 && behaviorGroups(hall).groups[1].empty(),
              "its citizen goes back to Group 1, the next group moves up, and Group tasks naming it go");
    }

    check(restored.view<Prop, Transform>().size() == propsBefore, "broken props stay broken (only Debris takes their place)");
    size_t emptyContainers = 0;
    for (ObjectId e : restored.view<Prop, Storage>()) emptyContainers += stacksIn(*restored.get<Storage>(e)) == 0 ? 1 : 0;
    check(emptyContainers > 0, "an emptied container stays empty");
    size_t ownedAfter = 0;
    for (ObjectId e : restored.view<Prop, Owner>()) ownedAfter += restored.get<Owner>(e)->faction == altarOwner ? 1 : 0;
    check(ownedAfter == ownedByLoser, "props abandoned with their Altar stay ownerless");

    std::vector<ObjectId> again = restored.view<Entity, Commandable>();
    std::sort(again.begin(), again.end());
    const Behaviors* b = restored.get<Behaviors>(again[0]);
    check(b && b->busy && b->step.kind == BehaviorKind::Rest && restored.has<Occupying>(again[0]), "a resting unit is still on its Rest step");
    check(restored.has<FollowOrder>(again[1]), "a standing order comes back");
    check(restored.get<Needs>(again[1])->healSeconds == 12.5, "partial healing comes back");
    check(restored.has<XpBuffer>(again[1]) && restored.get<XpBuffer>(again[1])->frac[0] == 0.25f, "partial XP comes back");
    check(rng.gen == after.gen, "the random numbers carry on where they left off");

    int carried = 0, corpses = 0, unconscious = 0, provokedUnits = 0, picking = 0, bonesPiles = 0, burning = 0;
    for (ObjectId e : restored.view<Carried>()) carried += restored.has<Carrying>(restored.get<Carried>(e)->by) ? 1 : 0;
    for (ObjectId e : restored.view<Corpse>()) corpses += restored.get<Corpse>(e)->hours == 3.5 ? 1 : 0;
    for (ObjectId e : restored.view<Unconscious>()) unconscious += restored.get<Unconscious>(e)->dying == 12.5f ? 1 : 0;
    for (ObjectId e : restored.view<Provoked>()) provokedUnits += restored.get<Provoked>(e)->against.empty() ? 0 : 1;
    for (ObjectId e : restored.view<Working>()) picking += restored.get<Working>(e)->unlocking ? 1 : 0;
    for (ObjectId e : restored.view<BonesPile>()) bonesPiles += restored.get<BonesPile>(e)->items.size() == 1 ? 1 : 0;
    for (ObjectId e : restored.view<Fuel, PropWork>()) burning += restored.get<Fuel>(e)->hours == 1.5 && restored.get<PropWork>(e)->progress == 0.25 ? 1 : 0;
    check(carried == 1, "a carried unit is still carried");
    check(corpses == 1, "a corpse keeps its age");
    check(unconscious == 1, "a knocked-out unit keeps its Dying");
    check(provokedUnits >= 1, "a provoked unit stays provoked");
    check(picking == 1, "a unit picking a lock is still picking it");
    check(bonesPiles == 1, "bones keep what they hold");
    check(burning == 1, "a forge keeps its fuel and progress");
    const Behaviors* rescuerAfter = restored.get<Behaviors>(again[2]);
    check(rescuerAfter && rescuerAfter->busy && rescuerAfter->step.kind == BehaviorKind::Rescue, "a rescuer is still on its Rescue step");
    propOrderSystem(restored, terrain);
    check(playerReputation(restored) == repBefore, "a reload costs no reputation");
    check(restored.resource<SquadBook>().names[4] == "Night Watch" && restored.resource<SquadBook>().current == 4,
          "squad names and the shown squad come back");
    check(squadOf(restored, again[1]) == 4 && squadOf(restored, again[0]) == 0, "units come back in their squads");
    check(citizenUnits(restored).size() == 1 && restored.get<Name>(citizenUnits(restored)[0])->text == "Resident" &&
          restored.has<SpawnHome>(citizenUnits(restored)[0]), "a citizen comes back as a citizen, with its home");

    // Sixteen to a squad: the extra units go to squads with room, and once all nine are full, to the citizens
    {
        World crowd;
        std::vector<ObjectId> units;
        for (int i = 0; i < MILITARY_MAX + 5; ++i) {
            const ObjectId u = makeEntity(crowd, Transform{ 1.0f, 0.0f, 1.0f }, FACTION_PLAYER);
            crowd.add(u, SquadMember{ 0 });
            units.push_back(u);
        }
        const int toCitizens = settleSquads(crowd);
        const std::array<int, SQUAD_COUNT> sizes = squadSizes(crowd);
        bool allFull = true;
        for (int n : sizes) allFull = allFull && n == SQUAD_SIZE;
        check(allFull, "a squad over sixteen is split until every squad holds sixteen");
        check(toCitizens == 5 && citizenUnits(crowd).size() == 5, "units past nine full squads become citizens");
        check(squadOf(crowd, units[0]) == 0 && squadOf(crowd, units[SQUAD_SIZE]) == 1, "the first sixteen keep their squad");
        check(!moveToSquad(crowd, units[0], 3), "a full squad takes no one");
        check(!makeMilitary(crowd, citizenUnits(crowd)[0], 0), "a citizen can't join a full military");
        makeCitizen(crowd, units[SQUAD_SIZE * 3]);
        check(makeMilitary(crowd, citizenUnits(crowd)[0], 0) && squadSizes(crowd)[3] == SQUAD_SIZE,
              "a citizen joins the squad with room");
        check(settleSquads(crowd) == 0, "settled squads stay as they are");
    }

    {
        const std::vector<Ghost>& g = restored.resource<Graveyard>().ghosts;
        check(g.size() == 2 && g[0].name == "Old Bones" && g[0].sprite == 2 && g[0].hours == 30.0 && !g[0].appeased && g[1].appeased,
              "the Graveyard comes back");
        check(restored.resource<Graveyard>().haunted, "a Ghost unappeased for over a day still haunts after a reload");
    }

    // Faction homes: each faction of people gets an Altar, and its units become citizens and military
    {
        World homes;
        Rng homeRng;
        homeRng.gen.seed(99);
        setupWorld(homes, terrain, homeRng, false);
        settleFactionHomes(homes, terrain);
        int factionsWithUnits = 0, housed = 0, split = 0;
        for (const FactionDef& f : factions()) {
            if (f.id == FACTION_PLAYER || f.id == FACTION_FACTIONLESS || isMobFaction(f.id)) continue;
            int units = 0, citizens = 0, military = 0;
            for (ObjectId u : homes.view<Entity, Faction>()) {
                if (homes.get<Faction>(u)->id != f.id || homes.has<Mob>(u)) continue;
                ++units;
                citizens += homes.has<Citizen>(u) ? 1 : 0;
                military += homes.has<SquadMember>(u) ? 1 : 0;
            }
            if (units == 0) continue;
            ++factionsWithUnits;
            for (const Claim& c : claimsOf(homes)) housed += c.owner == f.id ? 1 : 0;
            split += citizens > 0 && military > 0 && citizens + military == units ? 1 : 0;
        }
        check(factionsWithUnits > 0 && housed == factionsWithUnits, "every faction of people has an Altar");
        check(split == factionsWithUnits, "every faction's units are some citizens and some military");
        check(citizenUnits(homes).empty(), "another faction's citizens are not yours");

        // New Game's plan of a random set-up puts each camp where the game does, with an Altar, a campfire and four bedrolls
        const LayoutPlan plan = planRandomLayout(terrain, false);
        int matched = 0, kitted = 0, roomy = 0;
        for (const CampPlan& c : plan.camps) {
            int fires = 0, beds = 0;
            bool altarThere = false;
            for (ObjectId p : homes.view<Prop, Transform>()) {
                const Transform* pt = homes.get<Transform>(p);
                const PropKind kind = homes.get<Prop>(p)->kind;
                if (std::hypot(pt->x - c.x, pt->z - c.z) > 4.0f) continue;
                altarThere = altarThere || (kind == PropKind::Altar && std::hypot(pt->x - c.x, pt->z - c.z) < 0.01f);
                fires += kind == PropKind::Campfire ? 1 : 0;
                beds += kind == PropKind::Bedroll ? 1 : 0;
            }
            matched += altarThere ? 1 : 0;
            kitted += fires >= 1 && beds >= 4 ? 1 : 0;
            roomy += c.roomy ? 1 : 0;
        }
        std::printf("note: planned camps %d, matched %d, with kit %d, with room %d\n", (int)plan.camps.size(), matched, kitted, roomy);
        check(!plan.camps.empty() && matched == (int)plan.camps.size(), "the planned camps are where a random set-up puts them");
        check(kitted == (int)plan.camps.size(), "each camp has an Altar, a campfire and four bedrolls");
        check(roomy == (int)plan.camps.size(), "on open ground, each camp has room all round it");

        // A level file's faction with no camp gets a whole one where its units are
        World bare;
        Rng bareRng;
        int friendly = -1;
        for (const FactionDef& f : factions()) {
            if (f.id != FACTION_PLAYER && f.id != FACTION_FACTIONLESS && !isMobFaction(f.id)) {
                friendly = f.id;
                break;
            }
        }
        const Vec2 mid = systems_detail::findSpawn(terrain);
        for (int i = 0; i < 3; ++i) spawnUnit(bare, terrain, bareRng, friendly, mid.x + (float)i, mid.z);
        settleFactionHomes(bare, terrain);
        int fires = 0, beds = 0, altars = 0;
        for (ObjectId p : bare.view<Prop>()) {
            const PropKind kind = bare.get<Prop>(p)->kind;
            fires += kind == PropKind::Campfire ? 1 : 0;
            beds += kind == PropKind::Bedroll ? 1 : 0;
            altars += kind == PropKind::Altar ? 1 : 0;
        }
        check(altars == 1 && fires == 1 && beds == 4, "a faction placed by a level file gets an Altar, a campfire and four bedrolls");
    }

    // Ghosts: your dead rise, haunt after a day unappeased, and are appeased with bones at your Altar
    {
        World yard;
        Rng yardRng;
        setupWorld(yard, terrain, yardRng, false);
        std::vector<ObjectId> own = yard.view<Entity, Commandable>();
        std::sort(own.begin(), own.end());
        check(own.size() >= 2, "the level has two of your units");
        const ObjectId fallen = own[0], living = own[1];
        yard.get<Name>(fallen)->text = "Pip";
        const ObjectId npc = yard.view<Entity, Dialogue>().front();
        knockOut(yard, npc);
        finishDying(yard, npc);
        check(yard.resource<Graveyard>().ghosts.empty(), "another faction's dead are not your Ghosts");
        knockOut(yard, fallen);
        finishDying(yard, fallen);
        const std::vector<Ghost>& ghosts = yard.resource<Graveyard>().ghosts;
        check(ghosts.size() == 1 && ghosts[0].name == "Pip" && !ghosts[0].appeased, "a unit of yours that dies becomes a Ghost");
        const float before = numbersOf(yard, living).attack;
        ghostSystem(yard, 23.0 * 3600.0);
        check(!yard.resource<Graveyard>().haunted, "a Ghost waits a day before it haunts");
        ghostSystem(yard, 2.0 * 3600.0);
        check(yard.resource<Graveyard>().haunted && hauntedUnit(yard, living), "a Ghost unappeased for a day haunts your faction");
        check(numbersOf(yard, living).attack < before, "a Haunted unit hits softer");
        const ObjectId altarProp = placeBuiltProp(yard, terrain, PropKind::Altar, 200.5f, 200.5f, 0.0f, FACTION_PLAYER);
        check(!appeaseGhost(yard, altarProp, 0), "a Ghost can't be appeased without bones");
        const int someBones = itemTagged(ITEM_TAG_COMMON_BONES);
        check(propAcceptsItem(PropKind::Altar, someBones) && !propAcceptsItem(PropKind::Altar, itemTagged(ITEM_TAG_GOLD_COIN)),
              "an Altar keeps bones, and not just anything");
        yard.get<Storage>(altarProp)->slots[5] = ItemStack{ someBones, 2 };
        check(bonesIn(yard, altarProp) == 2 && appeaseGhost(yard, altarProp, 0), "bones in the Altar appease a Ghost");
        check(bonesIn(yard, altarProp) == 1 && !yard.resource<Graveyard>().haunted && !hauntedUnit(yard, living),
              "appeasing takes one bones and lifts the haunting");
    }

    // Airships: fly over closed ground, nobody gets off there, and those aboard die if it is destroyed
    {
        Terrain moat(std::make_shared<MoatSource>(256));
        check(unreachableLand(moat, 50.5f, 50.5f, AIRSHIP_LAND_TILES) && !unreachableLand(terrain, 50.5f, 50.5f, AIRSHIP_LAND_TILES),
              "land that can't be walked to is found, and only then");
        World sky;
        Rng skyRng;
        const ObjectId pilot = spawnUnit(sky, moat, skyRng, FACTION_PLAYER, 80.5f, 50.5f);
        const ObjectId ship = placeStartingAirship(sky, moat, 80.5f, 50.5f);
        check(ship != NO_OBJECT && isAirship(sky, ship) && occupyCapacity(sky, ship) == 4, "an Airship is placed, with room for four");
        sky.add(pilot, Occupying{ ship });
        flyAirship(sky, ship, Vec2{ 110.5f, 50.5f });
        for (int i = 0; i < 60 * 20; ++i) airshipSystem(sky, moat, 1.0f / 60.0f);
        const Transform* st = sky.get<Transform>(ship);
        const Transform* pt = sky.get<Transform>(pilot);
        check(std::fabs(st->x - 110.5f) < 0.2f && !moat.walkable(110, 50), "an Airship flies out over closed ground");
        check(std::hypot(pt->x - st->x, pt->z - st->z) < 1.0f && pt->y > st->y, "those aboard ride on its deck");
        check(stuckAboard(sky, pilot) && !disembark(sky, moat, pilot) && airshipOf(sky, pilot) == ship, "nobody gets off over closed ground");
        systems_detail::clearOrders(sky, pilot);
        check(airshipOf(sky, pilot) == ship, "no order takes a unit off it there");
        flyAirship(sky, ship, Vec2{ 140.5f, 50.5f });
        for (int i = 0; i < 60 * 20; ++i) airshipSystem(sky, moat, 1.0f / 60.0f);
        check(!stuckAboard(sky, pilot) && disembark(sky, moat, pilot) && airshipOf(sky, pilot) == NO_OBJECT,
              "over open ground on the far side, they get off");
        // Getting on takes five seconds, and there must still be room at the end; getting off takes one
        auto fly = [&](float seconds) {
            for (int i = 0; i < (int)(seconds * 60.0f); ++i) airshipSystem(sky, moat, 1.0f / 60.0f);
        };
        const Transform at = *sky.get<Transform>(ship);
        check(startBoarding(sky, pilot, ship), "a unit by an Airship starts getting on");
        fly(4.8f);
        check(airshipOf(sky, pilot) == NO_OBJECT, "getting on takes a while");
        fly(0.4f);
        check(airshipOf(sky, pilot) == ship, "after five seconds it is aboard");
        std::vector<ObjectId> crowd;
        for (int i = 0; i < 4; ++i) {
            const ObjectId u = spawnUnit(sky, moat, skyRng, FACTION_PLAYER, at.x - 1.0f + 0.5f * (float)i, at.z + 1.0f);
            crowd.push_back(u);
            check(startBoarding(sky, u, ship), "with room, another starts getting on");
        }
        fly(5.2f);
        int aboard = 0;
        for (ObjectId u : crowd) aboard += airshipOf(sky, u) == ship ? 1 : 0;
        check(aboard == 3 && occupantCount(sky, ship) == 4, "once full, the last to finish is turned away");
        check(!startBoarding(sky, crowd[3], ship) || airshipOf(sky, crowd[3]) == ship, "a full Airship takes nobody new");
        check(startLeaving(sky, pilot), "over open ground a unit starts getting off");
        fly(0.8f);
        check(airshipOf(sky, pilot) == ship, "getting off takes a moment");
        fly(0.4f);
        check(airshipOf(sky, pilot) == NO_OBJECT, "after a second it is off");
        systems_detail::clearOrders(sky, crowd[0]);
        check(airshipOf(sky, crowd[0]) == ship, "a new order doesn't take a unit off: only Evacuate does");
        for (ObjectId u : crowd) {
            if (airshipOf(sky, u) == ship) sky.remove<Occupying>(u);
        }

        sky.add(pilot, Occupying{ ship });
        destroyBuilding(sky, ship);
        check(sky.has<Corpse>(pilot), "those aboard die when it is destroyed");
    }

    // The Wagon, the Taming Cage, food from your stores, and clusters of spawns
    {
        World farm;
        Rng farmRng;
        farmRng.gen.seed(21);
        GameState farmState;
        Terrain& ground = terrain;
        ground.clearBlocked();
        const Vec2 mid = systems_detail::findSpawn(ground);
        const float cx = std::floor(mid.x) + 0.5f, cz = std::floor(mid.z) + 0.5f;
        placeBuiltProp(farm, ground, PropKind::Altar, cx - 8.0f, cz, 0.0f, FACTION_PLAYER);
        auto run = [&](float seconds) {
            for (int i = 0; i < (int)(seconds * 60.0f); ++i) {
                simStep(farm, ground, farmRng, farmState, 1.0f / 60.0f, false);
                takeEvents(farm);
            }
        };

        // A Wagon goes behind whoever pulls it, holds four times a Wheelbarrow, and slows all but a large animal
        const ObjectId hauler = spawnUnit(farm, ground, farmRng, FACTION_PLAYER, cx, cz);
        farm.get<Behaviors>(hauler)->rules.clear();
        const float pace = walkSpeedOf(farm, hauler, false);
        const ObjectId wagon = makeProp(farm, PropKind::Wagon, Transform{ cx + 1.0f, ground.heightAt(cx + 1.0f, cz), cz });
        check(farm.has<Storage>(wagon) && farm.get<Storage>(wagon)->slots.size() == 4 * (size_t)propInfo(PropKind::Wheelbarrow).slots,
              "a Wagon holds four times what a Wheelbarrow does");
        check(startPushing(farm, hauler, wagon), "a unit takes hold of a Wagon");
        wheelbarrowSystem(farm);
        check(farm.get<Transform>(wagon)->x < farm.get<Transform>(hauler)->x - 1.0f, "a Wagon goes behind whoever pulls it");
        check(std::fabs(walkSpeedOf(farm, hauler, false) - pace * PULLED_SPEED) < 1e-3f, "pulling a Wagon slows a unit to a tenth");
        stopPushing(farm, hauler);
        const ObjectId bear = spawnMob(farm, ground, farmRng, MobKind::Bear, mobFaction(MobKind::Bear), cx + 6.0f, cz + 6.0f);
        const ObjectId rabbit = spawnMob(farm, ground, farmRng, MobKind::Rabbit, mobFaction(MobKind::Rabbit), cx + 3.0f, cz + 3.0f);
        check(largeAnimal(farm, bear) && !largeAnimal(farm, rabbit) && !largeAnimal(farm, hauler), "a Bear is a large animal, a Rabbit isn't");
        farm.add(bear, Pushing{ wagon, 0.0f, cx + 6.0f, cz + 6.0f });
        check(vehiclePace(farm, bear) == 1.0f, "a large animal pulls a Wagon at its own pace");
        farm.remove<Pushing>(bear);
        farm.destroy(bear);

        // A Taming Cage of yours with a Rabbit in it: tamed in time, about a day for every 100 of its most health
        PropKind tamingCage = PropKind::Wagon;
        check(propKindNamed("TamingCage", tamingCage) && propInfo(tamingCage).taming && propInfo(tamingCage).cage, "props.txt has the Taming Cage");
        const ObjectId cage = placeBuiltProp(farm, ground, tamingCage, cx + 3.5f, cz + 3.5f, 0.0f, FACTION_PLAYER);
        *farm.get<Transform>(rabbit) = *farm.get<Transform>(cage);
        farm.add(rabbit, Occupying{ cage });
        if (Lock* lk = farm.get<Lock>(cage)) lk->locked = true;   // as putting it in does
        check(tamingCageOf(farm, rabbit) == cage, "the Rabbit is in the Taming Cage");
        const int rawMeat = mobInfo(MobKind::Bear).meat;
        check(isRawMeat(rawMeat), "a mob's meat is raw meat");
        check(std::fabs(tameHoursFor(100) - 24.0) < 1e-9 && std::fabs(tameHoursFor(300) - 72.0) < 1e-9, "taming takes a game day for every 100 most health");
        const double needed = tameHoursNeeded(farm, rabbit);
        tamingSystem(farm, needed * 0.5 * 3600.0);
        check(farm.has<Taming>(rabbit) && std::fabs(tamingProgress(farm, rabbit) - 0.5f) < 0.01f && factionOf(farm, rabbit) != FACTION_PLAYER,
              "halfway through its time the Rabbit is half tamed");
        tamingSystem(farm, needed * 0.6 * 3600.0);
        check(factionOf(farm, rabbit) == FACTION_PLAYER && farm.has<Commandable>(rabbit) && tamingCageOf(farm, rabbit) == NO_OBJECT &&
                  !farm.has<Taming>(rabbit) && !farm.get<Lock>(cage)->locked,
              "once its time is up it is yours to order, out of the opened cage");

        // Hungry with Eat on and no food carried: a unit of yours fetches food from your store and eats it
        const ObjectId hungry = spawnUnit(farm, ground, farmRng, FACTION_PLAYER, cx + 1.5f, cz - 2.5f);
        Needs& hn = *farm.get<Needs>(hungry);
        hn.hunger = hn.maxHunger / 5;
        const int before = hn.hunger;
        int food = 0;
        for (const ItemDef& d : itemDatabase()) {
            if (food == 0 && restoreAmount(d, UseEffect::Stat::Hunger) > 0) food = d.id;
        }
        const ObjectId pantry = placeBuiltProp(farm, ground, PropKind::Barrel, cx + 4.5f, cz - 3.5f, 0.0f, FACTION_PLAYER);
        farm.get<Storage>(pantry)->slots[0] = ItemStack{ food, 5 };
        run(30.0f);
        check(farm.get<Needs>(hungry)->hunger > before, "a hungry unit of yours eats food fetched from your store");

        // Clusters: as many spots as asked for, the first where it was asked, all kept apart on open ground
        const std::vector<Vec2> spots = clusterSpots(farm, ground, farmRng, cx, cz - 6.0f, 5, 1.0f, false);
        bool apart = spots.size() == 5 && spots[0].x == cx && spots[0].z == cz - 6.0f;
        for (size_t i = 0; i < spots.size(); ++i) {
            for (size_t j = i + 1; j < spots.size(); ++j) {
                apart = apart && std::hypot(spots[i].x - spots[j].x, spots[i].z - spots[j].z) >= 1.0f - 1e-4f;
            }
        }
        check(apart, "a cluster of 5 spawns is 5 spots kept apart, the first where it was asked");
    }

    // Factions: the Factionless, the rules for outsiders, and Deal with Downed
    {
        World fw;
        Rng frng;
        frng.gen.seed(33);
        GameState fstate;
        Terrain& ground = terrain;
        ground.clearBlocked();
        const Vec2 mid = systems_detail::findSpawn(ground);
        const float cx = std::floor(mid.x) + 0.5f, cz = std::floor(mid.z) + 0.5f;
        placeBuiltProp(fw, ground, PropKind::Altar, cx - 8.0f, cz, 0.0f, FACTION_PLAYER);
        auto run = [&](float seconds) {
            for (int i = 0; i < (int)(seconds * 60.0f); ++i) {
                simStep(fw, ground, frng, fstate, 1.0f / 60.0f, false);
                takeEvents(fw);
            }
        };
        const int red = 2;
        check(findFaction(red) && findFaction(red)->rules.factionless == StanceRule::Hostile, "factions.txt makes the Red Faction hostile to the Factionless");
        check(factionsHostile(fw, red, FACTION_FACTIONLESS) && !factionsHostile(fw, FACTION_PLAYER, FACTION_FACTIONLESS),
              "so they fight on sight; yours is neutral to them");
        check(!factionsAllied(fw, FACTION_FACTIONLESS, FACTION_FACTIONLESS) && factionsAllied(fw, FACTION_PLAYER, FACTION_PLAYER) &&
                  !factionAids(fw, FACTION_FACTIONLESS, FACTION_FACTIONLESS) && !factionCanClaim(FACTION_FACTIONLESS),
              "two Factionless units are not allies and don't help each other, and the Factionless hold no Altar");
        check(factionPlayerControlled(FACTION_PLAYER) && !factionPlayerControlled(red), "your faction is yours to control, the Red Faction runs itself");
        FactionRules written;
        written.downedBody = DownedBodyRule::FinishOff;
        written.recruit = RecruitRule::Both;
        written.markup = 40;
        FactionRules read;
        std::string err;
        check(parseFactionRules(factionRulesText(written), read, err) && read == written, "faction rules are written and read back the same");
        check(!parseFactionRules("downed=sideways", read, err), "an unknown rule is refused");

        // A Red Faction bear is one of theirs; a Kobold spawned with no faction is Factionless
        const ObjectId redBear = spawnMob(fw, ground, frng, MobKind::Bear, red, cx + 12.0f, cz + 12.0f);
        const ObjectId yourBear = spawnMob(fw, ground, frng, MobKind::Bear, FACTION_PLAYER, cx + 13.0f, cz + 12.0f);
        check(factionOf(fw, redBear) == red && fw.has<Mob>(redBear) && fw.has<Commandable>(yourBear) && !fw.has<Commandable>(redBear),
              "a mob may belong to any faction, and one of yours takes orders");
        fw.destroy(redBear);
        fw.destroy(yourBear);
        const ObjectId wildWolf = spawnMob(fw, ground, frng, MobKind::Wolf, FACTION_FACTIONLESS, cx + 14.0f, cz + 12.0f);
        check(factionOf(fw, wildWolf) == mobFaction(MobKind::Wolf) && factionName(factionOf(fw, wildWolf)) == factionName(FACTION_FACTIONLESS),
              "a Factionless mob is a wild one, shown as Factionless");
        fw.destroy(wildWolf);
        check(factionsAllied(fw, mobFaction(MobKind::Gorlicorn), mobFaction(MobKind::Rabbit)) &&
                  factionsAllied(fw, mobFaction(MobKind::Wolf), mobFaction(MobKind::Wolf)) &&
                  !factionsAllied(fw, mobFaction(MobKind::Wolf), mobFaction(MobKind::Rabbit)),
              "wild animals of one kind are allies, and so are a Gorlicorn and Rabbits; a Wolf and a Rabbit are not");
        int bossesColored = 0, bosses = 0;
        for (int k = 1; k <= mobKindCount(); ++k) {
            if (!isBossMob((MobKind)k)) continue;
            const ObjectId boss = spawnMob(fw, ground, frng, (MobKind)k, FACTION_FACTIONLESS, cx + 16.0f, cz + 12.0f);
            ++bosses;
            const Coloration* c = fw.get<Coloration>(boss);
            bossesColored += c && c->hue >= 0 && c->hue < 360 ? 1 : 0;
            fw.destroy(boss);
        }
        const ObjectId plainWolf = spawnMob(fw, ground, frng, MobKind::Wolf, FACTION_FACTIONLESS, cx + 14.0f, cz + 12.0f);
        check(bosses > 0 && bossesColored == bosses && !fw.has<Coloration>(plainWolf), "every boss spawns with its own Coloration, and other mobs with none");
        fw.destroy(plainWolf);
        LevelData level;
        level.map = "test";
        LevelObject bearRow;
        bearRow.type = LevelObject::Type::Mob;
        bearRow.kind = idx(MobKind::Bear);
        bearRow.owner = red;
        level.objects.push_back(bearRow);
        LevelData levelBack;
        check(levelFromText(levelToText(level), levelBack, err) && levelBack.objects.size() == 1 && levelBack.objects[0].owner == red,
              "a level file keeps a mob's faction");

        // Deal with Downed: your rules say cage them and take their weapons
        FactionRules mine = factionRules(fw, FACTION_PLAYER);
        mine.downedBody = DownedBodyRule::Cage;
        mine.downedTake = DownedTakeRule::Weapons;
        setFactionRules(fw, FACTION_PLAYER, mine);
        const ObjectId cage = makeProp(fw, PropKind::PrisonerCage, Transform{ cx + 3.5f, ground.heightAt(cx + 3.5f, cz - 3.5f), cz - 3.5f });
        fw.add(cage, Owner{ FACTION_PLAYER });
        int food = 0, sword = 0;
        for (const ItemDef& d : itemDatabase()) {
            if (food == 0 && restoreAmount(d, UseEffect::Stat::Hunger) > 0) food = d.id;
            if (sword == 0 && d.equipSlot == "main_hand") sword = d.id;
        }
        const ObjectId enemy = spawnUnit(fw, ground, frng, red, cx + 2.5f, cz + 2.5f);
        Inventory& pack = *fw.get<Inventory>(enemy);
        pack = Inventory{};
        addToInventory(pack, food, 3);
        if (sword) fw.get<Equipment>(enemy)->items[GearSlot::MainHand] = sword;
        fw.get<Behaviors>(enemy)->rules.clear();
        knockOut(fw, enemy);
        const ObjectId warden = spawnUnit(fw, ground, frng, FACTION_PLAYER, cx, cz);
        BehaviorRule deal;
        deal.kind = BehaviorKind::DealWithDowned;
        fw.get<Behaviors>(warden)->rules = { deal };
        *fw.get<Inventory>(warden) = Inventory{};
        run(40.0f);
        check(inPrisonerCage(fw, enemy) && fw.get<Lock>(cage)->locked, "Deal with Downed locks a knocked-out enemy in your Prisoner Cage");
        check(fw.get<Equipment>(enemy)->items[GearSlot::MainHand] == 0 && carriedCount(*fw.get<Inventory>(warden), food) == 3 &&
                  (sword == 0 || carriedCount(*fw.get<Inventory>(warden), sword) == 1),
              "after taking its weapons and food");

        // Carry out of the claim, taking nothing
        mine.downedBody = DownedBodyRule::CarryOut;
        mine.downedTake = DownedTakeRule::Nothing;
        setFactionRules(fw, FACTION_PLAYER, mine);
        const ObjectId intruder = spawnUnit(fw, ground, frng, red, cx + 3.5f, cz + 3.5f);
        fw.get<Behaviors>(intruder)->rules.clear();
        knockOut(fw, intruder);
        run(80.0f);
        const Transform* out = fw.get<Transform>(intruder);
        check(!fw.has<Carried>(intruder) && claimAt(claimsOf(fw), out->x, out->z, FACTION_PLAYER) == nullptr,
              "Deal with Downed carries a knocked-out enemy out of your claim");

        // A trespasser on your land, with your rule to attack them
        mine.trespass = TrespassRule::Attack;
        setFactionRules(fw, FACTION_PLAYER, mine);
        const ObjectId stranger = spawnUnit(fw, ground, frng, FACTION_FACTIONLESS, cx - 2.5f, cz + 1.5f);
        fw.get<Behaviors>(stranger)->rules.clear();
        check(!hostileToPlayer(fw, stranger), "a Factionless unit is not hostile to you at first");
        run(1.0f);
        check(hostileToPlayer(fw, stranger), "your rule to attack trespassers turns one standing in your claim against you");
        fw.destroy(stranger);

        // A Factionless unit who stays a minute in your claim joins you, if your rules take them in
        mine.trespass = TrespassRule::Ignore;
        mine.recruit = RecruitRule::Factionless;
        setFactionRules(fw, FACTION_PLAYER, mine);
        const ObjectId drifter = spawnUnit(fw, ground, frng, FACTION_FACTIONLESS, cx - 3.5f, cz - 1.5f);
        fw.get<Behaviors>(drifter)->rules.clear();
        run(RECRUIT_SECONDS + 2.0f);
        check(factionOf(fw, drifter) == FACTION_PLAYER && fw.has<Citizen>(drifter), "a Factionless unit who lingers in your claim joins you as a citizen");

        // A boss part tamed in your Taming Cage: its coloring and its taming hours come back from a save
        PropKind tamingKind = PropKind::Wagon;
        propKindNamed("TamingCage", tamingKind);
        const ObjectId bossCage = placeBuiltProp(fw, ground, tamingKind, cx + 5.5f, cz - 5.5f, 0.0f, FACTION_PLAYER);
        MobKind bossKind = MobKind::None;
        for (int k = 1; k <= mobKindCount() && bossKind == MobKind::None; ++k) {
            if (isBossMob((MobKind)k)) bossKind = (MobKind)k;
        }
        const ObjectId boss = spawnMob(fw, ground, frng, bossKind, FACTION_FACTIONLESS, cx + 5.5f, cz - 5.5f);
        *fw.get<Transform>(boss) = *fw.get<Transform>(bossCage);
        fw.add(boss, Occupying{ bossCage });
        fw.get<Coloration>(boss)->hue = 123;
        tamingSystem(fw, 3.0 * 3600.0);
        check(fw.has<Taming>(boss) && std::fabs(fw.get<Taming>(boss)->hours - 3.0) < 1e-6, "a boss can be tamed too");

        // Props as they come into being: a tree faces a way of its own by where it stands, a chair the way it was put
        const ObjectId tree1 = makeProp(fw, PropKind::Tree, Transform{ cx + 20.5f, 0.0f, cz + 3.5f });
        const ObjectId tree2 = makeProp(fw, PropKind::Tree, Transform{ cx + 22.5f, 0.0f, cz + 7.5f });
        const ObjectId tree1Again = makeProp(fw, PropKind::Tree, Transform{ cx + 20.5f, 0.0f, cz + 3.5f }, 1.0f);
        const ObjectId chair = makeProp(fw, PropKind::Chair, Transform{ cx + 24.5f, ground.heightAt(cx + 24.5f, cz + 3.5f), cz + 3.5f }, 1.25f);
        check(propInfo(PropKind::Tree).randomYaw && propInfo(PropKind::Debris).randomYaw && propInfo(PropKind::Boulder).randomYaw &&
                  !propInfo(PropKind::Chair).randomYaw,
              "props.txt turns trees, boulders and debris a way of their own, and not chairs");
        check(fw.get<Prop>(tree1)->yaw != fw.get<Prop>(tree2)->yaw && fw.get<Prop>(tree1)->yaw == fw.get<Prop>(tree1Again)->yaw &&
                  fw.get<Prop>(chair)->yaw == 1.25f,
              "a new tree faces a way of its own, the same for the same spot; a chair faces as it was placed");
        fw.destroy(tree1Again);
        // The Adjust tool's change to how a prop is drawn: kept in range, and kept by saves and level files
        PropVisual wild;
        wild.dx = 3.0f;
        wild.sy = 9.0f;
        wild.rx = 540.0f;
        const PropVisual kept = clampPropVisual(wild);
        check(kept.dx == PROP_VISUAL_MOVE && kept.sy == PROP_VISUAL_SCALE_MAX && std::fabs(kept.rx - 180.0f) < 1e-3f && PropVisual{}.plain(),
              "an adjusted prop moves half a tile at most, stretches from half to one and a half, and turns any way");
        PropVisual upsideDown;
        upsideDown.rx = 180.0f;
        upsideDown.dx = 0.25f;
        upsideDown.sz = 1.5f;
        fw.add(chair, upsideDown);
        LevelData propLevel = captureLevel(fw, "test");
        LevelData propLevelBack;
        bool levelKeeps = levelFromText(levelToText(propLevel), propLevelBack, err);
        bool levelFound = false;
        for (const LevelObject& o : propLevelBack.objects) {
            levelFound = levelFound || (o.type == LevelObject::Type::Prop && o.kind == idx(PropKind::Chair) && std::fabs(o.visual.rx - 180.0f) < 0.1f &&
                                        std::fabs(o.visual.dx - 0.25f) < 1e-3f && std::fabs(o.visual.sz - 1.5f) < 1e-3f);
        }
        check(levelKeeps && levelFound, "a level file keeps how a prop was adjusted");
        LevelData oldShop;
        check(levelFromText("format 1\nmap test\nnext 2\nprop 1 Shop 5.50 5.50 0.000 0\n", oldShop, err) && oldShop.objects.size() == 1 &&
                  oldShop.objects[0].kind == idx(PropKind::Stall),
              "a level's Shop, a prop that is gone, loads as a Stall");
        LevelData oldProps;
        check(levelFromText("format 1\nmap test\nnext 3\nprop 1 Wall 5.50 5.50 0.000 -1\nprop 2 MetalTable 7.50 5.50 0.000 -1\n", oldProps, err) &&
                  oldProps.objects.size() == 2 && oldProps.objects[0].kind == idx(PropKind::Debris) &&
                  oldProps.objects[1].kind == idx(PropKind::ResearchTable),
              "a level's Barrier loads as Debris, and its Metal Table as a Research Table");
        check(breakableKind(PropKind::Chair) && breakableKind(PropKind::Tree) && !breakableKind(PropKind::Debris),
              "any prop but Debris can be broken from the right-click menu");
        check(propInfo(PropKind::Campfire).fire && propInfo(PropKind::StandingTorch).fire && propInfo(PropKind::Altar).fire &&
                  !propInfo(PropKind::Chair).fire && propInfo(PropKind::StandingTorch).fireY > 1.0f,
              "props.txt gives the Campfire, Standing Torch and Altar a fire, at its place on each");

        // A Bedroll: only by a Campfire or in an Altar's claim, and Debris once it has neither
        const float farX = cx + 30.5f, farZ = cz + 0.5f;
        check(propInfo(PropKind::Bedroll).needsCamp && checkBuildSpot(fw, ground, PropKind::Bedroll, farX, farZ, 0) == BuildCheck::NoCamp &&
                  checkBuildSpot(fw, ground, PropKind::Bedroll, cx - 2.5f, cz + 6.5f, 0) != BuildCheck::NoCamp,
              "a Bedroll can't be laid away from a camp, and can in your Altar's claim");
        const ObjectId campfire = placeBuiltProp(fw, ground, PropKind::Campfire, farX + 2.0f, farZ, 0.0f, FACTION_PLAYER);
        check(checkBuildSpot(fw, ground, PropKind::Bedroll, farX, farZ, 0) != BuildCheck::NoCamp, "nor away from it by a Campfire");
        const ObjectId bedroll = placeBuiltProp(fw, ground, PropKind::Bedroll, farX, farZ, 0.0f, FACTION_PLAYER);
        run(2.0f);
        check(fw.has<Prop>(bedroll) && fw.get<Prop>(bedroll)->kind == PropKind::Bedroll, "a Bedroll by its Campfire stays");
        destroyBuilding(fw, campfire);
        run(2.0f);
        bool bedrollLeft = fw.has<Prop>(bedroll) && fw.get<Prop>(bedroll)->kind == PropKind::Bedroll;
        int debrisThere = 0;
        for (ObjectId e : fw.view<Prop, Transform>()) {
            const Transform* dt = fw.get<Transform>(e);
            debrisThere += fw.get<Prop>(e)->kind == PropKind::Debris && std::fabs(dt->x - farX) < 0.1f && std::fabs(dt->z - farZ) < 0.1f ? 1 : 0;
        }
        check(!bedrollLeft && debrisThere == 1, "a Bedroll whose Campfire is gone falls to Debris");

        // Recipes, research and Crafting Orders
        PropKind weaponBench = PropKind::Wagon;
        const int knifeRecipe = recipeNamed("Knife"), swordRecipe = recipeNamed("ShortSword");
        int gizmo = 0;
        for (const ItemDef& d : itemDatabase()) {
            if (isGizmo(d.id)) gizmo = d.id;
        }
        check(propKindNamed("WeaponBench", weaponBench) && propInfo(weaponBench).advanced && propInfo(PropKind::Stove).advanced && knifeRecipe >= 0 &&
                  swordRecipe >= 0 && recipes()[(size_t)swordRecipe].station == weaponBench && gizmo > 0,
              "recipes.txt has recipes for the Weapon Workbench, an Advanced Crafting prop, and there is a Gizmo");
        bool cooks = false;
        for (const Recipe& r : recipes()) cooks = cooks || (r.station == PropKind::Stove && r.gizmos == 0 && r.count == 5);
        check(cooks, "every cookable item is a Stove recipe, five at a time");
        check(recipeUnlocked(fw, FACTION_PLAYER, knifeRecipe) && !recipeUnlocked(fw, FACTION_PLAYER, swordRecipe), "a recipe that costs Gizmos is locked at first");
        const ObjectId desk = placeBuiltProp(fw, ground, PropKind::ResearchTable, cx - 4.5f, cz - 6.5f, 0.0f, FACTION_PLAYER);
        check(fw.has<Storage>(desk) && propAcceptsItem(PropKind::ResearchTable, gizmo) && !propAcceptsItem(PropKind::ResearchTable, food),
              "a Research Table holds Gizmos and nothing else");
        check(!researchRecipe(fw, desk, swordRecipe, FACTION_PLAYER), "with no Gizmos in it, nothing is researched");
        addToStore(*fw.get<Storage>(desk), gizmo, 3);
        check(researchRecipe(fw, desk, swordRecipe, FACTION_PLAYER) && recipeUnlocked(fw, FACTION_PLAYER, swordRecipe) && gizmosIn(fw, desk) == 2 &&
                  !recipeUnlocked(fw, red, swordRecipe),
              "Gizmos in a Research Table are spent to unlock a recipe for your faction, and not for another's");
        const ObjectId benchProp = placeBuiltProp(fw, ground, weaponBench, cx - 2.5f, cz - 6.5f, 0.0f, FACTION_PLAYER);
        check(fw.has<Output>(benchProp) && fw.has<Storage>(benchProp) && craftBlock(fw, benchProp) != nullptr, "a Weapon Workbench starts with no order");
        check(!setCraftOrder(fw, benchProp, recipeNamed("WoodArrows"), 0) && setCraftOrder(fw, benchProp, knifeRecipe, 1),
              "its order takes a recipe of its own, and not another prop's");
        const int ingot = recipes()[(size_t)knifeRecipe].inputs[0].item;
        const ObjectId pantry = placeBuiltProp(fw, ground, PropKind::Barrel, cx - 0.5f, cz - 6.5f, 0.0f, FACTION_PLAYER);
        fw.get<Storage>(pantry)->slots[0] = ItemStack{ ingot, 4 };
        const ObjectId smith = spawnUnit(fw, ground, frng, FACTION_PLAYER, cx - 2.5f, cz - 4.5f);
        *fw.get<Inventory>(smith) = Inventory{};
        Stats skilled = *fw.get<Stats>(smith);
        skilled.xp[idx(StatId::Forging)] = xpForLevel(MAX_LEVEL);
        fw.add(smith, skilled);
        BehaviorRule craftTask;
        craftTask.kind = BehaviorKind::AutoCraft;
        fw.get<Behaviors>(smith)->rules = { craftTask };
        run(300.0f);
        const int knifeItem = recipes()[(size_t)knifeRecipe].item;
        // Made, and perhaps carried out to a store already by a citizen hauling: wherever it is now, one was made
        int knives = factionStock(fw, FACTION_PLAYER, knifeItem);
        for (ObjectId u : fw.view<Entity, Inventory>()) {
            if (factionOf(fw, u) == FACTION_PLAYER) knives += carriedCount(*fw.get<Inventory>(u), knifeItem);
        }
        check(knives >= 1 && countInSlots(fw.get<Storage>(pantry)->slots.data(), (int)fw.get<Storage>(pantry)->slots.size(), ingot) < 4,
              "Auto-Craft fetches the ingredients from your store and works the prop into a batch");
        // One batch at a time: with its ingredients in it and a batch in its output, nothing more is made
        fw.get<Behaviors>(smith)->rules.clear();
        fw.remove<CraftJob>(smith);
        fw.remove<Working>(smith);
        *fw.get<Output>(benchProp) = Output{};
        *fw.get<Storage>(benchProp) = Storage{};
        fw.get<CraftOrder>(benchProp)->keep = 0;
        addToStore(*fw.get<Storage>(benchProp), ingot, 2);
        check(craftBlock(fw, benchProp) == nullptr, "with its recipe's ingredients in it, the prop can be worked");
        finishCraft(fw, benchProp, smith);
        check(countInSlots(fw.get<Output>(benchProp)->slots.data(), STORAGE_SLOTS, knifeItem) == 1 &&
                  countInSlots(fw.get<Storage>(benchProp)->slots.data(), (int)fw.get<Storage>(benchProp)->slots.size(), ingot) == 1,
              "a batch takes its ingredients and goes into the prop's output");
        check(craftBlock(fw, benchProp) != nullptr && std::string(craftBlock(fw, benchProp)) == "MISC_CRAFT_OUTPUT_FULL",
              "nothing more is made until that is carried out");
        *fw.get<Inventory>(smith) = Inventory{};
        check(takeCraftOutput(fw, smith, benchProp) == 1 && outputEmpty(fw, benchProp) && carriedCount(*fw.get<Inventory>(smith), knifeItem) == 1 &&
                  craftBlock(fw, benchProp) == nullptr,
              "once its output is carried out, it can be worked again");
        if (Taming* bossTaming = fw.get<Taming>(boss)) bossTaming->hours = 3.0;   // as it was before the hours above went by
        fw.get<CraftOrder>(benchProp)->keep = 3;
        const std::string tameFile = "roundtrip_taming.sav";
        check(writeSaveFile(tameFile, captureWorld(fw, gs, cam, frng, 1000, "test", map)), "the taming game is saved");
        SaveData tameData;
        World tameBack;
        Rng tameRng;
        check(readSaveFile(tameFile, tameData), "and read");
        restoreWorld(tameBack, terrain, tameRng, tameData);
        bool found = false;
        for (ObjectId u : tameBack.view<Mob, Coloration>()) {
            const Taming* tm = tameBack.get<Taming>(u);
            found = found || (tameBack.get<Coloration>(u)->hue == 123 && tm && std::fabs(tm->hours - 3.0) < 1e-3);
        }
        check(found, "a boss's Coloration and its hours of taming come back from a save");
        bool orderBack = false;
        for (ObjectId e : tameBack.view<CraftOrder, Prop>()) {
            const CraftOrder* co = tameBack.get<CraftOrder>(e);
            orderBack = orderBack || (tameBack.get<Prop>(e)->kind == weaponBench && co->recipe == knifeRecipe && co->keep == 3);
        }
        check(orderBack && recipeUnlocked(tameBack, FACTION_PLAYER, swordRecipe) && !recipeUnlocked(tameBack, red, swordRecipe),
              "a Crafting Order and the recipes a faction has researched come back from a save");
        bool chairBack = false, treeBack = false;
        for (ObjectId e : tameBack.view<Prop, Transform>()) {
            const Prop* pr = tameBack.get<Prop>(e);
            const PropVisual* pv = tameBack.get<PropVisual>(e);
            chairBack = chairBack || (pr->kind == PropKind::Chair && pv && pv->rx == 180.0f && pv->dx == 0.25f && pv->sz == 1.5f && pr->yaw == 1.25f);
            treeBack = treeBack || (pr->kind == PropKind::Tree && pr->yaw == fw.get<Prop>(tree1)->yaw);
        }
        check(chairBack && treeBack, "an adjusted prop and the way a tree faces come back from a save");
        std::remove(tameFile.c_str());
    }

    // Buildings: a foundation and its pieces are blueprints, supplied with logs and built by the two building tasks
    {
        World town;
        Rng townRng;
        townRng.gen.seed(3);
        GameState townState;
        Terrain& ground = terrain;
        ground.clearBlocked();
        const Vec2 mid = systems_detail::findSpawn(ground);
        const int cx = (int)std::floor(mid.x), cz = (int)std::floor(mid.z);
        placeBuiltProp(town, ground, PropKind::Altar, cx - 8.5f, cz + 0.5f, 0.0f, FACTION_PLAYER);
        check(checkFoundation(town, ground, cx, cz, cx + 1, cz + 4) == PieceCheck::TooSmall, "a foundation under 3 tiles a side is refused");
        check(checkFoundation(town, ground, cx, cz, cx + 64, cz + 4) == PieceCheck::TooBig, "a foundation over 64 tiles a side is refused");
        check(checkFoundation(town, ground, cx + 200, cz, cx + 204, cz + 4) == PieceCheck::OutsideClaim, "a foundation outside your claim is refused");
        const int x0 = cx, z0 = cz, x1 = cx + 4, z1 = cz + 4;   // 5 x 5
        check(checkFoundation(town, ground, x0, z0, x1, z1) == PieceCheck::Ok, "a 5 x 5 foundation in your claim may be laid");
        const ObjectId base = placeFoundation(town, ground, x0, z0, x1, z1);
        check(town.has<Blueprint>(base) && materialsNeeded(town, base) == 1, "it starts as a blueprint wanting one log");
        check(checkPiece(town, PropKind::WoodWall, x0, z0) == PieceCheck::FoundationUnfinished, "nothing is built on an unfinished foundation");

        // A builder with logs and both tasks
        const ObjectId builder = spawnUnit(town, ground, townRng, FACTION_PLAYER, cx - 3.5f, cz + 0.5f);
        Behaviors& bb = *town.get<Behaviors>(builder);
        bb.rules.clear();
        BehaviorRule construct;
        construct.kind = BehaviorKind::AutoConstruct;
        bb.rules = { construct };   // Auto-Construct hauls the materials too
        addToInventory(*town.get<Inventory>(builder), woodLogItem(), 20);
        Stats handy = *town.get<Stats>(builder);
        handy.xp[idx(StatId::Strength)] = xpForLevel(50);
        handy.xp[idx(StatId::Carpentry)] = xpForLevel(MAX_LEVEL);
        town.add(builder, handy);
        // More logs in a barrel of yours, for it to fetch
        const ObjectId barrel = placeBuiltProp(town, ground, PropKind::Barrel, cx - 4.5f, cz + 3.5f, 0.0f, FACTION_PLAYER);
        if (!town.has<Storage>(barrel)) town.add(barrel, Storage{});
        town.get<Storage>(barrel)->slots[0] = ItemStack{ woodLogItem(), 60 };
        town.get<Storage>(barrel)->slots[1] = ItemStack{ woodLogItem(), 60 };
        auto run = [&](float seconds) {
            for (int i = 0; i < (int)(seconds * 60.0f); ++i) {
                simStep(town, ground, townRng, townState, 1.0f / 60.0f, false);
                takeEvents(town);
            }
        };
        run(30.0f);
        check(!town.has<Blueprint>(base), "the builder supplies the foundation and builds it");
        check(std::hypot(town.get<Transform>(builder)->x - mid.x, town.get<Transform>(builder)->z - mid.z) < 12.0f,
              "it works from up to five tiles away, without walking onto every tile");

        check(checkPiece(town, PropKind::WoodDoor, x0 + 1, z1) == PieceCheck::Ok && checkPiece(town, PropKind::WoodDoor, x0 + 2, z0 + 2) == PieceCheck::Ok,
              "a door goes anywhere on a foundation a wall can");
        check(checkPiece(town, PropKind::Ladder, x0 + 1, z0 + 2) == PieceCheck::Ok &&
              checkPiece(town, PropKind::UpperFloor, x0, z0) == PieceCheck::Ok, "a ladder or floor may go in before the edges have walls");
        // Walls round the edge, a door in the entrance, a ladder inside and an upper floor along one side
        std::vector<ObjectId> walls;
        for (int x = x0; x <= x1; ++x) {
            for (int z = z0; z <= z1; ++z) {
                const bool edge = x == x0 || x == x1 || z == z0 || z == z1;
                if (!edge || (x == x0 + 2 && z == z1)) continue;
                check(checkPiece(town, PropKind::WoodWall, x, z) == PieceCheck::Ok, "a wall may go on the edge");
                walls.push_back(placePiece(town, ground, PropKind::WoodWall, x, z));
            }
        }
        const ObjectId door = placePiece(town, ground, PropKind::WoodDoor, x0 + 2, z1);
        check(checkPiece(town, PropKind::Ladder, x0 + 1, z0 + 2) == PieceCheck::Ok, "a ladder may go in");
        const ObjectId ladder = placePiece(town, ground, PropKind::Ladder, x0 + 1, z0 + 2);
        std::vector<ObjectId> floors;
        for (int z = z0; z <= z1; ++z) floors.push_back(placePiece(town, ground, PropKind::UpperFloor, x0, z));
        check(checkPiece(town, PropKind::WoodWall, x0, z0) == PieceCheck::Taken, "one piece to a tile");
        check(wallShapeOf(town, walls[0]).shape == 3, "a corner wall takes the corner shape");
        const WallShape side = wallShapeOf(town, walls[1]);
        check(side.shape == 2, "a wall between two walls is straight");
        // A unit standing on a wall's tile: the wall waits, and the unit is asked to move
        const ObjectId loafer = spawnUnit(town, ground, townRng, FACTION_PLAYER, x1 + 0.5f, z0 + 2.5f);
        town.get<Behaviors>(loafer)->rules.clear();
        ObjectId underLoafer = NO_OBJECT;
        for (ObjectId wl : walls) {
            const Transform* tr = town.get<Transform>(wl);
            if ((int)std::floor(tr->x) == x1 && (int)std::floor(tr->z) == z0 + 2) underLoafer = wl;
        }
        town.get<Blueprint>(underLoafer)->supplied = materialsNeeded(town, underLoafer);
        town.get<Blueprint>(underLoafer)->progress = 1.0;
        check(!finishBlueprint(town, ground, underLoafer) && town.get<Blueprint>(underLoafer)->retryIn > 0.0f,
              "a wall with a unit on its tile isn't finished yet");
        check(town.has<SoloMove>(loafer), "the unit in the way is asked to move");
        run(120.0f);
        int unfinished = 0;
        for (ObjectId e : town.view<Blueprint>()) unfinished += 1 + 0 * (int)e;
        std::printf("note: building: %d pieces still unfinished\n", unfinished);
        check(unfinished == 0, "every piece is supplied and built in time, with logs fetched from a barrel");
        check(countBarrelLogs(town, barrel) < 120, "logs were fetched from the barrel");
        const Transform* lt = town.get<Transform>(loafer);
        check((int)std::floor(lt->x) != x1 || (int)std::floor(lt->z) != z0 + 2, "the unit moved off the wall's tile");
        check(!ground.walkable(x0, z0 + 1) && ground.walkable(x0 + 2, z1) && ground.walkable(x0 + 2, z0 + 2),
              "finished walls close their tiles; the doorway and the inside stay open");
        check(town.has<Lock>(door) && town.get<Lock>(door)->locked, "a finished door is locked");
        check(doorLets(town, door, builder), "your own units go through your door");

        // Up the ladder, out of reach of melee from the ground
        Transform* bt = town.get<Transform>(builder);
        bt->x = x0 + 1.5f;
        bt->z = z0 + 3.5f;
        town.get<Behaviors>(builder)->rules.clear();
        check(canClimbUp(town, ladder) && startClimb(town, ground, builder, ladder), "a unit by a ladder climbs it");
        run(2.0f);
        check(town.has<Upstairs>(builder) && town.get<Transform>(builder)->y > ground.heightAt(bt->x, bt->z) + 2.0f,
              "after climbing it stands on the upper floor");
        int foes = -1;
        for (const FactionDef& f : factions()) {
            if (f.id != FACTION_PLAYER && !isMobFaction(f.id) && factionsHostile(town, f.id, FACTION_PLAYER)) {
                foes = f.id;
                break;
            }
        }
        const ObjectId raider = spawnUnit(town, ground, townRng, foes, x0 + 2.5f, z1 + 1.5f);
        town.add(raider, Equipment{});
        check(!canStrike(town, raider, builder), "melee from the ground can't reach the upper floor");
        town.add(raider, AtDoor{ door, 0.0f });
        bool smashed = false;
        for (int i = 0; i < 60 * 120 && !smashed; ++i) {
            town.get<AtDoor>(raider) ? (void)(town.get<AtDoor>(raider)->since = 0.0f) : (void)town.add(raider, AtDoor{ door, 0.0f });
            simStep(town, ground, townRng, townState, 1.0f / 60.0f, false);
            takeEvents(town);
            smashed = !town.has<Prop>(door) || town.get<Prop>(door)->kind != PropKind::WoodDoor;
        }
        check(smashed, "a hostile unit that can't pick the lock smashes the door");
        check(town.get<Health>(walls[1])->max == 2.0 * DOOR_HEALTH, "a wall takes twice the breaking a door does");

        // The buildings come back from a save
        const MapChoice townMap{ "packed", "test", "test", "" };
        const std::string townFile = "roundtrip_town.sav";
        check(writeSaveFile(townFile, captureWorld(town, townState, Camera{}, townRng, 1000, "town", townMap)), "a game with buildings is saved");
        SaveData townData;
        check(readSaveFile(townFile, townData), "and read");
        std::remove(townFile.c_str());
        World again;
        Rng againRng;
        ground.clearBlocked();
        restoreWorld(again, ground, againRng, townData);
        int foundations = 0, upstairs = 0;
        for (ObjectId e : again.view<Foundation>()) foundations += 1;
        for (ObjectId e : again.view<Upstairs>()) upstairs += again.has<Foundation>(again.get<Upstairs>(e)->foundation) ? 1 : 0;
        check(foundations >= 9 && upstairs == 1, "the foundation tiles and who is upstairs come back");
        check(!ground.walkable(x0, z0 + 1), "the walls close their tiles again after a reload");

        // With every wall and door on the ground floor gone, the upper floor falls in, and those on it come down
        for (ObjectId e : again.view<Prop>()) {
            const PropInfo& info = propInfo(again.get<Prop>(e)->kind);
            if ((info.wallRule || info.door) && again.has<Prop>(e)) destroyBuilding(again, e);
        }
        GameState againState;
        for (int i = 0; i < 10; ++i) simStep(again, ground, againRng, againState, 1.0f / 60.0f, false);
        int floorsLeft = 0;
        for (ObjectId e : again.view<Prop>()) floorsLeft += propInfo(again.get<Prop>(e)->kind).upperFloor ? 1 : 0;
        check(floorsLeft == 0 && again.view<Upstairs>().empty() && ground.walkable(x0, z0 + 1),
              "with no walls or doors left, the upper floors collapse and the walls' tiles open");
        ground.clearBlocked();
    }

    // A building goes up floor by floor: walls, a ladder and an upper floor on each, up to the top floor
    {
        World tower;
        Rng towerRng;
        towerRng.gen.seed(5);
        GameState towerState;
        Terrain& ground = terrain;
        ground.clearBlocked();
        const Vec2 mid = systems_detail::findSpawn(ground);
        const int cx = (int)std::floor(mid.x), cz = (int)std::floor(mid.z);
        placeBuiltProp(tower, ground, PropKind::Altar, cx - 8.5f, cz + 0.5f, 0.0f, FACTION_PLAYER);
        const int x0 = cx, z0 = cz, x1 = cx + 4, z1 = cz + 4;
        auto finish = [&](ObjectId e) {
            if (e == NO_OBJECT || !tower.has<Blueprint>(e)) return;
            tower.get<Blueprint>(e)->supplied = materialsNeeded(tower, e);
            tower.get<Blueprint>(e)->progress = 1.0;
            finishBlueprint(tower, ground, e);
        };
        placeFoundation(tower, ground, x0, z0, x1, z1);
        for (ObjectId e : tower.view<Foundation, Blueprint>()) finish(e);
        auto ladderX = [&](int level) { return level % 2 == 0 ? x0 + 1 : x0 + 3; };
        std::vector<ObjectId> pending;
        auto put = [&](PropKind kind, int x, int z, int level, bool instant) {
            const PieceCheck c = checkPiece(tower, kind, x, z, FACTION_PLAYER, level);
            if (c != PieceCheck::Ok) {
                std::printf("note: floor %d: %s at %d,%d is refused (%s)\n", level, propInfo(kind).nameKey.c_str(), x - x0, z - z0,
                            pieceRefusalKey(c));
                return false;
            }
            const ObjectId p = placePiece(tower, ground, kind, x, z, FACTION_PLAYER, BuildMaterial::Wood, level);
            if (instant) finish(p);
            else pending.push_back(p);
            return p != NO_OBJECT;
        };
        // One floor's walls and door; with withFloor, its ladder and the upper floor over it too
        auto storey = [&](int level, bool instant, bool withFloor) {
            bool ok = true;
            for (int x = x0; x <= x1; ++x) {
                for (int z = z0; z <= z1; ++z) {
                    if (x != x0 && x != x1 && z != z0 && z != z1) continue;
                    ok = put(x == x0 + 2 && z == z1 ? PropKind::WoodDoor : PropKind::WoodWall, x, z, level, instant) && ok;
                }
            }
            if (!withFloor) return ok;
            ok = put(PropKind::Ladder, ladderX(level), z0 + 2, level, instant) && ok;
            for (int x = x0; x <= x1; ++x) {
                for (int z = z0; z <= z1; ++z) {
                    if (x == ladderX(level) && z == z0 + 2) continue;
                    ok = put(PropKind::UpperFloor, x, z, level, instant) && ok;
                }
            }
            return ok;
        };
        check(storey(0, true, true) && storey(1, true, true), "the ground floor and the first upper floor are built");
        buildingSystem(tower, ground, 0.0f);
        // The second upper floor's walls are left to a builder on the ground, with logs in a barrel
        check(storey(2, false, false), "walls go on the second upper floor");
        const ObjectId wall2 = pending.empty() ? NO_OBJECT : pending.front();
        const Transform* w2 = wall2 != NO_OBJECT ? tower.get<Transform>(wall2) : nullptr;
        check(w2 && storeyOf(tower, wall2) == 2 && std::fabs(w2->y - (ground.heightAt(w2->x, w2->z) + storeyBaseHeight(2))) < 0.05f,
              "a wall on the second upper floor stands on that floor");
        const ObjectId builder = spawnUnit(tower, ground, towerRng, FACTION_PLAYER, cx - 3.5f, cz + 0.5f);
        BehaviorRule construct;
        construct.kind = BehaviorKind::AutoConstruct;
        tower.get<Behaviors>(builder)->rules = { construct };
        Stats handy = *tower.get<Stats>(builder);
        handy.xp[idx(StatId::Strength)] = xpForLevel(50);
        handy.xp[idx(StatId::Carpentry)] = xpForLevel(MAX_LEVEL);
        tower.add(builder, handy);
        const ObjectId barrel = placeBuiltProp(tower, ground, PropKind::Barrel, cx - 4.5f, cz + 3.5f, 0.0f, FACTION_PLAYER);
        if (!tower.has<Storage>(barrel)) tower.add(barrel, Storage{});
        tower.get<Storage>(barrel)->slots[0] = ItemStack{ woodLogItem(), 60 };
        for (int i = 0; i < 60 * 480 && !tower.view<Blueprint>().empty(); ++i) {
            simStep(tower, ground, towerRng, towerState, 1.0f / 60.0f, false);
            takeEvents(tower);
        }
        const int left = (int)tower.view<Blueprint>().size();
        std::printf("note: tower: %d pieces on the second upper floor still unfinished, the builder on floor %d\n", left, storeyOf(tower, builder));
        check(left == 0, "a builder climbs the ladders and builds the walls on the second upper floor");
        pending.clear();
        bool upward = put(PropKind::Ladder, ladderX(2), z0 + 2, 2, true);
        for (int x = x0; x <= x1; ++x) {
            for (int z = z0; z <= z1; ++z) {
                if (x != ladderX(2) || z != z0 + 2) upward = put(PropKind::UpperFloor, x, z, 2, true) && upward;
            }
        }
        for (int level = 3; level < STOREY_MAX; ++level) upward = storey(level, true, true) && upward;
        check(upward, "floor after floor goes up to the top floor");
        check(storey(STOREY_MAX, true, false), "walls go on the top floor");
        check(checkPiece(tower, PropKind::UpperFloor, x0 + 2, z0 + 2, FACTION_PLAYER, STOREY_MAX) == PieceCheck::NeedsFloor &&
              checkPiece(tower, PropKind::Ladder, x0 + 2, z0 + 2, FACTION_PLAYER, STOREY_MAX) == PieceCheck::NeedsFloor,
              "nothing goes up from the top floor");
        check(STOREY_MAX + 1 == 8, "a building is at most 8 floors high");
        ground.clearBlocked();
        ground.clearDecks();
    }

    // Stairs: three steps walked up from the tile before them, the top one a tile of the floor above; ladders climbed
    // only from inside, within a tile
    {
        World hall;
        Rng hallRng;
        hallRng.gen.seed(9);
        GameState hallState;
        Terrain& ground = terrain;
        ground.clearBlocked();
        const Vec2 mid = systems_detail::findSpawn(ground);
        const int cx = (int)std::floor(mid.x), cz = (int)std::floor(mid.z);
        placeBuiltProp(hall, ground, PropKind::Altar, cx - 8.5f, cz + 0.5f, 0.0f, FACTION_PLAYER);
        const int x0 = cx, z0 = cz, x1 = cx + 6, z1 = cz + 4;
        auto finish = [&](ObjectId e) {
            if (e == NO_OBJECT || !hall.has<Blueprint>(e)) return;
            hall.get<Blueprint>(e)->supplied = materialsNeeded(hall, e);
            hall.get<Blueprint>(e)->progress = 1.0;
            finishBlueprint(hall, ground, e);
        };
        placeFoundation(hall, ground, x0, z0, x1, z1);
        for (ObjectId e : hall.view<Foundation, Blueprint>()) finish(e);
        for (int x = x0; x <= x1; ++x) {
            for (int z = z0; z <= z1; ++z) {
                if (x != x0 && x != x1 && z != z0 && z != z1) continue;
                finish(placePiece(hall, ground, x == x0 + 3 && z == z1 ? PropKind::WoodDoor : PropKind::WoodWall, x, z));
            }
        }
        // Rising east, from x0+2 to x0+4, stepped onto from any floor tile beside x0+2; their top step is a tile of the
        // first upper floor
        check(checkPiece(hall, PropKind::Stairs, x0 + 3, z0 + 2, FACTION_PLAYER, 0, 1) == PieceCheck::Ok, "stairs may go on three tiles in a row");
        check(checkPiece(hall, PropKind::Stairs, x0 + 5, z0 + 2, FACTION_PLAYER, 0, 1) != PieceCheck::Ok, "stairs over a wall are refused");
        check(checkPiece(hall, PropKind::Stairs, x0 + 2, z0 + 2, FACTION_PLAYER, 0, 1) == PieceCheck::Ok,
              "stairs need no tile before their bottom step");
        const ObjectId stairs = placePiece(hall, ground, PropKind::Stairs, x0 + 3, z0 + 2, FACTION_PLAYER, BuildMaterial::Wood, 0, 1);
        finish(stairs);
        check(checkPiece(hall, PropKind::WoodWall, x0 + 2, z0 + 2) == PieceCheck::Taken &&
              checkPiece(hall, PropKind::WoodWall, x0 + 4, z0 + 2) == PieceCheck::Taken, "stairs take all three of their tiles");
        refreshBuildingTiles(hall, ground);
        check(!ground.walkable(x0 + 2, z0 + 2) && !ground.walkable(x0 + 4, z0 + 2) && ground.walkable(x0 + 1, z0 + 2),
              "the steps aren't walked across on the ground floor, only stepped up onto");
        bool laid = true;
        for (int x = x0; x <= x1; ++x) {
            for (int z = z0; z <= z1; ++z) {
                const bool onStairs = z == z0 + 2 && x >= x0 + 2 && x <= x0 + 4;
                const PieceCheck c = checkPiece(hall, PropKind::UpperFloor, x, z);
                if (onStairs) laid = laid && c == PieceCheck::Taken;
                else if (c == PieceCheck::Ok) finish(placePiece(hall, ground, PropKind::UpperFloor, x, z));
                else laid = false;
            }
        }
        check(laid && canClimbUp(hall, stairs), "an upper floor goes everywhere but over the stairs, and they lead up to it");
        {
            const BuildingMap m = buildingMap(hall);
            check(floorSurfaceOn(hall, m, x0 + 4, z0 + 2, 1) && !floorSurfaceOn(hall, m, x0 + 3, z0 + 2, 1),
                  "the stairs' top step is floor of the level above, the others aren't");
        }
        // A ladder inside, by the west wall: not climbed from outside the wall
        const ObjectId ladder = placePiece(hall, ground, PropKind::Ladder, x0 + 1, z0 + 1);
        finish(ladder);
        const ObjectId outside = spawnUnit(hall, ground, hallRng, FACTION_PLAYER, x0 - 0.5f, z0 + 1.5f);
        hall.get<Behaviors>(outside)->rules.clear();
        check(!atClimbSpot(hall, outside, ladder) && !startClimb(hall, ground, outside, ladder), "a ladder isn't climbed from outside the building");
        const ObjectId inside = spawnUnit(hall, ground, hallRng, FACTION_PLAYER, x0 + 1.5f, z0 + 2.5f);
        hall.get<Behaviors>(inside)->rules.clear();
        check(atClimbSpot(hall, inside, ladder), "a ladder is climbed from inside, within a tile of it");
        hall.get<Transform>(inside)->x = x0 + 5.5f;
        check(!atClimbSpot(hall, inside, ladder), "but not from further than a tile");
        // Up the stairs from their foot: walked up step by step, with a hop past each step's height, onto the top step
        Transform* it = hall.get<Transform>(inside);
        it->x = x0 + 1.5f;
        it->z = z0 + 2.5f;
        check(startClimb(hall, ground, inside, stairs), "a unit at the foot of the stairs goes up them");
        float highest = 0.0f, firstStep = 0.0f;
        for (int i = 0; i < 60 * 6 && hall.has<Climbing>(inside); ++i) {
            buildingSystem(hall, ground, 1.0f / 60.0f);
            const Transform* at = hall.get<Transform>(inside);
            const float up = at->y - ground.heightAt(at->x, at->z);
            highest = std::max(highest, up);
            if ((int)std::floor(at->x) == x0 + 2) firstStep = std::max(firstStep, up);
        }
        check(storeyOf(hall, inside) == 1 && (int)std::floor(hall.get<Transform>(inside)->x) == x0 + 4 && highest > storeyBaseHeight(1) &&
                  firstStep > storeyBaseHeight(1) / 3.0f,
              "it walks up them onto their top step on the first upper floor, hopping a little past each step");
        // Down again, stopped partway by a new order: it steps off at the nearer end, not left on the steps
        check(startClimb(hall, ground, inside, stairs), "a unit on the top step goes down the stairs");
        for (int i = 0; i < 60 * 6 && hall.has<Climbing>(inside); ++i) {
            buildingSystem(hall, ground, 1.0f / 60.0f);
            if (hall.get<Transform>(inside)->x < x0 + 2.2f) hall.remove<Climbing>(inside);
        }
        buildingSystem(hall, ground, 1.0f / 60.0f);
        check(storeyOf(hall, inside) == 0 && (int)std::floor(hall.get<Transform>(inside)->x) == x0 + 1,
              "stopped on the bottom step, it steps off at the stairs' foot");
        // Up again from beside the bottom step rather than in front of it
        it->x = x0 + 2.5f;
        it->z = z0 + 1.5f;
        check(startClimb(hall, ground, inside, stairs), "a unit beside the bottom step steps up onto it");
        for (int i = 0; i < 60 * 6 && hall.has<Climbing>(inside); ++i) buildingSystem(hall, ground, 1.0f / 60.0f);
        check(storeyOf(hall, inside) == 1 && (int)std::floor(it->x) == x0 + 4 && (int)std::floor(it->z) == z0 + 2,
              "and walks up onto their top step");
        // Only a unit on a piece's own tile holds it up; one beside it doesn't (corners are built first instead)
        {
            const ObjectId nearBy = spawnUnit(hall, ground, hallRng, FACTION_PLAYER, x0 + 4.5f, z0 + 3.5f);
            hall.get<Behaviors>(nearBy)->rules.clear();
            const ObjectId post = placePiece(hall, ground, PropKind::WoodWall, x0 + 5, z0 + 3);
            hall.get<Blueprint>(post)->supplied = materialsNeeded(hall, post);
            hall.get<Blueprint>(post)->progress = 1.0;
            hall.get<Transform>(nearBy)->x = x0 + 5.5f;
            check(!finishBlueprint(hall, ground, post) && hall.has<SoloMove>(nearBy), "a unit on a piece's tile holds it up, and is asked to move");
            hall.remove<SoloMove>(nearBy);
            hall.get<Transform>(nearBy)->x = x0 + 4.5f;
            check(finishBlueprint(hall, ground, post), "a unit on the tile beside it doesn't");
            breakBuilding(hall, post);
            hall.destroy(nearBy);
        }
        // A walk on the stairs isn't saved: a unit loaded partway up is set down off the steps, or on the top one upstairs
        {
            const ObjectId stray = spawnUnit(hall, ground, hallRng, FACTION_PLAYER, x0 + 3.5f, z0 + 2.5f);
            hall.get<Behaviors>(stray)->rules.clear();
            Transform* st = hall.get<Transform>(stray);
            st->y = ground.heightAt(st->x, st->z) + 1.2f;
            settleBuildingsAfterLoad(hall, ground);
            buildingSystem(hall, ground, 1.0f / 60.0f);
            const int sx = (int)std::floor(st->x), sz = (int)std::floor(st->z);
            const bool onSteps = sz == z0 + 2 && sx >= x0 + 2 && sx <= x0 + 4;
            const bool onTop = hall.has<Upstairs>(stray) && hall.get<Upstairs>(stray)->level == 1 && sx == x0 + 4;
            check(!onSteps || onTop, "a unit loaded partway up the stairs steps off them");
            hall.destroy(stray);
        }
        // A piece broken upstairs leaves its debris on that floor, where it can be seen and clicked
        {
            const ObjectId high = placePiece(hall, ground, PropKind::WoodWall, x0 + 5, z0 + 1, FACTION_PLAYER, BuildMaterial::Wood, 1);
            hall.get<Blueprint>(high)->supplied = materialsNeeded(hall, high);
            hall.get<Blueprint>(high)->progress = 1.0;
            check(finishBlueprint(hall, ground, high), "a wall is built on the upper floor");
            const float wallY = hall.get<Transform>(high)->y;
            breakBuilding(hall, high);
            ObjectId debris = NO_OBJECT;
            for (ObjectId e : hall.view<Prop, Transform>()) {
                const Transform* tr = hall.get<Transform>(e);
                if (hall.get<Prop>(e)->kind == PropKind::Debris && (int)std::floor(tr->x) == x0 + 5 && (int)std::floor(tr->z) == z0 + 1) debris = e;
            }
            check(debris != NO_OBJECT && storeyOf(hall, debris) == 1 && std::fabs(hall.get<Transform>(debris)->y - wallY) < 0.01f,
                  "debris from a piece broken upstairs is on that floor");
            if (debris != NO_OBJECT) hall.destroy(debris);
            rebuildBlocking(hall, ground);
        }
        // The one outside is sent to the upper floor: through the door, and up the stairs or the ladder
        walkSoloTo(hall, ground, outside, Vec2{ x0 + 5.5f, z0 + 1.5f }, 1);
        for (int i = 0; i < 60 * 40 && storeyOf(hall, outside) != 1; ++i) {
            simStep(hall, ground, hallRng, hallState, 1.0f / 60.0f, false);
            takeEvents(hall);
        }
        check(storeyOf(hall, outside) == 1, "a unit outside finds its way in and up to the upper floor");
        // A second floor up, with stairs on the first rising west from x0+4 to x0+2 along z0+3: one move order from
        // outside the building to the second floor goes up both flights
        hall.destroy(inside);
        hall.destroy(outside);
        for (int x = x0; x <= x1; ++x) {
            for (int z = z0; z <= z1; ++z) {
                if (x == x0 || x == x1 || z == z0 || z == z1) finish(placePiece(hall, ground, PropKind::WoodWall, x, z, FACTION_PLAYER, BuildMaterial::Wood, 1));
            }
        }
        check(checkPiece(hall, PropKind::Stairs, x0 + 3, z0 + 3, FACTION_PLAYER, 1, 3) == PieceCheck::Ok, "stairs go on an upper floor too");
        const ObjectId flight2 = placePiece(hall, ground, PropKind::Stairs, x0 + 3, z0 + 3, FACTION_PLAYER, BuildMaterial::Wood, 1, 3);
        finish(flight2);
        for (int x = x0; x <= x1; ++x) {
            for (int z = z0; z <= z1; ++z) {
                if (checkPiece(hall, PropKind::UpperFloor, x, z, FACTION_PLAYER, 1) == PieceCheck::Ok) {
                    finish(placePiece(hall, ground, PropKind::UpperFloor, x, z, FACTION_PLAYER, BuildMaterial::Wood, 1));
                }
            }
        }
        refreshBuildingTiles(hall, ground);
        {
            const BuildingMap m = buildingMap(hall);
            check(floorSurfaceOn(hall, m, x0 + 2, z0 + 3, 2) && floorSurfaceOn(hall, m, x0 + 5, z0 + 1, 2) && canClimbUp(hall, flight2),
                  "the second flight's top step is floor of the second floor");
        }
        const ObjectId climber = spawnUnit(hall, ground, hallRng, FACTION_PLAYER, x0 - 0.5f, z0 + 1.5f);
        hall.get<Behaviors>(climber)->rules.clear();
        walkSoloTo(hall, ground, climber, Vec2{ x0 + 5.5f, z0 + 1.5f }, 2);
        int climbs = 0;
        if (const Route* r = hall.get<Route>(climber)) {
            for (const RouteLeg& leg : r->legs) climbs += leg.ladder != NO_OBJECT ? 1 : 0;
        }
        check(climbs == 2, "one move order plans a way up both flights");
        for (int i = 0; i < 60 * 60 && !(storeyOf(hall, climber) == 2 && !hall.has<Route>(climber)); ++i) {
            simStep(hall, ground, hallRng, hallState, 1.0f / 60.0f, false);
            takeEvents(hall);
        }
        const Transform* ct = hall.get<Transform>(climber);
        check(storeyOf(hall, climber) == 2 && std::hypot(ct->x - (x0 + 5.5f), ct->z - (z0 + 1.5f)) < 1.0f,
              "and the unit walks it, onto the second floor where it was sent");
        ground.clearBlocked();
        ground.clearDecks();
    }

    // Materials: iron and Dragon Crystal make tougher pieces, and a blueprint wants its own material
    {
        World forge;
        Rng forgeRng;
        Terrain& ground = terrain;
        ground.clearBlocked();
        const Vec2 mid = systems_detail::findSpawn(ground);
        const int cx = (int)std::floor(mid.x), cz = (int)std::floor(mid.z);
        placeBuiltProp(forge, ground, PropKind::Altar, cx - 8.5f, cz + 0.5f, 0.0f, FACTION_PLAYER);
        const ObjectId base = placeFoundation(forge, ground, cx, cz, cx + 3, cz + 3, FACTION_PLAYER, BuildMaterial::Iron);
        check(materialOf(forge, base) == BuildMaterial::Iron && materialItem(BuildMaterial::Iron) == itemTagged(ITEM_TAG_IRON_INGOT),
              "an iron foundation wants Iron Ingots");
        forge.remove<Blueprint>(base);
        const ObjectId iron = placePiece(forge, ground, PropKind::WoodWall, cx, cz, FACTION_PLAYER, BuildMaterial::Iron);
        const ObjectId crystal = placePiece(forge, ground, PropKind::WoodWall, cx + 1, cz, FACTION_PLAYER, BuildMaterial::Crystal);
        for (ObjectId e : { iron, crystal }) {
            forge.get<Blueprint>(e)->supplied = materialsNeeded(forge, e);
            finishBlueprint(forge, ground, e);
        }
        check(forge.get<Health>(iron)->max == 2.0 * WALL_HEALTH && forge.get<Health>(crystal)->max == 3.0 * WALL_HEALTH,
              "an iron wall is twice as tough as wood, and a crystal one three times");
        ground.clearBlocked();
    }

    // Wood and stone go to their stockpiles; with none that has room, the task waits
    {
        World yard;
        Rng yardRng;
        yardRng.gen.seed(8);
        GameState yardState;
        Terrain& ground = terrain;
        ground.clearBlocked();
        const Vec2 mid = systems_detail::findSpawn(ground);
        placeBuiltProp(yard, ground, PropKind::Altar, mid.x - 6.5f, mid.z + 0.5f, 0.0f, FACTION_PLAYER);
        check(propAcceptsItem(PropKind::Altar, itemTagged(ITEM_TAG_COMMON_BONES)) && !propAcceptsItem(PropKind::Altar, woodLogItem()),
              "an Altar burns bones, not logs");
        check(propAcceptsItem(PropKind::StoneStockpile, itemTagged(ITEM_TAG_STONE)) && !propAcceptsItem(PropKind::StoneStockpile, woodLogItem()) &&
              propAcceptsItem(PropKind::WoodStockpile, woodLogItem()), "each stockpile takes only its own kind");
        const ObjectId rock = makeProp(yard, PropKind::Boulder, Transform{ mid.x + 2.5f, ground.heightAt(mid.x + 2.5f, mid.z + 0.5f), mid.z + 0.5f });
        claimOwnership(yard, rock);
        const ObjectId miner = spawnUnit(yard, ground, yardRng, FACTION_PLAYER, mid.x + 0.5f, mid.z + 0.5f);
        BehaviorRule mine;
        mine.kind = BehaviorKind::AutoHarvest;
        mine.propKind = idx(PropKind::Boulder);
        yard.get<Behaviors>(miner)->rules = { mine };
        Stats digger = *yard.get<Stats>(miner);
        digger.xp[idx(StatId::Mining)] = xpForLevel(MAX_LEVEL);
        yard.add(miner, digger);
        for (int i = 0; i < 60 * 3; ++i) {
            simStep(yard, ground, yardRng, yardState, 1.0f / 60.0f, false);
            takeEvents(yard);
        }
        check(yard.get<Behaviors>(miner)->rules[0].why == BehaviorWait::NoStockpile && !yard.has<Working>(miner),
              "with no Stone Stockpile, mining stone waits");
        const ObjectId pile = placeBuiltProp(yard, ground, PropKind::StoneStockpile, mid.x - 1.5f, mid.z + 2.5f, 0.0f, FACTION_PLAYER);
        check(addHaulForHarvest(yard, miner, idx(PropKind::Tree)) == NO_OBJECT, "with no Wood Stockpile, woodcutting gets no haul task");
        {
            const std::vector<BehaviorRule>& rules = yard.get<Behaviors>(miner)->rules;
            check(addHaulForHarvest(yard, miner, idx(PropKind::Boulder)) == pile && rules.size() == 2 && rules[0].kind == BehaviorKind::AutoHaul &&
                  rules[0].prop == pile && rules[1].kind == BehaviorKind::AutoHarvest,
                  "mining stone also gets Auto-Haul to the Stone Stockpile, placed above the Auto-Harvest");
            std::vector<BehaviorRule> order = defaultBehaviors();
            BehaviorRule harvest, haul;
            harvest.kind = BehaviorKind::AutoHarvest;
            harvest.propKind = idx(PropKind::Tree);
            haul.kind = BehaviorKind::AutoHaul;
            haul.prop = pile;
            BehaviorRule roam;
            roam.kind = BehaviorKind::Roam;
            addBehavior(order, harvest);
            addBehavior(order, roam);
            addBehavior(order, haul);
            const size_t base = defaultBehaviors().size();
            check(order[base].kind == BehaviorKind::AutoHaul && order[base + 1].kind == BehaviorKind::AutoHarvest &&
                  order[base + 2].kind == BehaviorKind::Roam, "a haul task added later goes in just above the first Auto-Harvest");
            std::swap(order[base], order[base + 1]);
            addBehavior(order, haul);
            check(order[base].kind == BehaviorKind::AutoHaul, "added again, a haul task below an Auto-Harvest moves above it");
        }
        for (int i = 0; i < 60 * 240; ++i) {
            simStep(yard, ground, yardRng, yardState, 1.0f / 60.0f, false);
            takeEvents(yard);
        }
        int stored = 0;
        for (const ItemStack& st : yard.get<Storage>(pile)->slots) stored += st.item == itemTagged(ITEM_TAG_STONE) ? st.count : 0;
        std::printf("note: stone stored %d, carried %d, why %d, busy %d, working %d, propgo %d\n", stored,
                    carriedCount(*yard.get<Inventory>(miner), itemTagged(ITEM_TAG_STONE)), (int)yard.get<Behaviors>(miner)->rules[0].why,
                    (int)yard.get<Behaviors>(miner)->busy, (int)yard.has<Working>(miner), (int)yard.has<PropOrder>(miner));
        check(stored > 0, "with one, the stone mined from a Boulder goes into the Stone Stockpile");
        // A Barrel takes all but logs, stone and bones by default; its Allowed Items can change that
        const ObjectId barrel = placeBuiltProp(yard, ground, PropKind::Barrel, mid.x + 3.5f, mid.z - 2.5f, 0.0f, FACTION_PLAYER);
        check(hasStoreFilter(PropKind::Barrel) && !hasStoreFilter(PropKind::StoneStockpile) && !propTakesItem(yard, barrel, woodLogItem()) &&
              !propTakesItem(yard, barrel, itemTagged(ITEM_TAG_STONE)) && !propTakesItem(yard, barrel, itemTagged(ITEM_TAG_COMMON_BONES)) &&
              propTakesItem(yard, barrel, itemTagged(ITEM_TAG_IRON_ORE)), "a Barrel takes all but logs, stone and bones by default");
        yard.add(barrel, StoreFilter{ STORE_ALL });
        check(propTakesItem(yard, barrel, woodLogItem()), "set to allow everything, it takes logs too");
        yard.add(barrel, StoreFilter{ STORE_ALL & ~storeBit(StoreCategory::Metal) });
        check(!propTakesItem(yard, barrel, itemTagged(ITEM_TAG_IRON_ORE)), "a kind switched off isn't taken");
        yard.remove<StoreFilter>(barrel);
        // Every store of yours gets a haul task in the Citizen Default Behavior, once
        {
            BehaviorGroups& bg = behaviorGroups(yard);
            storeHaulSync(yard);
            BehaviorRule toPile, toBarrel;
            toPile.kind = toBarrel.kind = BehaviorKind::AutoHaul;
            toPile.prop = pile;
            toBarrel.prop = barrel;
            bool bones = false;
            for (const BehaviorRule& r : bg.citizen) bones = bones || r.kind == BehaviorKind::AutoHaulBones;
            check(holdsBehavior(bg.citizen, toPile) && holdsBehavior(bg.citizen, toBarrel) && bones,
                  "the Citizen Default Behavior hauls to each store of yours, and bones to the Altar");
            removeBehavior(bg.citizen, (size_t)(std::find_if(bg.citizen.begin(), bg.citizen.end(),
                                                             [&](const BehaviorRule& r) { return sameBehavior(r, toBarrel); }) - bg.citizen.begin()));
            storeHaulSync(yard);
            check(!holdsBehavior(bg.citizen, toBarrel), "a store's haul task taken out of it stays out");
        }
        // Auto-Haul Ground Items: a stone lying in the claim is picked up and put away
        {
            Behaviors* mb = yard.get<Behaviors>(miner);
            const std::vector<BehaviorRule> keepRules = mb->rules;
            BehaviorRule haul, ground2;
            haul.kind = BehaviorKind::AutoHaul;
            haul.prop = pile;
            ground2.kind = BehaviorKind::AutoHaulGround;
            mb->rules = { haul, ground2 };
            mb->busy = false;
            yard.remove<Working>(miner);
            yard.remove<PropOrder>(miner);
            yard.remove<SoloMove>(miner);
            const Transform* mt = yard.get<Transform>(miner);
            const ObjectId lying = spawnGroundItem(yard, Transform{ mt->x + 2.0f, mt->y, mt->z + 1.0f }, itemTagged(ITEM_TAG_STONE), 1);
            for (int i = 0; i < 60 * 60 && yard.has<GroundItem>(lying); ++i) {
                simStep(yard, ground, yardRng, yardState, 1.0f / 60.0f, false);
                takeEvents(yard);
            }
            check(!yard.has<GroundItem>(lying), "Auto-Haul Ground Items picks up an item its hauls can put away");
            yard.get<Behaviors>(miner)->rules = keepRules;
        }
        // A finished foundation decks its tiles and a finished wall closes its tile, worked out for those tiles alone
        {
            const int fx = (int)std::floor(mid.x) + 8, fz = (int)std::floor(mid.z) - 8;
            const ObjectId f = placeFoundation(yard, ground, fx, fz, fx + 2, fz + 2, FACTION_PLAYER, BuildMaterial::Wood);
            check(f != NO_OBJECT, "a foundation is laid for the deck test");
            if (f != NO_OBJECT) {
                // One foundation tile per tile, each finished
                for (ObjectId e : yard.view<Foundation, Blueprint>()) finishBlueprint(yard, ground, e);
                buildingSystem(yard, ground, 0.0f);
                check(ground.onDeck(fx + 1, fz + 1) && std::fabs(ground.standHeight(fx + 1.5f, fz + 1.5f) -
                      (ground.heightAt(fx + 1.5f, fz + 1.5f) + DECK_HEIGHT)) < 1e-4f && !ground.onDeck(fx + 3, fz),
                      "a unit on a finished foundation stands on its deck");
                const ObjectId wall = placePiece(yard, ground, PropKind::WoodWall, fx, fz, FACTION_PLAYER, BuildMaterial::Wood);
                finishBlueprint(yard, ground, wall);
                buildingSystem(yard, ground, 0.0f);
                check(ground.blockedByBuilding(fx, fz) && !ground.walkable(fx, fz), "a finished wall closes its tile to walking");
                breakBuilding(yard, wall);
                buildingSystem(yard, ground, 0.0f);
                check(!ground.blockedByBuilding(fx, fz), "a lost wall opens its tile again");
            }
            ground.clearBlocked();
            ground.clearDecks();
        }
        {
            // Mining, the miner picks up a stone: its Auto-Haul, above the Auto-Harvest, comes first
            for (int i = 0; i < 60 * 120 && !yard.has<Working>(miner); ++i) {
                simStep(yard, ground, yardRng, yardState, 1.0f / 60.0f, false);
                takeEvents(yard);
            }
            const bool wasWorking = yard.has<Working>(miner);
            addToSlots(yard.get<Inventory>(miner)->slots.data(), INVENTORY_SLOTS, itemTagged(ITEM_TAG_STONE), 1);
            simStep(yard, ground, yardRng, yardState, 1.0f / 60.0f, false);
            takeEvents(yard);
            const Behaviors* mb = yard.get<Behaviors>(miner);
            check(wasWorking && mb->busy && isHaul(mb->step.kind) && !yard.has<Working>(miner),
                  "when what a unit carries changes, a haul task above its harvesting comes first");
        }

        // A Dragon Crystal is broken, not destroyed, and leaves crystals
        const ObjectId gem = makeProp(yard, PropKind::DragonCrystal, Transform{ mid.x + 4.5f, 0.0f, mid.z + 4.5f });
        check(destroyedByHand(PropKind::DragonCrystal) && propInfo(PropKind::DragonCrystal).rare, "a Dragon Crystal is broken by hand, and rare");
        destroyBuilding(yard, gem);
        int crystals = 0;
        for (ObjectId g : yard.view<GroundItem>()) crystals += yard.get<GroundItem>(g)->item == itemTagged(ITEM_TAG_DRAGON_CRYSTAL) ? 1 : 0;
        check(crystals == 1, "broken, it leaves Dragon Crystals");
    }

    // Cancelling a foundation blueprint drops what it and the blueprints on it hold; once building starts, it's spent
    {
        World site;
        Terrain& ground = terrain;
        ground.clearBlocked();
        const Vec2 mid = systems_detail::findSpawn(ground);
        const int cx = (int)std::floor(mid.x), cz = (int)std::floor(mid.z);
        placeBuiltProp(site, ground, PropKind::Altar, cx - 8.5f, cz + 0.5f, 0.0f, FACTION_PLAYER);
        auto logsOnGround = [&] {
            int n = 0;
            for (ObjectId g : site.view<GroundItem>()) n += site.get<GroundItem>(g)->item == woodLogItem() ? site.get<GroundItem>(g)->count : 0;
            return n;
        };
        const ObjectId plan = placeFoundation(site, ground, cx, cz, cx + 3, cz + 3, FACTION_PLAYER, BuildMaterial::Wood);
        check(site.view<Foundation>().size() == 16, "a 4 by 4 foundation is laid as 16 foundation tiles");
        check(supplyBlueprint(site, plan, 5) == 1, "a foundation tile's blueprint holds the one log it needs");
        check(destroyBuilding(site, plan) && logsOnGround() == 1 && !site.has<Prop>(plan) && site.view<Foundation>().size() == 15,
              "cancelled, the foundation tile's blueprint drops the log it held");
        const PieceCheck extend = checkFoundation(site, ground, cx + 4, cz, cx + 4, cz);
        check(extend == PieceCheck::Ok, "a building is extended by a single tile beside it");
        check(checkFoundation(site, ground, cx - 20, cz, cx - 19, cz) == PieceCheck::TooSmall, "a new building starts at least 3 by 3");
        const ObjectId plan2 = placeFoundation(site, ground, cx, cz, cx + 3, cz + 3, FACTION_PLAYER, BuildMaterial::Wood);
        supplyBlueprint(site, plan2, 1);
        site.get<Blueprint>(plan2)->progress = 0.2;
        destroyBuilding(site, plan2);
        check(logsOnGround() == 1, "once building has started, the log is spent and nothing more drops");

        // However heavy the load, a unit keeps a tenth of its speed
        Rng siteRng;
        const ObjectId mule = spawnUnit(site, ground, siteRng, FACTION_PLAYER, mid.x + 0.5f, mid.z + 6.5f);
        addToInventory(*site.get<Inventory>(mule), itemTagged(ITEM_TAG_STONE), 999);
        const float full = site.get<Entity>(mule)->speed;
        check(walkSpeedOf(site, mule) >= 0.1f * full - 1e-4f, "a unit under any load still moves at a tenth of its speed");
        ground.clearBlocked();
    }

    // Prisoner cages and Evacuate
    {
        World caged;
        Rng cageRng;
        cageRng.gen.seed(777);
        setupWorld(caged, terrain, cageRng, true);
        std::vector<ObjectId> own = caged.view<Entity, Commandable, Transform>();
        std::sort(own.begin(), own.end());
        check(own.size() >= 2, "the level has two of your units");
        if (own.size() >= 2) {
            const ObjectId inside = own[0], pusher = own[1];
            const Transform spot = *caged.get<Transform>(pusher);
            const ObjectId cage = makeProp(caged, PropKind::PrisonerCage, spot);
            if (Lock* lock = caged.get<Lock>(cage)) lock->locked = false;
            caged.add(inside, Occupying{ cage });
            check(inPrisonerCage(caged, inside), "a unit in a cage is caged");
            issueAttackOrder(caged, { pusher }, inside);
            check(!caged.has<AttackOrder>(pusher), "a caged unit can't be ordered attacked");
            check(propOccupant(caged, cage) == inside, "the cage knows who is in it");
            caged.add(pusher, PropOrder{ cage, PropAction::Evacuate });
            propOrderSystem(caged, terrain);
            check(!caged.has<Occupying>(inside) && !inPrisonerCage(caged, inside), "Evacuate pushes the occupant out");
            check(caged.has<OccupyBan>(inside), "the one pushed out can't occupy for a while");
            occupyBanSystem(caged, EVACUATE_OCCUPY_BAN_SECONDS + 1.0f);
            check(!caged.has<OccupyBan>(inside), "the ban runs out");
            for (ObjectId npc : caged.view<Entity, Transform>()) {
                if (caged.has<Commandable>(npc) || outCold(caged, npc) || caged.has<Occupying>(npc)) continue;
                caged.add(npc, Occupying{ cage });
                leaveOpenLocksSystem(caged, terrain);
                check(!caged.has<Occupying>(npc), "a unit that is not yours leaves an open cage");
                break;
            }
        }
    }

    // Damage is refused
    std::string text = slurp(first);
    {
        std::ofstream out(cut);
        out << text.substr(0, text.size() / 2);
    }
    SaveData damaged;
    check(!readSaveFile(cut, damaged), "a truncated save is refused");
    {
        std::ofstream out(cut);
        out << text << "nonsense line\n";
    }
    SaveData unknown;
    check(!readSaveFile(cut, unknown), "an unknown line is refused");
    const auto readEdited = [&](const std::string& find, const std::string& put) {
        std::string changed = text;
        const size_t at = changed.find(find);
        check(at != std::string::npos, "the save has the line to change");
        changed.replace(at, find.size(), put);
        {
            std::ofstream out(cut);
            out << changed;
        }
        SaveData edited;
        return readSaveFile(cut, edited);
    };
    check(!readEdited("version " + std::to_string(SAVE_VERSION), "version " + std::to_string(SAVE_VERSION + 1)), "a save from a newer game is refused");
    check(!readEdited("version " + std::to_string(SAVE_VERSION), "version " + std::to_string(SAVE_OLDEST_VERSION - 1)), "a save too old to upgrade is refused");
    check(!readEdited("\nprop Altar ", "\nprop NoSuchProp "), "a save naming a prop this game lacks is refused");
    check(readEdited(" Strength ", " NoSuchStat "), "a stat this game lacks is skipped");

    // A file of the version before: every version kept in tests/saves must still load, and be the same game
    {
        const std::string oldFile = std::string(KOBOLD_TEST_SAVES) + "/v6.sav";
        SaveData old;
        check(readSaveFile(oldFile, old), "a version 6 save is read");
        check(old.props.size() > 100 && old.units.size() > 50, "a version 6 save has its props and units");
        size_t namedRight = 0;
        for (const SaveProp& p : old.props) namedRight += p.kind == idx(PropKind::Altar) ? 1 : 0;
        check(namedRight > 0, "a version 6 prop number becomes the same prop");
        World oldWorld;
        Rng oldRng;
        restoreWorld(oldWorld, terrain, oldRng, old);
        check(!oldWorld.view<Entity, Commandable>().empty(), "a version 6 save restores your units");
        const std::string upgraded = "roundtrip_old.sav";
        check(writeSaveFile(upgraded, old), "a version 6 save is saved as the current version");
        const std::string once = slurp(upgraded);
        check(once.find("version " + std::to_string(SAVE_VERSION)) == 0 && once.find("\nsizes ") == std::string::npos, "the saved file is of the current version");
        SaveData again2;
        check(readSaveFile(upgraded, again2) && writeSaveFile(upgraded, again2) && slurp(upgraded) == once, "it reads back the same");
        std::remove(upgraded.c_str());
    }

    std::remove(first.c_str());
    std::remove(second.c_str());
    std::remove(cut.c_str());
    std::printf("%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
