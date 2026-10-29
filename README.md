[![CMake on multiple platforms](https://github.com/willmh93/bitloop/actions/workflows/cmake-multi-platform.yml/badge.svg)](https://github.com/willmh93/bitloop/actions/workflows/cmake-multi-platform.yml)

# Overview

Bitloop is a modular cross-platform CMake project manager tailored toward scientific simulations/visualizations and rapid prototyping.

[Web Demo](https://bitloop.dev)  -  [Example Gallery](https://github.com/willmh93/bitloop/tree/main?tab=readme-ov-file#example-snapshots--webp-animations)

For help getting started, view the Wiki:

- Building projects
  - [Building from source on Windows / Linux](https://github.com/willmh93/bitloop/wiki/Build-library-from-source) (todo: update)
  - [Building with Emscripten](https://github.com/willmh93/bitloop/wiki/Build-library-with-Emscripten) (todo: update)
  - [Creating a standalone project with the command-line tool](https://github.com/willmh93/bitloop/wiki/Creating-a-new-project-(CLI)) (todo: update)
- Tutorials (todo):
  - Creating a simple "Hello World" project
  - Using ImGui to control Scene variables (safely with multithreading)
  - User input and world navigation

### Main Features
- Projects are modular and can include each other as dependencies in a tree structure
  - Child projects can be opened and compiled as standalone projects
  - Cross-platform:  Windows, Linux, Emscripten
  - Designed for heavy per‑frame computations (e.g. Mandelbrot zoom)
- C++ NanoVG wrapper with integrated 128-bit float support (**fdd** class)
  - If you have prior experience with HTML Canvas drawing, this should feel second-nature to you
- FFmpeg video recording without blocking the UI (Desktop only)
- WebP snapshots / animations (all platforms).

### Design Principles
- Organization
  - Using CMake, you can create an empty bitloop project (e.g. "Science"), and include several subprojects ("Physics", "Biology") where you can create further subprojects
  - You can build any subtree of projects (e.g. just "Biology") in to a single final executable
  - All project data (images, audio, etc) are bundled into the final executable automatically
- Library Management
  - All bitloop projects use vcpkg as a package manager (vcpkg is fetched automatically for the current workspace)
  - Shared binaries - all projects in a workspace will share the same vcpkg instance and binaries to prevent unnecessary rebuilds
    - To create a workspace, simply add a "**.bitloop-workspace**" file in a project directory.
      - For the root workspace folder, add:  "**root=TRUE**"
      - For child workspaces, add: "**root=FALSE**"
    - vcpkg will be automatically cloned in to the highest level folder containing a "**.bitloop-workspace**" file (but not above the root workspace)
  - Bitloop itself is selected automatically:
    - Gallery projects use the checked-out `bitloop` source tree, even when a child is opened directly.
    - Framework examples use their enclosing Bitloop source tree.
    - A detached project keeps and installs its `bitloop` vcpkg dependency.
  - Workspace markers select shared tooling and source candidates. Child manifests are merged only through enabled `bitloop_add_dependency(...)` edges.

Advanced configurations may override automatic selection with `BITLOOP_PROVIDER=SOURCE|PACKAGE` and `BITLOOP_SOURCE_DIR=<path>`.
Project-local CMake tooling can be checked or refreshed with `bitloop tooling sync --check` and `bitloop tooling sync`.

An experimental source-build target, `bitloop-core` (alias `bitloop::core`), compiles an explicit subset of simulation and ImGui implementations. See [the core build notes](docs/experimental-core.md) for build instructions and current dependency limitations.

Optional [WebGPU compute support](docs/webgpu.md) provides Dawn/Emdawn device and readback helpers through `bitloop::webgpu`, alongside the existing OpenGL renderer.

Run `bitloop new project` interactively to choose Guided or Minimal scaffolding, optional AI development files, and—when invoked inside a Git worktree—publication to an existing empty remote plus submodule registration in the nearest enclosing repository. Scripted creation uses `bitloop new project <Name> --submodule --remote <RepositoryUrl>`. The child repository is committed and pushed; the parent gitlink is staged for review but never committed automatically.

Run `bitloop copy project` to copy an existing project from the current directory under a new name, or supply both names: `bitloop copy project FluidLesson FluidSim`. It keeps the source's scaffolding and offers the same Git integration (`--submodule`, `--remote`, `--no-submodule`), without the template questions. Copying requires Git to apply the source's `.gitignore` files, including nested rules and negations, even when the source is untracked or has no repository. Git metadata is always excluded; enclosing repositories and global ignore rules do not affect the copy.

The copy replaces case-sensitive whole-word project names in UTF-8 text, including CMake, namespaces, includes, and project display strings. Exact `<Name>_Project` and `<Name>_Scene` identifiers are also updated; other embedded names and different capitalization are preserved. The top-level `<Name>/` source directory and C/C++ source or header files with exactly that stem are renamed; other filenames are preserved. Binary files and symbolic links are copied unchanged. The source is untouched, and existing destinations or rename collisions are rejected.

### Layout
By default, projects contain a simple built-in viewport and sidebar for custom ImGui controls, along with a handy collection of tools/libraries.

<img src="https://i.imgur.com/vqsZn3m.png" width="700">

## Development progress

### Building

|                                                                      | Windows                  | Linux                   
| -------------------------------------------------------------------- | ------------------------ | ------------------------
| Modular nested projects                                              | :heavy_check_mark:       | :heavy_check_mark:      
| Standalone projects (with child projects as dependencies)            | :heavy_check_mark:       | :heavy_check_mark:      
| C++23 support                                                        | :heavy_check_mark:       | :heavy_check_mark:    
| Visual Studio                                                        | :heavy_check_mark:       | :heavy_check_mark:    
| Visual Studio Code                                                   | partial                  | partial    

### Architecture

|                                           | Windows                  | Linux                    | Web (Emscripten)                                          |
| ----------------------------------------- | ------------------------ | ------------------------ | --------------------------------------------------------- |
| Multithreading                            | :heavy_check_mark:       | :heavy_check_mark:       | :heavy_check_mark: (pthreads with COOP/COEP Headers)      |
| 128-bit float support (fdd)              | :heavy_check_mark:       | :heavy_check_mark:       | :heavy_check_mark:                                        |

### Graphics

|                                           | Windows                  | Linux                    | Web (Emscripten)                                          |
| ----------------------------------------- | ------------------------ | ------------------------ | --------------------------------------------------------- |
| High-DPI support                          | :heavy_check_mark:       | :heavy_check_mark:       | :heavy_check_mark:                                        |
| Mulitple Viewports (Split Screen)         | :heavy_check_mark:       | :heavy_check_mark:       | :heavy_check_mark:                                        |
| 2D Viewport (NanoVG wrapper)              | :heavy_check_mark:       | :heavy_check_mark:       | :heavy_check_mark:                                        |
| 3D Viewport                               | N/A                      | N/A                      | N/A                                                       |

### Recording / Image Capture

|                                           | Windows                  | Linux                    | Web (Emscripten)                                          |
| ----------------------------------------- | ------------------------ | ------------------------ | --------------------------------------------------------- |
| Custom-resolution image renders           | :heavy_check_mark:       | :heavy_check_mark:       | :heavy_check_mark:                                        |
| FFmpeg recording                          | :heavy_check_mark:       | :heavy_check_mark:       | N/A                                                       |
| WEBP animations                           | :heavy_check_mark:       | :heavy_check_mark:       | :heavy_check_mark:                                        |

### Other planned features

|                                             | Windows                  | Linux                    | Web (Emscripten)                                          |
| ------------------------------------------- | ------------------------ | ------------------------ | --------------------------------------------------------- |
| Timeline (keyframed variables, tweens, etc) | N/A                      | N/A                      | N/A                                                       |

### Example snapshots / WebP animations

<table>
  <tr>
    <td>
      <a href="https://bitloop.dev/main/assets/gallery/snap3_4k.webp">
        <img src="https://bitloop.dev/main/assets/gallery/snap3_1080p.webp" alt="Preview" width="350">
      </a>
    </td>
    <td>
      <a href="https://bitloop.dev/main/assets/gallery/snap4_4k.webp">
        <img src="https://bitloop.dev/main/assets/gallery/snap4_1080p.webp" alt="Preview" width="350">
      </a>
    </td>
  </tr>
  <tr>
    <td>
      <a href="https://bitloop.dev/main/assets/gallery/snap1_4k.webp">
        <img src="https://bitloop.dev/main/assets/gallery/snap1_1440p.webp" alt="Preview" width="350">
      </a>
    </td>
    <td>
      <a href="https://bitloop.dev/main/assets/gallery/snap2_4k.webp">
        <img src="https://bitloop.dev/main/assets/gallery/snap2_1440p.webp" alt="Preview" width="350">
      </a>
    </td>
  </tr>
  <tr>
    <td>
      <img src="https://bitloop.dev/main/assets/gallery/cardioid_ani.webp" alt="Alt text" width="350">
    </td>
  </tr>
</table>



