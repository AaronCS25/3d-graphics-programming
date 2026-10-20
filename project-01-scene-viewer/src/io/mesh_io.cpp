#include "mesh_io.hpp"

#include <happly.h>

#include <algorithm>
#include <array>
#include <limits>
#include <vector>

CHE load_ply(const std::string& path) {
    happly::PLYData ply(path);

    // happly hands back the "vertex" element as x/y/z triples and the "face"
    // element as one index list per polygon.
    const std::vector<std::array<double, 3>> raw = ply.getVertexPositions();
    const std::vector<std::vector<size_t>> faces = ply.getFaceIndices<size_t>();

    // Centre the model on the origin and scale its longest side to 2, so any
    // file — the bunny is 0.15 units tall, other scans are in millimetres —
    // lands where the camera of the viewer expects a unit sphere.
    glm::vec3 lo(std::numeric_limits<float>::max());
    glm::vec3 hi(std::numeric_limits<float>::lowest());
    for (const auto& p : raw) {
        const glm::vec3 q(static_cast<float>(p[0]), static_cast<float>(p[1]),
                          static_cast<float>(p[2]));
        lo = glm::min(lo, q);
        hi = glm::max(hi, q);
    }
    const glm::vec3 centre = 0.5f * (lo + hi);
    const glm::vec3 extent = hi - lo;
    const float longest = std::max({extent.x, extent.y, extent.z, 1e-9f});
    const float scale = 2.0f / longest;

    std::vector<glm::vec3> G;
    G.reserve(raw.size());
    for (const auto& p : raw) {
        const glm::vec3 q(static_cast<float>(p[0]), static_cast<float>(p[1]),
                          static_cast<float>(p[2]));
        G.push_back((q - centre) * scale);
    }

    // Fan triangulation: polygon (v0 v1 v2 v3 ...) becomes (v0 v1 v2),
    // (v0 v2 v3), ... — a no-op for triangles, correct for convex quads, and
    // it keeps the winding of the original face.
    std::vector<int> V;
    V.reserve(3 * faces.size());
    for (const std::vector<size_t>& face : faces) {
        for (size_t k = 1; k + 1 < face.size(); ++k) {
            V.push_back(static_cast<int>(face[0]));
            V.push_back(static_cast<int>(face[k]));
            V.push_back(static_cast<int>(face[k + 1]));
        }
    }

    return CHE(std::move(G), std::move(V));
}
