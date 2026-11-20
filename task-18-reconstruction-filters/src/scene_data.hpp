// The scene as the path tracer reads it: flat arrays of plain structs, the same
// on the CPU and on the GPU. HostScene owns them in host memory; SceneView is
// only pointers, so the GPU version builds one that points to device memory and
// passes it to the kernel by value.
#pragma once

#include "vec3.hpp"

#include <vector>

// What the intersection test reads, ray after ray: vertex a and the two edges
// of Möller-Trumbore. Kept apart from the shading data so the loop over
// triangles touches only these 36 bytes per triangle.
struct Triangle {
    Vec3 a, e1, e2;
};

// What is read once per hit: vertex normals, texture coordinates and material.
struct Shading {
    Vec3 n0, n1, n2;
    float uv[6]; // u0 v0 u1 v1 u2 v2
    int material;
};

// An object is a range of the triangle array and its bounding box (task 13).
struct Object {
    Vec3 box_min, box_max;
    int first, count;
};

// Every surface is diffuse: a colour, or an image when the material has
// map_Kd (as in task 12, where the map replaces Kd).
struct Diffuse {
    Vec3 albedo;
    int texture; // index into the textures, -1 if none
};

// One RGB image inside the shared texel array, 3 bytes per texel, row 0 at
// v = 0 (the bottom of the picture, as OpenGL reads it).
struct TextureInfo {
    int offset; // in bytes
    int width;
    int height;
};

struct SceneView {
    const Triangle* triangles;
    const Shading* shading;
    const Object* objects;
    int object_count;
    const Diffuse* materials;
    const unsigned char* texels;
    const TextureInfo* textures;
};

struct HostScene {
    std::vector<Triangle> triangles;
    std::vector<Shading> shading;
    std::vector<Object> objects;
    std::vector<Diffuse> materials;
    std::vector<unsigned char> texels;
    std::vector<TextureInfo> textures;

    SceneView view() const {
        return {triangles.data(), shading.data(),  objects.data(),
                static_cast<int>(objects.size()),  materials.data(),
                texels.data(),    textures.data()};
    }
};

// The camera, the lights and how many rays to spend. Plain values, passed to
// the kernel by value.
struct RenderSettings {
    float inv_proj_view[16]; // column-major, as glm stores it
    Vec3 camera_pos;
    int width;
    int height;
    int samples;     // paths per pixel
    int max_bounces; // diffuse bounces after the first hit
    Vec3 sun_dir;    // towards the sun, unit length
    Vec3 sun_color;  // irradiance of the sun, in units where Lambert is albedo * cos
    Vec3 sky_color;  // radiance of the sky, what a bounced ray finds if it escapes
    Vec3 background; // what a primary ray that hits nothing shows
    unsigned seed;
};
