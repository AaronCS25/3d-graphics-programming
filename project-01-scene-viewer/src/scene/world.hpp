// Everything a scene is made of: objects, the materials they share, the sun
// and where the camera starts. Building a world only reads files; upload()
// is the step that needs an OpenGL context, so a world can also be checked
// without a window.

#pragma once

#include "obj_loader.hpp"
#include "object.hpp"
#include "texture.hpp"

#include <glad/glad.h>

#include <glm/glm.hpp>

#include <string>
#include <vector>

// The maps of a material on the GPU, 0 where it has none.
struct MaterialMaps {
    GLuint diffuse = 0;
    GLuint specular = 0;
    GLuint normal = 0;
};

// The directional sun of task 10. While animated its azimuth turns and its
// elevation rises and falls with the timeline; otherwise the two angles are
// set by hand.
struct Sun {
    bool animated = true;
    float azimuth = 0.0f;          // degrees, the manual value
    float elevation = 50.0f;       // degrees, the manual value and the animated mean
    float azimuth_speed = 0.2f;    // rad/s
    float elevation_swing = 15.0f; // degrees
    float elevation_speed = 0.3f;  // rad/s
    glm::vec3 colour{1.0f};        // Id = Is
    float ambient = 0.18f;         // Ia
};

struct CameraStart {
    glm::vec3 target{0.0f};
    float orbit_radius = 10.0f;
    float orbit_height = 3.5f;
    float height_swing = 1.2f;
};

struct World {
    std::string name;
    std::vector<Object> objects;
    std::vector<Material> materials;
    std::vector<MaterialMaps> maps; // parallel to materials, after upload()
    TextureCache textures;
    Sun sun;
    CameraStart camera;
    // The sun's orthographic box wraps this sphere, which must hold every
    // object at every moment of the animation.
    glm::vec3 shadow_centre{0.0f};
    float shadow_radius = 10.0f;
};

// Adds every object (o / g) of an OBJ file, and its materials. Returns the
// index of the first new object.
int add_obj(World& world, const std::string& path, const glm::mat4& placement);

// Adds one object; its submeshes already index world.materials.
int add_object(World& world, const std::string& name, Mesh mesh, const glm::mat4& placement);

int add_material(World& world, const Material& material);

int find_object(const World& world, const std::string& name);

// Sun direction (towards the sun, unit length) at time t.
glm::vec3 sun_direction(const Sun& sun, float t);

// Recomputes anchor and model of every object for time t. Parents must come
// before their children in world.objects.
void animate(World& world, float t);

void upload(World& world);
void destroy(World& world);
