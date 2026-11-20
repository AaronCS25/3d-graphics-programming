find_package(Threads REQUIRED)

# Task 15's path tracer (CPU only) and the four reconstruction filters.
add_executable(task18 src/main.cpp src/film.cpp src/filters.cpp src/scene_build.cpp
                      src/obj_loader.cpp src/tiny_obj_loader.cpp
                      src/stb_image.cpp src/stb_image_write.cpp)
target_link_libraries(task18 PRIVATE gfx_common Threads::Threads)
# The scene is task 12's, read from its folder; the images go to this task's docs/.
target_compile_definitions(task18 PRIVATE
    SCENE_DIR="${CMAKE_SOURCE_DIR}/task-12-loading-scenes/scenes/"
    DOCS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/docs/")
