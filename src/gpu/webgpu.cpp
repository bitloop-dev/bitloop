#include <bitloop/gpu/webgpu.h>
#include <atomic>
#include <mutex>
#include <stdexcept>
#include <vector>
#ifndef __EMSCRIPTEN__
#include "webgpu_cache.h"
#endif
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dawn/native/DawnNative.h>
#endif

BL_BEGIN_NS

static std::string messageText(wgpu::StringView text)
{
    if (!text.data) return {};
    return text.length == WGPU_STRLEN ? std::string(text.data) : std::string(text.data, text.length);
}

static wgpu::StringView stringView(std::string_view text)
{
    return { text.data(), text.size() };
}

struct WebGpuDevice::State
{
    struct Result
    {
        std::atomic<Status> status = Status::Empty;
        std::mutex mutex;
        std::string error;
#ifndef __EMSCRIPTEN__
        std::shared_ptr<detail::WebGpuCache> cache;
#endif
        void fail(std::string message)
        {
            std::lock_guard lock(mutex);
            if (error.empty()) error = std::move(message);
            status = Status::Failed;
        }
    };

    std::shared_ptr<Result> result = std::make_shared<Result>();
    wgpu::Instance instance;
    wgpu::Adapter adapter;
    wgpu::Device device;
    wgpu::Queue queue;
    wgpu::Future request;
    wgpu::Limits limits;
    std::vector<wgpu::FeatureName> features;

    ~State() { if (device) device.Destroy(); }

    void fail(std::string message) { result->fail(std::move(message)); }
};

WebGpuDevice::WebGpuDevice() : state(std::make_shared<State>()) {}
WebGpuDevice::~WebGpuDevice() = default;
void WebGpuDevice::reset() { state = std::make_shared<State>(); }

void WebGpuDevice::initialize(const Options& options)
{
    reset();
    state->result->status = Status::Initializing;
    state->limits = options.limits;
    state->features.assign(options.features.begin(), options.features.end());
#ifndef __EMSCRIPTEN__
    if (!options.cache_directory.empty()) state->result->cache = std::make_shared<detail::WebGpuCache>(options.cache_directory);
#endif
    wgpu::InstanceDescriptor descriptor;
#ifdef _WIN32
    // Dawn's secure DLL loader needs an absolute search directory for the OS shader compiler.
    char system_directory[MAX_PATH + 2];
    const UINT length = GetSystemDirectoryA(system_directory, MAX_PATH);
    dawn::native::DawnInstanceDescriptor native;
    const char* paths[] = { system_directory };
    if (length && length < MAX_PATH)
    {
        system_directory[length] = '\\';
        system_directory[length + 1] = 0;
        native.additionalRuntimeSearchPathsCount = 1;
        native.additionalRuntimeSearchPaths = paths;
        descriptor.nextInChain = &native;
    }
#endif
#if !defined(__EMSCRIPTEN__) || defined(BITLOOP_WEBGPU_ASYNC_WAIT)
    constexpr wgpu::InstanceFeatureName timed_wait = wgpu::InstanceFeatureName::TimedWaitAny;
    descriptor.requiredFeatureCount = 1;
    descriptor.requiredFeatures = &timed_wait;
#endif
    state->instance = wgpu::CreateInstance(&descriptor);
    if (!state->instance)
    {
        state->fail("Could not create WebGPU instance");
        return;
    }

    wgpu::RequestAdapterOptions request;
    request.powerPreference = options.power;
    request.backendType = options.backend;
    std::weak_ptr<State> weak = state;
    state->request = state->instance.RequestAdapter(&request, wgpu::CallbackMode::AllowProcessEvents,
        [weak](wgpu::RequestAdapterStatus status, wgpu::Adapter adapter, wgpu::StringView message)
        {
            auto state = weak.lock();
            if (!state) return;
            if (status != wgpu::RequestAdapterStatus::Success)
            {
                state->fail("WebGPU adapter: " + messageText(message));
                return;
            }
            state->adapter = std::move(adapter);
            wgpu::DeviceDescriptor device;
            device.label = "Bitloop compute";
            device.requiredLimits = &state->limits;
            device.requiredFeatureCount = state->features.size();
            device.requiredFeatures = state->features.data();
            auto result = state->result;
#ifndef __EMSCRIPTEN__
            wgpu::DawnCacheDeviceDescriptor cache;
            if (result->cache)
            {
                cache.isolationKey = "bitloop-webgpu-v1";
                cache.functionUserdata = result->cache.get();
                cache.loadDataFunction = [](const void* key, size_t key_size, void* value, size_t value_size, void* userdata)
                { return static_cast<detail::WebGpuCache*>(userdata)->load(key, key_size, value, value_size); };
                cache.storeDataFunction = [](const void* key, size_t key_size, const void* value, size_t value_size, void* userdata)
                { static_cast<detail::WebGpuCache*>(userdata)->store(key, key_size, value, value_size); };
                device.nextInChain = &cache;
            }
#endif
            device.SetUncapturedErrorCallback([](const wgpu::Device&, wgpu::ErrorType, wgpu::StringView message, State::Result* result)
            {
                result->fail("WebGPU validation: " + messageText(message));
            }, result.get());
            // The one-shot callbacks retain error userdata through device loss, without retaining the device itself.
            device.SetDeviceLostCallback(wgpu::CallbackMode::AllowProcessEvents,
                [result](const wgpu::Device&, wgpu::DeviceLostReason reason, wgpu::StringView message)
                {
                    if (reason == wgpu::DeviceLostReason::Destroyed || reason == wgpu::DeviceLostReason::CallbackCancelled) return;
                    result->fail("WebGPU device lost: " + messageText(message));
                });
            state->request = state->adapter.RequestDevice(&device, wgpu::CallbackMode::AllowProcessEvents,
                [weak, result](wgpu::RequestDeviceStatus status, wgpu::Device device, wgpu::StringView message)
                {
                    auto state = weak.lock();
                    if (!state) return;
                    if (status != wgpu::RequestDeviceStatus::Success)
                    {
                        result->fail("WebGPU device: " + messageText(message));
                        return;
                    }
                    state->device = std::move(device);
                    state->queue = state->device.GetQueue();
                    result->status = Status::Ready;
                });
        });
}

void WebGpuDevice::poll() const { if (state->instance) state->instance.ProcessEvents(); }
WebGpuDevice::Status WebGpuDevice::status() const { return state->result->status; }
std::string WebGpuDevice::error() const { std::lock_guard lock(state->result->mutex); return state->result->error; }
const wgpu::Instance& WebGpuDevice::instance() const { return state->instance; }
const wgpu::Adapter& WebGpuDevice::adapter() const { return state->adapter; }
const wgpu::Device& WebGpuDevice::device() const { return state->device; }
const wgpu::Queue& WebGpuDevice::queue() const { return state->queue; }

wgpu::WaitStatus WebGpuDevice::wait(wgpu::Future future, uint64_t timeout_ns) const
{
#if defined(__EMSCRIPTEN__) && !defined(BITLOOP_WEBGPU_ASYNC_WAIT)
    if (timeout_ns) return wgpu::WaitStatus::Error;
#endif
    return state->instance.WaitAny(future, timeout_ns);
}

bool WebGpuDevice::waitReady() const
{
    while (status() == Status::Initializing)
        if (wait(state->request) != wgpu::WaitStatus::Success) return false;
    return status() == Status::Ready;
}

wgpu::Buffer WebGpuDevice::buffer(uint64_t size, wgpu::BufferUsage usage, std::string_view label) const
{
    wgpu::BufferDescriptor descriptor;
    descriptor.label = stringView(label);
    descriptor.size = size;
    descriptor.usage = usage;
    return state->device.CreateBuffer(&descriptor);
}

wgpu::ShaderModule WebGpuDevice::shader(std::string_view wgsl, std::string_view label) const
{
    wgpu::ShaderSourceWGSL source;
    source.code = stringView(wgsl);
    wgpu::ShaderModuleDescriptor descriptor;
    descriptor.label = stringView(label);
    descriptor.nextInChain = &source;
    return state->device.CreateShaderModule(&descriptor);
}

struct WebGpuReadback::State
{
    wgpu::Buffer buffer;
    size_t capacity = 0, size = 0;
    uint64_t generation = 0;
    bool pending = false, ready = false;
    std::string error;
    ~State() { if (buffer) buffer.Destroy(); }
};

WebGpuReadback::WebGpuReadback() : state(std::make_shared<State>()) {}
WebGpuReadback::~WebGpuReadback() = default;
void WebGpuReadback::reset() { state = std::make_shared<State>(); }
bool WebGpuReadback::ready() const { return state->ready; }
bool WebGpuReadback::pending() const { return state->pending; }
std::string WebGpuReadback::error() const { return state->error; }

void WebGpuReadback::copy(const wgpu::Device& device, const wgpu::CommandEncoder& encoder, const wgpu::Buffer& source, uint64_t offset, size_t size)
{
    if (state->pending || state->ready) throw std::logic_error("Unmap WebGPU readback before reusing it");
    if (!size) throw std::logic_error("WebGPU readback needs a nonempty range");
    if (size > state->capacity)
    {
        wgpu::BufferDescriptor descriptor;
        descriptor.label = "Bitloop readback";
        descriptor.size = size;
        descriptor.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
        state->buffer = device.CreateBuffer(&descriptor);
        state->capacity = size;
    }
    state->size = size;
    state->error.clear();
    encoder.CopyBufferToBuffer(source, offset, state->buffer, 0, size);
}

wgpu::Future WebGpuReadback::map()
{
    if (state->pending || state->ready || !state->size) throw std::logic_error("WebGPU readback is not ready to map");
    state->pending = true;
    const uint64_t generation = ++state->generation;
    std::weak_ptr<State> weak = state;
    return state->buffer.MapAsync(wgpu::MapMode::Read, 0, state->size, wgpu::CallbackMode::AllowProcessEvents,
        [weak, generation](wgpu::MapAsyncStatus status, wgpu::StringView message)
        {
            auto state = weak.lock();
            if (!state || generation != state->generation) return;
            state->pending = false;
            state->ready = status == wgpu::MapAsyncStatus::Success;
            if (!state->ready) state->error = messageText(message);
        });
}

std::span<const std::byte> WebGpuReadback::data() const
{
    if (!state->ready) return {};
    return { static_cast<const std::byte*>(state->buffer.GetConstMappedRange(0, state->size)), state->size };
}

void WebGpuReadback::unmap()
{
    ++state->generation;
    if (state->buffer) state->buffer.Unmap();
    state->pending = state->ready = false;
    state->size = 0;
}

BL_END_NS
