#include "world.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

int add_object(World& world, const std::string& name, Mesh mesh, const glm::mat4& placement) {
    Object object;
    object.name = name;
    object.loaded = std::move(mesh);
    object.mesh = object.loaded;
    object.placement = placement;
    rebuild_che(object);
    world.objects.push_back(std::move(object));
    return static_cast<int>(world.objects.size()) - 1;
}

int add_material(World& world, const Material& material) {
    world.materials.push_back(material);
    return static_cast<int>(world.materials.size()) - 1;
}

int add_obj(World& world, const std::string& path, const glm::mat4& placement) {
    Scene scene = load_obj(path);

    const int offset = static_cast<int>(world.materials.size());
    for (const Material& material : scene.materials) {
        add_material(world, material);
    }

    const int first = static_cast<int>(world.objects.size());
    for (Mesh& mesh : scene.meshes) {
        for (Submesh& submesh : mesh.submeshes) {
            submesh.material = submesh.material >= 0 ? submesh.material + offset : -1;
        }
        const std::string name = mesh.name;
        add_object(world, name, std::move(mesh), placement);
    }
    return first;
}

int find_object(const World& world, const std::string& name) {
    for (size_t i = 0; i < world.objects.size(); ++i) {
        if (world.objects[i].name == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

glm::vec3 sun_direction(const Sun& sun, float t) {
    float azimuth = glm::radians(sun.azimuth);
    float elevation = glm::radians(sun.elevation);
    if (sun.animated) {
        azimuth += t * sun.azimuth_speed;
        elevation += glm::radians(sun.elevation_swing) * std::sin(t * sun.elevation_speed);
    }
    return {std::cos(elevation) * std::cos(azimuth), std::sin(elevation),
            std::cos(elevation) * std::sin(azimuth)};
}

void animate(World& world, float t) {
    const glm::mat4 identity(1.0f);
    const glm::vec3 y_axis(0.0f, 1.0f, 0.0f);

    for (Object& object : world.objects) {
        const Motion& m = object.motion;
        const glm::mat4 parent =
            m.parent >= 0 ? world.objects[static_cast<size_t>(m.parent)].anchor : identity;

        glm::mat4 anchor = parent * glm::translate(identity, m.centre);
        if (m.orbit_radius != 0.0f || m.orbit_speed != 0.0f) {
            anchor = anchor *
                     glm::rotate(identity, glm::radians(m.orbit_tilt), glm::vec3(0, 0, 1)) *
                     glm::rotate(identity, m.orbit_speed * t + m.orbit_phase, y_axis) *
                     glm::translate(identity, glm::vec3(m.orbit_radius, 0.0f, 0.0f));
        }
        if (m.bounce_height != 0.0f) {
            const float hop = std::abs(std::sin(m.bounce_speed * t + m.bounce_phase));
            anchor =
                anchor * glm::translate(identity, glm::vec3(0.0f, m.bounce_height * hop, 0.0f));
        }
        object.anchor = anchor;
        object.model =
            anchor * glm::rotate(identity, m.spin_speed * t, m.spin_axis) * object.placement;
    }
}

void upload(World& world) {
    for (Object& object : world.objects) {
        object.gpu.upload(object.mesh);
        update_distances(object);
    }
    world.maps.clear();
    for (const Material& material : world.materials) {
        MaterialMaps maps;
        if (!material.diffuse_map.empty()) {
            maps.diffuse = world.textures.colour(material.diffuse_map);
        }
        if (!material.specular_map.empty()) {
            maps.specular = world.textures.colour(material.specular_map);
        }
        if (!material.bump_map.empty()) {
            maps.normal = world.textures.normal(material.bump_map, material.bump_scale);
        }
        world.maps.push_back(maps);
    }
}

void destroy(World& world) {
    for (Object& object : world.objects) {
        object.gpu.destroy();
    }
    world.textures.destroy();
    world = World();
}
