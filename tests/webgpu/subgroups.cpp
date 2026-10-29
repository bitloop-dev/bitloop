#include <bitloop/gpu/webgpu.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>

int main()
{
    try
    {
        bl::WebGpuDevice gpu;
        gpu.initialize();
        if (!gpu.waitReady()) throw std::runtime_error(gpu.error());
        if (!gpu.adapter().HasFeature(wgpu::FeatureName::Subgroups))
        {
            std::puts("SKIP: adapter does not expose subgroups");
            return 77;
        }
        const std::array features = {wgpu::FeatureName::Subgroups};
        bl::WebGpuDevice::Options options; options.features = features;
        gpu.initialize(options);
        if (!gpu.waitReady()) throw std::runtime_error(gpu.error());
        auto output = gpu.buffer(128 * sizeof(float), wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopySrc);
        auto module = gpu.shader(R"(
enable subgroups;
@group(0) @binding(0) var<storage, read_write> output: array<vec2f>;
@compute @workgroup_size(64)
fn main(@builtin(local_invocation_index) i: u32, @builtin(subgroup_size) size: u32) {
    output[i] = vec2f(subgroupAdd(1.0), f32(size));
}
)");
        wgpu::ComputePipelineDescriptor descriptor;
        descriptor.compute.module = module; descriptor.compute.entryPoint = "main";
        auto pipeline = gpu.device().CreateComputePipeline(&descriptor);
        wgpu::BindGroupEntry entry; entry.binding = 0; entry.buffer = output; entry.size = 128 * sizeof(float);
        wgpu::BindGroupDescriptor bindings;
        bindings.layout = pipeline.GetBindGroupLayout(0); bindings.entryCount = 1; bindings.entries = &entry;
        auto group = gpu.device().CreateBindGroup(&bindings);
        auto encoder = gpu.device().CreateCommandEncoder();
        auto pass = encoder.BeginComputePass();
        pass.SetPipeline(pipeline); pass.SetBindGroup(0, group); pass.DispatchWorkgroups(1); pass.End();
        bl::WebGpuReadback readback;
        readback.copy(gpu.device(), encoder, output, 0, 128 * sizeof(float));
        auto commands = encoder.Finish(); gpu.queue().Submit(1, &commands);
        gpu.wait(readback.map());
        if (!readback.ready()) throw std::runtime_error(readback.error());
        std::array<float, 128> values;
        std::memcpy(values.data(), readback.data().data(), sizeof(values));
        readback.unmap();
        for (int i = 0; i < 64; ++i)
            if (values[2*i+1] <= 0 || values[2*i] != std::min(values[2*i+1], 64.f))
                throw std::runtime_error("Incorrect hardware subgroup reduction");
        std::printf("PASS: subgroupAdd executed, subgroup size %.0f\n", values[1]);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
