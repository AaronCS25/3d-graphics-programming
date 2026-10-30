// The depth map: one primary ray per pixel, the distance to its nearest hit,
// and that distance as a grey level.
#pragma once

#include "ray_scene.hpp"

#include <camera.hpp>

#include <vector>

struct DepthMap {
    int width = 0;
    int height = 0;
    std::vector<float> distance; // row 0 at the bottom; +infinity where nothing was hit
    float nearest = 0.0f;        // over the pixels that hit something
    float farthest = 0.0f;
};

DepthMap render_depth(const RayScene& scene, const camera& view, int width, int height,
                      bool use_boxes);

// [nearest, farthest] -> [1, 0]: near surfaces white, far ones dark, and the
// background, infinitely far, black.
std::vector<unsigned char> to_grayscale(const DepthMap& map);
