find_package(imgui CONFIG REQUIRED)

add_executable(project01
    src/main.cpp
    src/app.cpp
    src/geometry/che.cpp
    src/geometry/che_simplify.cpp
    src/geometry/fast_marching.cpp
    src/geometry/mesh_bridge.cpp
    src/io/mesh_io.cpp
    src/io/obj_loader.cpp
    src/io/texture.cpp
    src/io/stb_image.cpp
    src/io/tiny_obj_loader.cpp
    src/render/shader.cpp
    src/render/gpu_mesh.cpp
    src/render/shadow_map.cpp
    src/render/renderer.cpp
    src/scene/shapes.cpp
    src/scene/object.cpp
    src/scene/world.cpp
    src/scene/presets.cpp
    src/interaction/picking.cpp
    src/ui/panel.cpp)
# Every module includes the others by file name, as the tasks do.
target_include_directories(project01 PRIVATE
    src src/geometry src/io src/render src/scene src/interaction src/ui)
target_link_libraries(project01 PRIVATE gfx_common imgui::imgui)
# Assets and shaders are read from the source tree, wherever the executable runs.
target_compile_definitions(project01 PRIVATE
    ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets/"
    SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders/")
