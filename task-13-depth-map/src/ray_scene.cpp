#include "ray_scene.hpp"

#include "intersect.hpp"

#include <limits>

RayScene::RayScene(const Scene& scene, const glm::mat4& model) {
    for (const Mesh& mesh : scene.meshes) {
        if (mesh.indices.empty()) {
            continue;
        }
        std::vector<glm::vec3> world(mesh.vertices.size());
        for (size_t i = 0; i < world.size(); ++i) {
            world[i] = glm::vec3(model * glm::vec4(mesh.vertices[i].position, 1.0f));
        }

        Object object;
        object.box_min = object.box_max = world[mesh.indices[0]];
        for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            const Triangle triangle{world[mesh.indices[i]], world[mesh.indices[i + 1]],
                                    world[mesh.indices[i + 2]]};
            // A zero-area triangle (the poles of a UV sphere repeat a vertex) cannot
            // be hit, but rounding can make Möller-Trumbore report a hit anyway.
            const glm::vec3 n = glm::cross(triangle.b - triangle.a, triangle.c - triangle.a);
            if (glm::dot(n, n) < 1e-12f) {
                continue;
            }
            object.triangles.push_back(triangle);
            for (const glm::vec3& p : {triangle.a, triangle.b, triangle.c}) {
                object.box_min = glm::min(object.box_min, p);
                object.box_max = glm::max(object.box_max, p);
            }
        }
        objects_.push_back(std::move(object));
    }
}

float RayScene::closest_hit(const Ray& ray, bool use_boxes) const {
    const glm::vec3 inv_direction = 1.0f / ray.direction;
    float closest = std::numeric_limits<float>::infinity();
    for (const Object& object : objects_) {
        // A box farther than the closest hit so far cannot hold a closer one.
        if (use_boxes &&
            !hit_box(ray, inv_direction, object.box_min, object.box_max, closest)) {
            continue;
        }
        for (const Triangle& triangle : object.triangles) {
            const float t = hit_triangle(ray, triangle.a, triangle.b, triangle.c);
            if (t > 0.0f && t < closest) {
                closest = t;
            }
        }
    }
    return closest;
}

int RayScene::triangle_count() const {
    int count = 0;
    for (const Object& object : objects_) {
        count += static_cast<int>(object.triangles.size());
    }
    return count;
}
