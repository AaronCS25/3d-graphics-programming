// The GPU version: the scene is copied to device memory once, then each sample
// is one kernel launch with one thread per pixel, adding its path to a
// per-pixel sum. Many short launches instead of one long one keep every kernel
// well under the watchdog that stops long kernels on a GPU that also drives a
// display, and the sum is built in the same order as on the CPU.

#include "path_tracer.hpp"
#include "render.hpp"

#include <cuda_runtime.h>

#include <chrono>
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

// Device memory released when it goes out of scope (task 14).
template <typename T>
class DeviceBuffer {
public:
    explicit DeviceBuffer(const std::vector<T>& host) : DeviceBuffer(host.size()) {
        if (!host.empty()) {
            CUDA_CHECK(cudaMemcpy(data_, host.data(), host.size() * sizeof(T),
                                  cudaMemcpyHostToDevice));
        }
    }
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
    void zero() {
        if (count_ > 0) {
            CUDA_CHECK(cudaMemset(data_, 0, count_ * sizeof(T)));
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

// One thread per pixel adds one path to the pixel's sum. The scene and the
// settings travel by value with the launch: only pointers and numbers.
__global__ void trace_kernel(SceneView scene, RenderSettings settings, int sample, Vec3* sum) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= settings.width || y >= settings.height) {
        return;
    }
    sum[y * settings.width + x] += trace_sample(scene, settings, x, y, sample);
}

} // namespace

std::string gpu_name() {
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

Image render_gpu(const HostScene& host, const RenderSettings& settings, double& kernel_seconds) {
    const auto start = std::chrono::steady_clock::now();
    const size_t pixels = static_cast<size_t>(settings.width) * settings.height;

    const DeviceBuffer<Triangle> triangles(host.triangles);
    const DeviceBuffer<Shading> shading(host.shading);
    const DeviceBuffer<Object> objects(host.objects);
    const DeviceBuffer<Diffuse> materials(host.materials);
    const DeviceBuffer<unsigned char> texels(host.texels);
    const DeviceBuffer<TextureInfo> textures(host.textures);
    DeviceBuffer<Vec3> sum(pixels);
    sum.zero();

    const SceneView scene{triangles.get(), shading.get(),
                          objects.get(),   static_cast<int>(host.objects.size()),
                          materials.get(), texels.get(),
                          textures.get()};

    // 8 x 8 pixels per block: neighbouring pixels see nearly the same
    // surfaces, so the threads of a warp mostly follow the same branches.
    const dim3 block(8, 8);
    const dim3 grid((settings.width + block.x - 1) / block.x,
                    (settings.height + block.y - 1) / block.y);

    Event begin, end;
    CUDA_CHECK(cudaEventRecord(begin.get()));
    for (int s = 0; s < settings.samples; ++s) {
        trace_kernel<<<grid, block>>>(scene, settings, s, sum.get());
        CUDA_CHECK(cudaGetLastError());
    }
    CUDA_CHECK(cudaEventRecord(end.get()));
    CUDA_CHECK(cudaEventSynchronize(end.get()));
    float kernel_ms = 0.0f;
    CUDA_CHECK(cudaEventElapsedTime(&kernel_ms, begin.get(), end.get()));
    kernel_seconds = kernel_ms / 1000.0;

    Image image;
    image.width = settings.width;
    image.height = settings.height;
    image.pixels.resize(pixels);
    sum.download(image.pixels.data());
    for (Vec3& p : image.pixels) {
        p = p * (1.0f / settings.samples); // the same average as trace_pixel()
    }
    image.seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return image;
}
