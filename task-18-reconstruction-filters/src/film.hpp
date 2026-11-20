// The film: where the samples land and how they become pixels.
//
// Rendering is split in two, so the four filters see the very same samples
// and their images differ only in the filter, not in the noise:
//
//   1. trace_samples(): spp paths per pixel through random points of the
//      pixel; each sample keeps its film position and its radiance;
//   2. reconstruct(): for every pixel, every sample within the filter's radius
//      of its centre, weighted by the filter (slides 16):
//
//          colour = sum_i f(x_i, y_i) L_i / sum_i f(x_i, y_i)
//
// A pixel is a point at its centre, not a little square: with a radius larger
// than half a pixel a sample counts for several pixels.
#pragma once

#include "filters.hpp"
#include "scene_data.hpp"

#include <vector>

struct FilmSample {
    float x, y; // film position, in pixels
    Vec3 radiance;
};

struct Samples {
    int width = 0;
    int height = 0;
    int per_pixel = 0;
    // Grouped by the pixel the sample fell in: those of pixel (x, y) are
    // [(y * width + x) * per_pixel, ... + per_pixel).
    std::vector<FilmSample> samples;
};

Samples trace_samples(const HostScene& scene, const RenderSettings& settings);

struct FilteredImage {
    int width = 0;
    int height = 0;
    std::vector<Vec3> pixels; // row 0 at the bottom
};

FilteredImage reconstruct(const Samples& samples, const Filter& filter);
