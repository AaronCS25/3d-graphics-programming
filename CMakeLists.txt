cmake_minimum_required(VERSION 3.21)
project(3d-graphics-programming LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # for clangd / IDE intellisense

# Shared dependencies (declared once in vcpkg.json, installed once).
find_package(glfw3 CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)

# Common interface target: every task links against this to get GLFW + glad
# (and later glm, stb, etc.) plus sensible warnings.
add_library(gfx_common INTERFACE)
target_link_libraries(gfx_common INTERFACE glfw glad::glad glm::glm)
target_compile_options(gfx_common INTERFACE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>)
# MinGW: bake libstdc++/libgcc into the exe so it runs no matter which copy of
# libstdc++-6.dll is first on PATH (Git for Windows ships an incompatible one).
target_link_options(gfx_common INTERFACE
    $<$<AND:$<CXX_COMPILER_ID:GNU>,$<PLATFORM_ID:Windows>>:-static-libstdc++ -static-libgcc>)

# One subdirectory per task. Add a line here when starting a new task.
add_subdirectory(task-01-hello-triangle)
add_subdirectory(task-02-sierpinski-triangle)
add_subdirectory(task-03-sphere)
add_subdirectory(task-05-transformations)
add_subdirectory(task-06-fast-marching)

