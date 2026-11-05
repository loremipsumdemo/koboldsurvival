# Kobold Survival

A 3D colony survival game written in C++17 with SDL3 and OpenGL. You lead a small party of units across a large
terrain world. They gather resources, build, eat, rest, fight and trade among rival factions and wild animals.

## Features

- **Units with stats and needs.** Each unit has stats such as Strength, Dexterity, Vitality and Melee, and needs:
  health, hunger, Rested and Comfort. A unit at 0 health is knocked out, and one whose Doom reaches 100 dies.
- **Behaviors.** Each unit works through a priority list of behaviors (Eat, Heal, Share, Rest, Rescue, plus
  automated ones like Auto-Harvest and Auto-Haul). You can reorder, switch off or add behaviors per unit.
- **Gathering and crafting.** Mine ore, cut trees, gather mushrooms, cook meat at campfires, and store items in
  barrels, chests and specialised storage.
- **Building and claims.** Lay foundations, plant Claim Flags to mark territory, and keep Vaults stocked so the
  claims do not decay.
- **Factions and reputation.** Rival camps start at set reputations with you. Below -25 they attack on sight, and
  at 50 or more they are allies. Talk to NPCs, trade with them, take prisoners, or disband units into the Exiles.
- **Combat.** Melee and ranged weapons (bows and crossbows with ammunition), armor, durability and gear tiers.
- **Wildlife.** Animals such as bears, wolves, deer and fish, each with its own faction, meat and bones. Some can
  be caged or brought into your party.
- **Scenarios.** Start as a lone Vagabond, a Pack of Two, a Beastmaster, a Full Random Camp, a Traveling Caravan,
  a Shabby Crew or a Starving Nobody, then adjust the party before play.
- **World and time.** A chunked, region-streamed terrain world with sea and sky, a world map, and a day/night
  calendar (one in-game day is 30 real minutes).
- **Saves.** Plain-text saves in `saves/`, with three rotating autosave slots.
- **Localization.** 31 languages with Noto fonts for Latin, Cyrillic, Greek, Arabic, Thai, Japanese, Korean and
  Chinese scripts.
- **Audio.** Music, ambient tracks and sound effects, each with its own volume slider.

## Controls

| Input | Action |
| --- | --- |
| Left click / drag | Select units |
| Right click | Orders and context menus (move, use a prop, talk, put down) |
| WASD / arrow keys | Move the camera |
| Middle mouse drag | Orbit the camera |
| Mouse wheel | Zoom, or scroll lists |
| R (held while placing) | Rotate a building |
| Space | Pause or resume |
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

On Windows the game runs without a console window, and log output goes to `log.txt` beside the executable.

## Project layout

```
src/                 Game source (single executable, an ECS-style design)
  main.cpp           Main loop, screens, input
  systems.cpp        Simulation systems: needs, behaviors, automation
  renderer*.cpp      OpenGL rendering of terrain, models, sky and UI
  combat, equip, inventory, items, factions, claims, build, party, ...
  game_log, world_labels   The message log and the text drawn over units
  look, surface_tiles      A world's look (sky, sun, rock and water colors) and its surface textures
  world_format, map_file   The world folder format, and the older single-file .map format
tools/
  pack_data.cpp      Packs data/ into data.pak
  build_stamp.cpp    Writes the build stamp shown on the title screen
data/                Game content, packed into data.pak
  itemdb.txt         Item database
  buildinfo.txt      Buildable structures
  factioninfo.txt    Factions and starting reputations
  language/          One KEY<TAB>text file per language (English is the reference)
  map/main/          The main world (world.txt, look.json, surface, region files)
  model/             glTF (.glb) models for props, units and mobs
  item/ icon/ emote/ mob/ profile/   PNG pictures
  font/              Noto fonts
BGM/                 Music and ambient tracks (.mp3)
SFX/                 Sound effects (.mp3)
```

## Modding the data

Most content is plain text or numbered files in `data/`. Each `*info.txt` file in `data/` explains its folder: what
each numbered picture, model, sound or track is for, and how to add a new one (it takes the next free number).
A missing picture or model falls back to its `_0` stand-in.

Language files copied to the `LANGUAGE` folder next to the game override the packed copy key by key, so text can be
edited without rebuilding.
