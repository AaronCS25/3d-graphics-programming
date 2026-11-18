// The scene as the GPU sees it. Task 13's RayScene holds a vector of objects,
// each with its own vector of triangles: pointers the GPU cannot follow. Here
// everything is flattened into two plain arrays, each copied to device memory
// with a single cudaMemcpy. The content is the same: world-space triangles
// grouped by object, and the bounding box of each object.
//
// depth_cuda.cu includes this header, so it only forward-declares glm and the
// OBJ scene: the full glm headers make nvcc warn about their constructors.
#pragma once

#include <glm/fwd.hpp>

#include <vector>

struct Scene; // obj_loader.hpp

// Vertex a and the two edges of Möller-Trumbore, e1 = b - a and e2 = c - a,
// computed once here instead of once per ray. Plain floats, no glm, so the
// same struct is read by the CUDA code.
struct GpuTriangle {
    float a[3];
    float e1[3];
    float e2[3];
};

// An object is a range [first, first + count) of the triangle array and its
// axis-aligned bounding box.
struct GpuObject {
    float box_min[3];
    float box_max[3];
    int first;
    int count;
};

struct GpuScene {
    std::vector<GpuTriangle> triangles;
    std::vector<GpuObject> objects;
};

// The meshes of the OBJ, moved to the world by the model matrix. Zero-area
// triangles are dropped, as in task 13.
GpuScene build_gpu_scene(const Scene& scene, const glm::mat4& model);
