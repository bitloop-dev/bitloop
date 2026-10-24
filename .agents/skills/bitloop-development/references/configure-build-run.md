# Configure, Build, And Run

Use this page when establishing, switching, or repairing a Codex-owned build context. Reuse a working context for ordinary later builds and runs.

## Current Context

A context consists of:

- the active project root;
- a host-runnable configure/build preset ending in `-codex`;
- the preset's dedicated build directory;
- the root application target and executable path;
- any required compiler environment wrapper.

Re-evaluate only when the context is missing, stale, broken, needs a different configuration, changes host/toolchain, or the user explicitly asks to switch.

## Build Isolation

Use Codex-owned build directories. Do not reuse or rewrite a user's normal build directory and do not search random existing build trees for one that happens to work.

Prefer existing project-local `-codex` presets. When none exist and a build is authorized, add local presets in ignored `CMakeUserPresets.json` that inherit the project's official host presets and write under `build/<preset>-codex`. Preserve every non-Codex entry.

On Windows, default to a release MSVC host preset. Use debug only for a concrete debugging need. Load the installed Visual Studio developer environment before invoking Ninja/MSVC; discover `VsDevCmd.bat` instead of hardcoding a Visual Studio version or edition.

Do not select wasm for local diagnostics unless wasm work was requested.

## Standard Workflow

When no context exists:

1. Inspect the active root's presets and root `bitloop_new_project(...)` target.
2. Establish a dedicated host-runnable `-codex` configure preset.
3. Configure it once.
4. Treat the successful preset and build directory as the current context.

In steady state:

1. Reuse the current preset.
2. Build the smallest target that proves the change.
3. Run the executable from that same build directory.
4. Put diagnostic output under the active project `.codex/runs` directory.

Representative headless syntax (`<frame-count>` is intentionally unspecified and must come from the validation claim):

```powershell
.<build-dir>\app\app.exe --headless --project <ProjectName> --frames <frame-count> --dt 0.0166667 --surface 800x600 --trace <ProjectRoot>\.codex\runs\<run>
```

For large scenario catalogues, inspect and run only the relevant Scene without editing or rebuilding the Project:

```powershell
.<build-dir>\app\app.exe --headless --project <ProjectName> --list-scenes
.<build-dir>\app\app.exe --headless --project <ProjectName> --scene <SceneId> --frames <frame-count> --trace <ProjectRoot>\.codex\runs\<run>
```

Use repeatable `--scene` for a deterministic multi-Scene launch set. Use `--scene-config <json-file>` only when exactly one selected Scene has registered a strict typed parser.

Use the actual configured executable path rather than assuming this example layout.

## Do Not

- Do not configure or install dependencies for a read-only documentation investigation.
- Do not silently update vcpkg baselines or registries while establishing a build context.
- Do not modify official presets solely for Codex-local state.
- Do not switch configurations without a correctness or debugging reason.
- Do not claim standalone package compatibility when the matching package release has not been published and tested.
