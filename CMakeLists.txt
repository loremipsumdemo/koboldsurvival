cmake_minimum_required(VERSION 3.16)
project(koboldsurvival)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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

# miniaudio: background music. It decodes MP3 itself and plays through the system's audio API, so it needs
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

add_executable(koboldsurvival
    src/main.cpp
    src/app.cpp
    src/app_actions.cpp
    src/app_panels.cpp
    src/app_talk.cpp
    src/app_input.cpp
    src/app_update.cpp
    src/app_render.cpp
    src/play_state.cpp
    src/storage.cpp
    src/trade.cpp
    src/selection.cpp
    src/level.cpp
    src/menus.cpp
    src/app_support.cpp
    src/renderer.cpp
    src/renderer_text.cpp
    src/renderer_widgets.cpp
    src/renderer_units.cpp
    src/renderer_world.cpp
    src/renderer_overlays.cpp
    src/renderer_menus.cpp
    src/renderer_panels.cpp
    src/renderer_sidebar.cpp
    src/renderer_popups.cpp
    src/text_cache.cpp
    src/renderer_terrain.cpp
    src/items.cpp
    src/language.cpp
    src/fontlist.cpp
    src/fonts.cpp
    src/inventory.cpp
    src/combat.cpp
    src/encumbrance.cpp
    src/experience.cpp
    src/hostility.cpp
    src/game_log.cpp
    src/world_labels.cpp
    src/factions.cpp
    src/equip.cpp
    src/stats.cpp
    src/party.cpp
    src/party_ui.cpp
    src/useitem.cpp
    src/need_behaviors.cpp
    src/sounds.cpp
    src/systems.cpp
    src/world_setup.cpp
    src/movement.cpp
    src/prop_use.cpp
    src/automation.cpp
    src/downed.cpp
    src/terrain.cpp
    src/overview.cpp
    src/terrain_mesh.cpp
    src/pathfind.cpp
    src/world_format.cpp
    src/look.cpp
    src/surface_tiles.cpp
    src/map_file.cpp
    src/model.cpp
    src/model_pick.cpp
    src/pak.cpp
    src/audio.cpp
    src/display.cpp
    src/build.cpp
    src/claims.cpp
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
    SDL3::SDL3
    SDL3_ttf::SDL3_ttf
    SDL3_image::SDL3_image
    OpenGL::GL
    GLEW::GLEW
    miniaudio
)

# A Windows GUI program, so no console window opens with the game. SDL_main.h (included in main.cpp) supplies
# the WinMain entry point this needs. Log lines go to log.txt beside the exe. Other systems ignore this.
set_target_properties(koboldsurvival PROPERTIES WIN32_EXECUTABLE TRUE)

# Rebuilds data.pak from data/ (fonts, sprites, models) next to the executable.
# The game reads only this file, so the loose data/ folder is not copied.
add_dependencies(koboldsurvival pack_data write_build_stamp)
add_custom_command(TARGET koboldsurvival POST_BUILD
    COMMAND pack_data "${CMAKE_SOURCE_DIR}/data" "$<TARGET_FILE_DIR:koboldsurvival>/data.pak"
    COMMENT "Packing data/ into data.pak"
)

# The language files (data/language) are also copied next to the exe as a loose LANGUAGE folder, beside data.pak, BGM
# and SFX, so they can be edited without rebuilding the pak. They are in the pak too,
# so the game still runs if this folder is missing. Files here override the pak's copy of the same language.
# The BGM and SFX folders (music and sound effects) are copied the same way. The audio module reads them loose.
add_custom_command(TARGET koboldsurvival POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/data/language" "$<TARGET_FILE_DIR:koboldsurvival>/LANGUAGE"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/BGM" "$<TARGET_FILE_DIR:koboldsurvival>/BGM"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/SFX" "$<TARGET_FILE_DIR:koboldsurvival>/SFX"
    COMMENT "Copying LANGUAGE, BGM and SFX folders next to the executable"
)
