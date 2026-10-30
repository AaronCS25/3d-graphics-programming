// A ray P(t) = O + t D and the primary ray of a pixel.
#pragma once

#include <glm/glm.hpp>

struct Ray {
    glm::vec3 origin;
    glm::vec3 direction; // unit length, so t is the distance along the ray
};

// The ray from the camera through the centre of pixel (x, y), row 0 at the
// bottom as in OpenGL. The pixel goes to NDC, (x, y, 1) is a point on the far
// plane, and the inverse of projection * view takes it back to the world
// (ray_view_dir of the slides, with the pixel centre instead of a random offset).
inline Ray primary_ray(int x, int y, int width, int height, const glm::mat4& inv_proj_view,
                       const glm::vec3& camera_pos) {
    const glm::vec2 screen((x + 0.5f) / width, (y + 0.5f) / height);
    const glm::vec4 view(screen.x * 2.0f - 1.0f, screen.y * 2.0f - 1.0f, 1.0f, 1.0f);
    const glm::vec4 q = inv_proj_view * view;
    const glm::vec3 p = glm::vec3(q) / q.w;
    return {camera_pos, glm::normalize(p - camera_pos)};
}
