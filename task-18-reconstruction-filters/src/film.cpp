#include "film.hpp"

#include "path_tracer.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <thread>

namespace {

// Runs body(y) for every row, the rows shared out among one thread per core.
void parallel_rows(int height, const std::function<void(int)>& body) {
    std::atomic<int> next{0};
    const unsigned count = std::max(1u, std::thread::hardware_concurrency());
    std::vector<std::thread> threads;
    for (unsigned i = 0; i < count; ++i) {
        threads.emplace_back([&] {
            for (int y = next++; y < height; y = next++) {
                body(y);
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
}

} // namespace

Samples trace_samples(const HostScene& host, const RenderSettings& settings) {
    Samples out;
    out.width = settings.width;
    out.height = settings.height;
    out.per_pixel = settings.samples;
    out.samples.resize(static_cast<size_t>(settings.width) * settings.height * settings.samples);

    const SceneView scene = host.view();
    parallel_rows(settings.height, [&](int y) {
        for (int x = 0; x < settings.width; ++x) {
            const int pixel = y * settings.width + x;
            for (int s = 0; s < settings.samples; ++s) {
                Rng rng(static_cast<unsigned>(pixel), static_cast<unsigned>(s), settings.seed);
                // A uniformly random point of the pixel's square.
                const float fx = x + rng.next();
                const float fy = y + rng.next();
                out.samples[static_cast<size_t>(pixel) * settings.samples + s] = {
                    fx, fy, trace_path(scene, settings, fx, fy, rng)};
            }
        }
    });
    return out;
}

FilteredImage reconstruct(const Samples& in, const Filter& filter) {
    FilteredImage image;
    image.width = in.width;
    image.height = in.height;
    image.pixels.assign(static_cast<size_t>(in.width) * in.height, vec3(0.0f));

    // A sample lies in [qx, qx + 1); it can reach pixel x only if its pixel qx
    // is within ceil(radius) of x.
    const int reach = static_cast<int>(std::ceil(filter.radius));

    // Gather, not splat: each thread writes only its own rows.
    parallel_rows(in.height, [&](int y) {
        const float cy = y + 0.5f;
        for (int x = 0; x < in.width; ++x) {
            const float cx = x + 0.5f;
            Vec3 sum = vec3(0.0f);
            float weights = 0.0f;
            for (int qy = std::max(0, y - reach); qy <= std::min(in.height - 1, y + reach); ++qy) {
                for (int qx = std::max(0, x - reach); qx <= std::min(in.width - 1, x + reach);
                     ++qx) {
                    const size_t first = (static_cast<size_t>(qy) * in.width + qx) * in.per_pixel;
                    for (int s = 0; s < in.per_pixel; ++s) {
                        const FilmSample& sample = in.samples[first + s];
                        const float w = filter.weight(sample.x - cx, sample.y - cy);
                        if (w != 0.0f) {
                            sum += sample.radiance * w;
                            weights += w;
                        }
                    }
                }
            }
            // Mitchell's negative lobes make some weights negative, but over a
            // full footprint the total stays positive.
            image.pixels[static_cast<size_t>(y) * in.width + x] =
                std::fabs(weights) > 1e-8f ? sum * (1.0f / weights) : vec3(0.0f);
        }
    });
    return image;
}
