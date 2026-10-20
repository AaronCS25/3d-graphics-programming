#include "shapes.hpp"

#include <glm/gtc/constants.hpp>

#include <cmath>

namespace {

void finish(Mesh& mesh) {
    for (size_t v = 0; v < mesh.vertices.size(); ++v) {
        mesh.position_ids.push_back(static_cast<int>(v));
    }
}

} // namespace

Mesh make_cube(float side, int first_material) {
    struct Face {
        glm::vec3 normal;
        glm::vec3 u; // image right; u x v == normal, so p0 -> p1 -> p2
        glm::vec3 v; // image up;    winds counter-clockwise from outside
    };
    const Face faces[6] = {
        {{1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}},
        {{-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}},
        {{0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}},
        {{0.0f, -1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
        {{0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
        {{0.0f, 0.0f, -1.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
    };
    const glm::vec2 corners[4] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};

    const float h = 0.5f * side;
    Mesh mesh;
    for (int f = 0; f < 6; ++f) {
        const Face& face = faces[f];
        const glm::vec3 centre = face.normal * h;
        const unsigned base = static_cast<unsigned>(mesh.vertices.size());
        for (const glm::vec2& uv : corners) {
            const glm::vec3 p =
                centre + face.u * h * (2.0f * uv.x - 1.0f) + face.v * h * (2.0f * uv.y - 1.0f);
            mesh.vertices.push_back({p, face.normal, uv, glm::vec4(face.u, 1.0f)});
        }
        for (const unsigned k : {0u, 1u, 2u, 0u, 2u, 3u}) {
            mesh.indices.push_back(base + k);
        }
        mesh.submeshes.push_back({6 * f, 6, first_material + f});
    }
    finish(mesh);
    return mesh;
}

Mesh make_sphere(int slices, int stacks, int material) {
    Mesh mesh;
    for (int i = 0; i <= stacks; ++i) {
        const float phi = glm::pi<float>() * static_cast<float>(i) / static_cast<float>(stacks);
        for (int j = 0; j <= slices; ++j) {
            const float theta =
                glm::two_pi<float>() * static_cast<float>(j) / static_cast<float>(slices);
            // The seam and the poles are placed exactly: sin(2 pi) and sin(pi)
            // are not quite 0 in floating point, and the CHE welds only
            // identical positions.
            const float ring = (i == 0 || i == stacks) ? 0.0f : std::sin(phi);
            const float around = j == slices ? 0.0f : theta;
            const glm::vec3 p{ring * std::cos(around), std::cos(phi), -ring * std::sin(around)};
            const glm::vec2 uv{static_cast<float>(j) / static_cast<float>(slices),
                               1.0f - static_cast<float>(i) / static_cast<float>(stacks)};
            // dP/dtheta without its sin(phi) factor: still defined at the poles.
            const glm::vec3 tangent{-std::sin(theta), 0.0f, -std::cos(theta)};
            mesh.vertices.push_back({p, p, uv, glm::vec4(tangent, 1.0f)});
        }
    }

    const int columns = slices + 1;
    for (int i = 0; i < stacks; ++i) {
        for (int j = 0; j < slices; ++j) {
            const unsigned a = static_cast<unsigned>(i * columns + j);
            const unsigned b = a + static_cast<unsigned>(columns);
            for (const unsigned k : {a, b, a + 1u, a + 1u, b, b + 1u}) {
                mesh.indices.push_back(k);
            }
        }
    }
    mesh.submeshes.push_back({0, static_cast<int>(mesh.indices.size()), material});
    finish(mesh);
    return mesh;
}

Mesh make_floor(float size, float tiles, int cells, int material) {
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    const glm::vec4 tangent(1.0f, 0.0f, 0.0f, 1.0f);
    Mesh mesh;
    for (int i = 0; i <= cells; ++i) {
        for (int j = 0; j <= cells; ++j) {
            const float s = static_cast<float>(j) / static_cast<float>(cells);
            const float t = static_cast<float>(i) / static_cast<float>(cells);
            const glm::vec3 p((s - 0.5f) * size, 0.0f, (0.5f - t) * size);
            mesh.vertices.push_back({p, up, glm::vec2(s, t) * tiles, tangent});
        }
    }
    const unsigned columns = static_cast<unsigned>(cells + 1);
    for (unsigned i = 0; i < static_cast<unsigned>(cells); ++i) {
        for (unsigned j = 0; j < static_cast<unsigned>(cells); ++j) {
            const unsigned a = i * columns + j;
            const unsigned b = a + columns;
            for (const unsigned k : {a, a + 1u, b + 1u, a, b + 1u, b}) {
                mesh.indices.push_back(k); // counter-clockwise seen from above
            }
        }
    }
    mesh.submeshes.push_back({0, static_cast<int>(mesh.indices.size()), material});
    finish(mesh);
    return mesh;
}
