---
name: perf-profiling
description: Measure and fix frame-rate or hitch problems in Kobold Survival by measurement - CPU (simulation) vs GPU (ground, water, clouds, models), using this repo's Profile build, KOBOLD_BENCH runs and the debug log. Use for low FPS, stutter, hitches, or requests to optimize or profile.
---

# Performance work in this repo

The loop: **set a budget, measure, find the one bottleneck, fix it, measure again, report the numbers.** Do not change code on a guess. The target hardware is weak (integrated Intel graphics at 1440p), so numbers measured on the dev machine are the real ones. 60 FPS is a 16.67 ms frame.

## 1. Measure

- **Uncap first.** VSync and the FPS limit hide headroom (a 4 ms and a 15 ms frame both show 60). A `KOBOLD_BENCH` run turns both off itself; frames in borderless mode may still sit at 16.6 ms, so treat 60 FPS as a ceiling there. Read milliseconds, not FPS.
- **Use an optimized build.** Debug builds skew everything. `x64-Release` is RelWithDebInfo with full inlining and LTO, which is representative.
- **Repeatable scene.** Use a save (the bench loads the newest one) or a fixed seed. Run each comparison three times on the same machine and say how the build was made.
- **Unattended run:** set `KOBOLD_BENCH=1` and launch `koboldsurvival.exe` from its build folder. It switches Debug logging on, cycles the cases in `src/app_build_test.cpp` over two camera views, writes `[bench]` lines (frame ms plus GPU ground/water/cloud pass times) to `%APPDATA%\KoboldSurvival\logs.txt`, and quits. Rewrite or empty `app_build_test.cpp` for each new test; add a case per option you are comparing. Read the log instead of taking screenshots.
- **Hitches:** with Debug logging on, any frame over 25 ms is written as `[hitch]` with the update / drawing / swap split.

## 2. Split CPU from GPU

- GPU: the bench's `ground`, `water`, `clouds` pass times, and `Renderer::passTimes()` (also shown by the *Show FPS* setting). If one pass dominates, look at pixel count first (render scale, ground/water detail bands, clouds, shadows).
- CPU: build the **Profile** configuration (`-DKOBOLD_PROFILE=ON`, see CLAUDE.md). `PROFILED(name, expr)` already wraps every `simStep` system; a report of ms per frame, per call, worst and calls is printed with `SDL_Log` every 10 s, to stderr, so start the exe with `Start-Process -RedirectStandardError <file>`. The run must last over 10 s. The player's maps live in the build folder's `MAP/`; copy it beside the Profile exe or the save will not load.
- No sampling profiler works in the non-admin shell, so finer CPU breakdowns need extra `PROFILE_SCOPE` or `PROFILE_LAP` sites. Adding those is a code change: say so.
- The side closest to the frame time is the bottleneck. Fixing the other side changes nothing.

## 3. Fix the biggest cost, preferring to do less work

Already used here, so check they still hold before adding more: terrain LOD and far-field mesh, model LODs by distance, cached claims, the prop index grid (`prop_index.h`), hierarchical paths (`nav_regions.cpp`), staggered unit thinking, one instanced draw for many models. Recurring ideas:

- Run expensive checks less often or spread them over frames (time-slice), instead of every step.
- Query a spatial grid rather than every object; cache results (paths, sight, claims) and invalidate with `World::revision<T>()`.
- Avoid per-frame allocations and per-frame rebuilds of text, meshes or lists when the inputs have not changed (see `SpotCache`, `HoverCache` in `app.h`).
- Cut state changes and draw calls (instancing, shared materials, atlases). Pixel cost scales with the square of render scale.
- Simulation changes must stay deterministic: same world, seed and steps give the same result.

## 4. Report

Give the scene or save, build type, hardware, before and after (average and worst frame ms, plus the pass or system that moved), the bottleneck and the fix. Say what was measured and what still needs the owner's eyes or play feel. Per project practice, batch the changes, build once, and finish with a short checklist of things to try in game.

## Gotchas

- Measuring with VSync on, or in a Debug build.
- Fixing the wrong side (CPU work when GPU-bound, or the reverse).
- Comparing runs from different scenes, cameras or save states.
- Leaving `app_build_test.cpp` or Debug logging set up for one test when the work is finished.
- Optimizing before a number says it is slow.
