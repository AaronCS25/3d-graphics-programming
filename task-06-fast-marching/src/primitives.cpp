#include "primitives.hpp"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kPi = 3.14159265358979323846f;

} // namespace

CHE make_sphere(float radius, int slices, int stacks) {
    slices = std::max(slices, 3);
    stacks = std::max(stacks, 2);

    // Vertex 0 is the north pole, then stacks-1 rings of `slices` vertices,
    // then the south pole. The ring at u = 2pi is never emitted: the last
    // column wraps around to column 0, which is what keeps the seam closed.
    std::vector<glm::vec3> G;
    G.reserve(static_cast<size_t>(2 + (stacks - 1) * slices));
    G.emplace_back(0.0f, 0.0f, radius);

    for (int i = 1; i < stacks; ++i) {
        const float v = kPi * static_cast<float>(i) / static_cast<float>(stacks);
        const float sin_v = std::sin(v);
        const float cos_v = std::cos(v);
        for (int j = 0; j < slices; ++j) {
            const float u = 2.0f * kPi * static_cast<float>(j) / static_cast<float>(slices);
            G.emplace_back(radius * std::cos(u) * sin_v, radius * std::sin(u) * sin_v,
                           radius * cos_v);
        }
    }
    G.emplace_back(0.0f, 0.0f, -radius);

    const int north = 0;
    const int south = static_cast<int>(G.size()) - 1;
    const auto ring = [slices](int i, int j) { return 1 + (i - 1) * slices + j % slices; };

    std::vector<int> V;
    V.reserve(static_cast<size_t>(6 * slices * (stacks - 1)));

    for (int j = 0; j < slices; ++j) {
        V.insert(V.end(), {north, ring(1, j), ring(1, j + 1)});
    }

    for (int i = 1; i < stacks - 1; ++i) {
        for (int j = 0; j < slices; ++j) {
            const int top = ring(i, j);
            const int top_next = ring(i, j + 1);
            const int bottom = ring(i + 1, j);
            const int bottom_next = ring(i + 1, j + 1);
            V.insert(V.end(), {top, bottom, bottom_next});
            V.insert(V.end(), {top, bottom_next, top_next});
        }
    }

    for (int j = 0; j < slices; ++j) {
        V.insert(V.end(), {south, ring(stacks - 1, j + 1), ring(stacks - 1, j)});
    }

    return CHE(std::move(G), std::move(V));
}

CHE make_torus(float major_radius, float minor_radius, int major_segments, int minor_segments) {
    major_segments = std::max(major_segments, 3);
    minor_segments = std::max(minor_segments, 3);

    // Vertex (i, j): i-th step around the ring, j-th step around the tube.
    // Neither direction emits its wrap-around column: `at` folds the index
    // back with a modulo, which is what closes the surface in both directions.
    std::vector<glm::vec3> G;
    G.reserve(static_cast<size_t>(major_segments * minor_segments));
    for (int i = 0; i < major_segments; ++i) {
        const float phi = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(major_segments);
        for (int j = 0; j < minor_segments; ++j) {
            const float theta =
                2.0f * kPi * static_cast<float>(j) / static_cast<float>(minor_segments);
            const float ring = major_radius + minor_radius * std::cos(theta);
            // y is up in the viewer, so the ring lies in the xz plane.
            G.emplace_back(ring * std::cos(phi), minor_radius * std::sin(theta),
                           ring * std::sin(phi));
        }
    }

    const auto at = [major_segments, minor_segments](int i, int j) {
        return (i % major_segments) * minor_segments + (j % minor_segments);
    };

    // Each quad of the grid becomes two triangles. With the ring in the xz
    // plane, d/dphi x d/dtheta points inward, so the corners are visited
    // theta-first to keep the winding counter-clockwise seen from outside.
    std::vector<int> V;
    V.reserve(static_cast<size_t>(6 * major_segments * minor_segments));
    for (int i = 0; i < major_segments; ++i) {
        for (int j = 0; j < minor_segments; ++j) {
            const int a = at(i, j);
            const int b = at(i + 1, j);
            const int c = at(i + 1, j + 1);
            const int d = at(i, j + 1);
            V.insert(V.end(), {a, d, c});
            V.insert(V.end(), {a, c, b});
        }
    }

    return CHE(std::move(G), std::move(V));
}
