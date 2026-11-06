# Kobold Survival

A 3D survival sandbox simulation written in C++17 with SDL3 and OpenGL. You lead a party of units across a terrain
world where they gather, build, eat, rest, fight and trade among rival factions and wild animals. There is no win or
lose state: the world keeps running, whatever happens to your party.

## Features

- **Units with stats and needs.** Each unit has attributes and skills such as Strength, Dexterity, Vitality, Melee,
  Mining and Cooking, which rise with use, and needs: health, hunger, Rested and Comfort.
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
- **Behaviors.** Each unit works through a priority list: Eat, Heal, Share, Rest, Leave Bed, Rescue, Auto-Harvest,
  Auto-Haul, Patrol, Patrol Spawn and Roam. You can reorder, switch off, add or remove them per unit. Units can also be
  set to Passive, Block or Sneak.
- **Gathering and crafting.** Mine ore, cut trees and gather mushrooms. Campfires and forges work by themselves from
  their fuel, and units work stoves, water pumps and training dummies. Items are kept in barrels, chests and
  specialised storage.
- **Building and claims.** The Build tab places props at once and at no cost, and its Destroy tool takes them down
  (Bones leave their item, Debris is cleared away, and anything else leaves Debris). A faction has one Altar, named
  for it ("Altar of Red Faction"), which claims the ground around it. Only its own faction can open it, and there is
  no lock to pick. It burns Wood Logs as upkeep. With none left it decays, and when it breaks the claim is gone and the
  props in the circle belong to nobody.
- **Factions and reputation.** Rival camps start at set reputations with you. At -25 or below they attack on sight, and
  at 50 or more they are allies. The game keeps running while you talk or trade, unless you turn on Automatic Conversation
  Pause in Settings or press Space. Talk to NPCs, trade with them, lock units in Prisoner Cages, pick locks, or disband
  your units into the Exiles.
- **Combat.** Melee and ranged weapons (bows and crossbows with ammunition), armor, blocking, dodging, gear tiers and
  elixirs.
- **Wildlife.** Animals from chickens and fish to bears and wolves, plus the Gorlicorn, Griffin and Manticore bosses, each with its own faction,
  meat and bones. Camps keep some in cages, and some scenarios start with them in your party.
- **Scenarios.** Start as a Vagabond, a Pack of Two, a Beastmaster, a Full Random Camp, a Traveling Caravan, a Shabby
  Crew, a Starving Nobody or the Dungeon Diggers, or build a Custom Scenario, then adjust the party before play.
- **World and time.** A chunked terrain world with sea and sky, a world map, and a day/night calendar (one in-game day
  is 30 real minutes). New Game always starts on the main world.
- **Saves.** Plain-text saves in the player's own folder (`%APPDATA%\KoboldSurvival\saves` on Windows), with manual saves
  and three rotating autosave slots. A save holds the whole level except its terrain. Saves of another format version
  are not listed. Settings are kept in the folder above them.
- **Localization.** 31 languages with Noto fonts for Latin, Cyrillic, Greek, Arabic, Thai, Japanese, Korean and
  Chinese scripts.
- **Audio.** Music, ambient tracks, sound effects and interface sounds, each with its own volume slider, under a global
  one.

## Controls

| Input | Action |
| --- | --- |
| Left click / drag | Select units. Shift adds to the selection. A box around only ground items picks them up |
| Right click | Orders and context menus (move, use a prop, talk, destroy bones, put down) |
| Right mouse held | Selected units walk straight at the pointer |
| WASD / arrow keys | Move the camera. Shift moves it faster |
| Middle mouse drag | Orbit the camera |
| Mouse wheel | Zoom, or scroll lists |
| Alt (held) | Show the names of items on the ground |
| Space | Pause or resume, also during a conversation |
| M | Open or close the world map |
| Escape | Cancel, back out of menus, open the game menu |
| F12 | Take a screenshot |

## Building

Requirements:

- CMake 3.16 or newer and a C++17 compiler (the project is set up for MSVC and Ninja on Windows through
  `CMakeSettings.json`, with x64-Debug and x64-Release configurations)
- SDL3, SDL3_ttf, SDL3_image, OpenGL and GLEW, findable by CMake (for example through vcpkg)

tinygltf and miniaudio are downloaded automatically by CMake's FetchContent.

```bash
cmake -S . -B out/build -G Ninja
```

```bash
cmake --build out/build
```

Or open the folder in Visual Studio and build the `koboldsurvival` target.

The build also:

- builds `tools/pack_data` and packs `data/` into `data.pak` next to the executable (the game reads only the pak),
- copies `data/language`, `BGM` and `SFX` next to the executable as loose `LANGUAGE`, `BGM` and `SFX` folders,
- stamps the build time (`BUILD.YYYY.MM.DD.HH.MM.SS`) into the title screen through `tools/build_stamp`.

## Project layout

```
src/                 Game source (single executable, an ECS-style design)
  main.cpp, app*.cpp  Start-up, the frame loop, input, the screens and the game's step
  orders, steering, automation, wild_ai, downed, carry, loot, needs, prop_use   The simulation systems
  world_setup, unit_spawn   Placing a new level, and making units
  renderer*.cpp      OpenGL rendering of terrain, models, sky and UI
  combat, equip, inventory, items, factions, claims, build, party, ...
  save_world, savegame     Turning a running level into a save, and the save files
  game_log, world_labels   The message log and the text drawn over units
  look, surface_tiles      A world's look (sky, sun, rock and water colors) and its surface textures
  world_format, map_file   The world folder format, and the blank map a Terrain starts as
tests/
  save_roundtrip.cpp Saves a level, loads it and compares (run with ctest; opens no window)
tools/
  pack_data.cpp      Packs data/ into data.pak
  build_stamp.cpp    Writes the build stamp shown on the title screen
data/                Game content, packed into data.pak
  itemdb.txt         The items
  factions.txt       Factions and starting reputations
  mobs.txt           Every mob's stats, temperament, meat, bones, picture and model
  sfx.txt, bgm.txt   Which sound or track plays for what
  emotes.txt, icons.txt, models.txt   Which file each emote, icon, prop and unit uses
  fonts.txt, languages.txt   The built-in fonts, and each language's first choice
  motd.txt           The title screen's notice board
  language/          One KEY<TAB>text file per language (English is the reference)
  map/main/          The main world (world.txt, look.json, surface, region files)
  model/             glTF (.glb) models for props, units and mobs
  item/ icon/ emote/ mob/ profile/ illustration/   PNG pictures
  font/              Noto fonts
BGM/                 Music and ambient tracks (.mp3)
SFX/                 Sound effects (.mp3)
```

## Modding the data

Most content is plain text in `data/`, and the game reads those tables at start-up, so changing a file name or a mob's
stats in them changes the game after a rebuild packs them. Each table explains its columns at the top. Keys are fixed by the
code (`src/game_tables.cpp`): a new sound, emote or mob kind also needs code. If a table has a mistake, the game stops at
start-up and names the file and line. A missing picture or model is drawn as a pink square.

Buildable props and their rules are in `src/props.h` and `src/build.cpp`.

Language files copied to the `LANGUAGE` folder next to the game override the packed copy key by key, so text can be
edited without rebuilding.
