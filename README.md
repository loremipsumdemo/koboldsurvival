# Kobold Survival

A 3D survival sandbox simulation written in C++17 with SDL3 and OpenGL. You lead a party of units across a terrain
world where they gather, build, eat, rest, fight and trade among rival factions and wild animals. There is no win or
lose state: the world keeps running, whatever happens to your party.

## Features

- **Units with stats and needs.** Each unit has attributes and skills such as Strength, Dexterity, Vitality, Melee,
  Mining and Cooking, which rise with use, and needs: health, hunger, Rested and Comfort.
- **Day, night and sleep.** Daylight runs from 06:00 to 18:00. Awake, Rested runs from full to 0 in 11 hours, so a unit
  up at dawn is spent an hour before dusk, and at 0 Rested it harvests and crafts at a fifth of its speed. The Sleep at
  night task (the last of the Needs) sends a unit to the nearest free bed from 18:00 and keeps it there until 06:00.
- **Being knocked out.** A unit at 0 health is Unconscious until its Conscious is back at 100, paid for in Rested and
  then Dying. Dying 100 makes it a corpse. Unconscious units and corpses can be carried and looted. One of your units
  that wakes near hostile units plays dead.
- **Mobs.** Mobs have no hunger, Rested or Comfort. A knocked-out mob wakes by itself unless something is taken from it.
  A mob that brings down another mob destroys its meat and kills it, leaving its bones in the corpse.
- **Corpses and bones.** After 24 in-game hours a corpse turns into a Bones prop, which holds what the corpse still held
  or wore. Nothing can reach those items until the prop goes: by itself after another 24 in-game hours, or when a unit
  is sent with the right-click menu's Destroy. Either way its items and 3 Bones fall to the ground. Food and bones left
  on the ground are gone after 24 in-game hours. A corpse carried into a campfire is destroyed at once, and what it had
  falls to the ground.
- **Behaviors.** Each unit works through a priority list: Eat, Heal, Share, Rescue, Rest, Leave Bed, Auto-Harvest,
  Auto-Haul, Patrol, Patrol Spawn and Roam. You can reorder, switch off, add or remove them per unit. Units can also be
  set to Passive, Block or Sneak.
- **Gathering and crafting.** Mine ore, cut trees and gather mushrooms. Campfires and forges work by themselves from
  their fuel, and units work stoves, water pumps and training dummies. Items are kept in barrels, chests and
  specialised storage.
- **Building and claims.** The Build tab places most props at once and at no cost (building pieces are blueprints, below), and its Destroy tool takes them down
  (Debris is cleared away, and anything else leaves Debris). Bones and the rare Dragon Crystal are broken with the
  right-click menu's Break and leave their items. A faction has one Altar, named
  for it ("Altar of Red Faction"), which claims the ground around it. Only its own faction can open it, and there is
  no lock to pick. It burns bones (any kind) as upkeep. With none left it decays, and when it breaks the claim is gone and the
  props in the circle belong to nobody.
- **Factions and reputation.** Rival camps start at set reputations with you. At -25 or below they attack on sight, and
  at 50 or more they are allies. The game keeps running while you talk or trade, unless you turn on Automatic Conversation
  Pause in Settings or press Space. Talk to NPCs, trade with them, lock units in Prisoner Cages (a caged unit is out of any fight), push a unit off a prop with Evacuate, pick locks, or disband
  your units into the Exiles.
- **Squads and citizens.** Your units are your military, in nine numbered squads of up to 16, or citizens, whom the
  game commands and who stay near your Altar. A squad that would go over 16 sends the extra units to squads with room,
  and once all nine are full, to the citizens. Right-click your Altar and choose Manage Units to move units between the two. The Faction tab of the
  Faction Panel shows the squads as a numpad, where you can pick and rename them. Settings has a soft and a hard cap on
  citizens. Rename any unit of yours by right-clicking it in the Squad Panel, or its portrait in Manage Units. Every
  faction of people starts with a camp (an Altar, a campfire and four bedrolls), with open ground ten tiles round it
  where the map allows, some citizens who keep near it, and some military who patrol its claim. New Game's map page shows
  where a random set-up puts the camps.
- **Behavior groups.** Your Altar keeps the task lists your citizens follow: right-click it and choose Behavior Groups.
  Each citizen follows one group, Group 1 at first, set with the Group button on its row in Manage Units (click for the
  next, right-click for the one before). There can be up to 64 groups of up to 256 tasks each. Add Task lists what can
  be added under headings (Groups, Needs, Building, Hauling, Harvesting, Movement), each with an Add All button for every
  task under it the list doesn't have. That is also the order of priority a new task is sorted into, in any list. A group's list may hold a Group task, which stands
  for the whole list of another group or a Default Behavior at that place, so a group can take the usual tasks from one
  and add its own (harvesting, hauling) below. Group 1 starts as just the Citizen Default Behavior, and a new group as
  just Group 1. The Citizen Default Behavior starts as the needs (with Rest only below 5%, since citizens sleep the night
  through, and Sleep at night last), then Auto-Construct, then Patrol Spawn; hauling and harvesting tasks added to it
  go between those two. The Military and Citizen Default Behaviors are edited there too but aren't groups: a unit of yours that
  joins the military gets the Military one. Auto-Harvest, Auto-Haul and Auto-Haul Bones are only offered for props in your
  claim.
- **Ghosts.** A unit of yours that dies joins the Graveyard as a Ghost. Each Ghost wants one bones (any kind) sacrificed
  at your Altar: right-click it and choose Appease Ghosts. A Ghost left unappeased for 24 game hours haunts your whole
  faction, and Haunted units hit and walk slower. The Altar keeps bones ahead of time, brought by hand or by Auto-Haul
  Bones; the same bones keep it burning.
- **Stockpiles.** Auto-Harvest of Trees puts the Wood Logs in a Wood Stockpile, and of Boulders (Mining) the Stone in a
  Stone Stockpile, once a unit carries a load. With no stockpile of that kind with room, the task waits. Giving a unit Auto-Harvest also gives it Auto-Haul to the nearest store of yours that takes what it yields: the matching stockpile for logs and stone, any storage for the rest. A haul task is always put just above the first Auto-Harvest in the list, so carrying a load off comes first. Whenever what a unit carries changes, a haul task above the one it is doing (harvesting, patrolling) takes over if it has something to take there.
- **Airships.** An Airship floats above the ground and carries up to four units. Getting on (Occupy) takes five seconds,
  and only happens if there is still room then; getting off (Evacuate) takes one, and only over open ground. A move order
  for those aboard flies it in a straight line over anything, water and void included. Melee can't reach anyone aboard,
  and a unit shot at from one that can't shoot back flees out of range until it has been clear long enough to calm
  down. If it is destroyed with units aboard they die. A map with land that can't be walked to starts you with one.
- **Buildings.** In the Build tab, drag out a Foundation (3 to 64 tiles a side, in your claim). A foundation, wall or
  door first asks what to make it of: wood (Wood Logs), iron (Iron Ingots) or Dragon Crystals; iron is twice as tough to
  break as wood and crystal three times, and a wall twice as tough as a door. Walls, Doors, Ladders and Upper Floors go
  on a finished foundation, one tile each (drag to lay a row of them): walls and doors on any tile, and ladders and upper
  floors only once every edge tile has a wall or a door. A building whose walls and doors are all gone loses its upper floors. Each piece starts as a blueprint
  needing one of its material (an upper floor one per tile); the size of a foundation shows beside the cursor as you drag.
  Right-click a blueprint for Auto-Construct, which brings the material any of your blueprints needs (carried or fetched
  from your stockpiles and stores) and builds those that have it, from up to five tiles away. A blueprint holds its
  material until building starts, when it is spent. Cancel Construction removes the blueprint and drops what it held; a
  cancelled foundation takes the blueprints on it with it. A piece isn't finished while a unit stands where it goes: the unit is asked to move, and the builder comes back to
  it later. Walls take the shape that fits the walls beside them, and close their tiles. A door lets its own faction
  and its allies through even when locked; others have to pick the lock or smash it, and smash walls they run into. Climb a Ladder to stand on the
  Upper Floor beside it: up there melee from the ground can't reach you, and you can shoot down.
- **Combat.** Melee and ranged weapons (bows and crossbows with ammunition), armor, blocking, dodging, gear tiers and
  elixirs.
- **Wildlife.** Animals from chickens and fish to bears and wolves, plus the Gorlicorn, Griffin and Manticore bosses, each with its own faction,
  meat and bones. Camps keep some in cages, and a Custom Squad can bring regular mobs.
- **Starting out.** Pick a map and level first, with a preview, then start as a Vagabond (one unit with nothing, every
  stat at level 1) or build a Custom Squad.
- **World and time.** A chunked terrain world with sea and sky, a world map, and a day/night calendar (one in-game day
  is 30 real minutes). New Game always starts on the main world.
- **Saves.** Plain-text saves in the player's own folder (`%APPDATA%\KoboldSurvival\saves` on Windows), with manual saves
  and three rotating autosave slots. A save holds the whole level except its terrain. Saves from an older version of the
  format are upgraded when read, and new props, mobs, stats and behaviors don't affect existing saves. Settings are kept in
  the folder above them.
- **Localization.** 31 languages with Noto fonts for Latin, Cyrillic, Greek, Arabic, Thai, Japanese, Korean and
  Chinese scripts.
- **Audio.** Music, ambient tracks, sound effects and interface sounds, each with its own volume slider, under a global
  one.

## Controls

| Input | Action |
| --- | --- |
| Left click / drag | Select units. Shift adds to the selection. A box around ground items, and no units you haven't selected, picks the items up |
| Right click on your Altar | Manage Units (move units between the military and the citizens), Behavior Groups or Appease Ghosts, with or without units selected |
| Optimize All / Shift + click | Optimize the gear of the squad shown, or first choose how |
| Numpad 1 to 9 | Select that squad. Shift adds it to the selection, and pressing it twice brings the camera to it |
| Ctrl + Numpad 1 to 9 | Move the selected units into that squad |
| Numpad 0 | Select your unit nearest the camera's centre |
| Right click | Orders and context menus (move, use a prop, talk, break bones or crystals, put down) |
| Right mouse held | Selected units walk straight at the pointer |
| Shift with moving a stack | Choose how many to trade or store. Without Shift, the whole stack moves |
| WASD / arrow keys | Move the camera. Shift moves it faster |
| Middle mouse drag | Orbit the camera |
| Mouse wheel | Zoom, or scroll lists |
| Alt (held) | Show the names of items on the ground. With several units selected, also show the formation keys |
| Alt + 1 / 2 / 3 / 4 | Selected units form a line, circle, rectangle or wedge, and later orders use that shape |
| Alt + 5 | Switch between tight and loose formations |
| Alt + right mouse drag | Place the selected units across the drag, looking away from where they are now |
| Space | Pause or resume, also during a conversation |
| M | Open or close the world map |
| Escape | Cancel, back out of menus, open the game menu |
| F12 | Take a screenshot |

## Building

Requirements:

- CMake 3.16 or newer and a C++17 compiler (the project is set up for MSVC and Ninja on Windows through
  `CMakeSettings.json`, with x64-Debug and x64-Release configurations)
- SDL3, SDL3_ttf, SDL3_image, OpenGL and GLEW, findable by CMake. `vcpkg.json` pins their versions: configure with vcpkg's
  toolchain file and it installs them

tinygltf and miniaudio are downloaded automatically by CMake's FetchContent.

```bash
cmake -S . -B out/build -G Ninja -DCMAKE_TOOLCHAIN_FILE=<vcpkg root>/scripts/buildsystems/vcpkg.cmake
```

```bash
cmake --build out/build
```

Or open the folder in Visual Studio and build the `koboldsurvival` target.

The build also:

- builds `tools/pack_data` and packs `data/` into `data.pak` next to the executable (the game reads only the pak),
- copies `data/language`, `BGM`, `SFX` and `MAP` next to the executable as loose folders,
- stamps the build time (`BUILD.YYYY.MM.DD.HH.MM.SS`) into the title screen through `tools/build_stamp`.

## Project layout

```
src/                 Game source (an ECS-style design). The simulation is a library, kobold_sim, with no graphics
  main.cpp, app*.cpp  Start-up, the frame loop, input and the screens
  world_gen, level_file    The world generator and map files, and level files
  app_worldgen, editor_ui  The World Generation screen and level editor
  map_theme            How a generated map looks: its sky, light, water and ground color
  simulation         One step of the game, with no window, and the events it reports to the screens
  orders, steering, automation, wild_ai, downed, carry, loot, needs, prop_use   The simulation systems
  world_setup, unit_spawn   Placing a new level, and making units
  squads, altar_ui     Squads of 16 and citizens, and the Altar's roster
  behavior_groups      The Altar's behavior groups and Default Behaviors, and citizens taking their group's tasks
  app_behavior_groups  The Behavior Groups window
  ghosts, airship      The Graveyard and Haunted, and Airships
  building             Foundations, blueprints and the building tasks, walls, doors, ladders and the upper floor
  app_building         The Build tab's foundation drag and pieces
  app_map_pick         New Game's map and level page
  renderer*.cpp      OpenGL rendering of terrain, models, sky and UI
  combat, equip, inventory, items, factions, claims, build, party, ...
  save_world, savegame, save_migrate     Turning a running level into a save, the save files, and upgrading old ones
  game_log, world_labels   The message log and the text drawn over units
  look, surface_tiles      A world's look (sky, sun, rock and water colors) and its surface textures
  chunk_source, world_format   Where a Terrain gets its chunks, and the world folder format
tests/
  save_roundtrip.cpp Saves a level, loads it and compares
  data_check.cpp     Checks the data tables agree, and lists text not yet translated
  sim_step.cpp       Plays with no window, and checks the same seed gives the same game
  terrain_check.cpp  Checks the ground on the main map and on a huge made-up map
  saves/             A save of each older format version, which must still load
                     (all run with ctest; none opens a window)
tools/
  pack_data.cpp      Packs data/ into data.pak
  build_stamp.cpp    Writes the build stamp shown on the title screen
  make_icons.py, make_items.py, make_emotes.py   Draw the icons, item sprites and emotes (Python and Pillow; not part of the build)
  make_models.py     Makes simple low-poly prop models (Python and Pillow; not part of the build)
  make_sfx.py        Synthesizes the sound effects in SFX/ (Python, NumPy and ffmpeg; not part of the build)
data/                Game content, packed into data.pak
  itemdb.txt         The items
  factions.txt       Factions and starting reputations
  mobs.txt           Every mob's stats, temperament, meat, bones, picture and model
  sfx.txt, bgm.txt   Which sound or track plays for what
  props.txt          Every prop: what it does, its model and its rules
  emotes.txt, icons.txt, models.txt   Which file each emote, icon and unit uses
  fonts.txt, languages.txt   The built-in fonts, and each language's first choice
  motd.txt           The title screen's notice board
  language/          One KEY<TAB>text file per language (English is the reference)
  map/main/          The main world (world.txt, look.json, surface, region files)
  model/             glTF (.glb) models for props, units and mobs
  item/ icon/ emote/ mob/ profile/ illustration/   PNG pictures
  font/              Noto fonts
BGM/                 Music and ambient tracks (.mp3)
SFX/                 Sound effects (.mp3), made by tools/make_sfx.py
MAP/                 Maps made by World Generation (.map), with their levels in MAP/LEVEL (.level)
```

## World Generation and the level editor

World Generation, on the title screen above Settings, opens the map settings and a preview. Generate writes a tiny recipe to `MAP/<name>.map` beside the game, and the game works out the ground from it as it
goes. Continue opens the level editor, which is regular gameplay with no units and time stopped. Tab switches between its own
interface and the game's. It saves to `MAP/LEVEL/<name>.level`. The party screen of New Game can pick a map and level. The editor writes beside the game: copy a finished map and level into `MAP/` here to keep them, since the build copies
this folder over the one beside the game.


The editor places entities, props and items. The words mean the same in the game and the code:

- An **object** is anything in a `World`, named by an `ObjectId`. What it is comes from its components.
- An **entity** is a living thing, with health, that moves: it has the `Entity` component. Props and ground items are objects, but not entities.
- A **unit** is an entity that is a person: a player unit (`Commandable`) or an NPC of another faction.
- A **mob** is an entity that also has `Mob`. A **boss mob** is one whose category in `data/mobs.txt` is `boss`.

A player unit placed in a level is the first member of the party from New Game. Party members beyond those placed start at the spawn point, and placed player units beyond the party's size are left out.

Entities collide as circles. A melee fighter claims one of the `ATTACK_SLOTS` (5) places around its target; the others wait in a ring outside and can't hit it until a place opens. Taking damage slows movement for a moment (`DAMAGE_SLOW_SECONDS`, `DAMAGE_SLOW_FACTOR` in combat.h); more hits restart the moment but don't slow it further. Carried weight slows a unit in proportion to its load, but never below `MIN_LOAD_SPEED` (10%) of its speed (encumbrance.h).

A squad of two or more walks at its slowest member's speed, in the chosen formation (`formation.h`). Slots go to the nearest units so paths don't cross, and a blocked slot moves to the nearest open place. After arriving, a unit that is blocked trades slots with a squadmate if that shortens both walks, or settles after a moment and tries again later, so nobody pushes forever. Settled units step aside for others.

A map has a style, chosen before it is generated (`MapStyle` in world_gen.h: Overworld, Walled Valley, Archipelago, Cavern, Galaxy Sea, Void Islands, Caldera), and a theme: sky, sun, moon, day and night, light, fog, water and ground color (`map_theme.h`). The Theme tab is on the World Generation screen and in the level editor, where it shows at once and Save writes it to the map. A map file lists only what differs from its theme preset. To add a theme setting, add a `MapTheme` member and a row in `map_theme.cpp`.

## Modding the data

Most content is plain text in `data/`, and the game reads those tables at start-up, so changing a file name or a mob's
stats in them changes the game after a rebuild packs them. Each table explains its columns at the top. If a table has a
mistake, or two tables disagree, the game stops at start-up and says what is wrong. A missing picture or model is drawn as
a pink square.

A new prop or mob needs no code: add a row to `data/props.txt` or `data/mobs.txt`, its language keys and its art. A new mob
also needs a line in `data/factions.txt`. A new sound or emote still needs code, since its key is in `src/game_tables.cpp`.

The icons, item sprites, emotes and sound effects are made by the scripts in `tools/`. Each explains how to add a new one at
the top of the file.

Language files copied to the `LANGUAGE` folder next to the game override the packed copy key by key, so text can be
edited without rebuilding.
