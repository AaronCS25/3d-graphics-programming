#include "depth_map.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <thread>

DepthMap render_depth(const RayScene& scene, const camera& view, int width, int height,
                      bool use_boxes) {
    DepthMap map;
    map.width = width;
    map.height = height;
    map.distance.assign(static_cast<size_t>(width) * height,
                        std::numeric_limits<float>::infinity());

    const glm::mat4 inv_proj_view = glm::inverse(view.projection(width, height) * view.view());
    const glm::vec3 origin = view.position();

    // Every pixel is independent of the others (one CUDA thread per pixel in the
    // slides). Here each CPU thread takes every n-th row, so the rows that cross
    // many objects and the ones that see only the floor are spread evenly.
    const int threads = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
    std::vector<std::thread> workers;
    for (int first = 0; first < threads; ++first) {
        workers.emplace_back([&, first] {
            for (int y = first; y < height; y += threads) {
                for (int x = 0; x < width; ++x) {
                    const Ray ray = primary_ray(x, y, width, height, inv_proj_view, origin);
                    map.distance[static_cast<size_t>(y) * width + x] =
                        scene.closest_hit(ray, use_boxes);
                }
            }
        });
    }
    for (std::thread& worker : workers) {
        worker.join();
    }

    map.nearest = std::numeric_limits<float>::infinity();
    map.farthest = 0.0f;
    for (const float t : map.distance) {
        if (std::isfinite(t)) {
            map.nearest = std::min(map.nearest, t);
            map.farthest = std::max(map.farthest, t);
        }
    }
    if (!std::isfinite(map.nearest)) {
        map.nearest = 0.0f;
    }
    return map;
}

std::vector<unsigned char> to_grayscale(const DepthMap& map) {
    std::vector<unsigned char> pixels(map.distance.size(), 0);
    const float range = std::max(map.farthest - map.nearest, 1e-6f);
    for (size_t i = 0; i < pixels.size(); ++i) {
        const float t = map.distance[i];
        if (!std::isfinite(t)) {
            continue;
        }
        const float depth = (t - map.nearest) / range; // [0, 1], 0 at the nearest hit
        pixels[i] = static_cast<unsigned char>(std::lround(255.0f * (1.0f - depth)));
    }
    return pixels;
}
