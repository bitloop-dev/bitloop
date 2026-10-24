# Project Workflow

Use this page when creating, wiring, or locating a Bitloop project.

## Create A Project

Run the generator from the directory that should own the new project:

```powershell
bitloop new project <ProjectName>
```

Named project generation uses the Guided variant by default. It includes inline explanations and example code. Pass `--minimal` to generate the same functional scaffold without that tutorial material:

```powershell
bitloop new project <ProjectName> --minimal
```

AI-assisted scaffolding is also enabled by default. Pass `--no-ai` to omit the root `AGENTS.md`, managed framework skill, and generated trace/checkpoint hooks:

```powershell
bitloop new project <ProjectName> --no-ai
```

Running `bitloop new project` without a name prompts for the project name, offers Guided or Minimal, and asks whether to include AI-assisted development scaffolding. Both Guided and AI-assisted scaffolding default to Yes. Supplying the name on the command line keeps generation non-interactive.

When generation runs inside a Git worktree, the interactive flow also offers to initialize and publish the new project as an independent repository and register it as a submodule. Git's nearest enclosing worktree owns the new submodule; a root `.bitloop-workspace` remains the shared Bitloop/vcpkg boundary and only affects the prompt default. Direct children of that root default to Yes, while independent nested child repositories default to No.

The remote must already exist and be empty. For non-interactive generation, opt in explicitly:

```powershell
bitloop new project <ProjectName> --submodule --remote <RepositoryUrl>
```

`--remote` implies `--submodule`, and `--no-submodule` suppresses the interactive offer. The generator creates and pushes the child's `main` branch, then stages the enclosing repository's `.gitmodules` entry and `160000` gitlink without committing or pushing the enclosing repository. It does not absorb the child's embedded `.git` directory or edit the parent's CMake dependency list.

Use a capitalized project name and keep the generated directory name identical. Every generated repository is independently configurable and includes its own dependency manifest and CMake wiring.

When a parent project should include the new child, add exactly one parent dependency after generation:

```cmake
bitloop_new_project(ParentName)
bitloop_add_dependency(ParentName "./ChildName")
bitloop_finalize()
```

Keep child-specific source, assets, packages, diagnostics, and configuration in the child. Parent wiring does not transfer ownership.

## Generated Shape

The default template provides:

- `<ProjectName>.h` for project/scene declarations and state.
- `<ProjectName>.cpp` for lifecycle, UI, and drawing hooks.
- `CMakeLists.txt`, presets, vcpkg manifests, and generated-project tooling.
- A nested `Project` type that creates scenes.
- A nested `Scene` type that owns simulation state and rendering.
- A `UI::sidebar()` method for buffered ImGui controls.

When AI-assisted scaffolding is selected, it also provides:

- Stub trace and checkpoint hooks behind `BITLOOP_DEV_MODE`.
- A root `AGENTS.md` and versioned local framework skill under `.agents/skills`.

Non-AI generation omits those files and hooks entirely.

Use this shape unless the project's existing architecture establishes a better local pattern.

## Projects, Scenes, And Viewports

Declare Scenes and add their Viewports in `projectPrepare()`:

```cpp
void MyProject::projectPrepare(Layout& layout)
{
    layout << create<MyScene>();
}
```

`Project` owns each Scene; `Layout` owns each Viewport. `layout << scene` is shorthand for `layout.add(scene)` and keeps the responsive automatic grid. Store a typed handle when one Scene needs several Viewports, a stable CLI ID, or later runtime access:

```cpp
auto scene = create<MyScene>("primary", config);
layout.add(scene, "main");
layout.add(scene, "map");
setDefaultScenes({ scene });
```

Scenes can intentionally have no Viewport for simulation-only work. If no defaults are specified, all declared Scenes launch. Keep authoritative state in its Scene unless it genuinely must be shared at Project scope.

Generated projects commonly include camera navigation. Remove or disable it when the primary interaction directly manipulates domain objects and would compete for the same gestures.

## Paths And Diagnostics

Use `ProjectBase::activeProject()->rootPath(...)` or the project `rootPath(...)` helper for project data. Avoid hardcoded absolute paths in simulation code.

Keep Codex diagnostics under:

```text
<ProjectRoot>/.codex/runs/run0000_YYYYMMDD_HHMMSS
```

Follow [tracing-diagnostics.md](tracing-diagnostics.md) for indexed run folders and compact trace reconstruction.

## Agent-Pack Updates

The project's `AGENTS.md` is project-owned. Install the managed framework skill into one explicitly selected project with `init`:

```powershell
bitloop agents init --project <ProjectRoot>
```

Once managed, a pack can be checked or updated individually:

```powershell
bitloop agents sync --check --project <ProjectRoot>
bitloop agents sync --project <ProjectRoot>
```

Audit or update every already-managed project beneath a directory with recursive sync:

```powershell
bitloop agents sync --recursive --check --project <SearchRoot>
bitloop agents sync --recursive --project <SearchRoot>
```

Recursive sync never installs a pack into an unmanaged project. Projects that opt out of AI scaffolding therefore remain untouched until explicitly initialized. Initialization installs only the managed skill; it does not create an `AGENTS.md` or source-level trace/checkpoint hooks.

Synchronization never updates dependencies or project-owned files. It refuses to overwrite local changes inside the managed pack unless `--force` is supplied; move durable project-specific instructions into the project `AGENTS.md` before forcing replacement.
