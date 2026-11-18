# Task 15 always builds its CPU version. The GPU version is added when CMake
# finds nvcc (CUDA toolkit + NVIDIA GPU); on macOS the program is CPU only.
#
# Two ways to build it:
#   - from the root project, as every other task;
#   - on its own, on a machine without vcpkg, such as a lab PC over SSH:
#         cmake -S task-15-path-tracer -B build/task15
#         cmake --build build/task15
#     glm, tinyobjloader and stb are then downloaded by FetchContent.
#
# GPU architecture: by default the GPU of the machine that builds it ("native",
# CMake >= 3.24). Pass -DCMAKE_CUDA_ARCHITECTURES=<sm> to choose another, or
# with an older CMake. A Blackwell GPU (sm_120) needs CUDA >= 12.8.
cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.24)
    set(CMAKE_CUDA_ARCHITECTURES native)
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(task15 LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    if(NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE Release)
    endif()

    # Header-only: SOURCE_SUBDIR names a folder without a CMakeLists.txt, so
    # FetchContent only downloads them (task 14).
    include(FetchContent)
    FetchContent_Declare(glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG 1.0.1
        GIT_SHALLOW TRUE
        SOURCE_SUBDIR headers-only)
    FetchContent_Declare(tinyobjloader
        GIT_REPOSITORY https://github.com/tinyobjloader/tinyobjloader.git
        GIT_TAG v2.0.0rc13
        GIT_SHALLOW TRUE
        SOURCE_SUBDIR headers-only)
    FetchContent_Declare(stb
        GIT_REPOSITORY https://github.com/nothings/stb.git
        GIT_TAG 9600522de53973336fa033a289858a4354cbb04c
        SOURCE_SUBDIR headers-only)
    FetchContent_MakeAvailable(glm tinyobjloader stb)

    set(REPO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)
    add_library(task15_deps INTERFACE)
    target_include_directories(task15_deps INTERFACE
        ${glm_SOURCE_DIR} ${tinyobjloader_SOURCE_DIR} ${stb_SOURCE_DIR} ${REPO_DIR}/common)
else()
    set(REPO_DIR ${CMAKE_SOURCE_DIR})
    # The shared camera brings glm; vcpkg's include folder brings the rest.
    # Not gfx_common: its -Wall -Wextra would also reach nvcc, which rejects them.
    add_library(task15_deps INTERFACE)
    target_link_libraries(task15_deps INTERFACE gfx_camera)
endif()

find_package(Threads REQUIRED)

add_executable(task15 src/main.cpp src/scene_build.cpp src/render_cpu.cpp
                      src/obj_loader.cpp src/tiny_obj_loader.cpp
                      src/stb_image.cpp src/stb_image_write.cpp)
target_link_libraries(task15 PRIVATE task15_deps Threads::Threads)
target_compile_options(task15 PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra>)
# The scenes are task 12's, read from its folder; the images go to this task's docs/.
target_compile_definitions(task15 PRIVATE
    SCENE_DIR="${REPO_DIR}/task-12-loading-scenes/scenes/"
    DOCS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/docs/")

include(CheckLanguage)
check_language(CUDA)
if(CMAKE_CUDA_COMPILER)
    enable_language(CUDA)
    target_sources(task15 PRIVATE src/render_gpu.cu)
    target_compile_definitions(task15 PRIVATE HAVE_CUDA)
    set_target_properties(task15 PROPERTIES CUDA_STANDARD 17 CUDA_STANDARD_REQUIRED ON)
    message(STATUS "task15: CPU + GPU (CUDA ${CMAKE_CUDA_COMPILER_VERSION})")
else()
    message(STATUS "task15: CPU only (no CUDA compiler found)")
endif()
