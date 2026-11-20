add_executable(task17 src/main.cpp src/samplers.cpp src/stats.cpp src/stb_image_write.cpp)
target_link_libraries(task17 PRIVATE gfx_common)
# Screenshots and the images of the README go to this task's docs/.
target_compile_definitions(task17 PRIVATE DOCS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/docs/")
