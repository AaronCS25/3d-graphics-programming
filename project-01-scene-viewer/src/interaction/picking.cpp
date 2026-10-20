#include "picking.hpp"

#include <cmath>
#include <limits>

namespace {

// Möller & Trumbore 1997: solves origin + t d = a + u (b - a) + v (c - a) with
// Cramer's rule. The hit is inside the triangle when u, v >= 0 and u + v <= 1.
// Both faces count: a scene is seen from inside as well as from outside.
bool intersect(const glm::vec3& origin, const glm::vec3& d, const glm::vec3& a, const glm::vec3& b,
               const glm::vec3& c, float& t) {
    const glm::vec3 e1 = b - a;
    const glm::vec3 e2 = c - a;
    const glm::vec3 p = glm::cross(d, e2);
    const float det = glm::dot(e1, p);
    if (std::abs(det) < 1e-12f) {
        return false; // the ray runs parallel to the triangle
    }
    const float inv = 1.0f / det;
    const glm::vec3 s = origin - a;
    const float u = glm::dot(s, p) * inv;
    if (u < 0.0f || u > 1.0f) {
        return false;
    }
    const glm::vec3 q = glm::cross(s, e1);
    const float v = glm::dot(d, q) * inv;
    if (v < 0.0f || u + v > 1.0f) {
        return false;
    }
    t = glm::dot(e2, q) * inv;
    return t > 0.0f;
}

} // namespace

Ray ray_through(const glm::mat4& view, const glm::mat4& projection, double x, double y, int width,
                int height) {
    const float nx = static_cast<float>(2.0 * x / width - 1.0);
    const float ny = static_cast<float>(1.0 - 2.0 * y / height);
    const glm::mat4 inverse = glm::inverse(projection * view);
    glm::vec4 near_point = inverse * glm::vec4(nx, ny, -1.0f, 1.0f);
    glm::vec4 far_point = inverse * glm::vec4(nx, ny, 1.0f, 1.0f);
    near_point /= near_point.w;
    far_point /= far_point.w;
    return {glm::vec3(near_point), glm::normalize(glm::vec3(far_point - near_point))};
}

Hit pick(const World& world, const Ray& ray) {
    Hit hit;
    hit.t = std::numeric_limits<float>::max();

    for (size_t i = 0; i < world.objects.size(); ++i) {
        const Object& object = world.objects[i];
        // In the object's own coordinates. The direction is not renormalized,
        // so t still measures along the world ray and compares across objects.
        const glm::mat4 inverse = glm::inverse(object.model);
        const glm::vec3 origin = glm::vec3(inverse * glm::vec4(ray.origin, 1.0f));
        const glm::vec3 direction = glm::mat3(inverse) * ray.direction;

        const std::vector<unsigned>& indices = object.mesh.indices;
        for (size_t k = 0; k + 2 < indices.size(); k += 3) {
            const glm::vec3& a = object.mesh.vertices[indices[k]].position;
            const glm::vec3& b = object.mesh.vertices[indices[k + 1]].position;
            const glm::vec3& c = object.mesh.vertices[indices[k + 2]].position;
            float t = 0.0f;
            if (!intersect(origin, direction, a, b, c, t) || t >= hit.t) {
                continue;
            }
            const glm::vec3 local = origin + t * direction;
            int closest = static_cast<int>(indices[k]);
            for (int corner = 1; corner < 3; ++corner) {
                const int v = static_cast<int>(indices[k + static_cast<size_t>(corner)]);
                if (glm::distance(local, object.mesh.vertices[static_cast<size_t>(v)].position) <
                    glm::distance(local,
                                  object.mesh.vertices[static_cast<size_t>(closest)].position)) {
                    closest = v;
                }
            }
            hit.object = static_cast<int>(i);
            hit.t = t;
            hit.point = ray.origin + t * ray.direction;
            hit.vertex = closest;
            hit.che_vertex = object.che_of_vertex[static_cast<size_t>(closest)];
        }
    }
    return hit;
}
