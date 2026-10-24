cmake_minimum_required(VERSION 3.16)
project(koboldgame)

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

# stb_truetype: the world tool's font rasterizer. stb has no CMakeLists, so this only fetches the sources.
FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG master
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(stb)

# miniaudio: background music. It decodes MP3 itself and plays through the system's audio API, so it needs
# no other libraries. The pinned release keeps builds reproducible.
set(MINIAUDIO_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG 0.11.25
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(miniaudio)

# The world tool (formerly the standalone worldmaker), hosted by the map editor screen.
# Its code lives in the wm namespace, under src/worldmaker.
set(WORLDTOOL_SOURCES
    src/worldmaker/app/console.cpp
    src/worldmaker/app/desktop.cpp
    src/worldmaker/app/mapview.cpp
    src/worldmaker/app/menu.cpp
    src/worldmaker/app/session.cpp
    src/worldmaker/core/debug_options.cpp
    src/worldmaker/core/tunables.cpp
    src/worldmaker/player/walker.cpp
    src/worldmaker/render/gl_api.cpp
    src/worldmaker/render/renderer.cpp
    src/worldmaker/render/terrain.cpp
    src/worldmaker/render/grid.cpp
    src/worldmaker/render/surface_tile.cpp
    src/worldmaker/render/surface_pixels.cpp
    src/worldmaker/render/skybox.cpp
    src/worldmaker/render/gpu_timer.cpp
    src/worldmaker/theme/theme.cpp
    src/worldmaker/theme/themefile.cpp
    src/worldmaker/world/chunk_gen.cpp
    src/worldmaker/world/noise.cpp
    src/worldmaker/world/segments.cpp
    src/worldmaker/world/world.cpp
    src/worldmaker/world/worldfile.cpp
    src/worldmaker_host.cpp
)

add_executable(koboldgame
    src/main.cpp
    src/renderer.cpp
    src/renderer_terrain.cpp
    src/items.cpp
    src/inventory.cpp
    src/combat.cpp
    src/equip.cpp
    src/stats.cpp
    src/party.cpp
    src/party_ui.cpp
    src/useitem.cpp
    src/systems.cpp
    src/terrain.cpp
    src/terrain_mesh.cpp
    src/pathfind.cpp
    src/world_format.cpp
    src/map.cpp
    src/model.cpp
    src/pak.cpp
    src/audio.cpp
    src/display.cpp
    src/settings_menu.cpp
    src/settings_draw.cpp
    src/app.rc         # embeds the exe icon (src/app.ico) on Windows
    ${WORLDTOOL_SOURCES}
)

# Packs data/ into data.pak. Built as a normal C++ target so it works on any machine that builds the game.
add_executable(pack_data tools/pack_data.cpp)
set_target_properties(pack_data PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools")

target_include_directories(koboldgame PRIVATE
    src
    src/worldmaker          # the world tool's own include root
    ${GLEW_INCLUDE_DIRS}
    ${tinygltf_SOURCE_DIR}   # tiny_gltf.h and stb_image.h live in the tinygltf repo root
    ${stb_SOURCE_DIR}        # stb_truetype.h for the world tool's font
)

target_link_libraries(koboldgame PRIVATE
    SDL3::SDL3
    SDL3_ttf::SDL3_ttf
    SDL3_image::SDL3_image
    OpenGL::GL
    GLEW::GLEW
    miniaudio
)

# Rebuilds data.pak from data/ (fonts, sprites, models) next to the executable.
# The game reads only this file, so the loose data/ folder is not copied.
add_dependencies(koboldgame pack_data)
add_custom_command(TARGET koboldgame POST_BUILD
    COMMAND pack_data "${CMAKE_SOURCE_DIR}/data" "$<TARGET_FILE_DIR:koboldgame>/data.pak"
    COMMENT "Packing data/ into data.pak"
)
