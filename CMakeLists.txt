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

# miniaudio: background music. It decodes MP3 itself and plays through the system's audio API, so it needs
# no other libraries. The pinned release keeps builds reproducible.
set(MINIAUDIO_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG 0.11.25
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(miniaudio)

# The look (the sky, sun, rock and water colors) and the surface textures, in the wm namespace under
# src/worldmaker. The game reads and writes these with each world folder. The rest of the old world tool is gone.
set(WORLDTOOL_SOURCES
    src/worldmaker/render/surface_pixels.cpp
    src/worldmaker/theme/theme.cpp
    src/worldmaker/theme/themefile.cpp
)

# Build stamp for the title screen, BUILD.YYYY.MM.DD.HH.MM.mmm. The stamp is taken at build time, not configure time,
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

add_executable(koboldgame
    src/main.cpp
    src/renderer.cpp
    src/renderer_terrain.cpp
    src/items.cpp
    src/language.cpp
    src/fontlist.cpp
    src/inventory.cpp
    src/combat.cpp
    src/equip.cpp
    src/stats.cpp
    src/party.cpp
    src/party_ui.cpp
    src/useitem.cpp
    src/behavior.cpp
    src/systems.cpp
    src/terrain.cpp
    src/overview.cpp
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
    ${BUILD_STAMP_CPP} # defines BUILD_STAMP, rewritten on every build
    ${WORLDTOOL_SOURCES}
)

# Packs data/ into data.pak. Built as a normal C++ target so it works on any machine that builds the game.
add_executable(pack_data tools/pack_data.cpp)
set_target_properties(pack_data PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools")

target_include_directories(koboldgame PRIVATE
    src
    src/worldmaker          # the look and surface code the world files use
    ${GLEW_INCLUDE_DIRS}
    ${tinygltf_SOURCE_DIR}   # tiny_gltf.h and stb_image.h live in the tinygltf repo root
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
add_dependencies(koboldgame pack_data write_build_stamp)
add_custom_command(TARGET koboldgame POST_BUILD
    COMMAND pack_data "${CMAKE_SOURCE_DIR}/data" "$<TARGET_FILE_DIR:koboldgame>/data.pak"
    COMMENT "Packing data/ into data.pak"
)
