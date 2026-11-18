#include "gpu_scene.hpp"

#include "obj_loader.hpp"

namespace {

void store(float out[3], const glm::vec3& v) {
    out[0] = v.x;
    out[1] = v.y;
    out[2] = v.z;
}

} // namespace

GpuScene build_gpu_scene(const Scene& scene, const glm::mat4& model) {
    GpuScene result;
    for (const Mesh& mesh : scene.meshes) {
        if (mesh.indices.empty()) {
            continue;
        }
        std::vector<glm::vec3> world(mesh.vertices.size());
        for (size_t i = 0; i < world.size(); ++i) {
            world[i] = glm::vec3(model * glm::vec4(mesh.vertices[i].position, 1.0f));
        }

        const int first = static_cast<int>(result.triangles.size());
        glm::vec3 box_min = world[mesh.indices[0]];
        glm::vec3 box_max = box_min;
        for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            const glm::vec3 a = world[mesh.indices[i]];
            const glm::vec3 b = world[mesh.indices[i + 1]];
            const glm::vec3 c = world[mesh.indices[i + 2]];
            // A zero-area triangle (the poles of a UV sphere repeat a vertex) cannot
            // be hit, but rounding can make Möller-Trumbore report a hit anyway.
            const glm::vec3 n = glm::cross(b - a, c - a);
            if (glm::dot(n, n) < 1e-12f) {
                continue;
            }
            GpuTriangle triangle;
            store(triangle.a, a);
            store(triangle.e1, b - a);
            store(triangle.e2, c - a);
            result.triangles.push_back(triangle);
            for (const glm::vec3& p : {a, b, c}) {
                box_min = glm::min(box_min, p);
                box_max = glm::max(box_max, p);
            }
        }

        GpuObject object;
        store(object.box_min, box_min);
        store(object.box_max, box_max);
        object.first = first;
        object.count = static_cast<int>(result.triangles.size()) - first;
        if (object.count > 0) {
            result.objects.push_back(object);
        }
    }
    return result;
}
