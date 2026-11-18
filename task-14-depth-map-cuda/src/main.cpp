// Task 14 — Depth Map by Ray Casting, in CUDA
//
// Task 13 with the rays cast on the GPU: one CUDA thread per pixel. The scene,
// the camera, the intersection tests and the grey mapping are the same, so the
// image is the same; what changes is who runs the loop over the pixels.
//
//   1. read the OBJ (obj_loader of task 12) and place it as task 13 does
//   2. flatten it into two arrays the GPU can read (gpu_scene)
//   3. upload them, cast the rays and map the distances to grey (depth_cuda.cu)
//   4. write the image to docs/depth.png
//
// There is no window: the program is meant to run on a lab machine over SSH.
//
// Usage:  task14 [gallery | path/to/scene.obj] [--no-boxes] [--size WxH]

#include <camera.hpp>

#include "depth_cuda.hpp"
#include "gpu_scene.hpp"
#include "obj_loader.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <stb_image_write.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <string>

namespace {

// Task 13's image size, so the two images can be compared pixel by pixel.
constexpr int kDefaultWidth = 1200;
constexpr int kDefaultHeight = 800;

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
};

// Task 12's gallery and the camera of task 13.
const Preset kPresets[] = {
    {"gallery", "gallery/gallery.obj", 0.0f, {0.0f, 1.2f, 0.0f}, 12.0f, 4.5f},
};

const Preset* presetOf(const std::string& name) {
    for (const Preset& preset : kPresets) {
        if (name == preset.name) {
            return &preset;
        }
    }
    return nullptr;
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

bool savePng(const DepthImage& image, const std::string& path) {
    stbi_flip_vertically_on_write(1); // the image's row 0 is the bottom, a PNG's is the top
    return stbi_write_png(path.c_str(), image.width, image.height, 1, image.grey.data(),
                          image.width) != 0;
}

} // namespace

int main(int argc, char** argv) {
    std::string name = kPresets[0].name;
    bool useBoxes = true;
    int width = kDefaultWidth;
    int height = kDefaultHeight;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--no-boxes") == 0) {
            useBoxes = false;
        } else if (std::strcmp(argv[i], "--size") == 0 && i + 1 < argc) {
            if (std::sscanf(argv[++i], "%dx%d", &width, &height) != 2 || width <= 0 ||
                height <= 0) {
                std::fprintf(stderr, "--size expects WIDTHxHEIGHT, e.g. 1920x1080\n");
                return 1;
            }
        } else {
            name = argv[i];
        }
    }

    try {
        std::printf("GPU: %s\n", cuda_device_name().c_str());

        const Preset* preset = presetOf(name);
        const std::string path = preset != nullptr ? std::string(SCENE_DIR) + preset->file : name;
        const Scene obj = load_obj(path);
        const Placement placement = placeScene(obj, preset);
        const GpuScene scene = build_gpu_scene(obj, placement.model);

        const glm::mat4 invProjView =
            glm::inverse(placement.view.projection(width, height) * placement.view.view());
        const glm::vec3 origin = placement.view.position();
        const DepthImage image = render_depth_cuda(scene, glm::value_ptr(invProjView),
                                                   glm::value_ptr(origin), width, height, useBoxes);

        std::printf("%s: %zu triangles in %zu objects, %dx%d rays, boxes %s\n", name.c_str(),
                    scene.triangles.size(), scene.objects.size(), width, height,
                    useBoxes ? "on" : "off");
        std::printf("kernels %.2f ms, total %.2f ms (with copies), depth %.2f - %.2f\n",
                    image.kernel_ms, image.total_ms, image.nearest, image.farthest);

        const std::string output = std::string(DOCS_DIR) + "depth.png";
        std::filesystem::create_directories(DOCS_DIR);
        if (!savePng(image, output)) {
            std::fprintf(stderr, "Could not write %s\n", output.c_str());
            return 1;
        }
        std::printf("wrote %s\n", output.c_str());
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
    return 0;
}
