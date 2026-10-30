#include "intersect.hpp"

#include <algorithm>
#include <cmath>

// P(t) = O + t D = (1 - u - v) a + u b + v c, written as a 3x3 system
//   [-D  e1  e2] (t, u, v)^T = O - a,  with e1 = b - a, e2 = c - a,
// and solved with Cramer's rule. The determinants are triple products, which is
// why the same cross products p and q appear in all of them.
float hit_triangle(const Ray& ray, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
    const glm::vec3 e1 = b - a;
    const glm::vec3 e2 = c - a;
    const glm::vec3 p = glm::cross(ray.direction, e2);
    const float det = glm::dot(e1, p);
    if (std::abs(det) < 1e-12f) {
        return -1.0f; // the ray is parallel to the triangle's plane
    }
    const float inv_det = 1.0f / det;

    const glm::vec3 s = ray.origin - a;
    const float u = glm::dot(s, p) * inv_det;
    if (u < 0.0f || u > 1.0f) {
        return -1.0f;
    }
    const glm::vec3 q = glm::cross(s, e1);
    const float v = glm::dot(ray.direction, q) * inv_det;
    if (v < 0.0f || u + v > 1.0f) {
        return -1.0f;
    }
    return glm::dot(e2, q) * inv_det;
}

// Each pair of parallel faces bounds an interval of t. The ray is inside the
// box where the three intervals overlap: from the last entry to the first exit.
bool hit_box(const Ray& ray, const glm::vec3& inv_direction, const glm::vec3& box_min,
             const glm::vec3& box_max, float t_max) {
    const glm::vec3 t0 = (box_min - ray.origin) * inv_direction;
    const glm::vec3 t1 = (box_max - ray.origin) * inv_direction;
    const glm::vec3 t_enter = glm::min(t0, t1);
    const glm::vec3 t_exit = glm::max(t0, t1);
    const float enter = std::max({t_enter.x, t_enter.y, t_enter.z, 0.0f});
    const float exit = std::min({t_exit.x, t_exit.y, t_exit.z, t_max});
    return enter <= exit;
}
