find_package(Threads REQUIRED)

add_executable(task13 src/main.cpp src/intersect.cpp src/ray_scene.cpp src/depth_map.cpp
                      src/obj_loader.cpp src/tiny_obj_loader.cpp src/stb_image_write.cpp)
target_link_libraries(task13 PRIVATE gfx_common Threads::Threads)
# The scenes are task 12's, read from its folder; the image goes to this task's docs/.
target_compile_definitions(task13 PRIVATE
    SCENE_DIR="${CMAKE_SOURCE_DIR}/task-12-loading-scenes/scenes/"
    DOCS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/docs/")
# MinGW: std::thread runs on winpthreads; bake it into the exe like libstdc++.
target_link_options(task13 PRIVATE $<$<AND:$<CXX_COMPILER_ID:GNU>,$<PLATFORM_ID:Windows>>:-static>)
