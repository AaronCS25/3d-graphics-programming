// Task 13's ray caster, moved to the GPU. Two kernels:
//
//   cast_rays   one thread per pixel: primary ray, closest hit over every
//               triangle (skipping the objects whose box the ray misses),
//               distance to global memory; nearest / farthest hit reduced per
//               warp and merged with one atomic per warp
//   to_grey     one thread per pixel: [nearest, farthest] -> [255, 0]
//
// Only the grey image (one byte per pixel) is copied back to the host.

#include "depth_cuda.hpp"

#include <cuda_runtime.h>

#include <chrono>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>

namespace {

void check(cudaError_t error, const char* call, const char* file, int line) {
    if (error != cudaSuccess) {
        throw std::runtime_error(std::string(file) + ":" + std::to_string(line) + ": " + call +
                                 " failed: " + cudaGetErrorString(error));
    }
}

#define CUDA_CHECK(call) check((call), #call, __FILE__, __LINE__)

// Device memory released when it goes out of scope, so an exception thrown
// between cudaMalloc and cudaFree does not leak it.
template <typename T>
class DeviceBuffer {
public:
    explicit DeviceBuffer(size_t count) : count_(count) {
        if (count_ > 0) {
            CUDA_CHECK(cudaMalloc(&data_, count_ * sizeof(T)));
        }
    }
    ~DeviceBuffer() {
        cudaFree(data_);
    }
    DeviceBuffer(const DeviceBuffer&) = delete;
    DeviceBuffer& operator=(const DeviceBuffer&) = delete;

    T* get() const {
        return data_;
    }
    void upload(const T* host) {
        if (count_ > 0) {
            CUDA_CHECK(cudaMemcpy(data_, host, count_ * sizeof(T), cudaMemcpyHostToDevice));
        }
    }
    void download(T* host) const {
        if (count_ > 0) {
            CUDA_CHECK(cudaMemcpy(host, data_, count_ * sizeof(T), cudaMemcpyDeviceToHost));
        }
    }

private:
    T* data_ = nullptr;
    size_t count_ = 0;
};

class Event {
public:
    Event() {
        CUDA_CHECK(cudaEventCreate(&event_));
    }
    ~Event() {
        cudaEventDestroy(event_);
    }
    Event(const Event&) = delete;
    Event& operator=(const Event&) = delete;

    cudaEvent_t get() const {
        return event_;
    }

private:
    cudaEvent_t event_ = nullptr;
};

// ---- vector math on float3 (CUDA has the type, not the operators) ----------

__device__ inline float3 load3(const float* v) {
    return make_float3(v[0], v[1], v[2]);
}
__device__ inline float3 operator-(float3 a, float3 b) {
    return make_float3(a.x - b.x, a.y - b.y, a.z - b.z);
}
__device__ inline float3 operator*(float3 a, float s) {
    return make_float3(a.x * s, a.y * s, a.z * s);
}
__device__ inline float dot(float3 a, float3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
__device__ inline float3 cross(float3 a, float3 b) {
    return make_float3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
__device__ inline float3 normalize(float3 v) {
    return v * rsqrtf(dot(v, v));
}

// Everything a thread needs to build its primary ray, passed by value to the
// kernel (it lands in constant memory, shared by every thread).
struct RayCamera {
    float inv_proj_view[16]; // column-major, as glm stores it
    float3 origin;
    int width;
    int height;
};

// Same as task 13's primary_ray(): the pixel centre to NDC, (x, y, 1) on the
// far plane, back to the world with the inverse of projection * view.
__device__ float3 primary_direction(const RayCamera& camera, int x, int y) {
    const float sx = (x + 0.5f) / camera.width * 2.0f - 1.0f;
    const float sy = (y + 0.5f) / camera.height * 2.0f - 1.0f;
    const float* m = camera.inv_proj_view;
    float q[4];
    for (int row = 0; row < 4; ++row) {
        q[row] = m[row] * sx + m[4 + row] * sy + m[8 + row] + m[12 + row]; // M * (sx, sy, 1, 1)
    }
    const float3 p = make_float3(q[0] / q[3], q[1] / q[3], q[2] / q[3]);
    return normalize(p - camera.origin);
}

// Möller-Trumbore, as in task 13: the distance t to triangle (a, e1, e2), or a
// negative value if the ray misses it. Both faces count.
__device__ float hit_triangle(float3 origin, float3 direction, const GpuTriangle& triangle) {
    const float3 e1 = load3(triangle.e1);
    const float3 e2 = load3(triangle.e2);
    const float3 p = cross(direction, e2);
    const float det = dot(e1, p);
    if (fabsf(det) < 1e-12f) {
        return -1.0f; // the ray is parallel to the triangle's plane
    }
    const float inv_det = 1.0f / det;

    const float3 s = origin - load3(triangle.a);
    const float u = dot(s, p) * inv_det;
    if (u < 0.0f || u > 1.0f) {
        return -1.0f;
    }
    const float3 q = cross(s, e1);
    const float v = dot(direction, q) * inv_det;
    if (v < 0.0f || u + v > 1.0f) {
        return -1.0f;
    }
    return dot(e2, q) * inv_det;
}

// Slab test: whether the ray enters the object's box before t_max.
__device__ bool hit_box(float3 origin, float3 inv_direction, const GpuObject& object,
                        float t_max) {
    const float3 t0 = make_float3((object.box_min[0] - origin.x) * inv_direction.x,
                                  (object.box_min[1] - origin.y) * inv_direction.y,
                                  (object.box_min[2] - origin.z) * inv_direction.z);
    const float3 t1 = make_float3((object.box_max[0] - origin.x) * inv_direction.x,
                                  (object.box_max[1] - origin.y) * inv_direction.y,
                                  (object.box_max[2] - origin.z) * inv_direction.z);
    const float enter = fmaxf(fmaxf(fminf(t0.x, t1.x), fminf(t0.y, t1.y)),
                              fmaxf(fminf(t0.z, t1.z), 0.0f));
    const float exit = fminf(fminf(fmaxf(t0.x, t1.x), fmaxf(t0.y, t1.y)),
                             fminf(fmaxf(t0.z, t1.z), t_max));
    return enter <= exit;
}

// One thread per pixel. range[0] and range[1] hold the nearest and farthest
// hit of the whole image as the bits of a float: for floats >= 0 the order of
// the bits as unsigned integers is the order of the values, so atomicMin and
// atomicMax on unsigned work on them directly.
__global__ void cast_rays(RayCamera camera, const GpuTriangle* __restrict__ triangles,
                          const GpuObject* __restrict__ objects, int object_count,
                          bool use_boxes, float* __restrict__ distance,
                          unsigned* __restrict__ range) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;

    float closest = INFINITY;
    if (x < camera.width && y < camera.height) {
        const float3 direction = primary_direction(camera, x, y);
        const float3 inv_direction =
            make_float3(1.0f / direction.x, 1.0f / direction.y, 1.0f / direction.z);
        for (int i = 0; i < object_count; ++i) {
            const GpuObject object = objects[i];
            // A box farther than the closest hit so far cannot hold a closer one.
            if (use_boxes && !hit_box(camera.origin, inv_direction, object, closest)) {
                continue;
            }
            for (int k = object.first; k < object.first + object.count; ++k) {
                const float t = hit_triangle(camera.origin, direction, triangles[k]);
                if (t > 0.0f && t < closest) {
                    closest = t;
                }
            }
        }
        distance[y * camera.width + x] = closest;
    }

    // Nearest and farthest over the 32 threads of the warp, without shared
    // memory: each step folds the upper half of the values onto the lower
    // half. The threads outside the image take part with neutral values,
    // because __shfl_down_sync needs the whole warp.
    const bool hit = isfinite(closest);
    float nearest = hit ? closest : INFINITY;
    float farthest = hit ? closest : 0.0f;
    for (int offset = 16; offset > 0; offset /= 2) {
        nearest = fminf(nearest, __shfl_down_sync(0xffffffffu, nearest, offset));
        farthest = fmaxf(farthest, __shfl_down_sync(0xffffffffu, farthest, offset));
    }
    const int lane = (threadIdx.y * blockDim.x + threadIdx.x) % warpSize;
    if (lane == 0) {
        atomicMin(&range[0], __float_as_uint(nearest));
        atomicMax(&range[1], __float_as_uint(farthest));
    }
}

// Same mapping as task 13's to_grayscale(): near surfaces white, far ones dark,
// the background (infinitely far) black.
__global__ void to_grey(const float* __restrict__ distance, int count,
                        const unsigned* __restrict__ range, unsigned char* __restrict__ grey) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= count) {
        return;
    }
    const float t = distance[i];
    if (!isfinite(t)) {
        grey[i] = 0;
        return;
    }
    const float nearest = __uint_as_float(range[0]);
    const float farthest = __uint_as_float(range[1]);
    const float depth = (t - nearest) / fmaxf(farthest - nearest, 1e-6f); // [0, 1]
    grey[i] = static_cast<unsigned char>(lroundf(255.0f * (1.0f - depth)));
}

unsigned float_bits(float value) {
    unsigned bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

float bits_float(unsigned bits) {
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

} // namespace

std::string cuda_device_name() {
    int count = 0;
    CUDA_CHECK(cudaGetDeviceCount(&count));
    if (count == 0) {
        throw std::runtime_error("No CUDA device found");
    }
    int device = 0;
    CUDA_CHECK(cudaGetDevice(&device));
    cudaDeviceProp properties{};
    CUDA_CHECK(cudaGetDeviceProperties(&properties, device));
    CUDA_CHECK(cudaFree(nullptr)); // creates the context now, not inside the timed render
    return std::string(properties.name) + " (sm_" + std::to_string(properties.major) +
           std::to_string(properties.minor) + ")";
}

DepthImage render_depth_cuda(const GpuScene& scene, const float inv_proj_view[16],
                             const float origin[3], int width, int height, bool use_boxes) {
    const auto start = std::chrono::steady_clock::now();
    const size_t pixels = static_cast<size_t>(width) * height;

    DeviceBuffer<GpuTriangle> triangles(scene.triangles.size());
    DeviceBuffer<GpuObject> objects(scene.objects.size());
    DeviceBuffer<float> distance(pixels);
    DeviceBuffer<unsigned char> grey(pixels);
    DeviceBuffer<unsigned> range(2);
    triangles.upload(scene.triangles.data());
    objects.upload(scene.objects.data());
    const unsigned initial_range[2] = {float_bits(INFINITY), float_bits(0.0f)};
    range.upload(initial_range);

    RayCamera camera;
    std::memcpy(camera.inv_proj_view, inv_proj_view, sizeof(camera.inv_proj_view));
    camera.origin = make_float3(origin[0], origin[1], origin[2]);
    camera.width = width;
    camera.height = height;

    // 16 x 16 pixels per block: 256 threads, 8 full warps. Neighbouring pixels
    // see nearly the same triangles, so the threads of a warp mostly follow
    // the same branches.
    const dim3 block(16, 16);
    const dim3 grid((width + block.x - 1) / block.x, (height + block.y - 1) / block.y);
    const int grey_block = 256;
    const int grey_grid = static_cast<int>((pixels + grey_block - 1) / grey_block);

    Event begin, end;
    CUDA_CHECK(cudaEventRecord(begin.get()));
    cast_rays<<<grid, block>>>(camera, triangles.get(), objects.get(),
                               static_cast<int>(scene.objects.size()), use_boxes, distance.get(),
                               range.get());
    CUDA_CHECK(cudaGetLastError());
    to_grey<<<grey_grid, grey_block>>>(distance.get(), static_cast<int>(pixels), range.get(),
                                       grey.get());
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaEventRecord(end.get()));
    CUDA_CHECK(cudaEventSynchronize(end.get()));

    DepthImage image;
    image.width = width;
    image.height = height;
    image.grey.resize(pixels);
    grey.download(image.grey.data());
    unsigned final_range[2] = {0, 0};
    range.download(final_range);
    image.nearest = bits_float(final_range[0]);
    image.farthest = bits_float(final_range[1]);
    if (!(image.nearest < INFINITY)) {
        image.nearest = 0.0f; // nothing was hit
    }

    float kernel_ms = 0.0f;
    CUDA_CHECK(cudaEventElapsedTime(&kernel_ms, begin.get(), end.get()));
    image.kernel_ms = kernel_ms;
    image.total_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return image;
}
