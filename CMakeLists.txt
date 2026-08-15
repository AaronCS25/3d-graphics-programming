cmake_minimum_required(VERSION 3.21)
project(3d-graphics-programming LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # for clangd / IDE intellisense

# Shared dependencies (declared once in vcpkg.json, installed once).
find_package(glfw3 CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)

# Common interface target: every task links against this to get GLFW + glad
# (and later glm, stb, etc.) plus sensible warnings.
add_library(gfx_common INTERFACE)
target_link_libraries(gfx_common INTERFACE glfw glad::glad)
target_compile_options(gfx_common INTERFACE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>)

# One subdirectory per task. Add a line here when starting a new task.
add_subdirectory(task-01-hello-triangle)
add_subdirectory(task-02-sierpinski-triangle)
