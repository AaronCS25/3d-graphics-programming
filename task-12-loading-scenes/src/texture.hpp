// Loading images into OpenGL textures, with the mipmaps and repeat wrapping of
// task 11. Every function returns 0 when the image cannot be read.

#pragma once

#include <glad/glad.h>

#include <glm/glm.hpp>

#include <map>
#include <string>
#include <utility>

// A colour image (map_Kd, map_Ks).
GLuint load_texture(const std::string& path);

// A tangent-space normal map. map_Bump often names a height map instead, a
// grayscale image; one of those is converted into a normal map on load.
GLuint load_normal_map(const std::string& path, float bump_scale);

// A 1x1 texture of a single colour, for a material that has no image for some
// map: sampled anywhere it answers the same value, so the shader needs no
// special case.
GLuint make_flat_texture(const glm::vec3& value);

// Many materials of a scene share the same images: each file is uploaded once.
class TextureCache {
  public:
    GLuint colour(const std::string& path);
    GLuint normal(const std::string& path, float bump_scale);
    void destroy();

  private:
    std::map<std::pair<std::string, float>, GLuint> m_textures;
};
