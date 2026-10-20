#include "presets.hpp"

#include "mesh_bridge.hpp"
#include "mesh_io.hpp"
#include "shapes.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>

namespace {

const std::string kAssets = ASSET_DIR;

// A task 11 material: the three maps of textures/<name>_*, and as Kd the mean
// colour of the image, what the surface looks like with the maps off.
Material textured(const std::string& name, const glm::vec3& mean, bool maps = true) {
    Material m;
    m.name = name;
    m.ambient = m.diffuse = mean;
    m.specular = glm::vec3(1.0f); // the specular map is ks itself
    m.shininess = 16.0f;
    m.diffuse_map = kAssets + "textures/" + name + "_albedo.jpg";
    if (maps) {
        m.specular_map = kAssets + "textures/" + name + "_specular.png";
        m.bump_map = kAssets + "textures/" + name + "_normal.png";
    }
    return m;
}

Material clay() {
    Material m;
    m.name = "clay";
    m.ambient = m.diffuse = glm::vec3(0.80f, 0.62f, 0.50f);
    m.specular = glm::vec3(0.25f);
    m.shininess = 24.0f;
    return m;
}

// Moves the mesh so that the point `pivot` of its bounding box is its origin,
// and puts that origin back where it was: the object does not move, but now it
// spins and bounces about that point.
void pivot_on_centre(Object& object) {
    glm::vec3 lo, hi;
    mesh_bounds(object.mesh, lo, hi);
    const glm::vec3 centre = 0.5f * (lo + hi);
    object.placement = glm::translate(glm::mat4(1.0f), -centre);
    object.motion.centre = centre;
}

// Scales the mesh so its longest side measures `size` and stands it, centred,
// on `position`.
void stand_on(Object& object, const glm::vec3& position, float size) {
    glm::vec3 lo, hi;
    mesh_bounds(object.mesh, lo, hi);
    const glm::vec3 extent = hi - lo;
    const float scale = size / std::max({extent.x, extent.y, extent.z, 1e-6f});
    const glm::vec3 base(0.5f * (lo.x + hi.x), lo.y, 0.5f * (lo.z + hi.z));
    object.placement =
        glm::scale(glm::mat4(1.0f), glm::vec3(scale)) * glm::translate(glm::mat4(1.0f), -base);
    object.motion.centre = position;
}

std::string lowercase_extension(const std::string& path) {
    const size_t dot = path.find_last_of('.');
    std::string ext = dot == std::string::npos ? "" : path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

} // namespace

void build_gallery(World& world) {
    world.name = "Gallery";
    add_obj(world, kAssets + "gallery.obj", glm::mat4(1.0f));

    const int spot = find_object(world, "spot");
    if (spot >= 0) {
        Object& object = world.objects[static_cast<size_t>(spot)];
        pivot_on_centre(object);
        object.motion.spin_speed = 0.5f;
    }

    const char* bouncing[] = {"sphere_gold", "sphere_plastic", "sphere_chrome", "sphere_rubber"};
    for (int k = 0; k < 4; ++k) {
        const int i = find_object(world, bouncing[k]);
        if (i >= 0) {
            Object& object = world.objects[static_cast<size_t>(i)];
            pivot_on_centre(object);
            object.motion.bounce_height = 0.8f;
            object.motion.bounce_speed = 2.2f;
            object.motion.bounce_phase = 0.6f * static_cast<float>(k);
        }
    }

    const int pedestal = find_object(world, "pedestal");
    const int glass = find_object(world, "sphere_glass");
    if (pedestal >= 0 && glass >= 0) {
        glm::vec3 lo, hi;
        mesh_bounds(world.objects[static_cast<size_t>(pedestal)].mesh, lo, hi);
        Object& object = world.objects[static_cast<size_t>(glass)];
        pivot_on_centre(object);
        object.motion.centre = glm::vec3(0.5f * (lo.x + hi.x), 2.6f, 0.5f * (lo.z + hi.z));
        object.motion.orbit_radius = 2.1f;
        object.motion.orbit_speed = 0.5f;
    }

    world.camera = {{0.0f, 1.2f, 0.0f}, 12.0f, 4.5f, 1.2f};
    world.sun.azimuth_speed = 0.15f;
    world.sun.elevation_swing = 10.0f;
    world.sun.elevation_speed = 0.25f;
    world.sun.ambient = 0.2f;
    world.shadow_centre = glm::vec3(0.0f, 1.5f, 0.0f);
    world.shadow_radius = 10.5f;
}

void build_solar(World& world) {
    world.name = "Solar";
    const glm::mat4 identity(1.0f);

    const int paving = add_material(world, textured("paving", {0.59f, 0.56f, 0.44f}));
    add_object(world, "floor", make_floor(12.0f, 4.0f, 48, paving),
               glm::translate(identity, glm::vec3(0.0f, -2.2f, 0.0f)));

    // One material per cube face, in the order of make_cube: +x, -x, +y, -y, +z, -z.
    const int first_face = add_material(world, textured("bricks", {0.52f, 0.42f, 0.33f}));
    add_material(world, textured("rock", {0.46f, 0.43f, 0.28f}));
    add_material(world, textured("steel", {0.51f, 0.51f, 0.51f}));
    add_material(world, textured("tiles", {0.71f, 0.62f, 0.54f}));
    add_material(world, textured("wood", {0.61f, 0.48f, 0.37f}));
    add_material(world, textured("fabric", {0.55f, 0.53f, 0.51f}));
    const int cube = add_object(world, "cube", make_cube(1.4f, first_face), identity);
    world.objects[static_cast<size_t>(cube)].motion.spin_axis =
        glm::normalize(glm::vec3(0.21f, 0.5f, 0.0f));
    world.objects[static_cast<size_t>(cube)].motion.spin_speed = 0.54f;

    const int earth_material = add_material(world, textured("earth", {0.33f, 0.40f, 0.51f}));
    const int earth = add_object(world, "earth", make_sphere(64, 32, earth_material),
                                 glm::scale(identity, glm::vec3(0.7f)));
    Motion& orbit = world.objects[static_cast<size_t>(earth)].motion;
    orbit.orbit_radius = 3.0f;
    orbit.orbit_speed = 0.35f;
    orbit.orbit_tilt = 15.0f;
    orbit.spin_speed = 0.6f;

    // The moon only has a colour image: its dust is almost matte everywhere.
    Material moon_material = textured("moon", {0.59f, 0.58f, 0.57f}, false);
    moon_material.specular = glm::vec3(0.05f);
    const int moon =
        add_object(world, "moon", make_sphere(32, 16, add_material(world, moon_material)),
                   glm::scale(identity, glm::vec3(0.18f)));
    Motion& moon_orbit = world.objects[static_cast<size_t>(moon)].motion;
    moon_orbit.parent = earth;
    moon_orbit.orbit_radius = 1.15f;
    moon_orbit.orbit_speed = 1.1f;
    moon_orbit.orbit_tilt = 25.0f;

    const CHE cow_surface = load_ply(kAssets + "models/cow.ply");
    const int cow =
        add_object(world, "cow", mesh_from_che(cow_surface, add_material(world, clay())), identity);
    stand_on(world.objects[static_cast<size_t>(cow)], glm::vec3(-3.9f, -2.2f, 3.2f), 2.4f);
    world.objects[static_cast<size_t>(cow)].motion.spin_speed = 0.35f;

    world.camera = {{-0.8f, -0.6f, 0.8f}, 10.5f, 3.5f, 1.2f};
    world.shadow_centre = glm::vec3(0.0f);
    world.shadow_radius = 9.0f;
}

void build_from_file(World& world, const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    world.name = slash == std::string::npos ? path : path.substr(slash + 1);
    const glm::mat4 identity(1.0f);

    const int first = static_cast<int>(world.objects.size());
    if (lowercase_extension(path) == "ply") {
        add_object(world, world.name, mesh_from_che(load_ply(path), add_material(world, clay())),
                   identity);
    } else {
        add_obj(world, path, identity);
    }

    // Every object of the file gets the same placement: the file as a whole
    // is scaled to a longest side of 8 and stands centred on the floor.
    glm::vec3 lo(1e30f), hi(-1e30f);
    for (size_t i = static_cast<size_t>(first); i < world.objects.size(); ++i) {
        glm::vec3 a, b;
        mesh_bounds(world.objects[i].mesh, a, b);
        lo = glm::min(lo, a);
        hi = glm::max(hi, b);
    }
    const glm::vec3 extent = hi - lo;
    const float scale = 8.0f / std::max({extent.x, extent.y, extent.z, 1e-6f});
    const glm::vec3 base(0.5f * (lo.x + hi.x), lo.y, 0.5f * (lo.z + hi.z));
    const glm::mat4 placement =
        glm::scale(identity, glm::vec3(scale)) * glm::translate(identity, -base);
    for (size_t i = static_cast<size_t>(first); i < world.objects.size(); ++i) {
        world.objects[i].placement = placement;
    }

    const int paving = add_material(world, textured("paving", {0.59f, 0.56f, 0.44f}));
    add_object(world, "floor", make_floor(16.0f, 5.0f, 48, paving),
               glm::translate(identity, glm::vec3(0.0f, -0.01f, 0.0f)));

    const float height = extent.y * scale;
    world.camera = {{0.0f, 0.5f * height, 0.0f}, 12.0f, 4.0f, 1.0f};
    world.shadow_centre = glm::vec3(0.0f, 0.5f * height, 0.0f);
    world.shadow_radius = std::max(11.5f, 0.5f * glm::length(extent) * scale);
}

const Preset kPresets[] = {
    {"Gallery", build_gallery},
    {"Solar", build_solar},
};
const int kPresetCount = static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0]));
