#include "mesh_bridge.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <unordered_map>

namespace {

// The exact bits of a position: welding merges only identical points, never
// two points that merely happen to be close.
struct PositionKey {
    std::uint32_t bits[3];

    bool operator==(const PositionKey& other) const {
        return std::memcmp(bits, other.bits, sizeof(bits)) == 0;
    }
};

struct PositionKeyHash {
    size_t operator()(const PositionKey& k) const {
        size_t h = 0;
        for (const std::uint32_t b : k.bits) {
            h ^= std::hash<std::uint32_t>()(b) + 0x9e3779b9 + (h << 6) + (h >> 2);
        }
        return h;
    }
};

} // namespace

Welded weld(const Mesh& mesh) {
    std::vector<glm::vec3> G;
    Welded welded;
    welded.che_of_vertex.reserve(mesh.vertices.size());

    std::unordered_map<PositionKey, int, PositionKeyHash> vertex_at;
    for (const Vertex& vertex : mesh.vertices) {
        // + 0 turns -0 into +0: equal as numbers, different as bits.
        const glm::vec3 position = vertex.position + glm::vec3(0.0f);
        PositionKey key{};
        std::memcpy(key.bits, &position, sizeof(key.bits));
        const auto found = vertex_at.find(key);
        if (found != vertex_at.end()) {
            welded.che_of_vertex.push_back(found->second);
        } else {
            const int id = static_cast<int>(G.size());
            G.push_back(vertex.position);
            vertex_at.emplace(key, id);
            welded.che_of_vertex.push_back(id);
        }
    }

    std::vector<int> V;
    V.reserve(mesh.indices.size());
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const int a = welded.che_of_vertex[mesh.indices[i]];
        const int b = welded.che_of_vertex[mesh.indices[i + 1]];
        const int c = welded.che_of_vertex[mesh.indices[i + 2]];
        if (a != b && b != c && c != a) {
            V.insert(V.end(), {a, b, c});
        }
    }

    welded.che = CHE(std::move(G), std::move(V));
    return welded;
}

glm::vec3 any_tangent(const glm::vec3& n) {
    const glm::vec3 helper = std::abs(n.x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
    return glm::normalize(glm::cross(helper, n));
}

Mesh mesh_from_che(const CHE& che, int material) {
    const std::vector<glm::vec3> normals = che.vertex_normals();

    Mesh mesh;
    mesh.vertices.reserve(static_cast<size_t>(che.n_vertices()));
    for (int v = 0; v < che.n_vertices(); ++v) {
        const glm::vec3& n = normals[static_cast<size_t>(v)];
        mesh.vertices.push_back({che.G(v), n, glm::vec2(0.0f), glm::vec4(any_tangent(n), 1.0f)});
        mesh.position_ids.push_back(v);
    }
    mesh.indices.assign(che.indices().begin(), che.indices().end());
    mesh.submeshes.push_back({0, static_cast<int>(mesh.indices.size()), material});
    return mesh;
}
