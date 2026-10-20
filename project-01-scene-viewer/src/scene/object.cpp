#include "object.hpp"

#include "mesh_bridge.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace {

double elapsed_ms(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
        .count();
}

bool uploaded(const Object& object) {
    return object.gpu.vao != 0;
}

} // namespace

void rebuild_che(Object& object) {
    Welded welded = weld(object.mesh);
    object.che = std::move(welded.che);
    object.che_of_vertex = std::move(welded.che_of_vertex);
    object.seeds.clear();
    object.distances = DistanceMap();
}

double update_distances(Object& object) {
    std::vector<float> scalars(object.mesh.vertices.size(), -1.0f);
    double milliseconds = 0.0;
    if (object.seeds.empty()) {
        object.distances = DistanceMap();
    } else {
        const auto start = std::chrono::steady_clock::now();
        object.distances = fast_marching(object.che, object.seeds);
        milliseconds = elapsed_ms(start);

        const float scale =
            object.distances.max_finite > 0.0f ? 1.0f / object.distances.max_finite : 0.0f;
        for (size_t v = 0; v < scalars.size(); ++v) {
            const float t = object.distances.t[static_cast<size_t>(object.che_of_vertex[v])];
            scalars[v] = std::isinf(t) ? -1.0f : t * scale;
        }
    }
    if (uploaded(object)) {
        object.gpu.upload_scalars(scalars);
    }
    return milliseconds;
}

void set_normals(Object& object, NormalSource source) {
    object.normals = source;
    if (object.simplified) {
        return; // a mesh rebuilt from the CHE already has the CHE's normals
    }
    if (source == NormalSource::Recomputed) {
        const std::vector<glm::vec3> normals = object.che.vertex_normals();
        for (size_t v = 0; v < object.mesh.vertices.size(); ++v) {
            object.mesh.vertices[v].normal = normals[static_cast<size_t>(object.che_of_vertex[v])];
        }
    } else {
        for (size_t v = 0; v < object.mesh.vertices.size(); ++v) {
            object.mesh.vertices[v].normal = object.loaded.vertices[v].normal;
        }
    }
    if (uploaded(object)) {
        object.gpu.update_vertices(object.mesh);
    }
}

SimplifyResult simplify(Object& object, float fraction) {
    const CHE original = weld(object.loaded).che;

    SimplifyResult result;
    result.triangles_before = original.n_triangles();
    const int target = std::max(4, static_cast<int>(fraction * original.n_triangles()));

    CHE reduced = original;
    const auto start = std::chrono::steady_clock::now();
    result.report = reduced.simplify(target);
    result.milliseconds = elapsed_ms(start);
    result.deviation = reduced.deviation_from(original);

    const int material = object.loaded.submeshes.empty() ? -1 : object.loaded.submeshes[0].material;
    object.mesh = mesh_from_che(reduced, material);
    object.simplified = true;
    rebuild_che(object);
    if (uploaded(object)) {
        object.gpu.upload(object.mesh);
    }
    return result;
}

void restore(Object& object) {
    object.mesh = object.loaded;
    object.simplified = false;
    rebuild_che(object);
    if (uploaded(object)) {
        object.gpu.upload(object.mesh);
    }
    set_normals(object, object.normals);
}

void mesh_bounds(const Mesh& mesh, glm::vec3& lo, glm::vec3& hi) {
    lo = glm::vec3(std::numeric_limits<float>::max());
    hi = glm::vec3(std::numeric_limits<float>::lowest());
    for (const Vertex& vertex : mesh.vertices) {
        lo = glm::min(lo, vertex.position);
        hi = glm::max(hi, vertex.position);
    }
}
