// Task 18 — Reconstruction Filters
//
// Task 15's path tracer renders the gallery at a low resolution, so that every
// pixel is visible, and the same samples are turned into pixels with four
// filters: Box, Triangle, Gaussian and Mitchell-Netravali (slides 16).
//
//   1. read the OBJ, place it and light it as task 15 does
//   2. trace spp samples per pixel, keeping where each one hit the film
//   3. reconstruct the image with each filter (film.cpp)
//   4. write docs/<filter>.png at the rendered size, and docs/comparison.png:
//      the four images enlarged without smoothing, side by side, above the 1D
//      profiles of the filters
//
// Usage:  task18 [--size WxH] [--spp N] [--scale K] [--bounces N]

#include <camera.hpp>

#include "film.hpp"
#include "filters.hpp"
#include "obj_loader.hpp"
#include "scene_build.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <stb_image_write.h>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#include <stb_easy_font.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr int kDefaultWidth = 240;
constexpr int kDefaultHeight = 160;
constexpr int kDefaultSamples = 256;
constexpr int kDefaultScale = 3;
constexpr int kDefaultBounces = 3;

// The close-up: the cow (black spots on white, its outline against the wall),
// in pixels of the 240 x 160 image counted from its top-left corner, scaled
// with the image when another --size is used.
constexpr float kCropX = 105.0f / 240.0f, kCropY = 45.0f / 160.0f;
constexpr float kCropW = 40.0f / 240.0f, kCropH = 40.0f / 160.0f;
constexpr int kCropTile = 320; // on-screen size of each close-up

// Task 12's gallery: the camera of tasks 13 and 15, the sun at time 0.
constexpr float kSceneSize = 16.0f;
const glm::vec3 kTarget(0.0f, 1.2f, 0.0f);
constexpr float kOrbitRadius = 12.0f;
constexpr float kOrbitHeight = 4.5f;
constexpr float kSunElevation = 50.0f; // degrees
constexpr float kSky = 0.20f;
const glm::vec3 kBackground(0.07f, 0.08f, 0.11f);

// ---- a small RGB canvas for the comparison image ---------------------------

struct Rgb {
    unsigned char r, g, b;
};

const Rgb kCanvasBackground{18, 20, 28};
const Rgb kText{230, 232, 238};
const Rgb kMuted{120, 124, 135};
// Okabe-Ito colours, distinguishable with colour blindness.
const Rgb kFilterColours[kFilterCount] = {
    {230, 159, 0}, {86, 180, 233}, {0, 158, 115}, {204, 121, 167}};

struct Canvas {
    int width, height;
    std::vector<Rgb> pixels; // row 0 at the top

    Canvas(int w, int h) : width(w), height(h), pixels(static_cast<size_t>(w) * h, kCanvasBackground) {}

    void put(int x, int y, Rgb c) {
        if (x >= 0 && y >= 0 && x < width && y < height) {
            pixels[static_cast<size_t>(y) * width + x] = c;
        }
    }

    void fill(int x0, int y0, int x1, int y1, Rgb c) {
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                put(x, y, c);
            }
        }
    }

    // stb_easy_font turns text into quads; each quad is filled, scaled. The
    // scale drops until the text fits in max_width.
    void text(int x, int y, const std::string& s, int scale, Rgb c, int max_width = 1 << 30) {
        static char buffer[60000];
        std::vector<char> copy(s.begin(), s.end());
        copy.push_back('\0');
        while (scale > 1 && stb_easy_font_width(copy.data()) * scale > max_width) {
            --scale;
        }
        const int quads = stb_easy_font_print(0.0f, 0.0f, copy.data(), nullptr, buffer,
                                              sizeof(buffer));
        struct Vertex {
            float x, y, z;
            unsigned char colour[4];
        };
        const auto* v = reinterpret_cast<const Vertex*>(buffer);
        for (int q = 0; q < quads; ++q) {
            const Vertex* quad = v + 4 * q;
            const float qx0 = std::min({quad[0].x, quad[1].x, quad[2].x, quad[3].x});
            const float qy0 = std::min({quad[0].y, quad[1].y, quad[2].y, quad[3].y});
            const float qx1 = std::max({quad[0].x, quad[1].x, quad[2].x, quad[3].x});
            const float qy1 = std::max({quad[0].y, quad[1].y, quad[2].y, quad[3].y});
            fill(x + static_cast<int>(qx0 * scale), y + static_cast<int>(qy0 * scale),
                 x + static_cast<int>(qx1 * scale), y + static_cast<int>(qy1 * scale), c);
        }
    }

    // A thick line, stamped point by point.
    void outline(int x0, int y0, int x1, int y1, int thickness, Rgb c) {
        fill(x0, y0, x1, y0 + thickness, c);
        fill(x0, y1 - thickness, x1, y1, c);
        fill(x0, y0, x0 + thickness, y1, c);
        fill(x1 - thickness, y0, x1, y1, c);
    }

    void line(float x0, float y0, float x1, float y1, int thickness, Rgb c) {
        const int steps = static_cast<int>(std::ceil(std::hypot(x1 - x0, y1 - y0))) + 1;
        for (int i = 0; i <= steps; ++i) {
            const float t = static_cast<float>(i) / steps;
            const int x = static_cast<int>(std::lround(x0 + (x1 - x0) * t));
            const int y = static_cast<int>(std::lround(y0 + (y1 - y0) * t));
            fill(x - thickness / 2, y - thickness / 2, x - thickness / 2 + thickness,
                 y - thickness / 2 + thickness, c);
        }
    }
};

unsigned char toByte(float v) {
    return static_cast<unsigned char>(std::lround(255.0f * std::clamp(v, 0.0f, 1.0f)));
}

// Radiance to 8-bit colour, clamped, no gamma (as tasks 12 and 15).
Rgb toRgb(const Vec3& v) {
    return {toByte(v.x), toByte(v.y), toByte(v.z)};
}

void savePng(const std::string& path, int width, int height, const std::vector<Rgb>& rgb) {
    if (stbi_write_png(path.c_str(), width, height, 3, rgb.data(), width * 3) == 0) {
        throw std::runtime_error("Could not write " + path);
    }
    std::printf("wrote %s\n", path.c_str());
}

std::vector<Rgb> topDown(const FilteredImage& image) {
    std::vector<Rgb> rgb(image.pixels.size());
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            rgb[static_cast<size_t>(image.height - 1 - y) * image.width + x] =
                toRgb(image.pixels[static_cast<size_t>(y) * image.width + x]);
        }
    }
    return rgb;
}

// The four images in a 2 x 2 grid, each enlarged `scale` times by repeating
// pixels (no smoothing, so the pixels stay visible); a row of close-ups of the
// same region of the four; and the 1D profiles of the four filters.
void saveComparison(const std::vector<FilteredImage>& images, const std::vector<Filter>& filters,
                    int samples, int scale, const std::string& path) {
    const int w = images[0].width, h = images[0].height;
    const int tileW = w * scale, tileH = h * scale;
    const int gap = 24, label = 46, title = 70, plotH = 360;
    const int cropX = static_cast<int>(kCropX * w), cropY = static_cast<int>(kCropY * h);
    const int cropW = std::max(1, static_cast<int>(kCropW * w));
    const int cropH = std::max(1, static_cast<int>(kCropH * h));
    const int cropScale = std::max(1, kCropTile / std::max(cropW, cropH));
    const int closeW = cropW * cropScale, closeH = cropH * cropScale;
    const int width = 2 * tileW + 3 * gap;
    const int closeTop = title + 2 * (label + tileH) + gap;
    const int height = closeTop + label + closeH + gap + plotH + gap;
    Canvas canvas(width, height);
    const int textWidth = width - 2 * gap;

    char heading[200];
    std::snprintf(heading, sizeof(heading),
                  "Task 18 - Reconstruction filters: %dx%d, %d spp, the same samples, enlarged %dx",
                  w, h, samples, scale);
    canvas.text(gap, 22, heading, 3, kText, textWidth);

    const Rgb frame{255, 255, 255};
    for (int i = 0; i < kFilterCount; ++i) {
        const std::vector<Rgb> rgb = topDown(images[i]);
        const int col = i % 2, row = i / 2;
        const int x0 = gap + col * (tileW + gap);
        const int y0 = title + row * (label + tileH);
        char name[96];
        std::snprintf(name, sizeof(name), "%s, radius %.1f", filters[i].name(), filters[i].radius);
        canvas.fill(x0, y0 + 10, x0 + 22, y0 + 32, kFilterColours[i]);
        canvas.text(x0 + 34, y0 + 10, name, 3, kText, tileW - 34);
        for (int y = 0; y < tileH; ++y) {
            for (int x = 0; x < tileW; ++x) {
                canvas.put(x0 + x, y0 + label + y,
                           rgb[static_cast<size_t>(y / scale) * w + x / scale]);
            }
        }
        canvas.outline(x0 + cropX * scale, y0 + label + cropY * scale,
                       x0 + (cropX + cropW) * scale, y0 + label + (cropY + cropH) * scale, 2,
                       frame);

        // The close-up of the same region, side by side with the other three.
        const int span = (width - 2 * gap - kFilterCount * closeW) / (kFilterCount - 1);
        const int cx0 = gap + i * (closeW + span);
        canvas.fill(cx0, closeTop + 10, cx0 + 22, closeTop + 32, kFilterColours[i]);
        canvas.text(cx0 + 34, closeTop + 10, filters[i].name(), 3, kText, closeW - 34);
        for (int y = 0; y < closeH; ++y) {
            for (int x = 0; x < closeW; ++x) {
                canvas.put(cx0 + x, closeTop + label + y,
                           rgb[static_cast<size_t>(cropY + y / cropScale) * w + cropX +
                               x / cropScale]);
            }
        }
    }

    // Profiles f(x) / f(0) for x in [-2.25, 2.25] pixels.
    const int px0 = gap + 60, px1 = width - gap - 20;
    const int py0 = height - gap - plotH + 50, py1 = height - gap - 40;
    const float xMin = -2.25f, xMax = 2.25f, yMin = -0.15f, yMax = 1.1f;
    const auto sx = [&](float x) { return px0 + (x - xMin) / (xMax - xMin) * (px1 - px0); };
    const auto sy = [&](float y) { return py1 - (y - yMin) / (yMax - yMin) * (py1 - py0); };
    canvas.text(gap, py0 - 46,
                "Filter profiles f(x) / f(0), x = distance to the pixel centre (pixels)", 3, kText,
                textWidth);
    canvas.line(sx(xMin), sy(0.0f), sx(xMax), sy(0.0f), 2, kMuted);
    canvas.line(sx(0.0f), sy(yMin), sx(0.0f), sy(yMax), 1, kMuted);
    for (int tick = -2; tick <= 2; ++tick) {
        canvas.line(sx(static_cast<float>(tick)), sy(0.0f) - 6, sx(static_cast<float>(tick)),
                    sy(0.0f) + 6, 2, kMuted);
        canvas.text(static_cast<int>(sx(static_cast<float>(tick))) - 8,
                    static_cast<int>(sy(yMin)) + 8, std::to_string(tick), 3, kMuted);
    }
    canvas.text(px0 - 52, static_cast<int>(sy(1.0f)) - 10, "1", 3, kMuted);
    canvas.text(px0 - 52, static_cast<int>(sy(0.0f)) - 10, "0", 3, kMuted);
    for (int i = 0; i < kFilterCount; ++i) {
        const float peak = filters[i].profile(0.0f);
        float prevX = sx(xMin), prevY = sy(filters[i].profile(xMin) / peak);
        for (int k = 1; k <= 900; ++k) {
            const float x = xMin + (xMax - xMin) * k / 900.0f;
            const float nx = sx(x), ny = sy(filters[i].profile(x) / peak);
            // Consecutive points are joined, so the box's jumps become vertical lines.
            canvas.line(prevX, prevY, nx, ny, 4, kFilterColours[i]);
            prevX = nx;
            prevY = ny;
        }
    }
    savePng(path, width, height, canvas.pixels);
}

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
        int width = kDefaultWidth, height = kDefaultHeight;
        int samples = kDefaultSamples, scale = kDefaultScale, bounces = kDefaultBounces;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--size" && i + 1 < argc) {
                if (std::sscanf(argv[++i], "%dx%d", &width, &height) != 2 || width <= 0 ||
                    height <= 0) {
                    throw std::runtime_error("--size expects WIDTHxHEIGHT, e.g. 240x160");
                }
            } else if (arg == "--spp" && i + 1 < argc) {
                samples = parsePositive(argv[++i], "--spp");
            } else if (arg == "--scale" && i + 1 < argc) {
                scale = parsePositive(argv[++i], "--scale");
            } else if (arg == "--bounces" && i + 1 < argc) {
                bounces = std::max(0, std::atoi(argv[++i]));
            } else {
                throw std::runtime_error("unknown argument: " + arg);
            }
        }

        // The scene, placed as in tasks 12, 13 and 15.
        const Scene obj = load_obj(std::string(SCENE_DIR) + "gallery/gallery.obj");
        const glm::vec3 extent = obj.bounds_max - obj.bounds_min;
        const float sceneScale = kSceneSize / std::max({extent.x, extent.y, extent.z, 1e-6f});
        const glm::vec3 anchor(0.5f * (obj.bounds_min.x + obj.bounds_max.x), obj.bounds_min.y,
                               0.5f * (obj.bounds_min.z + obj.bounds_max.z));
        const glm::mat4 model = glm::scale(glm::mat4(1.0f), glm::vec3(sceneScale)) *
                                glm::translate(glm::mat4(1.0f), -anchor);
        const HostScene scene = build_scene(obj, model);
        const camera view(kTarget, kOrbitRadius, kOrbitHeight, 0.0f);

        RenderSettings settings{};
        const glm::mat4 invProjView = glm::inverse(view.projection(width, height) * view.view());
        std::memcpy(settings.inv_proj_view, glm::value_ptr(invProjView),
                    sizeof(settings.inv_proj_view));
        const glm::vec3 eye = view.position();
        settings.camera_pos = {eye.x, eye.y, eye.z};
        settings.width = width;
        settings.height = height;
        settings.samples = samples;
        settings.max_bounces = bounces;
        const float elevation = glm::radians(kSunElevation);
        settings.sun_dir = {std::cos(elevation), std::sin(elevation), 0.0f};
        settings.sun_color = vec3(1.0f);
        settings.sky_color = vec3(kSky);
        settings.background = {kBackground.x, kBackground.y, kBackground.z};
        settings.seed = 2026;

        std::printf("gallery: %zu triangles, %dx%d, %d samples per pixel\n",
                    scene.triangles.size(), width, height, samples);
        auto start = std::chrono::steady_clock::now();
        const Samples traced = trace_samples(scene, settings);
        std::printf("traced %zu samples in %.2f s\n", traced.samples.size(),
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());

        std::vector<Filter> filters;
        std::vector<FilteredImage> images;
        for (int i = 0; i < kFilterCount; ++i) {
            filters.push_back(make_filter(static_cast<FilterType>(i)));
            start = std::chrono::steady_clock::now();
            images.push_back(reconstruct(traced, filters.back()));
            std::printf("%-20s radius %.1f: %.3f s\n", filters.back().name(), filters.back().radius,
                        std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
                            .count());
            savePng(std::string(DOCS_DIR) + filters.back().file_name() + ".png", width, height,
                    topDown(images.back()));
        }
        saveComparison(images, filters, samples, scale, std::string(DOCS_DIR) + "comparison.png");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
    return 0;
}
