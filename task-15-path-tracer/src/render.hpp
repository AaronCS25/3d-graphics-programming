// The two ways of sharing the pixels out. Both call trace_sample() of
// path_tracer.hpp with the same scene and settings, so they draw the same
// paths and differ only in where the loop over the pixels runs.
#pragma once

#include "scene_data.hpp"

#include <string>
#include <vector>

struct Image {
    int width = 0;
    int height = 0;
    std::vector<Vec3> pixels; // linear radiance, row 0 at the bottom
    double seconds = 0.0;     // wall time of the render, scene upload included
};

// One std::thread per core; the threads take rows from a shared counter, so
// the rows that cross many objects and the ones that see only sky are spread
// evenly.
Image render_cpu(const HostScene& scene, const RenderSettings& settings);

#ifdef HAVE_CUDA
// Name and compute capability of the GPU. It is also the first CUDA call, so
// the driver's start-up cost is paid here and not in the render's timing.
std::string gpu_name();

// One CUDA thread per pixel, one kernel launch per sample. kernel_seconds gets
// the time of the kernels alone (CUDA events), without the copies.
Image render_gpu(const HostScene& scene, const RenderSettings& settings, double& kernel_seconds);
#endif
