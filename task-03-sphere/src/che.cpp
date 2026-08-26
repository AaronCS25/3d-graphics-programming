#include "che.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_map>

namespace {

constexpr float kPi = 3.14159265358979323846f;

// Packs a directed edge into one key. Vertex indices are non-negative, so the
// low half never bleeds into the high half.
long long edge_key(int from, int to) {
    return (static_cast<long long>(from) << 32) | static_cast<unsigned>(to);
}

std::string error(const char* what, int where) {
    char buffer[128];
    std::snprintf(buffer, sizeof(buffer), "%s (at %d)", what, where);
    return buffer;
}

} // namespace

CHE::CHE(std::vector<glm::vec3> geometry, std::vector<int> vertex_array)
    : m_G(std::move(geometry)), m_V(std::move(vertex_array)) {
    build_opposites();
}

void CHE::build_opposites() {
    m_O.assign(m_V.size(), NIL);

    std::unordered_map<long long, int> half_edge_of;
    half_edge_of.reserve(m_V.size());
    for (int he = 0; he < n_half_edges(); ++he) {
        half_edge_of.emplace(edge_key(V(he), V(next(he))), he);
    }

    for (int he = 0; he < n_half_edges(); ++he) {
        const auto twin = half_edge_of.find(edge_key(V(next(he)), V(he)));
        if (twin != half_edge_of.end()) {
            m_O[static_cast<size_t>(he)] = twin->second;
        }
    }
}

int CHE::n_edges() const {
    int boundary = 0;
    for (int he = 0; he < n_half_edges(); ++he) {
        boundary += is_boundary(he);
    }
    return (n_half_edges() + boundary) / 2;
}

bool CHE::is_closed() const {
    for (int he = 0; he < n_half_edges(); ++he) {
        if (is_boundary(he)) {
            return false;
        }
    }
    return true;
}

std::vector<int> CHE::star(int he) const {
    std::vector<int> fan{he};

    // O[prev(h)] is the next half-edge leaving the same vertex.
    int h = O(prev(he));
    while (h != NIL && h != he) {
        fan.push_back(h);
        h = O(prev(h));
    }
    if (h == he) {
        return fan;
    }

    // The sweep ran into a boundary, so the fan is open: walk the other way
    // from the seed to collect the triangles on the far side.
    for (h = he; O(h) != NIL;) {
        h = next(O(h));
        fan.push_back(h);
    }
    return fan;
}

std::vector<int> CHE::link(int he) const {
    const std::vector<int> fan = star(he);

    std::vector<int> ring;
    ring.reserve(fan.size() + 1);
    for (const int h : fan) {
        ring.push_back(V(next(h)));
    }

    // An open fan leaves one vertex out: the far end of the boundary edge that
    // stopped the sweep.
    for (const int h : fan) {
        if (is_boundary(prev(h))) {
            ring.push_back(V(prev(h)));
            break;
        }
    }
    return ring;
}

glm::vec3 CHE::triangle_normal(int t) const {
    const glm::vec3& a = G(V(3 * t));
    const glm::vec3& b = G(V(3 * t + 1));
    const glm::vec3& c = G(V(3 * t + 2));
    return glm::normalize(glm::cross(b - a, c - a));
}

std::vector<glm::vec3> CHE::vertex_normals() const {
    std::vector<glm::vec3> normals(m_G.size(), glm::vec3(0.0f));

    for (int t = 0; t < n_triangles(); ++t) {
        const glm::vec3& a = G(V(3 * t));
        const glm::vec3& b = G(V(3 * t + 1));
        const glm::vec3& c = G(V(3 * t + 2));

        // Left unnormalized on purpose: its length is twice the triangle area,
        // which weights the average by area for free.
        const glm::vec3 weighted = glm::cross(b - a, c - a);
        for (int i = 0; i < 3; ++i) {
            normals[static_cast<size_t>(V(3 * t + i))] += weighted;
        }
    }

    for (glm::vec3& n : normals) {
        const float length = glm::length(n);
        n = length > 0.0f ? n / length : glm::vec3(0.0f, 0.0f, 1.0f);
    }
    return normals;
}

std::string CHE::validate() const {
    if (m_V.empty() || m_V.size() % 3 != 0) {
        return "vertex array size is not a positive multiple of 3";
    }
    if (m_O.size() != m_V.size()) {
        return "opposite array does not match the vertex array";
    }

    for (int he = 0; he < n_half_edges(); ++he) {
        if (V(he) < 0 || V(he) >= n_vertices()) {
            return error("vertex index out of range", he);
        }
        if (V(he) == V(next(he))) {
            return error("degenerate half-edge: both ends are the same vertex", he);
        }

        const int twin = O(he);
        if (twin == NIL) {
            continue;
        }
        if (twin < 0 || twin >= n_half_edges()) {
            return error("opposite index out of range", he);
        }
        if (O(twin) != he) {
            return error("opposite is not symmetric", he);
        }
        if (twin == he) {
            return error("half-edge is its own opposite", he);
        }
        // A consistently oriented surface glues each edge to its reverse.
        if (V(twin) != V(next(he)) || V(next(twin)) != V(he)) {
            return error("inconsistent orientation across an edge", he);
        }
    }
    return {};
}

CHE make_cube(float side) {
    const float h = 0.5f * side;

    std::vector<glm::vec3> G = {
        {-h, -h, -h}, {h, -h, -h}, {h, h, -h}, {-h, h, -h},
        {-h, -h, h},  {h, -h, h},  {h, h, h},  {-h, h, h},
    };

    // Every face is two triangles wound counter-clockwise seen from outside;
    // the eight corners are shared, so the cube is a closed manifold.
    std::vector<int> V = {
        4, 5, 6, 4, 6, 7, // +z
        1, 0, 3, 1, 3, 2, // -z
        5, 1, 2, 5, 2, 6, // +x
        0, 4, 7, 0, 7, 3, // -x
        7, 6, 2, 7, 2, 3, // +y
        0, 1, 5, 0, 5, 4, // -y
    };

    return CHE(std::move(G), std::move(V));
}

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
