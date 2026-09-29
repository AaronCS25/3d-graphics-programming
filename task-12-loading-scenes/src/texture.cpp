#include "texture.hpp"

#include <stb_image.h>

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

// How steep a height map is: a step from black to white between neighbouring
// texels tilts the normal as much as a slope of kHeightSlope.
constexpr float kHeightSlope = 3.0f;

GLuint upload(const unsigned char* pixels, int width, int height, int channels) {
    const GLenum format = channels == 1   ? GL_RED
                          : channels == 2 ? GL_RG
                          : channels == 3 ? GL_RGB
                                          : GL_RGBA;
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1); // rows are tightly packed, not 4-byte aligned
    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(format), width, height, 0, format,
                 GL_UNSIGNED_BYTE, pixels);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    return texture;
}

// Images are stored top row first; OpenGL puts v = 0 at the bottom.
unsigned char* read_image(const std::string& path, int& width, int& height, int& channels) {
    stbi_set_flip_vertically_on_load(1);
    unsigned char* pixels = stbi_load(path.c_str(), &width, &height, &channels, 0);
    if (pixels == nullptr) {
        std::fprintf(stderr, "could not load '%s': %s\n", path.c_str(), stbi_failure_reason());
    }
    return pixels;
}

// A height map h(u, v) becomes a normal map: the surface z = h tilts against
// its gradient, n = normalize(-dh/du, -dh/dv, 1), with the derivatives taken as
// central differences between neighbouring texels (wrapping, as the texture
// repeats). The normal is then stored as a colour, [-1, 1] -> [0, 255].
std::vector<unsigned char> height_to_normals(const unsigned char* pixels, int width, int height,
                                             int channels, float scale) {
    const auto h = [&](int x, int y) {
        x = (x + width) % width;
        y = (y + height) % height;
        return pixels[(static_cast<size_t>(y) * width + x) * channels] / 255.0f;
    };

    std::vector<unsigned char> normals(static_cast<size_t>(width) * height * 3);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const float du = 0.5f * (h(x + 1, y) - h(x - 1, y));
            const float dv = 0.5f * (h(x, y + 1) - h(x, y - 1));
            const glm::vec3 n = glm::normalize(
                glm::vec3(-kHeightSlope * scale * du, -kHeightSlope * scale * dv, 1.0f));
            unsigned char* out = &normals[(static_cast<size_t>(y) * width + x) * 3];
            for (int c = 0; c < 3; ++c) {
                out[c] = static_cast<unsigned char>(std::lround(127.5f * (n[c] + 1.0f)));
            }
        }
    }
    return normals;
}

} // namespace

GLuint load_texture(const std::string& path) {
    int width = 0, height = 0, channels = 0;
    unsigned char* pixels = read_image(path, width, height, channels);
    if (pixels == nullptr) {
        return 0;
    }
    const GLuint texture = upload(pixels, width, height, channels);
    stbi_image_free(pixels);
    return texture;
}

// A normal map is an RGB image, mostly blue; a height map has one grey channel
// (plus alpha, sometimes). The channel count is enough to tell them apart.
GLuint load_normal_map(const std::string& path, float bump_scale) {
    int width = 0, height = 0, channels = 0;
    unsigned char* pixels = read_image(path, width, height, channels);
    if (pixels == nullptr) {
        return 0;
    }
    GLuint texture = 0;
    if (channels <= 2) {
        const std::vector<unsigned char> normals =
            height_to_normals(pixels, width, height, channels, bump_scale);
        texture = upload(normals.data(), width, height, 3);
    } else {
        texture = upload(pixels, width, height, channels);
    }
    stbi_image_free(pixels);
    return texture;
}

GLuint make_flat_texture(const glm::vec3& value) {
    const unsigned char texel[3] = {static_cast<unsigned char>(std::lround(255.0f * value.r)),
                                    static_cast<unsigned char>(std::lround(255.0f * value.g)),
                                    static_cast<unsigned char>(std::lround(255.0f * value.b))};
    return upload(texel, 1, 1, 3);
}

// The key of a colour image has scale 0, so the same file can also be cached as
// a normal map with its own bump scale.
GLuint TextureCache::colour(const std::string& path) {
    const auto key = std::make_pair(path, 0.0f);
    const auto found = m_textures.find(key);
    if (found != m_textures.end()) {
        return found->second;
    }
    return m_textures[key] = load_texture(path);
}

GLuint TextureCache::normal(const std::string& path, float bump_scale) {
    const auto key = std::make_pair(path, bump_scale);
    const auto found = m_textures.find(key);
    if (found != m_textures.end()) {
        return found->second;
    }
    return m_textures[key] = load_normal_map(path, bump_scale);
}

void TextureCache::destroy() {
    for (const auto& entry : m_textures) {
        glDeleteTextures(1, &entry.second);
    }
    m_textures.clear();
}
