// From the OBJ to the path tracer's arrays: triangles in world space, their
// normals and texture coordinates, one box per object, the materials and their
// map_Kd images. Host only (glm, stb_image).
#pragma once

#include "obj_loader.hpp"
#include "scene_data.hpp"

// The meshes of the OBJ, moved to the world by the model matrix. Zero-area
// triangles are dropped, as in task 13. Images that cannot be read leave their
// material with its Kd.
HostScene build_scene(const Scene& scene, const glm::mat4& model);
