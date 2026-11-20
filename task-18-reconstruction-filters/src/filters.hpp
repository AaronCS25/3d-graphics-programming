// Reconstruction filters (slides 16, "Image Reconstruction"). A filter is a
// weight f(dx, dy) for a sample at offset (dx, dy) from a pixel centre, in
// pixel units, zero beyond its radius. All four are separable,
// f(dx, dy) = f(dx) f(dy), and are drawn as 1D profiles in docs/comparison.png.
//
// The radii and parameters are PBRT's defaults (the renderer the slides cite
// for Mitchell-Netravali).
#pragma once

enum class FilterType { Box, Triangle, Gaussian, Mitchell };
constexpr int kFilterCount = 4;

struct Filter {
    FilterType type;
    float radius;

    const char* name() const;
    const char* file_name() const; // docs/<file_name>.png

    // The 1D profile at offset x.
    float profile(float x) const;

    float weight(float dx, float dy) const {
        return profile(dx) * profile(dy);
    }
};

Filter make_filter(FilterType type);
