#include "gpu_mesh.hpp"

#include <cstddef>

void GpuMesh::upload(const Mesh& mesh) {
    if (vao == 0) {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glGenBuffers(1, &scalar_vbo);
        glGenBuffers(1, &ebo);
    }

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(mesh.vertices.size() * sizeof(Vertex)),
                 mesh.vertices.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(unsigned)),
                 mesh.indices.data(), GL_STATIC_DRAW);

    const GLsizei stride = sizeof(Vertex);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(offsetof(Vertex, position)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(offsetof(Vertex, normal)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(offsetof(Vertex, uv)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(offsetof(Vertex, tangent)));
    glEnableVertexAttribArray(3);

    glBindBuffer(GL_ARRAY_BUFFER, scalar_vbo);
    const std::vector<float> none(mesh.vertices.size(), -1.0f);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(none.size() * sizeof(float)), none.data(),
                 GL_DYNAMIC_DRAW);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(float), nullptr);
    glEnableVertexAttribArray(4);
    glBindVertexArray(0);

    submeshes = mesh.submeshes;
    triangles = static_cast<int>(mesh.indices.size() / 3);
}

void GpuMesh::update_vertices(const Mesh& mesh) {
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    static_cast<GLsizeiptr>(mesh.vertices.size() * sizeof(Vertex)),
                    mesh.vertices.data());
}

void GpuMesh::upload_scalars(const std::vector<float>& scalars) {
    glBindBuffer(GL_ARRAY_BUFFER, scalar_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(scalars.size() * sizeof(float)),
                    scalars.data());
}

void GpuMesh::draw(const Submesh& submesh) const {
    glBindVertexArray(vao);
    glDrawElements(
        GL_TRIANGLES, submesh.index_count, GL_UNSIGNED_INT,
        reinterpret_cast<void*>(static_cast<size_t>(submesh.first_index) * sizeof(unsigned)));
}

void GpuMesh::draw() const {
    for (const Submesh& submesh : submeshes) {
        draw(submesh);
    }
}

void GpuMesh::destroy() {
    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &vbo);
    glDeleteBuffers(1, &scalar_vbo);
    glDeleteBuffers(1, &ebo);
    vao = vbo = scalar_vbo = ebo = 0;
    submeshes.clear();
    triangles = 0;
}
