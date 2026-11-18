# Task 14 needs an NVIDIA GPU and the CUDA toolkit (nvcc). It builds two ways:
#
#   - from the root project, which adds this folder only when it finds nvcc
#     (on macOS it is skipped and the other tasks build as usual);
#   - on its own, on a machine without vcpkg, such as a lab PC over SSH:
#         cmake -S task-14-depth-map-cuda -B build/task14
#         cmake --build build/task14
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
    project(task14 LANGUAGES CXX CUDA)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    if(NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE Release)
    endif()

    # The three are header-only. SOURCE_SUBDIR names a folder without a
    # CMakeLists.txt, so FetchContent only downloads them and their own builds
    # (tests, examples, install rules) are left out.
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
    add_library(task14_deps INTERFACE)
    target_include_directories(task14_deps INTERFACE
        ${glm_SOURCE_DIR} ${tinyobjloader_SOURCE_DIR} ${stb_SOURCE_DIR} ${REPO_DIR}/common)
else()
    enable_language(CUDA)
    set(REPO_DIR ${CMAKE_SOURCE_DIR})
    # The shared camera brings glm; vcpkg's include folder brings the rest.
    # Not gfx_common: its -Wall -Wextra would also reach nvcc, which rejects them.
    add_library(task14_deps INTERFACE)
    target_link_libraries(task14_deps INTERFACE gfx_camera)
endif()

add_executable(task14 src/main.cpp src/gpu_scene.cpp src/depth_cuda.cu
                      src/obj_loader.cpp src/tiny_obj_loader.cpp src/stb_image_write.cpp)
target_link_libraries(task14 PRIVATE task14_deps)
target_compile_options(task14 PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra>)
set_target_properties(task14 PROPERTIES CUDA_STANDARD 17 CUDA_STANDARD_REQUIRED ON)
# The scenes are task 12's, read from its folder; the image goes to this task's docs/.
target_compile_definitions(task14 PRIVATE
    SCENE_DIR="${REPO_DIR}/task-12-loading-scenes/scenes/"
    DOCS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/docs/")
