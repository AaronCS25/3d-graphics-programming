// Ray-triangle and ray-box intersection tests.
#pragma once

#include "ray.hpp"

// Möller-Trumbore: the distance t at which the ray crosses triangle abc, or a
// negative value if it misses. Both faces count, a scene does not promise which
// way its walls face.
float hit_triangle(const Ray& ray, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c);

// Slab test: whether the ray enters the axis-aligned box before t_max.
// inv_direction is 1 / ray.direction, computed once per ray.
bool hit_box(const Ray& ray, const glm::vec3& inv_direction, const glm::vec3& box_min,
             const glm::vec3& box_max, float t_max);
