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

add_executable(koboldgame
    src/main.cpp
    src/renderer.cpp
    src/systems.cpp
    src/terrain.cpp
    src/navmesh.cpp
    src/map.cpp
    src/model.cpp
    src/pak.cpp
    src/app.rc          # embeds the exe icon (src/app.ico) on Windows
)

# Packs data/ into data.pak. Built as a normal C++ target so it works on any machine that builds the game.
add_executable(pack_data tools/pack_data.cpp)
set_target_properties(pack_data PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools")

target_include_directories(koboldgame PRIVATE
    src
    ${GLEW_INCLUDE_DIRS}
    ${tinygltf_SOURCE_DIR}   # tiny_gltf.h and stb_image.h live in the tinygltf repo root
)

target_link_libraries(koboldgame PRIVATE
    SDL3::SDL3
    SDL3_ttf::SDL3_ttf
    SDL3_image::SDL3_image
    OpenGL::GL
    GLEW::GLEW
)

# Rebuilds data.pak from data/ (fonts, sprites, models) next to the executable.
# The game reads only this file, so the loose data/ folder is not copied.
add_dependencies(koboldgame pack_data)
add_custom_command(TARGET koboldgame POST_BUILD
    COMMAND pack_data "${CMAKE_SOURCE_DIR}/data" "$<TARGET_FILE_DIR:koboldgame>/data.pak"
    COMMENT "Packing data/ into data.pak"
)
