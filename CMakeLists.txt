cmake_minimum_required(VERSION 3.16)
project(koboldsurvival)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# x64-Release builds as RelWithDebInfo, where MSVC defaults to /Ob1 and inlines only functions marked inline.
# Use full inlining, so it runs like a true Release build and still has debug info.
foreach(lang C CXX)
    string(REPLACE "/Ob1" "/Ob2" CMAKE_${lang}_FLAGS_RELWITHDEBINFO "${CMAKE_${lang}_FLAGS_RELWITHDEBINFO}")
endforeach()

find_package(SDL3 CONFIG REQUIRED)
find_package(SDL3_ttf CONFIG REQUIRED)
find_package(SDL3_image CONFIG REQUIRED)
find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)

# Silences the deprecation warning from tinygltf's own CMakeLists.txt
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

include(FetchContent)
set(TINYGLTF_HEADER_ONLY ON CACHE BOOL "" FORCE)
set(TINYGLTF_INSTALL OFF CACHE BOOL "" FORCE)
set(TINYGLTF_BUILD_LOADER_EXAMPLE OFF CACHE BOOL "" FORCE)
set(TINYGLTF_BUILD_GL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(TINYGLTF_BUILD_VALIDATOR_EXAMPLE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(tinygltf
    GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
    GIT_TAG v2.9.3)
FetchContent_MakeAvailable(tinygltf)

# miniaudio: music and sound effects. It decodes MP3 itself and plays through the system's audio API, so it needs
# no other libraries. The pinned release keeps builds reproducible.
set(MINIAUDIO_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG 0.11.25
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(miniaudio)

# Build stamp for the title screen, BUILD.YYYY.MM.DD.HH.MM.SS (24-hour). The stamp is taken at build time, not configure time,
# so build_stamp rewrites the generated source on every build. Listing that file as BYPRODUCTS makes Ninja run the
# writer before it compiles the file; without it, the build can compile the previous build's stamp.
add_executable(build_stamp tools/build_stamp.cpp)
set_target_properties(build_stamp PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools")
set(BUILD_STAMP_CPP "${CMAKE_BINARY_DIR}/generated/build_info.cpp")
if(NOT EXISTS "${BUILD_STAMP_CPP}")
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated")
    file(WRITE "${BUILD_STAMP_CPP}" "extern const char* const BUILD_STAMP = \"BUILD.unset\";\n")
endif()
add_custom_target(write_build_stamp COMMAND build_stamp "${BUILD_STAMP_CPP}" BYPRODUCTS "${BUILD_STAMP_CPP}" VERBATIM)
add_dependencies(write_build_stamp build_stamp)

# The simulation: no window, no graphics. The game and the tests share it. SDL3 is its only library, for file paths and
# the log. The tests link only this, so a call into OpenGL, fonts or image loading from here fails their build.
set(SIM_SOURCES
    src/simulation.cpp
    src/game_log.cpp
    src/world_labels.cpp
    src/commerce.cpp
    src/npc_camps.cpp
    src/items.cpp
    src/language.cpp
    src/fontlist.cpp
    src/inventory.cpp
    src/combat.cpp
    src/encumbrance.cpp
    src/experience.cpp
    src/hostility.cpp
    src/factions.cpp
    src/equip.cpp
    src/stats.cpp
    src/party.cpp
    src/useitem.cpp
    src/need_behaviors.cpp
    src/systems.cpp
    src/needs.cpp
    src/unit_status.cpp
    src/squads.cpp
    src/behavior_groups.cpp
    src/ghosts.cpp
    src/airship.cpp
    src/wheelbarrow.cpp
    src/building.cpp
    src/world_setup.cpp
    src/unit_spawn.cpp
    src/orders.cpp
    src/formation.cpp
    src/steering.cpp
    src/prop_use.cpp
    src/automation.cpp
    src/wild_ai.cpp
    src/downed.cpp
    src/carry.cpp
    src/loot.cpp
    src/terrain.cpp
    src/terrain_mesh.cpp
    src/pathfind.cpp
    src/world_format.cpp
    src/look.cpp
    src/surface_tiles.cpp
    src/chunk_source.cpp
    src/map_theme.cpp
    src/world_gen.cpp
    src/overview.cpp
    src/camera_limit.cpp
    src/level_file.cpp
    src/game_tables.cpp
    src/game_data_check.cpp
    src/pak.cpp
    src/build.cpp
    src/claims.cpp
    src/camp_kit.cpp
    src/prop_info.cpp
    src/save_world.cpp
    src/savegame.cpp
    src/save_migrate.cpp
    src/user_files.cpp
)

add_library(kobold_sim STATIC ${SIM_SOURCES})
target_include_directories(kobold_sim PUBLIC src)
target_link_libraries(kobold_sim PUBLIC SDL3::SDL3)

add_executable(koboldsurvival
    src/play_state.cpp
    src/storage.cpp
    src/trade.cpp
    src/selection.cpp
    src/main.cpp
    src/app.cpp
    src/app_actions.cpp
    src/app_party_actions.cpp
    src/app_settings.cpp
    src/app_saves.cpp
    src/app_bench.cpp
    src/app_debug_log.cpp
    src/app_build_test.cpp
    src/debug_log.cpp
    src/app_panels.cpp
    src/app_panel_grab.cpp
    src/app_talk.cpp
    src/app_worldgen.cpp
    src/app_context_menu.cpp
    src/app_input.cpp
    src/app_input_popups.cpp
    src/app_squads.cpp
    src/app_altar.cpp
    src/app_behavior_groups.cpp
    src/app_store_filter.cpp
    src/app_stall.cpp
    src/app_map_pick.cpp
    src/app_building.cpp
    src/altar_ui.cpp
    src/app_input_panels.cpp
    src/app_update.cpp
    src/app_render.cpp
    src/app_render_play.cpp
    src/level.cpp
    src/map_list.cpp
    src/menus.cpp
    src/app_support.cpp
    src/renderer.cpp
    src/renderer_assets.cpp
    src/ui_layout_panels.cpp
    src/ui_layout_popups.cpp
    src/renderer_text.cpp
    src/renderer_widgets.cpp
    src/renderer_units.cpp
    src/renderer_world.cpp
    src/renderer_preview.cpp
    src/renderer_overlays.cpp
    src/renderer_menus.cpp
    src/renderer_panels.cpp
    src/renderer_sidebar.cpp
    src/renderer_popups.cpp
    src/renderer_altar.cpp
    src/text_cache.cpp
    src/gl_util.cpp
    src/renderer_terrain.cpp
    src/fonts.cpp
    src/party_ui.cpp
    src/model.cpp
    src/model_pick.cpp
    src/audio.cpp
    src/display.cpp
    src/settings_menu.cpp
    src/settings_draw.cpp
    src/app.rc         # embeds the exe icon (src/app.ico) on Windows
    ${BUILD_STAMP_CPP} # defines BUILD_STAMP, rewritten on every build
)

# Packs data/ into data.pak. Built as a normal C++ target so it works on any machine that builds the game.
add_executable(pack_data tools/pack_data.cpp)
set_target_properties(pack_data PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools")

target_include_directories(koboldsurvival PRIVATE
    src
    ${GLEW_INCLUDE_DIRS}
    ${tinygltf_SOURCE_DIR}   # tiny_gltf.h and stb_image.h live in the tinygltf repo root
)

target_link_libraries(koboldsurvival PRIVATE
    kobold_sim
    SDL3::SDL3
    SDL3_ttf::SDL3_ttf
    SDL3_image::SDL3_image
    OpenGL::GL
    GLEW::GLEW
    miniaudio
)

# A Windows GUI program, so no console window opens with the game. SDL_main.h (included in main.cpp) supplies
# the WinMain entry point this needs. Other systems ignore this.
set_target_properties(koboldsurvival PROPERTIES WIN32_EXECUTABLE TRUE)

# Release builds optimize across source files, so small functions used everywhere can be inlined
include(CheckIPOSupported)
check_ipo_supported(RESULT KOBOLD_IPO_SUPPORTED OUTPUT KOBOLD_IPO_MESSAGE)
if(KOBOLD_IPO_SUPPORTED)
    set_property(TARGET koboldsurvival kobold_sim PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    set_property(TARGET koboldsurvival kobold_sim PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE)
endif()

# Per-system timers, written to log.txt every 10 seconds (src/profile.h). Off, they cost nothing.
option(KOBOLD_PROFILE "Time the game's systems and write the results to log.txt" OFF)
if(KOBOLD_PROFILE)
    target_compile_definitions(kobold_sim PUBLIC KOBOLD_PROFILE)
endif()

# Rebuilds data.pak from data/ (fonts, sprites, models) next to the executables. The game reads only this file, so the
# loose data/ folder is not copied. A target of its own, so the tests can have the pak without building the game.
add_custom_target(game_data ALL
    COMMAND pack_data "${CMAKE_SOURCE_DIR}/data" "${CMAKE_BINARY_DIR}/data.pak"
    COMMENT "Packing data/ into data.pak"
    VERBATIM
)
add_dependencies(game_data pack_data)
add_dependencies(koboldsurvival game_data write_build_stamp)

# The language files (data/language) are also copied next to the exe as a loose LANGUAGE folder, beside data.pak, BGM
# and SFX, so they can be edited without rebuilding the pak. They are in the pak too,
# so the game still runs if this folder is missing. Files here override the pak's copy of the same language.
# The BGM and SFX folders (music and sound effects) are copied the same way. The audio module reads them loose.
add_custom_command(TARGET koboldsurvival POST_BUILD
    # Cleared first so a language file deleted from data/language does not linger in the menu
    COMMAND ${CMAKE_COMMAND} -E remove_directory "$<TARGET_FILE_DIR:koboldsurvival>/LANGUAGE"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/data/language" "$<TARGET_FILE_DIR:koboldsurvival>/LANGUAGE"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/BGM" "$<TARGET_FILE_DIR:koboldsurvival>/BGM"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/SFX" "$<TARGET_FILE_DIR:koboldsurvival>/SFX"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/MAP" "$<TARGET_FILE_DIR:koboldsurvival>/MAP"
    COMMENT "Copying LANGUAGE, BGM, SFX and MAP folders next to the executable"
)

# Headless tests, run with ctest. save_roundtrip saves a level, loads it and compares. data_check checks the game's data.
# sim_step plays with no window and checks the same seed gives the same game.
# terrain_check checks the ground the game walks on. world_gen_check checks the generator and the map and level files.
enable_testing()
foreach(test save_roundtrip data_check sim_step terrain_check world_gen_check)
    add_executable(${test} tests/${test}.cpp)
    target_link_libraries(${test} PRIVATE kobold_sim)
    add_dependencies(${test} game_data)
    add_test(NAME ${test} COMMAND ${test} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
endforeach()

target_compile_definitions(save_roundtrip PRIVATE KOBOLD_TEST_SAVES="${CMAKE_SOURCE_DIR}/tests/saves")

foreach(target koboldsurvival kobold_sim save_roundtrip data_check sim_step terrain_check world_gen_check)
    target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(${target} PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4 /wd4244 /wd4458 /external:anglebrackets /external:W0>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>)
endforeach()
