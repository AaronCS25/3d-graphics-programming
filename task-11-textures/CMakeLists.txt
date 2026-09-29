add_executable(task11 src/main.cpp src/stb_image.cpp)
target_link_libraries(task11 PRIVATE gfx_common)
# Textures are read from the source tree, wherever the executable is run from.
target_compile_definitions(task11 PRIVATE TEXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/textures/")
