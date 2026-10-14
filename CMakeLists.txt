cmake_minimum_required(VERSION 3.16)
project(simple_game)

set(CMAKE_CXX_STANDARD 17)

find_package(SDL3 CONFIG REQUIRED)
find_package(Lua REQUIRED)
find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)

add_executable(simple_game src/main.cpp)
target_include_directories(simple_game PRIVATE ${LUA_INCLUDE_DIR} ${GLEW_INCLUDE_DIRS})
target_link_libraries(simple_game PRIVATE SDL3::SDL3 ${LUA_LIBRARIES} OpenGL::GL GLEW::GLEW)

add_custom_command(TARGET simple_game POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:simple_game>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/scripts" "$<TARGET_FILE_DIR:simple_game>/scripts"
)