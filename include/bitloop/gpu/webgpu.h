#pragma once

#include <bitloop/platform/platform_macros.h>
#include <webgpu/webgpu_cpp.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

BL_BEGIN_NS

// Keep device creation, commands, event processing and destruction on one owning thread.
class WebGpuDevice
{
public:
    enum class Status { Empty, Initializing, Ready, Failed };

    struct Options
    {
        wgpu::PowerPreference power = wgpu::PowerPreference::HighPerformance;
        wgpu::BackendType backend = wgpu::BackendType::Undefined;
        wgpu::Limits limits;
        std::span<const wgpu::FeatureName> features;
        // Optional native Dawn binary cache; browsers manage their own shader cache.
        std::string cache_directory;
    };

    WebGpuDevice();
    ~WebGpuDevice();
    WebGpuDevice(const WebGpuDevice&) = delete;
    WebGpuDevice& operator=(const WebGpuDevice&) = delete;

    void initialize(const Options& options);
    void initialize() { initialize(Options{}); }
    void reset();
    void poll() const;
    Status status() const;
    std::string error() const;

    // Browser waits suspend to its event loop and require BITLOOP_WEBGPU_ASYNC_WAIT.
    bool waitReady() const;
    wgpu::WaitStatus wait(wgpu::Future future, uint64_t timeout_ns = UINT64_MAX) const;

    const wgpu::Instance& instance() const;
    const wgpu::Adapter& adapter() const;
    const wgpu::Device& device() const;
    const wgpu::Queue& queue() const;
    wgpu::Buffer buffer(uint64_t size, wgpu::BufferUsage usage, std::string_view label = {}) const;
    wgpu::ShaderModule shader(std::string_view wgsl, std::string_view label = {}) const;

private:
    struct State;
    std::shared_ptr<State> state;
};

// Reuse staging storage; data() is valid until unmap/reset/destruction, with no CPU-side copy.
class WebGpuReadback
{
public:
    WebGpuReadback();
    ~WebGpuReadback();
    WebGpuReadback(const WebGpuReadback&) = delete;
    WebGpuReadback& operator=(const WebGpuReadback&) = delete;

    void copy(const wgpu::Device& device, const wgpu::CommandEncoder& encoder, const wgpu::Buffer& source, uint64_t offset, size_t size);
    wgpu::Future map(); // Submit the encoder before requesting its map.
    bool ready() const;
    bool pending() const;
    std::string error() const;
    std::span<const std::byte> data() const;
    void unmap();
    void reset();

private:
    struct State;
    std::shared_ptr<State> state;
};

BL_END_NS
