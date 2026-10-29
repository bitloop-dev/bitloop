#include <bitloop/gpu/webgpu.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

static constexpr uint32_t count = 4099;
static constexpr const char* code = R"(
struct Params { scale: f32, add: f32, count: u32, pad: u32 }
@group(0) @binding(0) var<storage, read_write> values: array<f32>;
@group(0) @binding(1) var<uniform> p: Params;
@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) id: vec3u) {
    if (id.x < p.count) { values[id.x] = values[id.x] * p.scale + p.add; }
}
)";

struct ComputeTest
{
    bl::WebGpuDevice gpu;
    bl::WebGpuReadback readback;
    wgpu::Buffer values, parameters;
    wgpu::ComputePipeline pipeline;
    wgpu::BindGroup bindings;
    wgpu::Future future;
    uint32_t parameter_stride = 0;
    int stage = 0, result = -1;
    std::vector<float> expected;

    ComputeTest() { gpu.initialize(); }

    void fail(const std::string& message)
    {
        std::fprintf(stderr, "WEBGPU_TEST_FAIL: %s\n", message.c_str());
        result = 1;
    }

    void setup()
    {
        wgpu::AdapterInfo info;
        gpu.adapter().GetInfo(&info);
        std::printf("WebGPU backend=%u adapter=%.*s\n", unsigned(info.backendType), int(info.description.length), info.description.data);
        wgpu::Limits limits;
        gpu.device().GetLimits(&limits);
        parameter_stride = limits.minUniformBufferOffsetAlignment;

        expected.resize(count);
        for (uint32_t i = 0; i < count; ++i) expected[i] = float(i) * 0.125f - 32.0f;
        values = gpu.buffer(count * sizeof(float), wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::CopySrc, "values");
        parameters = gpu.buffer(3 * parameter_stride, wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst, "batched parameters");
        gpu.queue().WriteBuffer(values, 0, expected.data(), count * sizeof(float));
        struct Params { float scale, add; uint32_t count, pad; };
        const std::array<Params, 3> params = { Params{2, 1, count, 0}, Params{0.5f, -3, count, 0}, Params{4, 0.25f, count, 0} };
        std::vector<std::byte> packed(3 * parameter_stride);
        for (int i = 0; i < 3; ++i) std::memcpy(packed.data() + i * parameter_stride, &params[i], sizeof(Params));
        gpu.queue().WriteBuffer(parameters, 0, packed.data(), packed.size());

        wgpu::BindGroupLayoutEntry entries[2];
        entries[0].binding = 0;
        entries[0].visibility = wgpu::ShaderStage::Compute;
        entries[0].buffer.type = wgpu::BufferBindingType::Storage;
        entries[1].binding = 1;
        entries[1].visibility = wgpu::ShaderStage::Compute;
        entries[1].buffer.type = wgpu::BufferBindingType::Uniform;
        entries[1].buffer.hasDynamicOffset = true;
        entries[1].buffer.minBindingSize = sizeof(Params);
        wgpu::BindGroupLayoutDescriptor layout_descriptor;
        layout_descriptor.entryCount = 2;
        layout_descriptor.entries = entries;
        auto layout = gpu.device().CreateBindGroupLayout(&layout_descriptor);
        wgpu::PipelineLayoutDescriptor pipeline_layout;
        pipeline_layout.bindGroupLayoutCount = 1;
        pipeline_layout.bindGroupLayouts = &layout;
        wgpu::ComputePipelineDescriptor descriptor;
        descriptor.layout = gpu.device().CreatePipelineLayout(&pipeline_layout);
        descriptor.compute.module = gpu.shader(code, "compute regression");
        descriptor.compute.entryPoint = "main";
        pipeline = gpu.device().CreateComputePipeline(&descriptor);
        wgpu::BindGroupEntry resources[2];
        resources[0].binding = 0;
        resources[0].buffer = values;
        resources[0].size = count * sizeof(float);
        resources[1].binding = 1;
        resources[1].buffer = parameters;
        resources[1].size = sizeof(Params);
        wgpu::BindGroupDescriptor bind_descriptor;
        bind_descriptor.layout = layout;
        bind_descriptor.entryCount = 2;
        bind_descriptor.entries = resources;
        bindings = gpu.device().CreateBindGroup(&bind_descriptor);

        auto encoder = gpu.device().CreateCommandEncoder();
        auto pass = encoder.BeginComputePass();
        pass.SetPipeline(pipeline);
        for (uint32_t i = 0; i < 3; ++i)
        {
            uint32_t offset = i * parameter_stride;
            pass.SetBindGroup(0, bindings, 1, &offset);
            pass.DispatchWorkgroups((count + 63) / 64);
            for (float& value : expected) value = value * params[i].scale + params[i].add;
        }
        pass.End();
        readback.copy(gpu.device(), encoder, values, 0, count * sizeof(float));
        auto commands = encoder.Finish();
        gpu.queue().Submit(1, &commands);
        future = readback.map();
        stage = 1;
    }

    void tick()
    {
        gpu.poll();
        if (gpu.status() == bl::WebGpuDevice::Status::Failed) { fail(gpu.error()); return; }
        if (stage == 0)
        {
            if (gpu.status() == bl::WebGpuDevice::Status::Ready) setup();
            return;
        }
        if (readback.pending()) return;
        if (!readback.ready()) { fail(readback.error()); return; }
        auto data = readback.data();
        if (stage == 1)
        {
            for (uint32_t i = 0; i < count; ++i)
            {
                float value;
                std::memcpy(&value, data.data() + i * sizeof(float), sizeof(float));
                if (value != expected[i]) { fail("batched dynamic uniforms produced wrong output"); return; }
            }
            readback.unmap();
            auto encoder = gpu.device().CreateCommandEncoder();
            readback.copy(gpu.device(), encoder, values, 5 * sizeof(float), sizeof(float));
            auto commands = encoder.Finish();
            gpu.queue().Submit(1, &commands);
            future = readback.map();
            stage = 2;
        }
        else
        {
            float value;
            std::memcpy(&value, data.data(), sizeof(float));
            if (value != expected[5]) { fail("reused scalar readback produced wrong output"); return; }
            readback.unmap();
            // Cancel an outstanding map to check cleanup without leaving a dangling callback.
            auto encoder = gpu.device().CreateCommandEncoder();
            readback.copy(gpu.device(), encoder, values, 0, sizeof(float));
            auto commands = encoder.Finish();
            gpu.queue().Submit(1, &commands);
            readback.map();
            readback.reset();
            gpu.poll();
            result = 0;
            std::puts("WEBGPU_TEST_PASS: 4099 values, 3 batched dispatches, dynamic uniforms, full/scalar readback reuse, cancellation");
        }
    }
};

static void report(int result)
{
#ifdef __EMSCRIPTEN__
    EM_ASM({ fetch('/result?code=' + $0, { method: 'POST' }); }, result);
#else
    (void)result;
#endif
}

#ifdef BITLOOP_TEST_COOPERATIVE_WAIT
static int runCooperative()
{
    ComputeTest test;
    if (!test.gpu.waitReady()) { test.fail(test.gpu.error()); report(1); return 1; }
    test.tick();
    while (test.result < 0)
    {
        if (test.gpu.wait(test.future) != wgpu::WaitStatus::Success) { test.fail("wait failed"); break; }
        test.tick();
    }
    report(test.result);
    return test.result;
}
#endif

int main()
{
#if defined(__EMSCRIPTEN__) && defined(BITLOOP_TEST_COOPERATIVE_WAIT)
    std::thread([] { runCooperative(); }).detach();
    emscripten_exit_with_live_runtime();
#elif defined(BITLOOP_TEST_COOPERATIVE_WAIT)
    int result = 1;
    std::thread worker([&] { result = runCooperative(); });
    worker.join();
    return result;
#elif defined(__EMSCRIPTEN__)
    auto* test = new ComputeTest;
    emscripten_set_main_loop_arg([](void* data)
    {
        auto* test = static_cast<ComputeTest*>(data);
        test->tick();
        if (test->result >= 0)
        {
            report(test->result);
            emscripten_cancel_main_loop();
            delete test;
        }
    }, test, 0, true);
#else
    ComputeTest test;
    while (test.result < 0) { test.tick(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    return test.result;
#endif
}
