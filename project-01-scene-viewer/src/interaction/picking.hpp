// Vertex picking. The click becomes a ray from the camera through the pixel;
// the nearest triangle it crosses, over every object of the scene, is the one
// under the cursor, so nothing behind it can be picked. The picked vertex is
// the corner of that triangle closest to where the ray went through.

#pragma once

#include "world.hpp"

#include <glm/glm.hpp>

struct Ray {
    glm::vec3 origin;
    glm::vec3 direction;
};

// The ray through window position (x, y) of a window width x height, in the
// same units as the cursor.
Ray ray_through(const glm::mat4& view, const glm::mat4& projection, double x, double y, int width,
                int height);

struct Hit {
    int object = -1; // -1: the ray hit nothing
    float t = 0.0f;  // ray parameter, comparable across objects
    glm::vec3 point{0.0f};
    int vertex = -1;     // vertex of the drawn mesh
    int che_vertex = -1; // the same point in the object's CHE
};

Hit pick(const World& world, const Ray& ray);
