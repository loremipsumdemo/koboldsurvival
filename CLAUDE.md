# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

Kobold Survival is a 3D, real-time (pausable) squad and colony game in C++17 on SDL3 and OpenGL 3.3 core, built for Windows with MSVC, Ninja and vcpkg. `src/` has about 160 `.cpp` files (~70k lines). Almost all content (items, props, mobs, recipes, factions, text) is data under `data/`. There is no test suite and no linter or formatter.

`README.md` has the player controls and the rules for text and languages. The `MAP/**/README.md` files document the map, level, scenario and blueprint file formats.

## Build and run

Dependencies: the vcpkg manifest (`vcpkg.json`: glew, sdl3, sdl3-image[png], sdl3-ttf); tinygltf and miniaudio come from CMake `FetchContent` at pinned tags.

Visual Studio's CMake integration reads `CMakeSettings.json`: `x64-Debug`, `x64-Release` (actually RelWithDebInfo, with full inlining and LTO) and `x64-Profile` (Release plus `-DKOBOLD_PROFILE=ON`), each built with Ninja in `out/build/<name>`. CI (`.github/workflows/build.yml`) only configures and builds.

From a bare shell, run VS's `Common7\Tools\VsDevCmd.bat -arch=x64 -host_arch=x64` first, then:

```powershell
cmake -G Ninja -B out/build/x64-Release -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build out/build/x64-Release
```

The first command configures once (`C:/vcpkg` is wherever vcpkg lives); after that only the build command is needed. The Profile build is the same with `-B out/build/x64-Profile -DCMAKE_BUILD_TYPE=Release -DKOBOLD_PROFILE=ON`. A full build is about 135 Ninja steps and takes minutes. `app.h` is included by ~40 files and the world, terrain and level headers by many more, so batch header edits into one build. Close a running game first, because it locks the exe.

- **Sources are listed by hand in `CMakeLists.txt`** (no glob). A new `.cpp` goes in `SIM_SOURCES` (the `kobold_sim` library) or in the `koboldsurvival` executable list.
- Next to `koboldsurvival.exe` the build produces `data.pak` and copies of `LANGUAGE/`, `BGM/`, `SFX/` and `MAP/`. The `game_data` target repacks all of `data/` on every build, and the game reads only the pak, so **any edit under `data/` (language files too) needs a build**. A loose file in `LANGUAGE/` overrides the packed language file, but the build wipes and recopies that folder, so edit the repo's copy, never the one beside the exe.
- The game is a GUI-subsystem exe that needs a real OpenGL 3.3 context. It refuses to start without one, and there is no headless mode. `kobold_sim` links only SDL3 and is built with LTO, so a throwaway `main` kept outside the repo can link `kobold_sim.lib` (with `/LTCG`) to run the generator, terrain or world set-up without a GPU.
- The owner removed the headless test programs on purpose (linking them doubled build time). Don't add test targets. Changes are checked by building and playing, so finish a batch with one build and a short list of things to try in game.

### Debugging aids

- Settings > Graphics > *Debug logging* writes timestamped, tagged lines (`[session]`, `[level]`, `[overview]`, `[hitch]`, `[test]`, `[bench]`) to `logs.txt` in the user-files folder, `%APPDATA%\KoboldSurvival\`, which also holds `settings.cfg` and `saves/`. Use it instead of screenshots for unattended checks.
- With `KOBOLD_BENCH=1` in the environment, `app_bench.cpp` turns logging on, loads the newest save, cycles the cases in `app_build_test.cpp` and quits. That file is scratch space for the build currently under test ("THE CURRENT BUILD'S TESTING"): rewrite or empty it, and don't treat it as permanent code.
- In the Profile build, `PROFILED(name, expr)` and `PROFILE_SCOPE(name)` (`profile.h`) time every `simStep` system and print a report through `SDL_Log` every 10 s. That goes to stderr, not `logs.txt` (the comment in `profile.h` is out of date), so launch the exe with stderr redirected. In other builds they compile away to the bare expression.
- The Build tab's Developer panel spawns, kills, cleans and destroys things and can draw the navmesh. F12 saves a screenshot to `screenshots/` beside the exe.

## Architecture

### Two targets: simulation and app

- `kobold_sim` (static library, SDL3 only, no window or GL): the ECS, `simStep` and every `*System`, factions, combat, needs, behaviors, building and claims, terrain and the world generator, the file formats (saves, `.map`, `.level`, `.scenario`, `.blueprint`), the data tables and language.
- `koboldsurvival` (exe): `App` (`app.h`, one large struct whose members are spread over the `app_*.cpp` files), `Renderer` (`renderer*.cpp`), UI layout and menus, audio (miniaudio), model loading (tinygltf), settings, and `level.cpp` (which turns a map, level or save into a World plus Terrain).

The sim reaches the UI only through the `SimEvents` world resource (`queueSound`, `queueLog`, `queueUnitLabel` and so on), which `takeEvents` drains into `App::showEvents`. Keep GL and UI types out of the sim.

### The World and the simulation step

- `ecs.h` is a hand-rolled ECS. Everything in a `World` is an `ObjectId` (units, mobs, props, ground items, squads), and its components say what it is. Components are plain structs in `components.h` and in feature headers such as `building.h`, `claims.h` and `stuck.h`. `Entity` marks a living thing (a unit, or a mob = `Entity` + `Mob`). `view<A, B>()` returns a copy, so loops may add and remove components. `resource<T>()` is a per-world singleton (`SimEvents`, `WorldClock`, `LevelInfo`). `revision<T>()` changes only on add or remove, and caches key on it.
- `simStep(World&, Terrain&, Rng&, GameState&, dt, running)` (`simulation.cpp`) calls about 50 `*System` functions in a hand-ordered sequence. Order matters: fighting runs first so a unit that dies is gone before others move, behaviors run after combat, and the slow once-a-second systems come last. It is deterministic for a given world, seed and step count, so use the `Rng&` you are passed and never `rand` or the clock. Many system `.cpp` files include `systems_internal.h` (shared helpers) and `using namespace systems_detail;`.
- `App::stepSimulation` runs fixed 1/60 s steps, at most 5 per frame. For drawing, `placeUnitsBetweenSteps` moves each unit's `Transform` partway between its last two step positions and `putUnitsBack` restores it at the end of the frame. Everything in between (rendering, hover) must only read positions. Input is handled at the start of the frame, on true positions.
- Unit orders are components, and `systems_internal.h` (`endPropTasks`, `clearOrders`, `hasPlayerOrder`) is where they are cleared and recognised: a new order component has to be registered there (see the comment above `hasPlayerOrder`). Autonomous work comes from `Behaviors` (`behavior.h`: `BehaviorKind` rules such as Eat, Auto-Harvest and Auto-Haul, saved by key) plus Behavior Groups and Schedules. NPC factions add camp, builder and role systems on top.
- Where things live: combat `combat.cpp`; who is hostile to whom `hostility.cpp`; orders `orders.cpp`; steering and squads `steering.cpp`; paths `pathfind.cpp` plus `nav_regions.cpp` (hierarchical); paced walks and un-sticking `stuck.cpp`; props, work and crafting `prop_use.cpp`, `automation.cpp`, `recipes.cpp`; building and land `building*.cpp`, `build_plan.cpp`, `claims.cpp`, `gates.cpp`; NPC factions `npc_camps.cpp`, `npc_builders.cpp` (builds from `MAP/BLUEPRINT` plans), `npc_roles.cpp`, `faction_rules.cpp`, `faction_land.cpp`, `commerce.cpp`; wild animals `wild_ai.cpp`; new-game set-up `world_setup.cpp`, `unit_spawn.cpp`, `scenario.cpp`. Behavior is specified in the header comments (`stuck.h`, `claims.h`, `factions.h`), so read them before changing a system.
- Per-game UI state that names objects lives in `AppSession` (`app.h`) and `play::PlaySession` (`play_state.h`). Both are replaced wholesale on every level start (`enterLevel`, `resetPlayState`), because `ObjectId`s restart with each World.

### Maps, levels and saves

On the title screen the content pipeline runs **World Generation** (a `.map` is a seed plus generator settings, not terrain), then the **Map Editor** (terrain brushes, kept as `MAP/<name>.edits`), then the **Level Editor** (placements, `MAP/LEVEL/*.level`), then **New Game** (map, level and scenario). **Building Blueprints** (`MAP/BLUEPRINT/*.blueprint`) are the plans NPC factions build from.

- `Terrain` (`terrain.h`) pulls 64x64-tile chunks from a `ChunkSource` (`chunk_source.h`): flat ground, a world folder, or `generatedChunkSource` (`world_gen.cpp`), wrapped by `editedSource` for Map Editor edits. It streams chunks around the camera and units, keeps up to 1024 for the simulation, and draws a coarse far field beyond them. Maps are 512 to 65536 tiles wide, and the game is made and tested for up to 8192.
- The generator works in whole numbers, so a recipe gives bit-identical ground on every machine. If what a recipe makes must change, bump `WORLD_GEN_VERSION` and keep the old path working.
- The Level Editor runs on ordinary gameplay code, and the World is the document (`applyLevel` and `captureLevel` in `level_file.cpp`). The Map Editor is the same screen with terrain brushes (the `editing` and `mapEditing` flags).
- Saves (`savegame.h`, `save_world.cpp`) are plain text, one record per line, with props, mobs, behaviors and stats stored by key. When a line's format changes, bump `SAVE_VERSION` and add a step to `save_migrate.cpp`. Unit models are saved by index (`UNIT_KEYS`), so only append to that list.
- Maps, levels, scenarios and blueprints made in the running game are written to the `MAP/` copy beside the exe (e.g. `out/build/x64-Release/MAP`), not to the repo. Copy one into the repo's `MAP/` to ship it.

### Data tables

The tables in `data/*.txt` (`itemdb`, `props`, `mobs`, `recipes`, `factions`, `models`, `sfx`, `bgm`, `emotes`, `icons`, `fonts`, `languages`, `patrons`) are read from the pak by `loadGameTables` (`game_tables.cpp`) and cross-checked by `gameDataProblems()` (`game_data_check.cpp`). A mistake is a fatal start-up error ("The game's data has a mistake: ...") that says what is wrong, with the file and line for a malformed row. Each file's header comment documents its columns and flags, so read it before editing.

- Prefer a new column or flag over a new named enum entry (`props.h` explains why). A plain prop is just a `props.txt` row plus a model.
- Kinds the code names (`Sfx`, `MobKind`, the named `PropKind`s, `EmoteId`, icon ids) each have a parallel key array in `game_tables.cpp` (`SFX_KEYS`, `MOB_KEYS`, `PROP_KEYS`, `EMOTE_KEYS`, `ICON_KEYS`). Adding one takes the enum entry, the key array entry, the table row and, for art, the script. `tools/asset_names.py` regex-parses `EMOTE_KEYS` and `ICON_KEYS`, so keep their `{ "Key", N }` shape.
- Keys and ids are persisted in saves and levels. Never rename or reuse a prop, mob or recipe key or a faction id (`currentPropKey` in `props.h` aliases retired ones).
- A prop with a 3D model carries its particle origin in `props.txt` (the `fire:x:y:z:size` flag), measured on the `.glb`. Update it whenever the model changes.

### Text and languages

Everything the player can read is a key: `lang("KEY")` or `langf("KEY", { ... })`, never English in code. Keys live in `data/language/language_en.txt` (the base) and `language_zh-CN.txt`, with the same keys in the same order under the same `#` category headers. Nothing verifies that. A key missing from one language falls back to its English, and a key missing everywhere shows its own name. `python tools/sort_language.py . --write` files new keys under their category in every language file.

### UI and rendering

- Layouts are recomputed every frame. Pure functions in `ui_layout_*.{h,cpp}` (e.g. `layoutStorage(W, H, ...)` returns a struct of `Rect`s) feed both drawing (`App::draw*` in `app_render*.cpp`, calling `Renderer::draw*` in `renderer_*.cpp`) and hit-testing (`App::pressOn*` and the `app_input*.cpp` files). A new popup therefore needs a layout, a draw call, press, wheel and key handling at the right place in the input chain (topmost first, mirroring draw order), and a closing rule (`closeStalePanels`).
- Menu screens are the `Screen` enum plus `Button{rect, Action, param}` lists handled by `App::performAction` (`app_actions.cpp`).
- The renderer uses GL 3.3 core, with shaders as raw strings inside `renderer.cpp` and `renderer_terrain.cpp`. Models are drawn instanced with distance LODs. Units are two rigged GLBs (`f1`, `m1`) skinned on the GPU, and `Renderer::unitPoseJoints` picks the clip (idle, walk, attack, consume, dance, run). Terrain is chunk meshes plus a far-field mesh (`terrain_mesh.cpp`, `renderer_terrain.cpp`).

## Asset tools (`tools/`)

Python 3 with Pillow (`make_sfx.py` also needs NumPy and ffmpeg) generates the art and audio from code, and each script's docstring explains how to add an asset. They write into `data/` (or `SFX/`), after which you rebuild: `make_models.py` (prop GLBs named by `props.txt`), `make_items.py` (item sprites from `itemdb.txt`), `make_icons.py`, `make_emotes.py`, `make_building_textures.py`, `make_sfx.py`, and `make_unit_clips.py` (adds clips to the rigged `data/model/unit/f1.glb` and `m1.glb`). **They overwrite every asset they list, including ones replaced by hand.** Pass the prop keys, sound keys or names you mean to regenerate, and check `git status` afterwards. Previews go to `out/art_preview/`. When making or editing a model, never leave two parts sharing a coplanar face (z-fighting): bury or push one out by 0.01 to 0.02 in the first pass.

`tools/pack_data.cpp` (the pak packer, which also fills in the see-through pixels of PNGs) and `tools/build_stamp.cpp` (the title screen's build stamp) are built by CMake.

## Conventions

- There is no formatter config, so match the file you are in: 4-space indent, `camelCase` functions and members, `PascalCase` types, `UPPER_SNAKE` constants.
- `core.autocrlf=true` and there is no `.gitattributes`. Most working-tree files are CRLF and a few are LF. The Edit tool keeps a file's endings, but the Write tool writes LF, so restore CRLF when rewriting a whole CRLF file.
