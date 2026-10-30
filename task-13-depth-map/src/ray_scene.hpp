// The scene as the ray caster sees it: triangles in world space, grouped by
// object, each object with its bounding box. No BVH: every ray tests every
// object, and every triangle of each object whose box it enters.
#pragma once

#include "obj_loader.hpp"
#include "ray.hpp"

#include <vector>

struct Triangle {
    glm::vec3 a, b, c;
};

struct Object {
    std::vector<Triangle> triangles;
    glm::vec3 box_min{0.0f};
    glm::vec3 box_max{0.0f};
};

class RayScene {
public:
    // The meshes of the OBJ, moved to the world by the model matrix.
    RayScene(const Scene& scene, const glm::mat4& model);

    // The distance to the nearest surface along the ray, +infinity if it hits
    // nothing. With use_boxes off, the bounding boxes are ignored.
    float closest_hit(const Ray& ray, bool use_boxes) const;

    int triangle_count() const;

private:
    std::vector<Object> objects_;
};
