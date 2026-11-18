#include "path_tracer.hpp"
#include "render.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>

Image render_cpu(const HostScene& host, const RenderSettings& settings) {
    const auto start = std::chrono::steady_clock::now();
    Image image;
    image.width = settings.width;
    image.height = settings.height;
    image.pixels.assign(static_cast<size_t>(settings.width) * settings.height, vec3(0.0f));

    const SceneView scene = host.view();
    std::atomic<int> next_row{0};
    const auto worker = [&] {
        for (int y = next_row++; y < settings.height; y = next_row++) {
            for (int x = 0; x < settings.width; ++x) {
                image.pixels[static_cast<size_t>(y) * settings.width + x] =
                    trace_pixel(scene, settings, x, y);
            }
        }
    };

    const unsigned threads = std::max(1u, std::thread::hardware_concurrency());
    std::vector<std::thread> workers;
    for (unsigned i = 0; i < threads; ++i) {
        workers.emplace_back(worker);
    }
    for (std::thread& thread : workers) {
        thread.join();
    }

    image.seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return image;
}
