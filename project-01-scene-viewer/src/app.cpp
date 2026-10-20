#include "app.hpp"

#include "picking.hpp"
#include "presets.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>

namespace {

// A vertex that only degenerate triangles used is left out of the CHE when
// welding: no front can start there.
bool on_surface(const CHE& che, int v) {
    const std::vector<int>& V = che.indices();
    return std::find(V.begin(), V.end(), v) != V.end();
}

camera camera_for(const CameraStart& start) {
    camera cam(start.target, start.orbit_radius, start.orbit_height, start.height_swing);
    cam.set_animated(true);
    return cam;
}

} // namespace

bool App::create() {
    if (!renderer.create()) {
        return false;
    }
    load_preset(0);
    return !world.objects.empty();
}

void App::destroy() {
    ::destroy(world);
    renderer.destroy();
}

void App::enter(World next) {
    ::destroy(world);
    world = std::move(next);
    upload(world);
    selected = -1;
    simplified_once = false;
    timeline.time = 0.0f;
    cam = camera_for(world.camera);
    animate(world, timeline.time);
}

void App::load_preset(int index) {
    World next;
    try {
        kPresets[index].build(next);
    } catch (const std::exception& e) {
        message = std::string("could not build ") + kPresets[index].name + ": " + e.what();
        std::fprintf(stderr, "%s\n", message.c_str());
        return;
    }
    enter(std::move(next));
    preset = index;
    message = std::string("loaded ") + kPresets[index].name;
}

void App::load_file(const std::string& path) {
    World next;
    try {
        build_from_file(next, path);
    } catch (const std::exception& e) {
        message = "could not load " + path + ": " + e.what();
        std::fprintf(stderr, "%s\n", message.c_str());
        return;
    }
    enter(std::move(next));
    preset = -1;
    message = "loaded " + world.name;
}

void App::reset() {
    timeline.time = 0.0f;
    cam = camera_for(world.camera);
}

void App::update(float seconds) {
    if (timeline.playing) {
        timeline.time = std::fmod(timeline.time + seconds * timeline.speed, Timeline::kLength);
        cam.update(seconds);
    }
    animate(world, timeline.time);
}

glm::vec3 App::light_direction() const {
    return sun_direction(world.sun, timeline.time);
}

void App::render(int framebuffer_width, int framebuffer_height, int window_width) {
    const int x = window_width > 0 ? panel_width * framebuffer_width / window_width : 0;
    const int width = std::max(1, framebuffer_width - x);
    const int height = framebuffer_height;

    Frame frame;
    frame.view = cam.view();
    frame.projection = cam.projection(width, height);
    frame.camera_position = cam.position();
    frame.light_direction = light_direction();
    frame.x = x;
    frame.width = width;
    frame.height = height;
    frame.selected = selected;
    // The false-colour depth spans the world's shadow sphere as seen from here.
    const float distance = glm::distance(frame.camera_position, world.shadow_centre);
    frame.depth_near = std::max(0.1f, distance - world.shadow_radius);
    frame.depth_far = distance + world.shadow_radius;
    performance.stats = renderer.draw(world, frame, settings);
}

void App::pick(double x, double y, int width, int height, bool add) {
    const Ray ray = ray_through(cam.view(), cam.projection(width, height), x, y, width, height);
    const Hit hit = ::pick(world, ray);
    if (hit.object < 0) {
        message = "nothing under the cursor";
        return;
    }
    selected = hit.object;
    Object& object = world.objects[static_cast<size_t>(hit.object)];
    if (!on_surface(object.che, hit.che_vertex)) {
        message = "that vertex is not on the surface";
        return;
    }
    if (!add) {
        object.seeds.clear();
    }
    if (std::find(object.seeds.begin(), object.seeds.end(), hit.che_vertex) == object.seeds.end()) {
        object.seeds.push_back(hit.che_vertex);
    }
    performance.fmm_ms = update_distances(object);
    settings.view = ViewMode::Distance;
    char text[160];
    std::snprintf(text, sizeof(text), "%s: seed at v%d (%zu seeds), %.2f ms", object.name.c_str(),
                  hit.che_vertex, object.seeds.size(), performance.fmm_ms);
    message = text;
}

void App::clear_seeds() {
    for (Object& object : world.objects) {
        if (!object.seeds.empty()) {
            object.seeds.clear();
            update_distances(object);
        }
    }
    message = "seeds cleared";
}

Object* App::selection() {
    return selected >= 0 && selected < static_cast<int>(world.objects.size())
               ? &world.objects[static_cast<size_t>(selected)]
               : nullptr;
}

void App::set_normals(NormalSource source) {
    if (Object* object = selection()) {
        ::set_normals(*object, source);
    }
}

void App::simplify_selected() {
    Object* object = selection();
    if (object == nullptr) {
        return;
    }
    last_simplify = simplify(*object, simplify_fraction);
    simplified_once = true;
    char text[160];
    std::snprintf(text, sizeof(text), "%s: %d -> %d triangles in %.1f ms", object->name.c_str(),
                  last_simplify.triangles_before, object->che.n_triangles(),
                  last_simplify.milliseconds);
    message = text;
}

void App::restore_selected() {
    if (Object* object = selection()) {
        restore(*object);
        simplified_once = false;
        message = object->name + " restored";
    }
}

int run_checks() {
    int failures = 0;
    for (int p = 0; p < kPresetCount; ++p) {
        World world;
        try {
            kPresets[p].build(world);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "%s: %s\n", kPresets[p].name, e.what());
            ++failures;
            continue;
        }
        animate(world, 0.0f);
        std::printf("%s: %zu objects, %zu materials\n", kPresets[p].name, world.objects.size(),
                    world.materials.size());

        // The object with the most triangles gets the algorithms.
        size_t largest = 0;
        for (size_t i = 0; i < world.objects.size(); ++i) {
            const Object& object = world.objects[i];
            const std::string problem = object.che.validate();
            std::printf("  %-16s %6zu vertices -> CHE V=%5d F=%5d %s %s\n", object.name.c_str(),
                        object.mesh.vertices.size(), object.che.n_vertices(),
                        object.che.n_triangles(), object.che.is_closed() ? "closed" : "open  ",
                        problem.empty() ? "valid" : problem.c_str());
            if (object.che.n_triangles() > world.objects[largest].che.n_triangles()) {
                largest = i;
            }
        }

        Object& object = world.objects[largest];
        object.seeds = {object.che.V(0), object.che.V(object.che.n_half_edges() / 2)};
        const double fmm_ms = update_distances(object);
        const auto reached = std::count_if(object.distances.t.begin(), object.distances.t.end(),
                                           [](float t) { return !std::isinf(t); });
        std::printf("  fast marching on %s from 2 seeds: %.2f ms, t_max %.3f, %ld of %d reached\n",
                    object.name.c_str(), fmm_ms, object.distances.max_finite,
                    static_cast<long>(reached), object.che.n_vertices());

        const SimplifyResult result = simplify(object, 0.25f);
        std::printf("  simplify %s: %d -> %d triangles, %.1f ms, error mean %.4f max %.4f\n",
                    object.name.c_str(), result.triangles_before, object.che.n_triangles(),
                    result.milliseconds, result.deviation.mean, result.deviation.max);
        failures += object.che.validate().empty() ? 0 : 1;

        // A ray from where the camera starts towards its target.
        const camera cam = camera_for(world.camera);
        const Ray ray{cam.position(), glm::normalize(world.camera.target - cam.position())};
        const Hit hit = pick(world, ray);
        std::printf("  pick towards the camera target: %s\n",
                    hit.object >= 0 ? world.objects[static_cast<size_t>(hit.object)].name.c_str()
                                    : "(nothing)");
        failures += hit.object >= 0 ? 0 : 1;
    }
    return failures;
}
