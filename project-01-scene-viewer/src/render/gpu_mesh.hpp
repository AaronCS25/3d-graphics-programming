// A Mesh on the GPU: the vertex buffer of task 12 (position, normal, uv,
// tangent), plus a second buffer with one scalar per vertex, the normalized
// geodesic distance of the Fast Marching Method (task 06), updated on its own.

#pragma once

#include <glad/glad.h>

#include "obj_loader.hpp"

#include <vector>

struct GpuMesh {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint scalar_vbo = 0;
    GLuint ebo = 0;
    std::vector<Submesh> submeshes;
    int triangles = 0;

    // Creates the buffers on the first call and replaces them afterwards;
    // the scalars are reset to -1, "no distance".
    void upload(const Mesh& mesh);
    // New vertex data for the same vertices (recomputed normals).
    void update_vertices(const Mesh& mesh);
    void upload_scalars(const std::vector<float>& scalars);

    void draw(const Submesh& submesh) const;
    void draw() const;
    void destroy();
};
