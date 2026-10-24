# Experimental core build

`bitloop-core` is an experimental static-library target for finding the boundary between simulation/UI logic and the platform, renderer and capture implementations. Its CMake alias is `bitloop::core`.

It is excluded from the default build and is not installed or exported. Existing projects continue to use `bitloop::bitloop`. The vcpkg manifests and their merger are unchanged.

## Build

From a project using the checked-out framework, explicitly select the target:

```sh
cmake --build <configured-build-directory> --target bitloop-core --parallel 2
```

For example, from the CppTest project, use its configured Web preset:

```sh
cmake --build --preset wasm32-release --target bitloop-core --parallel 2
```

The output is `_bitloop/libbitloop-core.a` for Emscripten or `_bitloop/bitloop-core.lib` for single-configuration MSVC builds. Run native MSVC build commands from a Visual Studio developer terminal.

## Selecting sources and dependencies

`ALL_CORE_SRC_FILES` in the framework CMakeLists.txt is an explicit candidate list. It includes scene/project processing, camera/events/layout/viewport logic, development trace/checkpoint implementations, JSON/text helpers and the ImGui extensions.

The target does not compile the application entry point, main window, platform backend, NanoVG backend, image loader, capture implementations, settings UI, compression implementation or headless runner.

Its explicit library dependencies are ImGui, ImPlot, fltx, concurrentqueue and nlohmann-json. Thread-pool and ordered-map include paths are public because framework headers use those facilities.

This candidate shares the normal target's public compile definitions so the headers agree about configuration and class layout, including definitions added later by project setup. It does not inherit the full target's library links or SDL/WebGL application link options. Do not link both archives into the same executable: they currently contain overlapping implementations.

## What a successful build means

A static archive build checks compilation, not that every external reference can be resolved. Several selected files still call window, rendering and capture functionality. Further separation is required before this is a standalone simulation runtime or a complete script SDK.

The current vcpkg ImGui build enables SDL3 and FreeType, and its imported target propagates those dependencies. Bitloop's ImGui helpers and other public headers also still expose backend types. Configuration still discovers the full target's dependencies, and the existing installed include tree remains available during this experiment.

The initial Emscripten archive builds successfully. Symbol inspection still finds references such as `SDL_GetKeyName`, `SDL_SetWindowFullscreen`, `bl::MainWindow::singleton` and NanoVG drawing functions. These identify implementation boundaries that still need separating.

The next validation is a linked consumer that uses scene state and the needed UI functions. For a Wasm side module, its remaining imports must also be provided by the host and checked by loading and invoking it. A later reduced SDK installation will verify header/package independence.
