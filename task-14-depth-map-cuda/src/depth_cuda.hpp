// The depth map on the GPU: one CUDA thread per pixel. This header is plain
// C++ (no CUDA types), so main.cpp is compiled by the host compiler and only
// depth_cuda.cu goes through nvcc.
#pragma once

#include "gpu_scene.hpp"

#include <string>
#include <vector>

struct DepthImage {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> grey; // row 0 at the bottom, as task 13
    float nearest = 0.0f;            // over the pixels that hit something
    float farthest = 0.0f;
    double kernel_ms = 0.0; // the two kernels, timed with CUDA events
    double total_ms = 0.0;  // plus the allocations and copies host <-> device
};

// Name and compute capability of the GPU. It is also the first CUDA call, so
// the driver's start-up cost is paid here and not in the render's timing.
// Throws std::runtime_error if there is no CUDA device.
std::string cuda_device_name();

// Casts width x height primary rays against the scene. inv_proj_view is the
// inverse of projection * view in glm's column-major layout; origin is the
// camera position. With use_boxes off, the bounding boxes are ignored.
DepthImage render_depth_cuda(const GpuScene& scene, const float inv_proj_view[16],
                             const float origin[3], int width, int height, bool use_boxes);
