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
#include "systems.h"
#include "terrain.h"

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

Entity firstProp(World& w, PropKind kind) {
    std::vector<Entity> props = w.view<Prop, Transform>();
    std::sort(props.begin(), props.end());
    for (Entity e : props) {
        if (w.get<Prop>(e)->kind == kind) return e;
    }
    return NULL_ENTITY;
}

Entity firstStocked(World& w) {
    for (Entity e : w.view<Prop, Storage, Transform>()) {
        for (const ItemStack& s : w.get<Storage>(e)->slots) {
            if (s.count > 0) return e;
        }
    }
    return NULL_ENTITY;
}

int stacksIn(const Storage& st) {
    int n = 0;
    for (const ItemStack& s : st.slots) n += s.count > 0 ? 1 : 0;
    return n;
}

} // namespace

int main(int, char**) {
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
        check(sfxInfo(Sfx::Recovers).file == "sfx_56.mp3" && sfxInfo(Sfx::UiClick).ui && !sfxInfo(Sfx::Hit).ui, "sfx.txt reaches the sounds");
        check(propModelFile(PropKind::Stove) == "model_42.glb" && unitModelFile(3) == "unit/unit_03.glb", "models.txt reaches the props and units");
        check(emoteFile(16) == "emote_16.png" && iconFile(51) == "icon_51.png", "emotes.txt and icons.txt give each id its picture");
        check(mobInfo(MobKind::Bat).hover > 0.0f && mobBones(MobKind::Frog) == 150 && mobTemperament(MobKind::Cow) == MobTemperament::Passive,
              "mobs.txt reaches the mobs");
        check(std::string(soundtrackNamed("Playing").music) == "bgm_01.mp3" && !soundtrackNamed("Pause").music &&
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
    const Entity emptied = firstStocked(world);
    check(emptied != NULL_ENTITY, "the level has a stocked container");
    if (emptied) *world.get<Storage>(emptied) = Storage{};
    const Entity altar = firstProp(world, PropKind::Altar);
    check(altar != NULL_ENTITY, "the level has an Altar");
    const int altarOwner = altar ? world.get<Owner>(altar)->faction : -1;
    if (altar) breakBuilding(world, altar);
    size_t ownedByLoser = 0;
    for (Entity e : world.view<Prop, Owner>()) ownedByLoser += world.get<Owner>(e)->faction == altarOwner ? 1 : 0;

    // A resting unit and a unit with a standing order
    std::vector<Entity> people = world.view<Unit, Commandable>();
    std::sort(people.begin(), people.end());
    Entity bed = firstProp(world, PropKind::Bedroll);
    check(people.size() >= 2 && bed != NULL_ENTITY, "the level has two units and a bedroll");
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
        for (Entity e : world.view<Mob, Unit>()) {
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
    for (Entity e : world.view<Mob, SpawnHome>()) bossRabbits += world.get<Mob>(e)->kind == MobKind::Rabbit ? 1 : 0;
    check(bossRabbits == mobInfo(MobKind::Gorlicorn).escortCount, "only the Gorlicorn brings rabbits");

    // States a fresh level never has
    std::vector<Entity> npcs;
    for (Entity e : world.view<Unit, Dialogue>()) npcs.push_back(e);
    std::sort(npcs.begin(), npcs.end());
    check(npcs.size() >= 3 && people.size() >= 3, "the level has NPCs and three of your units");
    const Entity patient = npcs[0], dead = npcs[1], angry = npcs[2], rescuer = people[2];
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

    Entity lockedProp = NULL_ENTITY;
    for (Entity e : world.view<Prop, Lock, Transform>()) {
        const Lock* l = world.get<Lock>(e);
        if (l->locked && l->faction > 0) lockedProp = e;
    }
    check(lockedProp != NULL_ENTITY, "a faction has a locked prop");
    const Entity picker = makeUnit(world, *world.get<Transform>(lockedProp), FACTION_PLAYER);
    world.add(picker, defaultStats());
    world.add(picker, Working{ lockedProp, true });

    const Entity bones = makeProp(world, PropKind::Bones, *world.get<Transform>(people[0]));
    world.add(bones, BonesPile{ 2.0, { ItemStack{ itemTagged(ITEM_TAG_COMMON_BONES), 3 } } });
    const Entity forge = firstProp(world, PropKind::Forge);
    check(forge != NULL_ENTITY, "the level has a forge");
    *world.get<Fuel>(forge) = Fuel{ 1.5, propInfo(PropKind::Forge).input };
    world.get<PropWork>(forge)->progress = 0.25;
    const Entity lying = spawnGroundItem(world, *world.get<Transform>(people[0]), itemTagged(ITEM_TAG_COMMON_BONES), 2);
    world.get<GroundItem>(lying)->hours = 5.0;

    const size_t propsBefore = world.view<Prop, Transform>().size();
    const std::map<int, int> repBefore = playerReputation(world);
    const std::string first = "roundtrip_a.sav", second = "roundtrip_b.sav", cut = "roundtrip_cut.sav";
    const Camera cam;
    const GameState gs;
    const MapChoice map{ "packed", "test", "test", "" };
    check(writeSaveFile(first, captureWorld(world, gs, cam, rng, 1000, "test", map)), "the save is written");

    SaveData loaded;
    check(readSaveFile(first, loaded), "the save is read");
    Rng after;
    World restored;
    restoreWorld(restored, terrain, after, loaded);
    check(writeSaveFile(second, captureWorld(restored, gs, cam, after, 1000, "test", map)), "the reloaded game is saved");
    check(slurp(first) == slurp(second), "both saves match");

    check(restored.view<Prop, Transform>().size() == propsBefore, "broken props stay broken (only Debris takes their place)");
    size_t emptyContainers = 0;
    for (Entity e : restored.view<Prop, Storage>()) emptyContainers += stacksIn(*restored.get<Storage>(e)) == 0 ? 1 : 0;
    check(emptyContainers > 0, "an emptied container stays empty");
    size_t ownedAfter = 0;
    for (Entity e : restored.view<Prop, Owner>()) ownedAfter += restored.get<Owner>(e)->faction == altarOwner ? 1 : 0;
    check(ownedAfter == ownedByLoser, "props abandoned with their Altar stay ownerless");

    std::vector<Entity> again = restored.view<Unit, Commandable>();
    std::sort(again.begin(), again.end());
    const Behaviors* b = restored.get<Behaviors>(again[0]);
    check(b && b->busy && b->step.kind == BehaviorKind::Rest && restored.has<Occupying>(again[0]), "a resting unit is still on its Rest step");
    check(restored.has<FollowOrder>(again[1]), "a standing order comes back");
    check(restored.get<Needs>(again[1])->healSeconds == 12.5, "partial healing comes back");
    check(restored.has<XpBuffer>(again[1]) && restored.get<XpBuffer>(again[1])->frac[0] == 0.25f, "partial XP comes back");
    check(rng.gen == after.gen, "the random numbers carry on where they left off");

    int carried = 0, corpses = 0, unconscious = 0, provokedUnits = 0, picking = 0, bonesPiles = 0, burning = 0;
    for (Entity e : restored.view<Carried>()) carried += restored.has<Carrying>(restored.get<Carried>(e)->by) ? 1 : 0;
    for (Entity e : restored.view<Corpse>()) corpses += restored.get<Corpse>(e)->hours == 3.5 ? 1 : 0;
    for (Entity e : restored.view<Unconscious>()) unconscious += restored.get<Unconscious>(e)->dying == 12.5f ? 1 : 0;
    for (Entity e : restored.view<Provoked>()) provokedUnits += restored.get<Provoked>(e)->against.empty() ? 0 : 1;
    for (Entity e : restored.view<Working>()) picking += restored.get<Working>(e)->unlocking ? 1 : 0;
    for (Entity e : restored.view<BonesPile>()) bonesPiles += restored.get<BonesPile>(e)->items.size() == 1 ? 1 : 0;
    for (Entity e : restored.view<Fuel, PropWork>()) burning += restored.get<Fuel>(e)->hours == 1.5 && restored.get<PropWork>(e)->progress == 0.25 ? 1 : 0;
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

    // Prisoner cages and Evacuate
    {
        World caged;
        Rng cageRng;
        cageRng.gen.seed(777);
        setupWorld(caged, terrain, cageRng, true);
        std::vector<Entity> own = caged.view<Unit, Commandable, Transform>();
        std::sort(own.begin(), own.end());
        check(own.size() >= 2, "the level has two of your units");
        if (own.size() >= 2) {
            const Entity inside = own[0], pusher = own[1];
            const Transform spot = *caged.get<Transform>(pusher);
            const Entity cage = makeProp(caged, PropKind::PrisonerCage, spot);
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
            for (Entity npc : caged.view<Unit, Transform>()) {
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
    {
        std::ofstream out(cut);
        std::string changed = text;
        changed.insert(changed.find("\nsizes ") + 7, "9");
        out << changed;
    }
    SaveData sized;
    check(!readSaveFile(cut, sized), "a save from a game with other list sizes is refused");

    std::remove(first.c_str());
    std::remove(second.c_str());
    std::remove(cut.c_str());
    std::printf("%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
