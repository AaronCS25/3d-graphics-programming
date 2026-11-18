// Task 15 — Simple Path Tracer (offline), CPU vs GPU
//
// Task 12's gallery, lit by task 12's sun and seen from task 13's camera, path
// traced with every surface diffuse: primary rays, a shadow ray towards the sun
// at every hit, and a few diffuse bounces that bring in the sky and the light
// reflected by the other surfaces. The same path tracer (path_tracer.hpp) runs
// on the CPU and, when the program is built with CUDA, on the GPU, and the two
// times are compared.
//
//   1. read the OBJ and its map_Kd images, place it as tasks 12 and 13 do
//   2. flatten it into the path tracer's arrays (scene_build)
//   3. render on the CPU, on the GPU, or both (render_cpu / render_gpu)
//   4. write docs/<device>_<samples>spp.png and print the times
//
// Usage:  task15 [gallery | path/to/scene.obj] [--cpu] [--gpu] [--spp N]
//                [--bounces N] [--size WxH]
//         Without --cpu / --gpu it runs every version it was built with.

#include <camera.hpp>

#include "obj_loader.hpp"
#include "render.hpp"
#include "scene_build.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <stb_image_write.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

constexpr int kDefaultWidth = 1200;
constexpr int kDefaultHeight = 800;
constexpr int kDefaultSamples = 64;
constexpr int kDefaultBounces = 3;

// Same placement as tasks 12 and 13: longest side to this size, centred on the
// y axis, standing on y = 0.
constexpr float kSceneSize = 16.0f;

struct Preset {
    const char* name;
    const char* file; // relative to SCENE_DIR
    float yaw;        // degrees about y, applied to the whole scene
    glm::vec3 target;
    float orbit_radius;
    float orbit_height;
    float ambient;       // task 12's Ia: here the radiance of the sky
    float sun_elevation; // degrees
};

// Task 12's gallery: its camera at the start of the animation (task 13's) and
// its sun at time 0 (azimuth 0).
const Preset kPresets[] = {
    {"gallery", "gallery/gallery.obj", 0.0f, {0.0f, 1.2f, 0.0f}, 12.0f, 4.5f, 0.20f, 50.0f},
};

// Task 12's clear colour, what a primary ray that misses everything shows.
const glm::vec3 kBackground(0.07f, 0.08f, 0.11f);

const Preset* presetOf(const std::string& name) {
    for (const Preset& preset : kPresets) {
        if (name == preset.name) {
            return &preset;
        }
    }
    return nullptr;
}

Vec3 toVec3(const glm::vec3& v) {
    return {v.x, v.y, v.z};
}

struct Placement {
    glm::mat4 model;
    camera view;
};

// The model matrix of task 12 and the camera that frames the result: the
// preset's, or for any other file an orbit at about twice the scene's radius.
Placement placeScene(const Scene& scene, const Preset* preset) {
    const glm::vec3 extent = scene.bounds_max - scene.bounds_min;
    const float scale = kSceneSize / std::max({extent.x, extent.y, extent.z, 1e-6f});
    const glm::vec3 anchor(0.5f * (scene.bounds_min.x + scene.bounds_max.x), scene.bounds_min.y,
                           0.5f * (scene.bounds_min.z + scene.bounds_max.z));
    const float yaw = preset != nullptr ? preset->yaw : 0.0f;

    Placement placement;
    if (preset != nullptr) {
        placement.view = camera(preset->target, preset->orbit_radius, preset->orbit_height, 0.0f);
    } else {
        const float radius = 0.5f * glm::length(extent) * scale;
        placement.view = camera(glm::vec3(0.0f, 0.5f * extent.y * scale, 0.0f), 1.7f * radius,
                                0.7f * radius, 0.0f);
    }
    placement.model =
        glm::rotate(glm::mat4(1.0f), glm::radians(yaw), glm::vec3(0.0f, 1.0f, 0.0f)) *
        glm::scale(glm::mat4(1.0f), glm::vec3(scale)) * glm::translate(glm::mat4(1.0f), -anchor);
    return placement;
}

// Radiance to 8-bit colour: clamped, no tone mapping and no gamma, as task 12
// writes its colours, so the two images can be put side by side.
std::vector<unsigned char> toRgb8(const Image& image) {
    std::vector<unsigned char> rgb(image.pixels.size() * 3);
    for (size_t i = 0; i < image.pixels.size(); ++i) {
        const Vec3 p = image.pixels[i];
        const float channels[3] = {p.x, p.y, p.z};
        for (int c = 0; c < 3; ++c) {
            rgb[3 * i + c] = static_cast<unsigned char>(
                std::lround(255.0f * std::clamp(channels[c], 0.0f, 1.0f)));
        }
    }
    return rgb;
}

std::string savePng(const Image& image, const char* device, int samples) {
    std::filesystem::create_directories(DOCS_DIR);
    const std::string path =
        std::string(DOCS_DIR) + device + "_" + std::to_string(samples) + "spp.png";
    const std::vector<unsigned char> rgb = toRgb8(image);
    stbi_flip_vertically_on_write(1); // row 0 is the bottom, a PNG's is the top
    if (stbi_write_png(path.c_str(), image.width, image.height, 3, rgb.data(),
                       image.width * 3) == 0) {
        throw std::runtime_error("Could not write " + path);
    }
    return path;
}

#ifdef HAVE_CUDA
// How far apart the two images are, in 8-bit levels.
void compare(const Image& a, const Image& b) {
    const std::vector<unsigned char> x = toRgb8(a);
    const std::vector<unsigned char> y = toRgb8(b);
    size_t differing = 0;
    int largest = 0;
    for (size_t i = 0; i < x.size(); i += 3) {
        int d = 0;
        for (int c = 0; c < 3; ++c) {
            d = std::max(d, std::abs(x[i + c] - y[i + c]));
        }
        differing += d > 0 ? 1 : 0;
        largest = std::max(largest, d);
    }
    std::printf("CPU vs GPU image: %zu of %zu pixels differ, by at most %d levels\n", differing,
                x.size() / 3, largest);
}
#endif

int parsePositive(const char* text, const char* flag) {
    const int value = std::atoi(text);
    if (value <= 0) {
        throw std::runtime_error(std::string(flag) + " expects a positive number");
    }
    return value;
}

} // namespace

int main(int argc, char** argv) {
    try {
        std::string name = kPresets[0].name;
        bool runCpu = false;
        bool runGpu = false;
        int width = kDefaultWidth;
        int height = kDefaultHeight;
        int samples = kDefaultSamples;
        int bounces = kDefaultBounces;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--cpu") {
                runCpu = true;
            } else if (arg == "--gpu") {
                runGpu = true;
            } else if (arg == "--spp" && i + 1 < argc) {
                samples = parsePositive(argv[++i], "--spp");
            } else if (arg == "--bounces" && i + 1 < argc) {
                bounces = std::atoi(argv[++i]);
                if (bounces < 0) {
                    throw std::runtime_error("--bounces expects 0 or more");
                }
            } else if (arg == "--size" && i + 1 < argc) {
                if (std::sscanf(argv[++i], "%dx%d", &width, &height) != 2 || width <= 0 ||
                    height <= 0) {
                    throw std::runtime_error("--size expects WIDTHxHEIGHT, e.g. 1920x1080");
                }
            } else {
                name = arg;
            }
        }
#ifdef HAVE_CUDA
        if (!runCpu && !runGpu) {
            runCpu = runGpu = true;
        }
#else
        if (runGpu) {
            throw std::runtime_error("This build has no CUDA: --gpu needs nvcc (see README)");
        }
        runCpu = true;
#endif

        const Preset* preset = presetOf(name);
        const std::string path = preset != nullptr ? std::string(SCENE_DIR) + preset->file : name;
        const Scene obj = load_obj(path);
        const Placement placement = placeScene(obj, preset);
        const HostScene scene = build_scene(obj, placement.model);

        RenderSettings settings{};
        const glm::mat4 invProjView =
            glm::inverse(placement.view.projection(width, height) * placement.view.view());
        std::memcpy(settings.inv_proj_view, glm::value_ptr(invProjView),
                    sizeof(settings.inv_proj_view));
        settings.camera_pos = toVec3(placement.view.position());
        settings.width = width;
        settings.height = height;
        settings.samples = samples;
        settings.max_bounces = bounces;
        // Task 12's sun at time 0: azimuth 0, the preset's elevation, colour 1.
        const float elevation = glm::radians(preset != nullptr ? preset->sun_elevation : 50.0f);
        settings.sun_dir = toVec3(glm::vec3(std::cos(elevation), std::sin(elevation), 0.0f));
        settings.sun_color = vec3(1.0f);
        settings.sky_color = vec3(preset != nullptr ? preset->ambient : 0.2f);
        settings.background = toVec3(kBackground);
        settings.seed = 2026;

        std::printf("%s: %zu triangles in %zu objects, %zu textures\n", name.c_str(),
                    scene.triangles.size(), scene.objects.size(), scene.textures.size());
        std::printf("%dx%d, %d samples per pixel, up to %d bounces\n", width, height, samples,
                    bounces);

        Image cpu, gpu;
        if (runCpu) {
            std::printf("CPU: %u threads ...\n", std::max(1u, std::thread::hardware_concurrency()));
            std::fflush(stdout);
            cpu = render_cpu(scene, settings);
            std::printf("CPU: %.2f s -> %s\n", cpu.seconds, savePng(cpu, "cpu", samples).c_str());
        }
#ifdef HAVE_CUDA
        if (runGpu) {
            std::printf("GPU: %s ...\n", gpu_name().c_str());
            std::fflush(stdout);
            double kernelSeconds = 0.0;
            gpu = render_gpu(scene, settings, kernelSeconds);
            std::printf("GPU: %.2f s (kernels %.2f s) -> %s\n", gpu.seconds, kernelSeconds,
                        savePng(gpu, "gpu", samples).c_str());
        }
        if (runCpu && runGpu) {
            std::printf("speed-up: %.1fx\n", cpu.seconds / gpu.seconds);
            compare(cpu, gpu);
        }
#endif
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
    return 0;
}
