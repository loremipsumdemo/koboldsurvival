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
            if (f.id == FACTION_PLAYER || f.id == FACTION_EXILE || isMobFaction(f.id)) continue;
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
            if (f.id != FACTION_PLAYER && f.id != FACTION_EXILE && !isMobFaction(f.id)) {
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
        check(!toggleEntrance(town, base, x0, z0), "a corner can't be an entrance");
        check(!toggleEntrance(town, base, x0 + 2, z0 + 2), "a tile inside can't be an entrance");
        check(toggleEntrance(town, base, x0 + 2, z1), "a tile on the edge becomes an entrance");
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

        check(checkPiece(town, PropKind::WoodDoor, x0 + 1, z1) == PieceCheck::EntranceOnly, "a door goes only on an entrance");
        check(checkPiece(town, PropKind::Ladder, x0 + 1, z0 + 2) == PieceCheck::EdgesOpen &&
              checkPiece(town, PropKind::UpperFloor, x0, z0) == PieceCheck::EdgesOpen, "no ladder or upper floor before every edge has a wall or door");
        check(checkPiece(town, PropKind::WoodWall, x0 + 2, z1) == PieceCheck::NotOnEntrance, "a wall can't close an entrance");
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
        check(edgesClosed(town, base) && checkPiece(town, PropKind::Ladder, x0 + 1, z0 + 2) == PieceCheck::Ok, "with every edge closed, a ladder may go in");
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
        check(canClimbUp(town, ladder) && startClimb(town, builder, ladder), "a unit by a ladder climbs it");
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
        for (ObjectId e : again.view<Foundation>()) foundations += again.get<Foundation>(e)->entrances.size() == 1 ? 1 : 0;
        for (ObjectId e : again.view<Upstairs>()) upstairs += again.has<Foundation>(again.get<Upstairs>(e)->foundation) ? 1 : 0;
        check(foundations == 1 && upstairs == 1, "the foundation, its entrance, and who is upstairs come back");
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
        check(supplyBlueprint(site, plan, 5) == 1, "a foundation blueprint holds the one log it needs");
        check(destroyBuilding(site, plan) && logsOnGround() == 1 && site.view<Blueprint>().empty(),
              "cancelled, the foundation blueprint drops the log it held");
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
