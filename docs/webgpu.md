# Optional WebGPU compute

`bitloop::webgpu` adds compute using Dawn on desktop and the matching Emdawnwebgpu release in a browser. It does not change Bitloop's OpenGL drawing, NanoVG, ImGui, or existing GLSL projects. Include `<bitloop/gpu/webgpu.h>` explicitly; `<bitloop.h>` remains independent of WebGPU.

## Build

Request the non-default `webgpu` feature in your project's `vcpkg.json`:

```json
"dependencies": [
  { "name": "bitloop", "features": ["webgpu"] }
]
```

Then link the GPU consumer and deploy its executable's runtime:

```cmake
target_link_libraries(MyProject PRIVATE bitloop::webgpu)
bitloop_deploy_webgpu_runtime(MyProject)
```

The managed project tooling uses the same manifest request in source/gallery mode: it selects `BITLOOP_WITH_WEBGPU` and retains the feature's Dawn dependencies when replacing the Bitloop port with the source checkout. Do not duplicate Dawn or its platform backend selection in the project manifest. Refresh existing tooling with `bitloop tooling sync --project <project>`; parents that compose projects need the updated tooling too.

For a direct framework build, set `BITLOOP_WITH_WEBGPU=ON`; this also selects the root manifest's `webgpu` dependency feature. The registry port maps `bitloop[webgpu]` to this option automatically. The component requires `find_package(Dawn CONFIG REQUIRED)` and `dawn::webgpu_dawn`; both platforms use that target.

Plain `"bitloop"` neither requests Dawn nor links WebGPU. The base `bitloop::bitloop` target never links `bitloop::webgpu`. A combined Gallery containing a GPU project installs Dawn for that build, while a standalone non-GPU project does not. Merely carrying the managed Dawn overlay in tooling does not install it. With shared Dawn builds, distribution size primarily grows through Dawn/DXC DLLs; static builds can add linked code to the executable. No fixed size saving is promised.

The tested baseline supplies Dawn **20250922.223923** with Bitloop’s managed overlay, including the same release's Emdawn bindings. Keep native and wasm releases matched: Dawn's C++ wrapper is version-sensitive. Windows needs `dxc` (which includes `d3d12`), Apple platforms `metal`, and Linux/Android `vulkan` (with `default-features: false` when choosing only these). The wasm package automatically supplies the Emdawn port. Existing registry baselines have not changed; installation as a released Bitloop package awaits the matching framework/registry release.

`BITLOOP_WEBGPU_ASYNC_WAIT=ON` additionally enables Emscripten Asyncify for cooperative `wait()` and `waitReady()`. This is optional and OFF by default. It preserves a synchronous C++ call surface by suspending to the browser's event loop; it does not spin or block that loop. Asyncify adds code size and instrumentation cost, so batch commands and wait only at necessary readback boundaries. Native waits need no special option.

The wasm component follows Bitloop's pthread build. Serve it over HTTPS or localhost with `Cross-Origin-Opener-Policy: same-origin` and `Cross-Origin-Embedder-Policy: require-corp`.

## Windows compiler and runtime deployment

Windows WebGPU builds use DXC through the `dawn[dxc]` dependency selected by `bitloop[webgpu]`. This uses Microsoft's versioned `directx-dxc` binary package from the unchanged vcpkg baseline; it does not build LLVM. The managed Dawn overlay is required at this baseline, including for package builds; project tooling selects it before installation. Custom vcpkg integrations must supply the same overlay explicitly. The framework root and standalone WebGPU fixture also select the overlay directly.

Call `bitloop_deploy_webgpu_runtime(MyExecutable)` for each executable using WebGPU, after its link wiring. It copies the normal transitive DLLs plus `dxcompiler.dll` and `dxil.dll` beside the executable; the compiler DLLs require explicit handling because Dawn loads them dynamically. For a nested project library, deploy on the parent executable instead. The helper is a no-op outside Windows. The helper also installs these files to `bin`; install your executable there with `install(TARGETS MyExecutable RUNTIME DESTINATION bin)` for a relocatable distribution. Normal dependency license notices still accompany redistribution. Generic `bitloop_finalize()` does not deploy GPU runtime files.

Dawn searches beside the executable, so end users need no developer SDK or vcpkg PATH. Building with DXC does not require optional subgroups on every device: request `wgpu::FeatureName::Subgroups` only when `gpu.adapter().HasFeature(...)` reports support, with an algorithm fallback for unsupported adapters. Older shader-model hardware retains Dawn's FXC path. Browser/Metal/Vulkan compiler selection is unchanged. A compiler change can invalidate cached binaries and alter rounding; it does not itself establish a simulation speedup.

## Ownership and progress

Create a `bl::WebGpuDevice` on the thread that will own its WebGPU calls. `initialize()` starts asynchronous adapter/device requests; repeatedly call `poll()` from that thread while allowing browser events to run. Inspect `status()` and `error()`. Alternatively, with cooperative waits enabled:

```cpp
bl::WebGpuDevice gpu;
gpu.initialize();
if (!gpu.waitReady()) throw std::runtime_error(gpu.error());

auto values = gpu.buffer(bytes, wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopySrc);
auto module = gpu.shader(wgsl_source);
auto encoder = gpu.device().CreateCommandEncoder();
// Build pipelines/bind groups once, then record many compute dispatches here.
bl::WebGpuReadback readback;
readback.copy(gpu.device(), encoder, values, 0, bytes);
auto commands = encoder.Finish();
gpu.queue().Submit(1, &commands);
auto future = readback.map();
gpu.wait(future);
if (!readback.ready()) throw std::runtime_error(readback.error());
auto result = readback.data(); // mapped bytes, no intermediate CPU copy
// Consume result before unmapping.
readback.unmap();
```

For a fully asynchronous caller, omit `wait`: keep the readback alive, return to the event loop, call `gpu.poll()` on later ticks and consume it once `ready()`. Its staging buffer grows only when needed and is reused after `unmap()`. `reset()` or destruction cancels outstanding readback. Offsets/sizes follow normal WebGPU copy/map alignment rules; copying a scalar at a four-byte-aligned source offset works because staging maps start at zero.

Use raw `wgpu` buffers, pipelines, bind groups and encoders for the actual algorithm. Query device limits before assigning dynamic uniform offsets. Different dispatch parameters in one submission need distinct buffer ranges/dynamic offsets: repeated queue writes to one uniform location before submission do not produce per-dispatch values.

Keep device creation, resource calls, polling/waits and destruction on one owning thread. Browser GPU objects cannot be passed to another JavaScript worker. Bitloop's existing render-thread queue remains appropriate for its OpenGL presentation; dispatch WebGPU from its chosen owner rather than moving its handles through that queue. Destroy project GPU resources before resetting/destroying their device. Pending initialization and readback callbacks do not retain their owners.

## Optional native shader cache

Set `WebGpuDevice::Options::cache_directory` to an application-owned, writable directory before `initialize(options)` to retain Dawn's compiled binaries across devices and launches. Empty is the default and disables persistent caching. The application chooses the location, for example below its platform user-data directory. Browsers ignore this option and keep their browser-managed shader cache.

The first use of a new shader/layout/compiler key still compiles normally. Dawn provides the compatibility key; Bitloop stores opaque key/value bytes with full-key and checksum validation. Missing, truncated, damaged or unavailable files fall back to compilation. A load retains its bytes between Dawn's size query and copy; completed entries are published through temporary files. Clearing the cache directory is safe; no simulation state is stored. Old entries are not automatically pruned.

The managed Dawn overlay sorts `bindings.ignored_by_robustness_transform` before assigning it to the compilation request. Dawn 20250922 otherwise serializes an unordered binding vector, causing equivalent pipelines to miss across launches. The same reusable overlay enables Windows DXC; the framework does not parse or rewrite binary cache keys.

## Focused validation

`tests/webgpu` is a standalone CMake project that builds only this component and Dawn. It tests 4,099 values, three compute dispatches in one pass with dynamic uniforms, full/scalar readback reuse, and pending-map cancellation. `BITLOOP_WEBGPU_ASYNC_WAIT=ON` runs the calculation on a worker using cooperative waits; OFF uses asynchronous polling. Native builds register it with CTest. A separate native subgroup fixture requests the feature when available and executes `subgroupAdd`; unsupported adapters return an explicit skip. Installation of the fixture exercises the same DLL deployment helper as applications.

The native cache unit test covers persistence, corruption/truncation, replacing a damaged entry, a file disappearing between load query/copy and unavailable storage. Actual cross-process cache reuse is validated by consuming applications, since it also depends on the selected Dawn backend/version.

The browser executable is `bitloop_webgpu_test.html`. `tests/webgpu/run_browser.py --browser <chrome-or-edge> --build <build-folder> --output <result.json>` serves the required headers and waits for the actual browser test result. It needs no Python packages. A successful wasm compilation alone is not a browser pass.
