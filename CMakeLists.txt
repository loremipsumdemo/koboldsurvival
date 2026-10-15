cmake_minimum_required(VERSION 3.16)
project(simple_game)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(SDL3 CONFIG REQUIRED)
find_package(Lua REQUIRED)
find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)

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

add_executable(simple_game
    src/main.cpp
    src/model.cpp
)

target_include_directories(simple_game PRIVATE ${LUA_INCLUDE_DIR} ${GLEW_INCLUDE_DIRS})
target_link_libraries(simple_game PRIVATE
    SDL3::SDL3
    ${LUA_LIBRARIES}
    OpenGL::GL
    GLEW::GLEW
    tinygltf
)

add_custom_command(TARGET simple_game POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:simple_game>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/scripts" "$<TARGET_FILE_DIR:simple_game>/scripts"
)