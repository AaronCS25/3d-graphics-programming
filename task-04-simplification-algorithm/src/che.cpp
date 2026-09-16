#include "che.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_map>

namespace {

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

    // Meshes from the wild are not always manifold: an edge shared by three
    // triangles, or by two with the same orientation, has no unique twin, and
    // the first-match rule above can leave O pointing one way only. A walk
    // over an asymmetric O never returns to its start, so break those links:
    // the edge is treated as a boundary instead of a wrong gluing.
    const std::vector<int> first_pass = m_O;
    for (int he = 0; he < n_half_edges(); ++he) {
        const int twin = first_pass[static_cast<size_t>(he)];
        if (twin != NIL && first_pass[static_cast<size_t>(twin)] != he) {
            m_O[static_cast<size_t>(he)] = NIL;
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
