#include "filters.hpp"

#include <cmath>

namespace {

// Gaussian: sigma 0.5 pixel, cut at 1.5 pixels.
constexpr float kGaussianSigma = 0.5f;

// Mitchell-Netravali: B = C = 1/3, the pair Mitchell and Netravali (1988)
// recommend between blurring (large B) and ringing (large C).
constexpr float kMitchellB = 1.0f / 3.0f;
constexpr float kMitchellC = 1.0f / 3.0f;

float gaussian(float x) {
    return std::exp(-x * x / (2.0f * kGaussianSigma * kGaussianSigma));
}

// The cubic of Mitchell and Netravali on |x| in [0, 2):
//   |x| < 1:  ((12 - 9B - 6C)|x|^3 + (-18 + 12B + 6C)x^2 + (6 - 2B)) / 6
//   |x| < 2:  ((-B - 6C)|x|^3 + (6B + 30C)x^2 + (-12B - 48C)|x| + (8B + 24C)) / 6
// The second piece is negative between 1 and 2: the negative lobes.
float mitchell(float x) {
    const float B = kMitchellB, C = kMitchellC;
    x = std::fabs(x);
    if (x < 1.0f) {
        return ((12.0f - 9.0f * B - 6.0f * C) * x * x * x + (-18.0f + 12.0f * B + 6.0f * C) * x * x +
                (6.0f - 2.0f * B)) /
               6.0f;
    }
    if (x < 2.0f) {
        return ((-B - 6.0f * C) * x * x * x + (6.0f * B + 30.0f * C) * x * x +
                (-12.0f * B - 48.0f * C) * x + (8.0f * B + 24.0f * C)) /
               6.0f;
    }
    return 0.0f;
}

} // namespace

Filter make_filter(FilterType type) {
    switch (type) {
    case FilterType::Box: return {type, 0.5f};      // exactly the pixel's own square
    case FilterType::Triangle: return {type, 1.0f}; // reaches the neighbours' centres
    case FilterType::Gaussian: return {type, 1.5f};
    case FilterType::Mitchell: return {type, 2.0f};
    }
    return {FilterType::Box, 0.5f};
}

const char* Filter::name() const {
    switch (type) {
    case FilterType::Box: return "Box";
    case FilterType::Triangle: return "Triangle (tent)";
    case FilterType::Gaussian: return "Gaussian";
    case FilterType::Mitchell: return "Mitchell-Netravali";
    }
    return "?";
}

const char* Filter::file_name() const {
    switch (type) {
    case FilterType::Box: return "box";
    case FilterType::Triangle: return "triangle";
    case FilterType::Gaussian: return "gaussian";
    case FilterType::Mitchell: return "mitchell";
    }
    return "filter";
}

float Filter::profile(float x) const {
    const float ax = std::fabs(x);
    if (ax > radius) {
        return 0.0f;
    }
    switch (type) {
    case FilterType::Box:
        // Equal weight: every sample in the footprint counts the same.
        return 1.0f;
    case FilterType::Triangle:
        // Linear falloff from the centre to zero at the radius.
        return 1.0f - ax / radius;
    case FilterType::Gaussian:
        // The Gaussian's support is infinite; it is cut at the radius, and its
        // value there is subtracted so the cut does not leave a step (PBRT).
        return std::fmax(0.0f, gaussian(ax) - gaussian(radius));
    case FilterType::Mitchell:
        // The cubic is defined on [-2, 2]: scale the radius to 2.
        return mitchell(2.0f * ax / radius);
    }
    return 0.0f;
}
