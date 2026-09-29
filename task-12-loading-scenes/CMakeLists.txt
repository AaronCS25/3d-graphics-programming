add_executable(task12 src/main.cpp src/obj_loader.cpp src/texture.cpp src/stb_image.cpp
                      src/tiny_obj_loader.cpp)
target_link_libraries(task12 PRIVATE gfx_common)
# Scenes are read from the source tree, wherever the executable is run from.
target_compile_definitions(task12 PRIVATE SCENE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/scenes/")
